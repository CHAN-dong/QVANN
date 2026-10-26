#!/usr/bin/env bash
set -euo pipefail
REPRO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
V3DB_PYTHON="${V3DB_VENV:-${HOME}/.venvs/v3db-repro}/bin/python"
"${V3DB_PYTHON}" "${REPRO_ROOT}/scripts/run_repro.py" --suite "${1:-all}"
if [[ "${1:-all}" == "all" ]]; then
    "${V3DB_PYTHON}" "${REPRO_ROOT}/scripts/summarize.py"
fi
