#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUTPUT_DIR="${VANN_OUTPUT_DIR:-$ROOT_DIR/results/reproduced}"
RUNTIME_DIR="$ROOT_DIR/third_party/runtime/lib"

for binary in \
  e_budget_relaxed_hnsw_experiment \
  expansion_centric_merkle_experiment \
  refinement_merkle_ordering_experiment; do
  if [[ ! -x "$BUILD_DIR/$binary" ]]; then
    echo "Missing $BUILD_DIR/$binary; run scripts/build_key_experiments.sh first" >&2
    exit 1
  fi
done

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
export OMP_NUM_THREADS="${OMP_NUM_THREADS:-1}"
export OPENBLAS_NUM_THREADS="${OPENBLAS_NUM_THREADS:-1}"
export LD_LIBRARY_PATH="$RUNTIME_DIR:$ROOT_DIR/third_party/LearnedEarlyTer-20SIGMOD:$ROOT_DIR/third_party/LearnedEarlyTer-20SIGMOD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Run in a fresh directory while retaining the relative input paths expected
# by the original experiment sources.
WORK_DIR="$(mktemp -d "$ROOT_DIR/.veriann-run-XXXXXX")"
trap 'rm -rf -- "$WORK_DIR"' EXIT
ln -s "$ROOT_DIR/dataset" "$WORK_DIR/dataset"
ln -s "$ROOT_DIR/merkle_hnsw" "$WORK_DIR/merkle_hnsw"
mkdir "$WORK_DIR/work"
cd "$WORK_DIR/work"
for binary in e_budget_relaxed_hnsw_experiment expansion_centric_merkle_experiment refinement_merkle_ordering_experiment; do
  echo "[run] $binary $(date -Iseconds)"
  "$BUILD_DIR/$binary" 2>&1 | tee "$OUTPUT_DIR/$binary.log"
done

cp e_budget_relaxed_hnsw_q100.csv "$OUTPUT_DIR/"
cp expansion_centric_merkle_q100.csv expansion_centric_merkle_per_query.csv "$OUTPUT_DIR/"
cp refinement_merkle_ordering_q100.csv refinement_merkle_ordering_per_query.csv "$OUTPUT_DIR/"

echo "Reproduced CSV files are in $OUTPUT_DIR"

