"""Monta o zip de release (layout FOMOD) a partir do artefato do CI e gera as notas da versão.

Uso:
    python3 tools/package_release.py --artifact <pasta do artefato> --version 0.1.0 [--out dist]
    python3 tools/package_release.py --check          # só valida fomod/ e patches/ (sem artefato)

Layout do zip (ADR 0041):
    fomod/info.xml, fomod/ModuleConfig.xml     <Version> reescrito com a versão
    00 Core/...                                todo o artefato (SKSE/Plugins/AgeOfSkyrim.dll, PDB se houver, JSONs, ...)
    patch_<nome>/...                           cada patches/<nome>/ do repositório, sem os .md

Só biblioteca padrão. Erros saem como anotações ::error (GitHub Actions) e código de saída 1.
"""
import argparse
import json
import pathlib
import re
import shutil
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CORE = "00 Core"
PATCH_PREFIX = "patch_"
DLL = "SKSE/Plugins/AgeOfSkyrim.dll"
SETTINGS = "SKSE/Plugins/AgeOfSkyrim/settings.json"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$")


class PackageError(Exception):
    pass


# ---- JSON com comentários (o mod aceita // e /* */) -----------------------------------------------

def strip_json_comments(text):
    """Remove comentários // e /* */ fora de strings (mesma tolerância do nlohmann::json do DLL)."""
    out, i, n, in_str = [], 0, len(text), False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
            out.append(c)
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
            continue
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        else:
            out.append(c)
        i += 1
    return "".join(out)


def load_json(path):
    return json.loads(strip_json_comments(pathlib.Path(path).read_text(encoding="utf-8")))


# ---- Validação do FOMOD ---------------------------------------------------------------------------

def fomod_sources(module_config):
    """Pastas/arquivos de origem citados no ModuleConfig.xml (atributo source)."""
    tree = ET.parse(module_config)
    root = tree.getroot()
    if root.tag != "config":
        raise PackageError(f"{module_config}: raiz deve ser <config>, achei <{root.tag}>")
    if root.find("moduleName") is None or not (root.findtext("moduleName") or "").strip():
        raise PackageError(f"{module_config}: falta <moduleName>")
    required = [f.get("source") for f in root.findall("requiredInstallFiles/*")]
    if CORE not in required:
        raise PackageError(f"{module_config}: '{CORE}' precisa estar em <requiredInstallFiles>")
    optional = []
    for plugin in root.iter("plugin"):
        if not plugin.get("name"):
            raise PackageError(f"{module_config}: <plugin> sem name")
        if plugin.find("typeDescriptor") is None:
            raise PackageError(f"{module_config}: plugin '{plugin.get('name')}' sem <typeDescriptor>")
        optional += [f.get("source") for f in plugin.findall("files/*")]
    if any(not s for s in required + optional):
        raise PackageError(f"{module_config}: <folder>/<file> sem source")
    return required, optional


def patch_dirs(repo_root):
    base = pathlib.Path(repo_root) / "patches"
    return sorted(p for p in base.iterdir() if p.is_dir()) if base.is_dir() else []


def validate_patch(patch_dir):
    """Todo JSON do patch precisa ser válido; Compat/*.json precisa seguir o formato do ADR 0042."""
    files = [f for f in patch_dir.rglob("*") if f.is_file() and f.suffix.lower() != ".md"]
    if not files:
        raise PackageError(f"{patch_dir}: patch vazio")
    for f in files:
        if f.suffix.lower() != ".json":
            continue
        try:
            data = load_json(f)
        except json.JSONDecodeError as e:
            raise PackageError(f"{f}: JSON inválido: {e}") from e
        if f.parent.name == "Compat":
            validate_compat(f, data)


def validate_compat(path, data):
    if not isinstance(data, dict):
        raise PackageError(f"{path}: raiz deve ser objeto")
    loaded = data.get("whenLoaded", [])
    if not isinstance(loaded, list) or not all(isinstance(p, str) and p for p in loaded):
        raise PackageError(f"{path}: whenLoaded deve ser lista de nomes de plugin")
    for block in ("discovery", "conquest"):
        b = data.get(block, {})
        if not isinstance(b, dict):
            raise PackageError(f"{path}: '{block}' deve ser objeto")
        for ref in b.get("exclude", []):
            if not re.fullmatch(r"[^|]+\|0[xX][0-9A-Fa-f]{1,8}", str(ref)):
                raise PackageError(f"{path}: '{ref}' não está no formato Plugin.esp|0xID")


