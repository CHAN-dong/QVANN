#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX_BIN="${CXX:-g++}"
FAISS_DIR="$ROOT_DIR/third_party/LearnedEarlyTer-20SIGMOD"
RUNTIME_DIR="$ROOT_DIR/third_party/runtime/lib"
BUILD_DIR="$ROOT_DIR/build"

mkdir -p "$BUILD_DIR"

COMMON_FLAGS=(
  -O3 -DNDEBUG -std=c++17 -Wall -Wextra -Wpedantic
  -I"$ROOT_DIR/src"
  -I"$ROOT_DIR/third_party"
  -I"$FAISS_DIR"
  -I"$FAISS_DIR/include"
)

LINK_FLAGS=(
  -L"$FAISS_DIR"
  -L"$FAISS_DIR/lib"
  -Wl,-rpath,'$ORIGIN/../third_party/LearnedEarlyTer-20SIGMOD'
  -Wl,-rpath,'$ORIGIN/../third_party/LearnedEarlyTer-20SIGMOD/lib'
  -lfaiss -l_lightgbm -lcrypto -lpthread -ldl
)

if [[ -d "$RUNTIME_DIR" ]]; then
  LINK_FLAGS=(
    -L"$RUNTIME_DIR"
    -Wl,-rpath,'$ORIGIN/../third_party/runtime/lib'
    "${LINK_FLAGS[@]}"
    -lopenblas -lgfortran -lquadmath
  )
else
  LINK_FLAGS+=( -lopenblas -llapack )
fi

if [[ -n "${VANN_EXTRA_LIB_DIR:-}" ]]; then
  LINK_FLAGS=( -L"$VANN_EXTRA_LIB_DIR" "${LINK_FLAGS[@]}" )
fi

build_one() {
  local source="$1"
  local output="$2"
  echo "[build] $output"
  "$CXX_BIN" "${COMMON_FLAGS[@]}" "$ROOT_DIR/src/experiments/$source" \
    -o "$BUILD_DIR/$output" "${LINK_FLAGS[@]}"
}

build_one e_budget_relaxed_hnsw.cpp e_budget_relaxed_hnsw_experiment
build_one expansion_centric_merkle.cpp expansion_centric_merkle_experiment
build_one refinement_merkle_ordering.cpp refinement_merkle_ordering_experiment

echo "Built key experiments in $BUILD_DIR"

