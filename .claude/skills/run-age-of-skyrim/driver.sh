#!/usr/bin/env bash
# Driver do Age of Skyrim para agentes: o DLL só compila no GitHub Actions (MSVC) e só roda dentro do Skyrim
# (Windows). Daqui dá para: conferir tudo que é local, acompanhar o CI de um commit, baixar o artefato,
# montar o zip de release (FOMOD) e resumir os tempos de um AgeOfSkyrim.log.
#
#   driver.sh check                      traduções + testes Python + fomod/patches
#   driver.sh ci [sha]                   espera o run do Build do commit (padrão HEAD) e mostra o resultado;
#                                        se falhou, imprime os erros do compilador (anotações)
#   driver.sh artifact [sha] [dir]       baixa e extrai o artefato AgeOfSkyrim-<sha> (padrão dir: dist/artifact)
#   driver.sh package <dir> <versão>     monta dist/AgeOfSkyrim-<versão>.zip (FOMOD) a partir do artefato extraído
#   driver.sh perf <AgeOfSkyrim.log>     resumo de desempenho do log do jogo
#
# Caminhos relativos à raiz do repositório. Token do GitHub: GH_TOKEN, ou a credencial do git (git credential fill).
set -euo pipefail
ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$ROOT"
REPO=${AOS_REPO:-LeonardoCruzx/age-of-skyrim}

token() {
  if [ -n "${GH_TOKEN:-}" ]; then echo "$GH_TOKEN"; return; fi
  # Sem helper (fora do VS Code) o fill falha: segue anônimo em vez de abortar pelo pipefail.
  printf "protocol=https\nhost=github.com\n\n" | GIT_TERMINAL_PROMPT=0 git credential fill 2>/dev/null \
    | sed -n 's/^password=//p' || true
}
api() {  # GET autenticado (anônimo se não houver token: 60 req/h)
  local t; t=$(token)
  if [ -n "$t" ]; then curl -fsS -H "Authorization: Bearer $t" "https://api.github.com/$1"
  else curl -fsS "https://api.github.com/$1"; fi
}
run_of() {  # imprime "id status conclusion" do run do Build para o sha
  api "repos/$REPO/actions/runs?head_sha=$1&per_page=20" | python3 -c '
import json,sys
runs=[r for r in json.load(sys.stdin)["workflow_runs"] if r["name"]=="Build"]
print(*(runs and [runs[0]["id"],runs[0]["status"],runs[0]["conclusion"]] or ["none","none","none"]))'
}
full_sha() { git rev-parse "${1:-HEAD}"; }

cmd=${1:-help}; shift || true
case "$cmd" in
check)
  python3 tools/check_translations.py
  python3 -m unittest discover -s tests -q
  python3 tools/package_release.py --check
  ;;
ci)
  sha=$(full_sha "${1:-HEAD}")
  echo "commit $sha"
  for i in $(seq 1 60); do
    read -r id st co < <(run_of "$sha")
    if [ "$st" = completed ]; then break; fi
    echo "  run $id: ${st} (esperando 30 s)"; sleep 30
  done
  echo "run $id: $st / $co  https://github.com/$REPO/actions/runs/$id"
  if [ "$co" != success ] && [ "$id" != none ]; then
    job=$(api "repos/$REPO/actions/runs/$id/jobs" | python3 -c 'import json,sys;print(json.load(sys.stdin)["jobs"][0]["id"])')
    api "repos/$REPO/check-runs/$job/annotations?per_page=100" | python3 -c '
import json,sys
for a in json.load(sys.stdin):
    if "Node.js" in a["message"]: continue
    print("=====", a["annotation_level"], a.get("path"), a.get("start_line")); print(a["message"][-3000:])'
    exit 1
  fi
  ;;
artifact)
  sha=$(full_sha "${1:-HEAD}"); dir=${2:-dist/artifact}
  read -r id st co < <(run_of "$sha")
  [ "$co" = success ] || { echo "Build de $sha não terminou com sucesso ($st/$co); rode: driver.sh ci $sha" >&2; exit 1; }
  url=$(api "repos/$REPO/actions/runs/$id/artifacts" | python3 -c 'import json,sys;a=json.load(sys.stdin)["artifacts"];print(a[0]["archive_download_url"] if a and not a[0]["expired"] else "")')
  [ -n "$url" ] || { echo "Run $id sem artefato (expirou?)" >&2; exit 1; }
  t=$(token); [ -n "$t" ] || { echo "Baixar artefato exige token (GH_TOKEN ou credencial do git)" >&2; exit 1; }
  rm -rf "$dir"; mkdir -p "$dir"
  curl -fsSL -H "Authorization: Bearer $t" -o "$dir.zip" "$url"
  unzip -q "$dir.zip" -d "$dir" && rm "$dir.zip"
  echo "artefato do run $id em $dir:"; find "$dir" -maxdepth 3 | sort | head -40
  ;;
package)
  [ $# -ge 2 ] || { echo "uso: driver.sh package <dir do artefato> <versão>" >&2; exit 2; }
  # Zip de teste: copia fomod/, patches/, CMakeLists.txt e o CHANGELOG para uma pasta temporária, com a seção
  # [Não lançado] renomeada para a versão pedida (o packager exige '## [X.Y.Z]'). O repositório não muda.
  art=$(realpath "$1"); tmp=$(mktemp -d)
  cp -r fomod patches CMakeLists.txt "$tmp/"
  sed "s/^## \[Não lançado\]/## [$2]/" CHANGELOG.md > "$tmp/CHANGELOG.md"
  python3 tools/package_release.py --artifact "$art" --version "$2" --out "$ROOT/dist" \
    --repo-root "$tmp" --allow-version-mismatch
  rm -rf "$tmp"
  unzip -l "dist/AgeOfSkyrim-$2.zip" | tail -n +4 | head -20
  ;;
perf)
  [ $# -ge 1 ] || { echo "uso: driver.sh perf <AgeOfSkyrim.log>" >&2; exit 2; }
  python3 tools/perf_report.py "$@"
  ;;
*)
  sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
  ;;
esac