def validate_repo(repo_root=ROOT):
    """Confere XMLs bem-formados, patches válidos e que cada patch tem um <plugin> (e vice-versa)."""
    repo_root = pathlib.Path(repo_root)
    try:
        ET.parse(repo_root / "fomod/info.xml")
        _, optional = fomod_sources(repo_root / "fomod/ModuleConfig.xml")
    except ET.ParseError as e:
        raise PackageError(f"XML mal formado em fomod/: {e}") from e
    dirs = patch_dirs(repo_root)
    expected = {PATCH_PREFIX + d.name for d in dirs}
    missing = expected - set(optional)
    unknown = set(optional) - expected
    if missing:
        raise PackageError(f"patch sem <plugin> no ModuleConfig.xml: {sorted(missing)}")
    if unknown:
        raise PackageError(f"ModuleConfig.xml cita pasta inexistente: {sorted(unknown)}")
    for d in dirs:
        validate_patch(d)
    return len(dirs)


# ---- Versão e changelog ---------------------------------------------------------------------------

def cmake_version(repo_root=ROOT):
    m = re.search(r"project\(\s*\w+\s+VERSION\s+(\d+\.\d+\.\d+)", (pathlib.Path(repo_root) / "CMakeLists.txt").read_text())
    return m.group(1) if m else None


def extract_changelog(text, version):
    """Seção '## [versão]' do CHANGELOG (Keep a Changelog), sem o título. None se não existir."""
    pattern = re.compile(rf"^## \[{re.escape(version)}\][^\n]*\n(.*?)(?=^## \[|\Z)", re.MULTILINE | re.DOTALL)
    m = pattern.search(text)
    if not m:
        return None
    body = re.sub(r"^\[[^\]]+\]:\s*\S+\s*$", "", m.group(1), flags=re.MULTILINE)  # links de referência
    return body.strip()


def release_notes(version, changelog_section):
    return "\n".join([
        f"# Age of Skyrim {version}",
        "",
        changelog_section,
        "",
        "## Instalação / Installation",
        "",
        f"- Instale `AgeOfSkyrim-{version}.zip` pelo MO2 ou Vortex (instalador FOMOD). / "
        f"Install `AgeOfSkyrim-{version}.zip` with MO2 or Vortex (FOMOD installer).",
        "- Requisitos / Requirements: SKSE64, Address Library for SKSE Plugins.",
        "- Opcionais / Optional: SkyUI (MCM), PrismaUI (mapa tático / tactical map).",
        "- Log: `Documents/My Games/Skyrim Special Edition/SKSE/AgeOfSkyrim.log`.",
        "",
    ])


# ---- Montagem do zip ------------------------------------------------------------------------------

def release_warnings(artifact):
    """Avisos (não bloqueiam): settings.json de desenvolvimento indo para o jogador."""
    warnings = []
    path = pathlib.Path(artifact) / SETTINGS
    if not path.is_file():
        return [f"{SETTINGS} ausente no artefato"]
    try:
        s = load_json(path)
    except json.JSONDecodeError as e:
        return [f"{SETTINGS} inválido: {e}"]
    if s.get("language", "auto") != "auto":
        warnings.append(f"settings.json: language = '{s.get('language')}' (o release deveria usar 'auto')")
    if isinstance(s.get("debug"), dict) and s["debug"].get("enabled"):
        warnings.append("settings.json: debug.enabled = true (hotkeys de debug ligadas no release)")
    return warnings


