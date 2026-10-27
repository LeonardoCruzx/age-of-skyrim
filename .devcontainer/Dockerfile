FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        clangd clang-format cmake ninja-build ripgrep jq zip unzip \
    && curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt-get install -y nodejs \
    && npm install -g @anthropic-ai/claude-code \
    && rm -rf /var/lib/apt/lists/*

# Pasta criada com dono vscode: o volume nomeado herda essa permissão na primeira criação.
RUN mkdir -p /home/vscode/.claude && chown -R vscode:vscode /home/vscode/.claude \
    && git config --system --add safe.directory '*'
