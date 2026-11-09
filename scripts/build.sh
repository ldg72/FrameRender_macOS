#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$task_root"
cmake --preset macos
cmake --build --preset macos --parallel 6
ctest --preset macos
