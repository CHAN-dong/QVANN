#!/usr/bin/env bash
set -euo pipefail
REPRO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="${HOME}/.cargo/bin:${HOME}/.local/bin:${PATH}"
export RUSTUP_TOOLCHAIN=nightly-2026-10-16
export VIRTUAL_ENV="${V3DB_VENV:-${HOME}/.venvs/v3db-repro}"
export CARGO_TARGET_DIR="${V3DB_TARGET_DIR:-${HOME}/.cache/v3db-target}"
export PATH="${VIRTUAL_ENV}/bin:${PATH}"
mkdir -p "${REPRO_ROOT}/logs"
command -v rustup >/dev/null
command -v uv >/dev/null
rustup toolchain install "${RUSTUP_TOOLCHAIN}" --profile minimal
if [[ ! -x "${VIRTUAL_ENV}/bin/python" ]]; then
    uv venv --python 3.12.15 "${VIRTUAL_ENV}"
fi
uv pip install --python "${VIRTUAL_ENV}/bin/python" -r "${REPRO_ROOT}/requirements.lock"
cd "${REPRO_ROOT}/upstream"
test "$(git rev-parse HEAD)" = "$(tr -d '\r\n' < "${REPRO_ROOT}/upstream_commit.txt")"
maturin develop --release --locked 2>&1 | tee "${REPRO_ROOT}/logs/build-pinned.log"