def stage(artifact, repo_root, version, dest):
    artifact, repo_root, dest = pathlib.Path(artifact), pathlib.Path(repo_root), pathlib.Path(dest)
    if not (artifact / DLL).is_file():
        raise PackageError(f"artefato sem {DLL} (pasta errada? esperado o conteúdo de AgeOfSkyrim-<sha>)")
    shutil.copytree(artifact, dest / CORE)
    for d in patch_dirs(repo_root):
        shutil.copytree(d, dest / (PATCH_PREFIX + d.name), ignore=shutil.ignore_patterns("*.md"))
    (dest / "fomod").mkdir()
    shutil.copy2(repo_root / "fomod/ModuleConfig.xml", dest / "fomod/ModuleConfig.xml")
    info = (repo_root / "fomod/info.xml").read_text(encoding="utf-8")
    info, count = re.subn(r"<Version>[^<]*</Version>", f"<Version>{version}</Version>", info)
    if count != 1:
        raise PackageError("fomod/info.xml precisa de exatamente um <Version>")
    (dest / "fomod/info.xml").write_text(info, encoding="utf-8")
    # Toda origem citada no ModuleConfig precisa existir no pacote.
    required, optional = fomod_sources(dest / "fomod/ModuleConfig.xml")
    for src in required + optional:
        if not (dest / src).exists():
            raise PackageError(f"ModuleConfig.xml cita '{src}', ausente no pacote")


def write_zip(stage_dir, zip_path):
    stage_dir = pathlib.Path(stage_dir)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(p for p in stage_dir.rglob("*") if p.is_file()):  # ordem determinística
            z.write(f, f.relative_to(stage_dir).as_posix())


def package(artifact, version, out, repo_root=ROOT, allow_version_mismatch=False):
    """Valida, monta e escreve <out>/AgeOfSkyrim-<versão>.zip + RELEASE_NOTES.md. Devolve (zip, notas, avisos)."""
    repo_root, out = pathlib.Path(repo_root), pathlib.Path(out)
    if not VERSION_RE.match(version):
        raise PackageError(f"versão '{version}' não segue X.Y.Z[-sufixo] (tag v0.1.0 -> 0.1.0)")
    base = version.split("-")[0]
    cm = cmake_version(repo_root)
    if cm != base and not allow_version_mismatch:
        raise PackageError(f"versão {version} difere do CMakeLists.txt ({cm}); suba project(VERSION) antes da tag")
    changelog = (repo_root / "CHANGELOG.md").read_text(encoding="utf-8")
    section = extract_changelog(changelog, version) or extract_changelog(changelog, base)
    if not section:
        raise PackageError(f"CHANGELOG.md sem a seção '## [{version}]' (renomeie [Não lançado] ao lançar)")
    validate_repo(repo_root)
    out.mkdir(parents=True, exist_ok=True)
    zip_path = out / f"AgeOfSkyrim-{version}.zip"
    with tempfile.TemporaryDirectory() as tmp:
        stage(artifact, repo_root, version, tmp)
        write_zip(tmp, zip_path)
    notes = out / "RELEASE_NOTES.md"
    notes.write_text(release_notes(version, section), encoding="utf-8")
    return zip_path, notes, release_warnings(artifact)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--artifact", help="pasta com o conteúdo do artefato AgeOfSkyrim-<sha>")
    ap.add_argument("--version", help="versão sem o 'v' (ex.: 0.1.0)")
    ap.add_argument("--out", default="dist")
    ap.add_argument("--repo-root", default=str(ROOT))
    ap.add_argument("--allow-version-mismatch", action="store_true")
    ap.add_argument("--check", action="store_true", help="só valida fomod/ e patches/")
    args = ap.parse_args(argv)
    try:
        if args.check:
            n = validate_repo(args.repo_root)
            print(f"fomod/ ok, {n} patch(es) ok.")
            return 0
        if not args.artifact or not args.version:
            ap.error("--artifact e --version são obrigatórios (ou use --check)")
        zip_path, notes, warnings = package(args.artifact, args.version.lstrip("v"), args.out,
                                            args.repo_root, args.allow_version_mismatch)
    except (PackageError, OSError, ET.ParseError) as e:
        print(f"::error title=Pacote::{e}")
        return 1
    for w in warnings:
        print(f"::warning title=Pacote::{w}")
    print(f"{zip_path} ({zip_path.stat().st_size // 1024} KiB), notas em {notes}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
