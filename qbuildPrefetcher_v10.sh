#!/bin/sh
arch="${arch:-glc}"
NUM_CORES="${NUM_CORES:-2}"
TRACE_PERCENT="${TRACE_PERCENT:-20}"
tracesDirName="${tracesDirName:-traces}"
isDebug="${isDebug:--1}"
doMinSim="${doMinSim:-0}"
isProfile="${isProfile:-0}"
# Check for CLI args to override prefetchers
[ -n "$1" ] && prefetcher_L1="$1"
[ -n "$2" ] && prefetcher_L2="$2"
TRACE_PERCENT="${TRACE_PERCENT:-20}"
tracesDirName="${tracesDirName:-traces}"
[ -n "$3" ] && prefetcher_L3="$3"

champsimDirName=champsim_v10
PfBuilder=qbuildPrefetcher_v10
PfRunner=qrun_champsim_v10

# Build
echo "=== Building Champsim with prefetchers: ${prefetcher_L1}, ${prefetcher_L2}, ${prefetcher_L3} ==="
echo "Cores: ${NUM_CORES}"
cd "${champsimDirName}" || exit 1
./build_champsim_parallel.sh "${arch}_${NUM_CORES}c" "${prefetcher_L1}" "${prefetcher_L2}" "${prefetcher_L3}" "${NUM_CORES}" || exit 1
cd ..
