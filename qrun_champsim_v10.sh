#!/bin/sh

# Load config
arch="${arch:-glc}"
NUM_CORES="${NUM_CORES:-2}"
TRACE_PERCENT="${TRACE_PERCENT:-20}"
tracesDirName="${tracesDirName:-traces}"
isDebug="${isDebug:--1}"
doMinSim="${doMinSim:-0}"
isProfile="${isProfile:-0}"
DB_FNAME="${DB_FNAME:-./champsim_results/champsim_results.db}"

champsimDirName=champsim_v10
PfBuilder=qbuildPrefetcher_v10
PfRunner=qrun_champsim_v10


FAST_WARMUP=500000
FAST_SIM=1000000

# Require 1 argument from user
if [ $# -ne 1 ] && [ $# -ne 4 ] && [ $# -ne 5 ]; then
  echo "Usage: $0 <trace_name_substring> [<L1> <L2> <L3>]"
  exit 1
fi
if [ $# -eq 4 ]; then
  prefetcher_L1="$2"
  prefetcher_L2="$3"
  prefetcher_L3="$4"
fi
 if [ $# -eq 5 ]; then
   prefetcher_L1="$2"
   prefetcher_L2="$3"
   prefetcher_L3="$4"
   SHOW_BIN_RUN=1
 fi



# Match trace based on user arg
trace_file=$(find "${tracesDirName}" -maxdepth 1 -name "*$1*.xz" | head -n 1)
trace_name=$(basename "$trace_file" .champsimtrace.xz)

case "$trace_name" in
    [0-9][0-9][0-9].*)
        # SPEC traces: fixed values
        WARMUP="50000000"; SIM="200000000"
        echo "SPEC trace: $trace_name"
        ;;
    LLM*)
        # Extract number before _*M (331, 125, etc.)
        trace_count=$(echo "$trace_name" | sed 's/.*_\([0-9]\+\)M.*/\1/')
        
        # Convert to simTraces_num and use your existing calculation
        simTraces_num=$((trace_count * 1000000))
        percent=$(( (simTraces_num * TRACE_PERCENT) / 100 ))
        traceBoundround_up=$(( (percent / 1000000) * 1000000 ))
        toWarmUp=$(( (percent / 1000000) * 1000000 ))
        toSim=$((simTraces_num-toWarmUp-toWarmUp))
        
        WARMUP="$toWarmUp"
        SIM="$toSim"
        
        echo "LLM trace: ${trace_count}M → Warmup=$WARMUP, Sim=$SIM"
        ;;
    *)
        WARMUP="500000000"; SIM="200000000"
        ;;
esac


# simTraces_num=$(echo "$trace_file" | grep -oP 'memTraces\[\K[0-9]+(?=\]_FE)')

# # Compute warmup and simulation instructions from TRACE_PERCENT
# percent=$(( (simTraces_num * $TRACE_PERCENT) / 100 ))
# traceBoundround_up=$(( (percent / 1000000) * 1000000 ))
# toWarmUp=$(( (percent / 1000000) * 1000000 ))
# toSim=$((simTraces_num-toWarmUp-toWarmUp))

echo "Total instructions: $simTraces_num"
echo "Warmup instructions: $toWarmUp"
echo "Simulation instructions: $toSim"

if [ -z "$trace_file" ]; then
  echo "No trace file found containing: $1"
  exit 1
fi

BIN="./${champsimDirName}/bin/perceptron-${prefetcher_L1}-${prefetcher_L2}-${prefetcher_L3}-lru-${NUM_CORES}core"
# Use exact binary name
# PERF="perf record -e cycles -F 13999  --call-graph=dwarf,512 --mmap-pages=32768 --buildid-all -o ./perf_v5.data"
PERF="perf record -e cycles:pp --call-graph dwarf -F 13999  --call-graph=fp --mmap-pages=16384 --buildid-all -o ./perf_v5.data"

PERF_COLLECT_INTERVAL=900 # seconds (15 minutes)
PERF_BEGIN_AT_TIME=5400000 # ms 
PERF_DATA_FILE=perf_v2_later.data
# PERF="perf record -e cycles -F 7019 --call-graph=dwarf,512 --delay=$PERF_BEGIN_AT_TIME --mmap-pages=32768 --buildid-all -o ./$PERF_DATA_FILE"
# PERF="perf record -e cycles -F 7019 --call-graph=dwarf,512 --delay=1800000 --mmap-pages=32768 --buildid-all -o ./perf_v4.data -- sleep 900"

# PERF="AMDuProfCLI collect --config ibs --cpu 48-63 --affinity 48-63 -o amdBin.perf"
# PERF="sudo perf mem record  -a  --ldlat=1  --data  --phys-data  --sample-cpu  -g  --call-graph dwarf,65528  -T  -P  -v  -o champsim_memory.data"

# $PERF ${BINARY} -warmup_instructions ${N_WARM}000000 -simulation_instructions ${N_SIM}000000 ${OPTION} -traces ${TRACE}

tmpSim=$toSim
tmpWarm=$toWarmUp
#if [ "$doMinSim" -eq 1 ]; then
#    toWarmUp=10000
#    toSim=20000000
#    # if [ "$isProfile" -eq 1 ]; then
#    #     toWarmUp=$tmpWarm
#    #     toSim=$tmpSim
#    # fi
#fi
echo "DB: $toSim | $toWarmUp | $tmpSim | $tmpWarm | $doMinSim | $isProfile"
# Run binary on matched trace
echo "=== Running on trace: $(basename "$trace_file") === Warmup: $WARMUP | Sim: $SIM ==="
echo "Binary: $BIN"
args=$(yes "$trace_file" | head -n "$NUM_CORES" | paste -sd' ')

