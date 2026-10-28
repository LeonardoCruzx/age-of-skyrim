"""Resume os tempos "[tempo] <operação>: <ms> ms" do AgeOfSkyrim.log (ver docs/PERFORMANCE.md).

    python3 tools/perf_report.py "%USERPROFILE%/Documents/My Games/Skyrim Special Edition/SKSE/AgeOfSkyrim.log"
    python3 tools/perf_report.py AgeOfSkyrim.log --strict          # código 1 se alguma meta estourar
    python3 tools/perf_report.py AgeOfSkyrim.log --target Stock::CountByCategory=2

Metas (ADR 0005 / ROADMAP): evento < 1 ms, tick estratégico < 5 ms. Operações de carregamento (rodam uma vez
por load) e de debug são só informativas. Só biblioteca padrão.
"""
import argparse
import math
import re
import sys

LINE_RE = re.compile(r"\[tempo\]\s+(?P<op>.+?):\s+(?P<ms>\d+(?:\.\d+)?)\s*ms")

EVENT_MS = 1.0
TICK_MS = 5.0

# Categoria das operações conhecidas; o resto é classificado pelo nome (ver categorize).
KNOWN = {
    "Config::MergeDiscovered": "load",
    "Config::ComputeNeighbors": "load",
    "Discovery::Scan": "load",
    "Stock::Load": "load",
    "BannerController::ApplyAll": "load",
    "SelfTest::Run": "debug",
    "Claims::TryClaimHere": "event",
    "Stock::CountByCategory": "event",
    "WarMap::BuildSnapshot": "tick",  # M11: meta de 5 ms com ~300 territórios (ADR 0052)
}
TARGETS = {"event": EVENT_MS, "tick": TICK_MS, "load": None, "debug": None}


def categorize(op):
    if op in KNOWN:
        return KNOWN[op]
    name = op.split("::")[-1].lower()
    if "tick" in name or "hourly" in name:
        return "tick"
    if name.startswith(("load", "scan", "merge", "compute", "build", "init")):
        return "load"
    if "selftest" in op.lower() or "debug" in op.lower():
        return "debug"
    return "event"


def parse(lines):
    """{operação: [ms, ...]} na ordem do log."""
    samples = {}
    for line in lines:
        m = LINE_RE.search(line)
        if m:
            samples.setdefault(m.group("op").strip(), []).append(float(m.group("ms")))
    return samples


def percentile(values, p):
    s = sorted(values)
    return s[min(len(s) - 1, max(0, math.ceil(p / 100 * len(s)) - 1))]


def summarize(samples, overrides=None):
    """Linhas do relatório: dict com op, categoria, n, min, média, p95, máx, meta, acima (n acima da meta)."""
    overrides = overrides or {}
    rows = []
    for op, values in samples.items():
        cat = categorize(op)
        target = overrides.get(op, TARGETS[cat])
        rows.append({
            "op": op, "cat": cat, "n": len(values),
            "min": min(values), "avg": sum(values) / len(values), "p95": percentile(values, 95), "max": max(values),
            "target": target, "over": sum(1 for v in values if target is not None and v > target),
        })
    rows.sort(key=lambda r: (r["target"] is None, -(r["max"] / r["target"] if r["target"] else r["max"])))
    return rows


def render(rows):
    head = f"{'operação':<34} {'tipo':<6} {'n':>5} {'mín':>9} {'média':>9} {'p95':>9} {'máx':>9} {'meta':>6}  status"
    out = [head, "-" * len(head)]
    for r in rows:
        meta = f"{r['target']:g}" if r["target"] is not None else "-"
        status = "info" if r["target"] is None else (f"ALERTA ({r['over']}/{r['n']} acima)" if r["over"] else "ok")
        out.append(f"{r['op']:<34} {r['cat']:<6} {r['n']:>5} {r['min']:>9.3f} {r['avg']:>9.3f} "
                   f"{r['p95']:>9.3f} {r['max']:>9.3f} {meta:>6}  {status}")
    return "\n".join(out)


def parse_targets(items):
    targets = {}
    for item in items or []:
        op, _, ms = item.rpartition("=")
        if not op:
            raise ValueError(f"--target espera OPERAÇÃO=MS, recebi '{item}'")
        targets[op] = float(ms)
    return targets


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--strict", action="store_true", help="código de saída 1 se alguma meta estourar")
    ap.add_argument("--target", action="append", metavar="OP=MS", help="meta específica (repetível)")
    ap.add_argument("--github", action="store_true", help="alertas como anotações ::warning")
    args = ap.parse_args(argv)
    try:
        overrides = parse_targets(args.target)
    except ValueError as e:
        ap.error(str(e))
    with open(args.log, encoding="utf-8", errors="replace") as f:
        rows = summarize(parse(f), overrides)
    if not rows:
        print("Nenhuma linha [tempo] no log (o mod carregou? o log é o AgeOfSkyrim.log?).")
        return 1 if args.strict else 0
    print(render(rows))
    alerts = [r for r in rows if r["over"]]
    for r in alerts:
        msg = f"{r['op']}: máx {r['max']:.3f} ms > meta {r['target']:g} ms ({r['over']}/{r['n']} amostras)"
        print(f"::warning title=Performance::{msg}" if args.github else f"ALERTA: {msg}")
    print(f"\n{sum(r['n'] for r in rows)} amostra(s), {len(rows)} operação(ões), {len(alerts)} alerta(s).")
    return 1 if (alerts and args.strict) else 0


if __name__ == "__main__":
    sys.exit(main())
