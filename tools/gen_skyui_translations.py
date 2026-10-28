"""Gera Interface/Translations/AgeOfSkyrim_<IDIOMA>.txt (SkyUI) a partir dos JSON de tradução do mod.

Só as chaves "skyui.<Nome>" viram tokens "$AOS_<Nome>" (nome do mod na lista do MCM e o aviso de DLL
ausente: os únicos textos que o SkyUI traduz sozinho). O resto do MCM pede os textos ao DLL, que segue o
idioma do settings.json (ADR 0033).

Formato exigido pelo Skyrim/SkyUI: UTF-16 LE com BOM, uma linha "$TOKEN<TAB>texto", CRLF.
O arquivo carregado é o do sLanguage do jogo. Não existe idioma oficial PT-BR no Skyrim SE: quem joga
em português usa o jogo em inglês (sLanguage=ENGLISH), então AgeOfSkyrim_ENGLISH.txt sai em inglês e
os demais idiomas oficiais recebem uma cópia em inglês (nunca aparece "$AOS_..." cru). PORTUGUESE e
BRAZILIAN (usados por algumas traduções da comunidade) recebem o pt-br.

Uso: python tools/gen_skyui_translations.py <pasta de saída>   (ex.: package/Interface/Translations)
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "data/SKSE/Plugins/AgeOfSkyrim/Translations"
PREFIX = "skyui."

# idioma do jogo (sLanguage) -> JSON do mod
LANGUAGES = {
    "ENGLISH": "en", "FRENCH": "en", "GERMAN": "en", "ITALIAN": "en", "SPANISH": "en",
    "POLISH": "en", "RUSSIAN": "en", "CZECH": "en", "JAPANESE": "en", "CHINESE": "en",
    "PORTUGUESE": "pt-br", "BRAZILIAN": "pt-br",
}


def load(lang):
    text = (SRC / f"{lang}.json").read_text(encoding="utf-8")
    text = re.sub(r"^\s*//.*$", "", text, flags=re.MULTILINE)
    return json.loads(text)


def tokens(strings):
    out = []
    for key in sorted(k for k in strings if k.startswith(PREFIX)):
        value = strings[key]
        if "\t" in value or "\n" in value:
            raise SystemExit(f"::error title=Tradução SkyUI::'{key}' não pode ter TAB/quebra de linha")
        out.append(f"$AOS_{key[len(PREFIX):]}\t{value}")
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    out_dir = pathlib.Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    cache = {}
    for game_lang, mod_lang in LANGUAGES.items():
        lines = cache.setdefault(mod_lang, tokens(load(mod_lang)))
        data = "﻿" + "\r\n".join(lines) + "\r\n"
        (out_dir / f"AgeOfSkyrim_{game_lang}.txt").write_bytes(data.encode("utf-16-le"))
    print(f"{len(LANGUAGES)} arquivo(s) em {out_dir} ({len(cache['en'])} token(s) cada).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