# echo "$BIN -warmup $WARMUP -simulation_instructions $SIM -traces $args"
# exit 0


# Set warmup and simulation values
if [ "$doMinSim" -eq 1 ]; then
    WARMUP=$FAST_WARMUP
    SIM=$FAST_SIM
    echo "Doing Min Run"
else
    # Use the calculated WARMUP and SIM from trace parsing above
    echo "Using calculated values: Warmup=$WARMUP, Sim=$SIM"
fi

 if [ "$SHOW_BIN_RUN" -eq 1 ]; then
     echo "BINARY RUN:"
     echo "$BIN -warmup $WARMUP -simulation_instructions $SIM -traces $args"
    #  gdb --args "$BIN" -warmup "$WARMUP" -simulation_instructions "$SIM" -traces "$args" -ex run
    gdb "$BIN" \
    -ex "r -warmup $WARMUP -simulation_instructions $SIM -traces $args"
    
    # gdb --batch \
    # -ex "run -warmup $WARMUP -simulation_instructions $SIM -traces $args" \
    # -ex "quit" \
    # --args "$BIN"
#     gdbserver localhost:1234 ./champsim_v6_Edge/bin/perceptron-no-no-no-lru-1core \
# -warmup 50000000 -simulation_instructions 200000000 -traces traces/649.fotonik3d_s-10881B.champsimtrace.xz
# gdbserver :1234 ./champsim_v6_Edge/bin/perceptron-no-no-no-lru-1core -warmup 50000000 -simulation_instructions 200000000 -traces traces/649.fotonik3d_s-10881B.champsimtrace.xz
# gdb -ex "cd $(pwd)"     -ex "file ./champsim_v6_Edge/bin/perceptron-no-no-no-lru-1core"     -ex "break main.cc:578"     -ex 'run -warmup 50000000 -simulation_instructions 200000000 -traces traces/649.fotonik3d_s-10881B.champsimtrace.xz'

     exit 0

 fi


# Run simulation
if [ "$isProfile" -eq 1 ]; then
    echo "Profiling enabled"
    echo ""$BIN" -warmup "$WARMUP" -simulation_instructions "$SIM" -traces $args & $PERF -p $! sleep XXX"
    # "$BIN" -warmup "$WARMUP" -simulation_instructions "$SIM" -traces $args & $PERF -p $! sleep $PERF_COLLECT_INTERVAL
    $PERF "$BIN" -warmup "$WARMUP" -simulation_instructions "$SIM" \
        --db "$DB_FNAME" --arch "${arch:-glc}" --bypass "${BYPASS_MODEL:-none}" \
        --pf_l1 "$prefetcher_L1" --pf_l2 "$prefetcher_L2" --pf_l3 "$prefetcher_L3" \
        -traces $args
    # gdb -ex "cd $(pwd)" \
    # -ex "set non-stop on" \
    # -ex "set pagination off" \
    # -ex "file ./champsim_v6_Edge/bin/perceptron-no-no-no-lru-1core" \
    # -ex "break main.cc:578" \
    # -ex "run -warmup 50000000 -simulation_instructions 200000000 -traces traces/649.fotonik3d_s-10881B.champsimtrace.xz" \
    # -ex "continue &"

else
    echo "Profiling disabled" 
    "$BIN" -warmup "$WARMUP" -simulation_instructions "$SIM" \
        --db "$DB_FNAME" --arch "${arch:-glc}" --bypass "${BYPASS_MODEL:-none}" \
        --pf_l1 "$prefetcher_L1" --pf_l2 "$prefetcher_L2" --pf_l3 "$prefetcher_L3" \
        -traces $args
fi


# if [ "$isProfile" -eq 1 ]; then
#   echo "Profiling enabled"
#   echo "$PERF $BIN   -warmup 100000   -simulation_instructions 10000000   -traces $args" 
#   $PERF "$BIN"   -warmup 1000000   -simulation_instructions 5000000   -traces $args # "$trace_file" "${trace_file}" # "${trace_file}" "${trace_file}" "$trace_file" "${trace_file}" "${trace_file}" "${trace_file}" 
# fi
# if [ "$doMinSim" -eq 1 ]; then
#   echo "Doing Min Run"
#   "$BIN"   -warmup 4000000  -simulation_instructions 13000000 -traces $args # "$trace_file" "${trace_file}" # "${trace_file}" "${trace_file}" "$trace_file" "${trace_file}" "${trace_file}" "${trace_file}" 
# fi
# if [ "$isProfile" -eq 0 ]; then
#   echo "Profiling disabled"
#   if [ "$doMinSim" -eq 0 ]; then
#   "$BIN"   -warmup 50000000   -simulation_instructions 200000000   -traces $args # "$trace_file" "${trace_file}" # "${trace_file}" "${trace_file}" "$trace_file" "${trace_file}" "${trace_file}" "${trace_file}"
#   # "$BIN"   -warmup 50000000   -simulation_instructions 200000000   -traces $args # "$trace_file" "${trace_file}" # "${trace_file}" "${trace_file}" "$trace_file" "${trace_file}" "${trace_file}" "${trace_file}"
#   # "$BIN"   -warmup 0   -simulation_instructions 17000000   -traces $args # "$trace_file" "${trace_file}" # "${trace_file}" "${trace_file}" "$trace_file" "${trace_file}" "${trace_file}" "${trace_file}"
#   fi
# fi

# "$BIN" \
#   -warmup 100000 \
#   -simulation_instructions 19000000 \
#   -traces "${trace_file}" "${trace_file}" "${trace_file}" "${trace_file}"
