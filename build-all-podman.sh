#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

# Make the Docker-compatible Podman wrapper available.
export PATH="$PWD/.local-bin:$PATH"

# Activate Emscripten
source "$HOME/emsdk/emsdk_env.sh"

# Activate the project's Python environment afterwards
if [ -f "$PWD/.venv/bin/activate" ]; then
    source "$PWD/.venv/bin/activate"
else
    echo "ERROR: .venv not found" >&2
    exit 1
fi

echo "=== Building firmware with Podman ==="
sh build.sh

echo
echo "=== Building web emulator with Emscripten ==="
sh web/emu/build.sh

echo
echo "=== All builds completed successfully ==="
EOF

chmod +x build-all.sh

