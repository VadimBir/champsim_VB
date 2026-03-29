#!/bin/bash
set -euo pipefail

if [ "$#" -ne 5 ]; then
    echo "Illegal number of parameters"
    echo "Usage: ./build_champsim_parallel.sh [core_arch] [l1d_pref] [l2c_pref] [llc_pref] [num_core]"
    exit 1
fi

CORE_UARCH=$1
L1D_PREFETCHER=$2
L2C_PREFETCHER=$3
LLC_PREFETCHER=$4
NUM_CORE=$5

BRANCH=perceptron
LLC_REPLACEMENT=lru
PGO_FILE="profiles_v4/PGO_4_SPEC_LLM.profdata"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_BASE="/tmp/champsim_builds"
mkdir -p "$WORK_BASE"
WORKDIR="$(mktemp -d "$WORK_BASE/build.XXXXXX")"

JOBS="${JOBS:-$(nproc --ignore=6)}"

if command -v tput >/dev/null 2>&1; then
    BOLD=$(tput bold)
    NORMAL=$(tput sgr0)
else
    BOLD=""
    NORMAL=""
fi

cleanup() {
    status=$?
    if [ $status -eq 0 ]; then
        rm -rf "$WORKDIR"
    else
        echo ""
        echo "[ERROR] Build failed. Workdir kept for debug:"
        echo "        $WORKDIR"
    fi
    exit $status
}
trap cleanup EXIT INT TERM

re='^[0-9]+$'
if ! [[ "$NUM_CORE" =~ $re ]]; then
    echo "[ERROR] num_core is NOT a number <${NUM_CORE}>"
    exit 1
fi

if [ "$NUM_CORE" -lt 1 ]; then
    echo "[ERROR] Number of cores must be >= 1"
    exit 1
fi

if [ ! -f "$ROOT/inc/Arch/${CORE_UARCH}.h" ]; then
    echo "[ERROR] Cannot find core architecture definition ${CORE_UARCH}"
    find "$ROOT/inc/Arch" -name "*.h"
    exit 1
fi

if [ ! -f "$ROOT/branch/${BRANCH}.bpred" ]; then
    echo "[ERROR] Cannot find branch predictor ${BRANCH}"
    find "$ROOT/branch" -name "*.bpred"
    exit 1
fi

if [ ! -f "$ROOT/prefetcher/${L1D_PREFETCHER}.l1d_pref" ]; then
    echo "[ERROR] Cannot find L1D prefetcher ${L1D_PREFETCHER}"
    find "$ROOT/prefetcher" -name "*.l1d_pref"
    exit 1
fi

if [ ! -f "$ROOT/prefetcher/${L2C_PREFETCHER}.l2c_pref" ]; then
    echo "[ERROR] Cannot find L2C prefetcher ${L2C_PREFETCHER}"
    find "$ROOT/prefetcher" -name "*.l2c_pref"
    exit 1
fi

if [ ! -f "$ROOT/prefetcher/${LLC_PREFETCHER}.llc_pref" ]; then
    echo "[ERROR] Cannot find LLC prefetcher ${LLC_PREFETCHER}"
    find "$ROOT/prefetcher" -name "*.llc_pref"
    exit 1
fi

if [ ! -f "$ROOT/replacement/${LLC_REPLACEMENT}.llc_repl" ]; then
    echo "[ERROR] Cannot find LLC replacement policy ${LLC_REPLACEMENT}"
    find "$ROOT/replacement" -name "*.llc_repl"
    exit 1
fi

if [ ! -f "$ROOT/$PGO_FILE" ]; then
    echo "[ERROR] Missing required PGO file: $ROOT/$PGO_FILE"
    exit 1
fi

mkdir -p "$WORKDIR"

rsync -a --prune-empty-dirs \
    --include='/Makefile' \
    --include='/inc/***' \
    --include='/src/***' \
    --include='/branch/***' \
    --include='/prefetcher/***' \
    --include='/replacement/***' \
    --include='/profiles_v4/' \
    --include='/profiles_v4/PGO_4_SPEC_LLM.profdata' \
    --exclude='/obj/***' \
    --exclude='/bin/***' \
    --exclude='/tracer/***' \
    --exclude='*.o' \
    --exclude='*.d' \
    --exclude='*.bak' \
    --exclude='*' \
    "$ROOT/" "$WORKDIR/"

mkdir -p "$WORKDIR/bin"

cp "$ROOT/inc/Arch/${CORE_UARCH}.h" "$WORKDIR/inc/defs.h"
cp "$ROOT/branch/${BRANCH}.bpred" "$WORKDIR/branch/branch_predictor.cc"
cp "$ROOT/prefetcher/${L1D_PREFETCHER}.l1d_pref" "$WORKDIR/prefetcher/l1d_prefetcher.cc"
cp "$ROOT/prefetcher/${L2C_PREFETCHER}.l2c_pref" "$WORKDIR/prefetcher/l2c_prefetcher.cc"
cp "$ROOT/prefetcher/${LLC_PREFETCHER}.llc_pref" "$WORKDIR/prefetcher/llc_prefetcher.cc"
cp "$ROOT/replacement/${LLC_REPLACEMENT}.llc_repl" "$WORKDIR/replacement/llc_replacement.cc"

sed -i.bak "s/^#define NUM_CPUS [0-9][0-9]*/#define NUM_CPUS ${NUM_CORE}/" "$WORKDIR/inc/champsim.h"

echo "Building ChampSim in isolated workdir: $WORKDIR"
echo "Cores: ${NUM_CORE} | Core architecture: ${CORE_UARCH}"
echo "L1D: ${L1D_PREFETCHER} | L2C: ${L2C_PREFETCHER} | LLC: ${LLC_PREFETCHER}"
echo "Replacement: ${LLC_REPLACEMENT}"
echo "PGO: ${PGO_FILE}"
echo

(
    cd "$WORKDIR"
    make -j"$JOBS" run_clang
)

if [ ! -f "$WORKDIR/bin/champsim" ]; then
    echo "${BOLD}ChampSim build FAILED!${NORMAL}"
    exit 1
fi

mkdir -p "$ROOT/bin"

BINARY_NAME="${BRANCH}-${L1D_PREFETCHER}-${L2C_PREFETCHER}-${LLC_PREFETCHER}-${LLC_REPLACEMENT}-${NUM_CORE}core"
rm -f "$ROOT/bin/${BINARY_NAME}"
cp "$WORKDIR/bin/champsim" "$ROOT/bin/${BINARY_NAME}"

echo ""
echo "${BOLD}ChampSim is successfully built${NORMAL}"
echo "Branch Predictor: ${BRANCH}"
echo "L1D Prefetcher: ${L1D_PREFETCHER}"
echo "L2C Prefetcher: ${L2C_PREFETCHER}"
echo "LLC Prefetcher: ${LLC_PREFETCHER}"
echo "LLC Replacement: ${LLC_REPLACEMENT}"
echo "Cores: ${NUM_CORE}"
echo "Binary: $ROOT/bin/${BINARY_NAME}"