#!/usr/bin/env bash
set -euo pipefail

# Submódulo da CommonLibSSE-NG (o clangd precisa dos headers).
git submodule update --init --recursive

echo "Pronto. Rode 'claude' no terminal e faça login com a conta PESSOAL."
