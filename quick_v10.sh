#!/bin/bash

eval $(grep -v '^#' config_fast.ini | xargs -d '\n')
# trace="Pythi"
# trace="OPT"
arch="${arch:-glc}"
TRACE_PERCENT="${TRACE_PERCENT:-20}"
tracesDirName="${tracesDirName:-traces}"
# trace="LLM512.GPT"
# trace="654.roms_s-1007B"
# trace="410.bwaves-2097B"
# trace="429.mcf-22B"
# trace="459.GemsFDTD-1169B"
# trace="429.mcf-217B"
# trace="473.astar-359B"
trace="LLM256.Pythia-70M"
# trace="LLM256.OPT"
# trace="mcf-22B"
# trace="403.gcc-17B"
# trace="649.fotonik3d_s-10881"
# trace="605.mcf_s-1644B"
# trace="445.gobmk-30B"
# trace="483.xalancbmk-127B.champsimtrace"

champsimDirName=champsim_v10
PfBuilder=qbuildPrefetcher_v10
PfRunner=qrun_champsim_v10
# trace="623.xalancbmk_s-700B" # PATT is very very bad
L1="visio"
L2="no"
L3="no"
L1=$prefetcher_L1
L2=$prefetcher_L2
L3=$prefetcher_L3

ByP_Type="Pf Based ByP"
DB_FNAME="./CHAMPSIM_RESULTS.db"
BYPASS_MODEL="none"
sed -i "s/^isDebug=.*/isDebug=-1/" config_fast.ini
sed -i "s/^doMinSim=.*/doMinSim=0/" config_fast.ini
sed -i "s/^NUM_CORES=.*/NUM_CORES=2/" config_fast.ini
sed -i "s/^isProfile=.*/isProfile=0/" config_fast.ini
PROCESSES_NUM=1

parse_args() {
  echo "Function called with $# arguments: $@"
  shopt -s nocasematch
  while [[ $# -gt 0 ]]; do
    
    case $1 in
    -1|--L1)
      [[ -z "$2" || "$2" =~ ^- ]] && { echo "Error: $1 requires a value"; exit 1; }
      L1="$2"
      shift 2 ;;
    -2|--L2)
      [[ -z "$2" || "$2" =~ ^- ]] && { echo "Error: $1 requires a value"; exit 1; }
      L2="$2"
      shift 2 ;;
    -3|--L3)
      [[ -z "$2" || "$2" =~ ^- ]] && { echo "Error: $1 requires a value"; exit 1; }
      L3="$2"
      shift 2 ;;
    -bypca|-ByPca|-ByPcache|-CaByP)
        ByP_Type=$(grep -Eq '^[[:space:]]*#define[[:space:]]+BYPASS_L1D_OnNewMiss' "./$champsimDirName/inc/champsim.h" && echo "Cache Based ByP" || echo "Pf Based ByP"); shift ;;
    -ByPpf|-ByPpref|-PfByP)
        sed -i 's|^[[:space:]]*//\?[[:space:]]*#define BYPASS_L1D_OnNewMiss|// #define BYPASS_L1D_OnNewMiss|' "./$champsimDirName/inc/champsim.h"; shift ;;
    -ByPModel|-ByPmodel|-bypmodel)
        model="$2"

        src=$(find "$champsimDirName/src/ByP_Models" -type f -name "${model}.bypass" -print -quit)
        dst="$champsimDirName/src/ooo_ByP_Model.cc"
        # src="$champsimDirName/src/ByP_Models/${model}.bypass"
        # dst="$champsimDirName/src/ooo_ByP_Model.cc"

        if [[ ! -f "$src" ]]; then
            echo -e "\033[1;91mERROR: bypass model '$model' not found\033[0m"
            exit 1
        fi

        cp "$src" "$dst"

        echo -e "\033[1;91mLoaded bypass model: $model\033[0m"
        BYPASS_MODEL="$model"
        shift 2
    ;;
    -d|--debug)
      [[ -z "$2" || ( "$2" =~ ^- && ! "$2" =~ ^-[0-9]+$ ) ]] && { echo "Error: --debug requires a value"; exit 1; }
      sed -i "s/^isDebug=.*/isDebug=$2/" config_fast.ini
      shift 2 ;;
    -f|--fast)
      sed -i "s/^doMinSim=.*/doMinSim=1/" config_fast.ini
      shift ;;
    -c|--cores)
      [[ -z "$2" || "$2" =~ ^- ]] && { echo "Error: --cores requires a value"; exit 1; }
      sed -i "s/^NUM_CORES=.*/NUM_CORES=$2/" config_fast.ini
      shift 2 ;;
    --profile)
      sed -i "s/^isProfile=.*/isProfile=1/" config_fast.ini
      shift ;;
    --trace)
      trace="$2"
      shift 2;;
    -p|--processes_num)
      [[ -z "$2" || "$2" =~ ^- ]] && { echo "Error: --processes_num requires a value"; exit 1; }
      PROCESSES_NUM="$2"; shift 2 ;;
    --db)
      [[ -z "$2" || "$2" =~ ^- ]] && { echo "Error: --db requires a value"; exit 1; }
      DB_FNAME="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Error: Unknown option: $1"; echo ""; usage ;;
  esac
  
  done
}

