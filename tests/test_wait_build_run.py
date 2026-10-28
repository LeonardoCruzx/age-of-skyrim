"""Testes do tools/wait_build_run.py com uma API falsa (sem rede)."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import wait_build_run as w  # noqa: E402

SHA = "a" * 40


def run(id_, status="completed", conclusion="success", event="push"):
    return {"id": id_, "status": status, "conclusion": conclusion, "event": event}


class FakeApi:
    """Cada chamada a runs() devolve o próximo estado da lista `timeline` (o último se repete)."""

    def __init__(self, timeline, artifacts=()):
        self.timeline, self.artifacts, self.calls, self.dispatched = timeline, set(artifacts), 0, []

    def runs(self, workflow, sha):
        state = self.timeline[min(self.calls, len(self.timeline) - 1)]
        self.calls += 1
        return state

    def has_artifact(self, run_id, name):
        return name == f"AgeOfSkyrim-{SHA}" and run_id in self.artifacts

    def dispatch(self, workflow, ref):
        self.dispatched.append(ref)


def wait(api, **kw):
    clock = iter(range(0, 10**6, 30))
    return w.wait_for_run(api, SHA, "v0.1.0", log=lambda *_: None, sleep=lambda _: None,
                          clock=lambda: next(clock), **kw)


class WaitBuildRunTest(unittest.TestCase):
    def test_build_pronto(self):
        api = FakeApi([[run(2, conclusion="failure"), run(1)]], artifacts={1})
        self.assertEqual(wait(api), 1)
        self.assertEqual(api.dispatched, [])

    def test_espera_build_em_andamento(self):
        api = FakeApi([[run(5, "in_progress", None)], [run(5, "in_progress", None)], [run(5)]], artifacts={5})
        self.assertEqual(wait(api), 5)
        self.assertEqual(api.dispatched, [])

    def test_dispara_quando_artefato_expirou(self):
        api = FakeApi([[run(1)], [run(1)], [run(1), run(9, "queued", None, "workflow_dispatch")],
                       [run(1), run(9, event="workflow_dispatch")]], artifacts={9})
        self.assertEqual(wait(api), 9)
        self.assertEqual(api.dispatched, ["v0.1.0"])

    def test_build_disparado_falha(self):
        api = FakeApi([[], [run(3, conclusion="failure", event="workflow_dispatch")]])
        self.assertIsNone(wait(api))
        self.assertEqual(api.dispatched, ["v0.1.0"])

    def test_sem_dispatch(self):
        self.assertIsNone(wait(FakeApi([[run(1, conclusion="failure")]]), dispatch=False))

    def test_tempo_esgotado(self):
        self.assertIsNone(wait(FakeApi([[run(1, "in_progress", None)]]), timeout_min=1))


if __name__ == "__main__":
    unittest.main()
