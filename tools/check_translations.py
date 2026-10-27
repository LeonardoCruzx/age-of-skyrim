"""Falha se en.json e pt-br.json não tiverem exatamente as mesmas chaves e os mesmos placeholders {}."""
import json
import pathlib
import re
import sys

DIR = pathlib.Path(__file__).resolve().parent.parent / "data/SKSE/Plugins/AgeOfSkyrim/Translations"


def load(name):
    text = (DIR / name).read_text(encoding="utf-8")
    text = re.sub(r"^\s*//.*$", "", text, flags=re.MULTILINE)  # o mod aceita comentários de linha
    return json.loads(text)


def main():
    en, pt = load("en.json"), load("pt-br.json")
    errors = [f"falta em pt-br.json: {k}" for k in en.keys() - pt.keys()]
    errors += [f"falta em en.json: {k}" for k in pt.keys() - en.keys()]
    for k in en.keys() & pt.keys():
        if en[k].count("{}") != pt[k].count("{}"):
            errors.append(f"placeholders diferentes em '{k}'")
    for e in errors:
        print(f"::error title=Tradução::{e}")
    print(f"{len(en)} chave(s) conferida(s), {len(errors)} erro(s).")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