# # print all vars 
# echo "Initial vars:"
# echo "L1: $L1"
# echo "L2: $L2"
# echo "L3: $L3"
# echo "trace: $trace"
# echo "PROCESSES_NUM: $PROCESSES_NUM"
# echo "isDebug: $(grep isDebug config_fast.ini)"
# echo "doMinSim: $(grep doMinSim config_fast.ini)"
# echo "NUM_CORES: $(grep NUM_CORES config_fast.ini)"
# echo "isProfile: $(grep isProfile config_fast.ini)"
# echo ""

# exit 0

usage() {
  echo ""
  echo "Usage:"
  echo "  $0 [OPTIONS]"
  echo ""
  echo "Prefetcher selection:"
  echo "  -1, --L1 PREF             Select L1 prefetcher"
  echo "  -2, --L2 PREF             Select L2 prefetcher"
  echo "  -3, --L3 PREF             Select LLC prefetcher"
  echo ""
  echo "Bypass control (choose one):"
  echo "  --ByPca, --ByPcache       Cache performs bypass"
  echo "  --ByPpf, --ByPpref        Prefetcher performs bypass"
  echo ""
  echo "Bypass control (choose one):"
  echo "  -ByPModel       if using ByPca then a model file can be selected"
  echo ""
  echo "Simulation configuration:"
  echo "  --trace FILE              Trace file to run"
  echo "  -c, --cores N             Number of cores"
  echo "  -p, --processes_num N     Number of parallel processes"
  echo ""
  echo "Debug levels:"
  echo "  -d, --debug LEVEL"
  echo "        2   Full output: build + simulation"
  echo "        1   Full simulation output"
  echo "        0   Print IPC and result"
  echo "       -1   Print IPC only"
  echo "       -2   Time only"
  echo ""
  echo "Execution modes:"
  echo "  -f, --fast                Enable minimum simulation"
  echo "  --profile                 Enable profiling"
  echo ""
  echo "Other:"
  echo "  -h, --help                Show this help"
  echo ""
  echo "Examples:"
  echo "  $0 -1 no -2 no -3 ipcp -c 8 -p 4 --trace trace.champsimtrace"
  echo "  $0 --L1 next_line --L2 spp --L3 no --ByPca --debug 1"
  echo "  $0 -1 no -2 no -3 no --ByPpf -f --trace trace.champsimtrace"
  echo ""
  exit 1
}

parse_args "$@"
# Store argument-set values before config reload
ARG_L1="$L1"
ARG_L2="$L2" 
ARG_L3="$L3"
eval $(grep -v '^#' config_fast.ini | xargs -d '\n')
# Restore argument values after config reload
L1="$ARG_L1"
L2="$ARG_L2"
L3="$ARG_L3"
export DB_FNAME BYPASS_MODEL

