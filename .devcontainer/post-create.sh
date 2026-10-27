#!/usr/bin/env bash
set -euo pipefail

# Volume recém-criado vem com dono root.
sudo chown -R vscode:vscode /home/vscode/.claude

# Claude Code CLI (a extensão do VS Code também usa a config em CLAUDE_CONFIG_DIR).
sudo npm install -g @anthropic-ai/claude-code

# Submódulo da CommonLibSSE-NG (necessário para o clangd enxergar os headers).
git config --global --add safe.directory "$(pwd)"
git submodule update --init --recursive

echo
echo "Pronto. Rode 'claude' no terminal e faça login com a conta PESSOAL."
