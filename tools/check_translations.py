"""Falha se en.json e pt-br.json não tiverem exatamente as mesmas chaves e os mesmos placeholders {}.

Também confere os scripts Papyrus (data/Scripts/Source/*.psc): toda chave pedida ao DLL com
AgeOfSkyrim.Tr/Tr1/Tr2("chave") precisa existir, com 0/1/2 placeholders {}, e todo token "$AOS_Nome"
precisa da chave "skyui.Nome" (gerada para o SkyUI por tools/gen_skyui_translations.py).
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIR = ROOT / "data/SKSE/Plugins/AgeOfSkyrim/Translations"
SCRIPTS = ROOT / "data/Scripts/Source"

# Chaves montadas em runtime no Papyrus ("mcm.type." + tipo): todos os sufixos possíveis.
DYNAMIC = {"mcm.type.": ["unknown", "fort", "tower", "camp", "village", "city"]}


def load(name):
    text = (DIR / name).read_text(encoding="utf-8")
    text = re.sub(r"^\s*//.*$", "", text, flags=re.MULTILINE)  # o mod aceita comentários de linha
    return json.loads(text)


def check_scripts(en):
    errors = []
    for psc in sorted(SCRIPTS.glob("*.psc")):
        text = psc.read_text(encoding="utf-8")
        for fn, key in re.findall(r'AgeOfSkyrim\.(Tr[12]?)\(\s*"([^"]*)"', text):
            keys = [key + s for s in DYNAMIC[key]] if key in DYNAMIC else [key]
            arity = {"Tr": 0, "Tr1": 1, "Tr2": 2}[fn]
            for k in keys:
                if k not in en:
                    errors.append(f"{psc.name}: chave '{k}' não existe em en.json")
                elif en[k].count("{}") != arity:
                    errors.append(f"{psc.name}: '{k}' usada com {fn} precisa de {arity} placeholder(s) {{}}")
        for token in re.findall(r'"\$AOS_(\w+)"', text):
            if f"skyui.{token}" not in en:
                errors.append(f"{psc.name}: token '$AOS_{token}' sem a chave 'skyui.{token}'")
    return errors


def main():
    en, pt = load("en.json"), load("pt-br.json")
    errors = [f"falta em pt-br.json: {k}" for k in en.keys() - pt.keys()]
    errors += [f"falta em en.json: {k}" for k in pt.keys() - en.keys()]
    for k in en.keys() & pt.keys():
        if en[k].count("{}") != pt[k].count("{}"):
            errors.append(f"placeholders diferentes em '{k}'")
    errors += check_scripts(en)
    for e in errors:
        print(f"::error title=Tradução::{e}")
    print(f"{len(en)} chave(s) conferida(s), {len(errors)} erro(s).")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
