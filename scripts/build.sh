#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${QVANN_DEPS_DIR:?Set QVANN_DEPS_DIR to the external dependency directory}"
FAISS="$QVANN_DEPS_DIR/LearnedEarlyTer-20SIGMOD"
mkdir -p "$ROOT/build"
"${CXX:-g++}" -O3 -DNDEBUG -std=c++17 -fopenmp \
  -I"$ROOT/src" -I"$QVANN_DEPS_DIR" -I"$FAISS" -I"$FAISS/include" \
  "$ROOT/src/qvann_benchmark.cpp" \
  -L"$FAISS" -L"$FAISS/lib" \
  "-Wl,-rpath,$FAISS" "-Wl,-rpath,$FAISS/lib" \
  "-Wl,-rpath,$QVANN_DEPS_DIR/runtime/lib" \
  -lfaiss -l_lightgbm -lcrypto -lpthread -ldl -o "$ROOT/build/qvann"
