"""Gera dados de exemplo do mapa tático (M11) no mesmo formato do snapshot do DLL (docs/warmap-schema.json).

Uso:
    python3 tools/warmap_sample.py                 # escreve data/PrismaUI/views/AgeOfSkyrim/sample/sample-data.js
    python3 tools/warmap_sample.py --count 300 --json saida.json   # JSON puro (testes / tamanho com 300 territórios)

Depois abra data/PrismaUI/views/AgeOfSkyrim/index.html?sample no navegador (&lang=en para inglês).
Posições/nomes são fictícios e procedurais (sem mapa nem dados do jogo); os textos vêm das traduções do mod.
"""
import argparse
import json
import math
import pathlib
import random
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
TRANSLATIONS = ROOT / "data/SKSE/Plugins/AgeOfSkyrim/Translations"
OUT_JS = ROOT / "data/PrismaUI/views/AgeOfSkyrim/sample/sample-data.js"
TAMRIEL = 0x0000003C

# Províncias de exemplo: (chave, nome, centro aproximado em unidades do jogo).
HOLDS = [
    ("haafingar", "Haafingar", (-150000, 110000)),
    ("hjaalmarch", "Hjaalmarch", (-80000, 80000)),
    ("pale", "The Pale", (20000, 110000)),
    ("winterhold", "Winterhold", (130000, 120000)),
    ("reach", "The Reach", (-170000, 0)),
    ("whiterun", "Whiterun Hold", (-10000, 10000)),
    ("eastmarch", "Eastmarch", (130000, 30000)),
    ("falkreath", "Falkreath Hold", (-80000, -90000)),
    ("rift", "The Rift", (130000, -80000)),
]
TYPES = ["fort", "tower", "camp", "village", "camp", "fort", "village"]


def make_id(key):
    """Mesmo hash do DLL (src/Core/Ids.h): FNV-1a 32 bits, sem distinção de maiúsculas; 0 vira 1."""
    h = 2166136261
    for c in key.lower().encode("utf-8"):
        h ^= c
        h = (h * 16777619) & 0xFFFFFFFF
    return h or 1


def hexid(n):
    return f"{n:08X}"


def load_translations(lang):
    text = (TRANSLATIONS / f"{lang}.json").read_text(encoding="utf-8")
    text = re.sub(r"^\s*//.*$", "", text, flags=re.MULTILINE)
    data = json.loads(text)
    return {k: v for k, v in data.items() if k.startswith(("warmap.", "route."))}


