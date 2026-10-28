"""Acha (e espera, se preciso) o run do workflow Build que gerou o artefato AgeOfSkyrim-<sha> de um commit.

Usado pelo release.yml (ADR 0040): o release não recompila; reaproveita o zip que o build.yml já gerou para o
mesmo SHA. Se o build ainda está rodando, espera. Se não há build com sucesso (ou o artefato expirou), dispara o
build.yml por workflow_dispatch na ref da tag e espera esse run. O download em si fica com `gh run download`.

    python3 tools/wait_build_run.py --sha <sha> --ref v0.1.0     # imprime run_id (e grava em $GITHUB_OUTPUT)
    python3 tools/wait_build_run.py --sha <sha> --list           # só consulta (funciona sem token em repo público)

Token: GH_TOKEN ou GITHUB_TOKEN (actions:read; actions:write para disparar o build). Só biblioteca padrão.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

API = "https://api.github.com"


class Api:
    def __init__(self, repo, token):
        self.repo, self.token = repo, token

    def request(self, path, method="GET", body=None):
        req = urllib.request.Request(f"{API}/repos/{self.repo}/{path}", method=method,
                                     data=json.dumps(body).encode() if body else None)
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("X-GitHub-Api-Version", "2022-11-28")
        if self.token:
            req.add_header("Authorization", f"Bearer {self.token}")
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            return e.code, e.read()

    def json(self, path):
        status, data = self.request(path)
        if status != 200:
            raise RuntimeError(f"GET {path}: HTTP {status}: {data[:200]!r}")
        return json.loads(data)

    def runs(self, workflow, sha):
        return self.json(f"actions/workflows/{workflow}/runs?head_sha={sha}&per_page=30")["workflow_runs"]

    def has_artifact(self, run_id, name):
        arts = self.json(f"actions/runs/{run_id}/artifacts?per_page=100")["artifacts"]
        return any(a["name"] == name and not a.get("expired") for a in arts)

    def dispatch(self, workflow, ref):
        status, data = self.request(f"actions/workflows/{workflow}/dispatches", "POST", {"ref": ref})
        if status != 204:
            raise RuntimeError(f"workflow_dispatch de {workflow} em {ref}: HTTP {status}: {data[:200]!r}")


def wait_for_run(api, sha, ref, workflow="build.yml", timeout_min=45, poll_s=30, dispatch=True,
                 log=print, sleep=time.sleep, clock=time.monotonic):
    """Id do run com sucesso e artefato válido para `sha`, ou None (build falhou / tempo esgotado)."""
    name = f"AgeOfSkyrim-{sha}"
    deadline = clock() + timeout_min * 60
    known = None  # ids de runs que já existiam quando disparamos o build (None = ainda não disparamos)
    while True:
        runs = api.runs(workflow, sha)
        for run in (r for r in runs if r["status"] == "completed" and r["conclusion"] == "success"):
            if api.has_artifact(run["id"], name):
                log(f"Build {run['id']} ({run['event']}) tem o artefato {name}.")
                return run["id"]
        pending = [r for r in runs if r["status"] != "completed"]
        if known is not None:
            done = [r for r in runs if r["id"] not in known and r["status"] == "completed"]
            if done:
                log(f"::error title=Release::o build disparado terminou sem artefato "
                    f"(run {done[0]['id']}: {done[0]['conclusion']}).")
                return None
        if pending:
            log(f"Esperando o build {pending[0]['id']} ({pending[0]['status']}) do commit {sha[:7]}...")
        elif known is None:
            if not dispatch:
                log(f"::error title=Release::nenhum build com sucesso e artefato válido para {sha[:7]}.")
                return None
            log(f"Nenhum build com artefato para {sha[:7]}: disparando {workflow} em {ref}.")
            known = {r["id"] for r in runs}
            api.dispatch(workflow, ref)
        if clock() > deadline:
            log(f"::error title=Release::tempo esgotado ({timeout_min} min) esperando o build de {sha[:7]}.")
            return None
        sleep(poll_s)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "LeonardoCruzx/age-of-skyrim"))
    ap.add_argument("--sha", required=True)
    ap.add_argument("--ref", default=os.environ.get("GITHUB_REF_NAME"), help="ref para workflow_dispatch (a tag)")
    ap.add_argument("--workflow", default="build.yml")
    ap.add_argument("--timeout-min", type=int, default=45)
    ap.add_argument("--no-dispatch", action="store_true")
    ap.add_argument("--list", action="store_true", help="só lista os runs do commit e sai")
    args = ap.parse_args(argv)
    api = Api(args.repo, os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN"))
    if args.list:
        for r in api.runs(args.workflow, args.sha):
            art = r["conclusion"] == "success" and api.has_artifact(r["id"], f"AgeOfSkyrim-{args.sha}")
            print(r["id"], r["event"], r["status"], r["conclusion"], "artefato:", art)
        return 0
    if not args.ref and not args.no_dispatch:
        ap.error("--ref é obrigatório para disparar o build (ou use --no-dispatch)")
    run_id = wait_for_run(api, args.sha, args.ref, args.workflow, args.timeout_min, dispatch=not args.no_dispatch)
    if run_id is None:
        return 1
    print(f"run_id={run_id}")
    if out := os.environ.get("GITHUB_OUTPUT"):
        with open(out, "a", encoding="utf-8") as f:
            f.write(f"run_id={run_id}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
