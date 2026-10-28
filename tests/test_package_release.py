"""Testes do tools/package_release.py: monta um pacote a partir de um artefato falso e confere o zip."""
import json
import pathlib
import shutil
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import package_release as pr  # noqa: E402

CHANGELOG = """# Changelog

## [Não lançado]
- coisa nova

## [0.2.0] - 2026-11-01
### Adicionado
- territórios

## [0.1.0] - 2026-10-01
- primeira

[0.2.0]: https://example.invalid/compare/v0.1.0...v0.2.0
"""


class JsonCommentsTest(unittest.TestCase):
    def test_remove_comentarios_fora_de_strings(self):
        text = '{ // linha\n "a": "http://x//y", /* bloco */ "b": 1 }'
        self.assertEqual(json.loads(pr.strip_json_comments(text)), {"a": "http://x//y", "b": 1})

    def test_aspas_escapadas(self):
        self.assertEqual(json.loads(pr.strip_json_comments('{"a": "x\\"//y"}')), {"a": 'x"//y'})


class ChangelogTest(unittest.TestCase):
    def test_extrai_secao(self):
        self.assertEqual(pr.extract_changelog(CHANGELOG, "0.2.0"), "### Adicionado\n- territórios")
        self.assertEqual(pr.extract_changelog(CHANGELOG, "0.1.0"), "- primeira")
        self.assertIsNone(pr.extract_changelog(CHANGELOG, "9.9.9"))

    def test_changelog_real_tem_nao_lancado(self):
        text = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertTrue(pr.extract_changelog(text, "Não lançado"))


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp)
        # Repositório mínimo: cópia de fomod/ e patches/ reais + CMakeLists e CHANGELOG de teste.
        self.repo = self.tmp / "repo"
        shutil.copytree(ROOT / "fomod", self.repo / "fomod")
        shutil.copytree(ROOT / "patches", self.repo / "patches")
        (self.repo / "CMakeLists.txt").write_text("project(AgeOfSkyrim VERSION 0.2.0 LANGUAGES CXX)\n")
        (self.repo / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
        # Artefato falso no layout do build.yml.
        self.art = self.tmp / "artifact"
        plugins = self.art / "SKSE/Plugins"
        (plugins / "AgeOfSkyrim").mkdir(parents=True)
        (plugins / "AgeOfSkyrim.dll").write_bytes(b"MZ")
        (plugins / "AgeOfSkyrim.pdb").write_bytes(b"pdb")
        (plugins / "AgeOfSkyrim/settings.json").write_text('{ "language": "pt-br", // dev\n "debug": {"enabled": true} }')

    def package(self, version="0.2.0", **kw):
        return pr.package(self.art, version, self.tmp / "dist", self.repo, **kw)

    def test_zip_no_layout_fomod(self):
        zip_path, notes, warnings = self.package()
        self.assertEqual(zip_path.name, "AgeOfSkyrim-0.2.0.zip")
        with zipfile.ZipFile(zip_path) as z:
            names = set(z.namelist())
            info = ET.fromstring(z.read("fomod/info.xml"))
            ET.fromstring(z.read("fomod/ModuleConfig.xml"))
        self.assertIn("00 Core/SKSE/Plugins/AgeOfSkyrim.dll", names)
        self.assertIn("00 Core/SKSE/Plugins/AgeOfSkyrim.pdb", names)
        self.assertIn("patch_civil-war-mods/SKSE/Plugins/AgeOfSkyrim/Compat/civil-war-mods.json", names)
        self.assertFalse(any(n.endswith(".md") and n.startswith("patch_") for n in names))
        self.assertEqual(info.findtext("Version"), "0.2.0")
        text = notes.read_text(encoding="utf-8")
        self.assertIn("territórios", text)
        self.assertIn("Address Library", text)
        self.assertEqual(len(warnings), 2)  # language != auto e debug ligado

    def test_pdb_opcional(self):
        (self.art / "SKSE/Plugins/AgeOfSkyrim.pdb").unlink()
        zip_path, _, _ = self.package()
        with zipfile.ZipFile(zip_path) as z:
            self.assertNotIn("00 Core/SKSE/Plugins/AgeOfSkyrim.pdb", z.namelist())

    def test_prerelease_usa_secao_da_base(self):
        zip_path, _, _ = self.package("0.2.0-beta.1")
        self.assertEqual(zip_path.name, "AgeOfSkyrim-0.2.0-beta.1.zip")

    def test_erros(self):
        with self.assertRaises(pr.PackageError):
            self.package("0.3.0")  # difere do CMakeLists
        with self.assertRaises(pr.PackageError):
            self.package("0.1.0", allow_version_mismatch=False)
        self.package("0.1.0", allow_version_mismatch=True)  # seção existe; só a versão difere
        with self.assertRaises(pr.PackageError):
            self.package("v0.2")
        (self.art / "SKSE/Plugins/AgeOfSkyrim.dll").unlink()
        with self.assertRaises(pr.PackageError):
            self.package()

    def test_patch_sem_plugin_no_xml(self):
        (self.repo / "patches/novo/SKSE").mkdir(parents=True)
        (self.repo / "patches/novo/SKSE/x.json").write_text("{}")
        with self.assertRaises(pr.PackageError):
            pr.validate_repo(self.repo)

    def test_xml_mal_formado(self):
        (self.repo / "fomod/ModuleConfig.xml").write_text("<config><moduleName>x</config>")
        with self.assertRaises(pr.PackageError):
            pr.validate_repo(self.repo)

    def test_main_check(self):
        self.assertEqual(pr.main(["--check", "--repo-root", str(self.repo)]), 0)


if __name__ == "__main__":
    unittest.main()