echo "L1" "$L1"
cat config_fast.ini | grep -E 'isDebug=|doMinSim=|NUM_CORES=|isProfile='
echo -e "\e[31m$ByP_Type\e[0m"
status=0
# arr of BUF_SZ values 
BUF_SZ=(512)
# echo "BUF_SZ: ${BUF_SZ[*]}"
for buf in "${BUF_SZ[@]}"; do
  # sed -i "s/^#define[ \t]\+BUF_SZ[ \t]\+[0-9]\+/#define BUF_SZ $buf/" ./pin_champsim/inc/ooo_cpu.h
  echo "=== BUF_SZ = $buf === TRACE: $trace"
  # sed -i 's/^#define[ \t]\+BUF_SZ[ \t]\+[0-9]\+/#define BUF_SZ '${buf}'/' ./pin_champsim/inc/ooo_cpu.h
  for n in $PROCESSES_NUM; do
    START=$(date +%s%3N)
    # mkdir -p profiles
    # export LLVM_PROFILE_FILE="profiles/PGO_${trace}_champsim_%p.profraw"
    # export LLVM_PROFILE_FILE="profiles/test_profile.profraw"
    # echo "LLVM_PROFILE_FILE: $LLVM_PROFILE_FILE"
    mid=$((n - 1))
    pids=()
    # if debug is 2 we do verbose build else void output 
    status=0
    echo "    *** BUILDING ***"
    if [ "$isDebug" -eq 2 ]; then
      echo "DEBUG: Building prefetcher with L1: $L1, L2: $L2, L3: $L3"
      output=&(./"$PfBuilder".sh "$L1" "$L2" "$L3") # 2>&1 | grep -A 3 -E "error" # > /dev/null 2>&1 # toDel.log 2>&1
      echo "$output"
      status=$?
    else
      output=$(./"$PfBuilder".sh "$L1" "$L2" "$L3" 2>&1)
      status=$?
      # set output colour red and use it in the error output 
      redColour="\033[1;91m"
      resetColour="\033[0m"
      greenColour="\033[1;92m"
      if [ $status -ne 0 ]; then
        # echo "Build failed (exit code $status)"
        echo -e "${redColour}*** BUILD FAIL ***"
        # echo "$output" | grep -A 3 -E "error"
        echo -e "$output" | grep -B 4 -A 4 -E "error|Error" # | sed "s/^/\t/;s/$//"
        echo -e "${resetColour}"
        exit 1
      fi
      if [ $status -eq 0 ]; then
        echo -e "${greenColour}Build successful${resetColour}"
      fi
    fi
    if [ "$isDebug" -eq 10 ]; then
          # ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" # | grep -E "Finished CPU|now IPC :" # # > /dev/null 2>&1
          echo "./"$PfRunner".sh \"$trace\" \"$L1\" \"$L2\" \"$L3\""
          ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" "1"
          exit 0
    fi
  
    echo "=== ;$n; processes ==="
    for ((i = 0; i < n; i++)); do
      {
        # Extract context lines for visibility
        # Detect fatal condition
        # if echo "$output" | grep -qi "error:"; then
        #     echo "*** BUILD FAIL ***"
        #     exit 1
        # fi
        # START=$(date +%s%3N)
        # OUT=$(./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" | grep "Finished" | head -n 1)
        # perf record -F 13500 -g -o perf.data ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" #| grep "Finished" | head -n 1)
        #      perf record -F 13499 --call-graph dwarf -o perf.data ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" #| grep -E "Finished CPU|now IPC :"

        if [ "$isDebug" -gt 0 ]; then
          ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" # | grep -E "Finished CPU|now IPC :" # # > /dev/null 2>&1 
        else
          if [ "$isDebug" -lt 0 ]; then
            if [ "$isDebug" -eq -2 ]; then
              ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" | grep -E "Finished CPU|FINAL ROI CORE AVG IPC:|Degree/Access Ratio|Global Hit Rate|DEADLOCK|SANITY|failed|Aborted" # > /dev/null 2>&1 
            else
              ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" | grep -E "Finished CPU|FINAL ROI CORE AVG IPC:|CFG|Degree/Access Ratio|Global Hit Rate|DEADLOCK|SANITY|trace_|_instructions |failed|Aborted" # # > /dev/null 2>&1 
            fi
          else 
            ./"$PfRunner".sh "$trace" "$L1" "$L2" "$L3" | grep -E "Finished CPU|FINAL ROI CORE AVG IPC:|CFG|Degree/Access Ratio|Global Hit Rate|DEADLOCK|SANITY|trace_|_instructions |now IPC:|failed|Aborted" # # > /dev/null 2>&1 
          fi
        fi 
        #>&2; # | grep -E "Finished CPU" #|now IPC :"
        # echo "$n; $((ELAPSED / 1000)) "
        # echo "$full_output" #
      } & 
      pids+=($!)
      sleep 0.1
      # if [ $status -ne 0 ]; then
      #     echo "FAIL:q run"$PfRunner".sh exited with code $status"
      #     exit $status
      # fi
    done
    for pid in "${pids[@]}"; do wait "$pid"; done
    END=$(date +%s%3N)
    ELAPSED=$((END - START))
    echo "[DONE] BUF_SZ = $buf | $n processes | time: ${ELAPSED} ms | Effectiveness: $(awk "BEGIN { printf \"%.6f\", ($n * 1000) / $ELAPSED }") s"

  done
done
