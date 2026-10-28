"""Testes do tools/perf_report.py com um log de exemplo (tests/data/AgeOfSkyrim_sample.log)."""
import io
import pathlib
import sys
import unittest
from contextlib import redirect_stdout

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import perf_report  # noqa: E402

LOG = ROOT / "tests/data/AgeOfSkyrim_sample.log"


class PerfReportTest(unittest.TestCase):
    def setUp(self):
        with open(LOG, encoding="utf-8") as f:
            self.samples = perf_report.parse(f)
        self.rows = {r["op"]: r for r in perf_report.summarize(self.samples)}

    def test_parse_ignora_linhas_sem_tempo(self):
        self.assertEqual(sum(len(v) for v in self.samples.values()), 12)
        self.assertEqual(self.samples["Stock::CountByCategory"], [0.21, 0.33, 1.42])

    def test_estatisticas(self):
        r = self.rows["Stock::CountByCategory"]
        self.assertEqual((r["n"], r["min"], r["max"]), (3, 0.21, 1.42))
        self.assertAlmostEqual(r["avg"], (0.21 + 0.33 + 1.42) / 3)

    def test_categorias_e_metas(self):
        self.assertEqual(self.rows["Discovery::Scan"]["cat"], "load")
        self.assertIsNone(self.rows["Discovery::Scan"]["target"])
        self.assertEqual(self.rows["Economy::HourlyTick"]["cat"], "tick")
        self.assertEqual(self.rows["Economy::HourlyTick"]["target"], 5.0)
        self.assertEqual(self.rows["Claims::TryClaimHere"]["target"], 1.0)
        self.assertEqual(self.rows["SelfTest::Run"]["cat"], "debug")
        self.assertEqual(perf_report.categorize("Ai::DecideCounterAttack"), "event")

    def test_alertas(self):
        self.assertEqual(self.rows["Stock::CountByCategory"]["over"], 1)  # 1.42 > 1 ms
        self.assertEqual(self.rows["Economy::HourlyTick"]["over"], 1)     # 6.75 > 5 ms
        self.assertEqual(self.rows["Claims::TryClaimHere"]["over"], 0)
        self.assertEqual(self.rows["Discovery::Scan"]["over"], 0)         # carregamento é só informativo

    def test_meta_sobrescrita(self):
        rows = {r["op"]: r for r in perf_report.summarize(self.samples, {"Stock::CountByCategory": 2.0})}
        self.assertEqual(rows["Stock::CountByCategory"]["over"], 0)

    def test_main_strict(self):
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(perf_report.main([str(LOG), "--strict"]), 1)
            self.assertEqual(perf_report.main([str(LOG)]), 0)
            self.assertEqual(perf_report.main([str(LOG), "--strict", "--target", "Stock::CountByCategory=2",
                                               "--target", "Economy::HourlyTick=7"]), 0)
        self.assertIn("ALERTA", out.getvalue())


if __name__ == "__main__":
    unittest.main()