def build(count=60, seed=11, lang="pt-br"):
    rnd = random.Random(seed)
    player = hexid(make_id("player_1"))
    factions = [
        {"id": hexid(make_id("imperial")), "name": "Imperial Legion", "color": "#9B1C1C", "player": False},
        {"id": hexid(make_id("stormcloak")), "name": "Stormcloaks", "color": "#1F4E99", "player": False},
        {"id": player, "name": "Minha Facção", "color": "#2E7D32", "player": True},
    ]
    owners = [None, None, factions[0]["id"], factions[1]["id"]]

    territories = []
    per_hold = max(1, count // len(HOLDS))
    for hi, (hkey, _hname, (cx, cy)) in enumerate(HOLDS):
        for i in range(per_hold if hi < len(HOLDS) - 1 else count - per_hold * (len(HOLDS) - 1)):
            ttype = "city" if i == 0 else TYPES[(i + hi) % len(TYPES)]
            key = f"sample_{hkey}_{i}"
            ang, rad = rnd.uniform(0, 2 * math.pi), (0 if i == 0 else rnd.uniform(6000, 38000))
            owner = rnd.choice(owners)
            if hkey == "whiterun" and i < 5:
                owner = player
            territories.append({
                "id": hexid(make_id(key)), "key": key,
                "name": f"{_hname} {'' if i == 0 else i}".strip(),
                "type": ttype, "province": hexid(make_id(f"hold_{hkey}")), "owner": owner,
                "x": round(cx + rad * math.cos(ang), 1), "y": round(cy + rad * math.sin(ang), 1),
                "ws": hexid(TAMRIEL), "neighbors": [], "seat": False, "target": False, "layers": {},
            })

    # Vizinhos: 4 mais próximos (como Config::ComputeNeighbors).
    for t in territories:
        near = sorted((math.hypot(t["x"] - o["x"], t["y"] - o["y"]), o["id"]) for o in territories if o is not t)
        t["neighbors"] = [i for _, i in near[:4]]

    mine = [t for t in territories if t["owner"] == player]
    seat = mine[1] if len(mine) > 1 else territories[0]
    seat["seat"] = True
    enemy = next((t for t in territories if t["owner"] == factions[0]["id"]), None)
    if enemy:
        enemy["target"] = True
    # Camada de exemplo (como a guarnição da M14 registraria).
    for t in territories[::3]:
        t["layers"]["garrison"] = {"soldiers": rnd.randint(2, 20), "morale": rnd.choice(["alta", "média", "baixa"])}

    routes = []
    for a, b, blocked in ((mine[0], mine[2], False), (mine[1], mine[3], True)) if len(mine) >= 4 else ():
        dist = math.hypot(a["x"] - b["x"], a["y"] - b["y"])
        routes.append({
            "id": hexid(make_id("route/" + "/".join(sorted([a["id"], b["id"]])))), "a": a["id"], "b": b["id"],
            "owner": player, "active": True, "blocked": blocked, "blocker": enemy["id"] if blocked and enemy else None,
            "distance": round(dist, 1), "goldPerHour": round((0.5 + 0.5 * dist / 10000), 3),
        })

    xs = [t["x"] for t in territories]
    ys = [t["y"] for t in territories]
    return {
        "schema": 1,
        "lang": lang,
        "i18n": load_translations(lang),
        "factions": factions,
        "playerFaction": player,
        "territories": territories,
        "provinces": [{"id": hexid(make_id(f"hold_{k}")), "name": n} for k, n, _ in HOLDS],
        "worldspaces": [{"id": hexid(TAMRIEL), "name": "Skyrim", "minX": min(xs), "minY": min(ys),
                         "maxX": max(xs), "maxY": max(ys), "count": len(territories)}],
        "worldspace": hexid(TAMRIEL),
        "routes": routes,
        "seat": {"territory": seat["id"], "unclassified": 7, "stock": [
            {"key": "gold", "name": "Ouro", "count": 1250}, {"key": "food", "name": "Comida", "count": 84},
            {"key": "metals", "name": "Metais", "count": 31}]},
        "player": {"x": seat["x"] + 900.0, "y": seat["y"] - 600.0, "ws": hexid(TAMRIEL)},
        "layers": [{"key": "garrison", "label": "Guarnição"}],
        "limits": {"maxRoutes": 8, "maxTargets": 10},
        "buildMs": 0.0,
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--count", type=int, default=60, help="número de territórios (padrão 60)")
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--json", type=pathlib.Path, help="escreve JSON puro neste arquivo em vez do sample-data.js")
    args = ap.parse_args(argv)

    data = build(args.count, args.seed)
    if args.json:
        args.json.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"{args.json}: {len(data['territories'])} territórios")
        return 0
    i18n = {lang: load_translations(lang) for lang in ("en", "pt-br")}
    OUT_JS.parent.mkdir(parents=True, exist_ok=True)
    OUT_JS.write_text(
        "// Gerado por tools/warmap_sample.py — dados FICTÍCIOS para testar a view fora do jogo (index.html?sample).\n"
        f"window.AOS_SAMPLE_DATA = {json.dumps(data, ensure_ascii=False)};\n"
        f"window.AOS_SAMPLE_I18N = {json.dumps(i18n, ensure_ascii=False)};\n",
        encoding="utf-8")
    print(f"{OUT_JS.relative_to(ROOT)}: {len(data['territories'])} territórios, {len(data['routes'])} rotas")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
