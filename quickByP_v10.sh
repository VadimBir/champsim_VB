#!/usr/bin/env bash
set -u
set -o pipefail

# =========================================================
arch="${arch:-glc}"
NUM_CORES="${NUM_CORES:-2}"
prefetcher_L1="${prefetcher_L1:-no}"
prefetcher_L2="${prefetcher_L2:-no}"
prefetcher_L3="${prefetcher_L3:-no}"
TRACE_PERCENT="${TRACE_PERCENT:-20}"
tracesDirName="${tracesDirName:-traces}"
champsimDirName="${champsimDirName:-champsim_v10}"
isDebug="${isDebug:--1}"
isProfile="${isProfile:-0}"
doMinSim="${doMinSim:-0}"
# USER SETTINGS
# =========================================================

OUT_ROOT="./output13"

# =========================================================
# RUNTIME-EDITABLE DURING EXECUTION
# Edit THIS SAME SCRIPT while it runs, save it, and new value
# will be used by wait_for_slot().
# =========================================================
MAX_CONCURRENT_PROCS=6

# Poll interval for slot checking / stale temp cleanup
POLL_SEC=7

# Keep the launch pacing behaviour
LAUNCH_DELAY_SEC=1

# Isolated build temp root
WORK_BASE="/tmp/champsim_builds"
DB_FNAME="${OUT_ROOT:-./champsim_results}/champsim_results.db"

# If temp build dir is older than this and not active anymore, delete it during run
BUILD_STALE_SEC=0

# If old run dir from crashed previous launch is older than this and owner pid is dead, delete it during run
RUN_STALE_SEC=0

# If free space on FS containing WORK_BASE drops below this, new builds wait
MIN_FREE_KB_WORK=0

# Keep failed build dirs for a while so you can inspect them before stale cleanup removes them
KEEP_FAILED_BUILD_DIRS=1

# Make parallelism
if command -v nproc >/dev/null 2>&1; then
  BUILD_JOBS="${BUILD_JOBS:-$(nproc --ignore=6 2>/dev/null || nproc)}"
else
  BUILD_JOBS="${BUILD_JOBS:-4}"
fi
[[ "$BUILD_JOBS" =~ ^[0-9]+$ ]] || BUILD_JOBS=4
(( BUILD_JOBS > 0 )) || BUILD_JOBS=1

# Debug filtering for console output
#  2 = full log after each completed run
#  1 = broad filtered log
#  0 = key lines
# -1 = concise
# -2 = very concise
DEBUG_LEVEL=-1

# Fast / min sim mode
DO_MIN_SIM=0
FAST_WARMUP=500000
FAST_SIM=3000000

# CORE_LIST=(16 1 2  8 4)
CORE_LIST=(16 8 1 2 4)

TRACE_LIST=(
  "LLM256.Pythia-70M_21M"
)

# Format:
#   "L1 L2"
# or
#   "L1 L2 L3"
PREFETCH_LIST=(
  "next_line-1-2-4 next_line"
  "next_line ip_stride"
  "next_line no"
  "no next_line"
  "no no"
)

LLC_PREFETCHER_DEFAULT="no"

MODEL_LIST=(
"ByP_cleanApcLpmScore.bypass"
"ByP_fixCapacityDemandProjection.bypass"
"ByP_freeCreditApcRatio.bypass"
"ByP_l1ReliefVsL2Cost.bypass"
"ByP_queueAwareApcLpmScore.bypass"
"ByP_queueWeightedProjection.bypass"
"ByP_ratioApcLpmScore.bypass"
"ByP_softBudgetWindow.bypass"
"ByP_threeLevelCascadeScore.bypass"
"ByP_w_capacityDemandProjection.bypass"
"ByP_x2OpinionPowerLaw.bypass"
    
# "ByP_capacityDemandProjection.bypass"
# "ByP_apcLpmOnly.bypass"
# "ByP_l1cap_l2creditCheck.bypass"
# "ByP_layerMismatch25x.bypass"
# "ByP_rqOccupancyLpmGated.bypass"
# "ByP_trigger30.bypass"
# "ByP_windowBudgetModel.bypass"
# "no.bypass"

# "B001_trigger_30pct.bypass"
# "B002_trigger_40pct.bypass"
# "B003_trigger_50pct.bypass"
# "B004_trigger_60pct.bypass"
# "B005_trigger_70pct.bypass"
# "B006_trigger_80pct.bypass"
# "B007_rqcost_03x.bypass"
# "B008_rqcost_04x.bypass"
# "B009_rqcost_05x.bypass"
# "B010_rqcost_06x.bypass"
# "B011_rqcost_07x.bypass"
# "B012_rqcost_08x.bypass"
# "B013_rqcost_09x.bypass"
# "B014_rqcost_10x.bypass"
# "B015_lpmrgate_05.bypass"
# "B016_lpmrgate_06.bypass"
# "B017_lpmrgate_07.bypass"
# "B018_lpmrgate_08.bypass"
# "B019_lpmrgate_09.bypass"
# "B020_lpmrgate_10.bypass"
# "B021_apc_l1d_threshold_02.bypass"
# "B022_apc_l1d_threshold_03.bypass"
# "B023_apc_ratio_trigger.bypass"
# "B024_apc_l2c_low_threshold.bypass"
# "B025_apc_l2c_med_threshold.bypass"
# "B026_inv_apc_ratio.bypass"
# "B027_camat_l1d_high.bypass"
# "B028_camat_l1d_med.bypass"
# "B029_camat_l2c_low.bypass"
# "B030_camat_l2c_med.bypass"
# "B031_camat_ratio_budget.bypass"
# "B032_inv_camat_budget.bypass"
# "B033_mu_l1d_threshold_03.bypass"
# "B034_mu_l1d_threshold_05.bypass"
# "B035_mu_l2c_threshold_02.bypass"
# "B036_kappa_l1d_threshold_02.bypass"
# "B037_kappa_l1d_threshold_04.bypass"
# "B038_mu_kappa_combined.bypass"
# "B039_mshr_l1d_30pct.bypass"
# "B040_mshr_l1d_50pct.bypass"
# "B041_mshr_l1d_70pct.bypass"
# "B042_mshr_dynamic_threshold.bypass"
# "B043_mshr_free_slots_count.bypass"
# "B044_mshr_free_slots_ratio.bypass"
# "B045_exponential_cost.bypass"
# "B046_logarithmic_budget.bypass"
# "B047_power_law_cost.bypass"
# "B048_harmonic_mean_budget.bypass"
# "B049_geometric_mean_budget.bypass"
# "B050_weighted_product_gate.bypass"
# "B051_adaptive_threshold_cam.bypass"
# "B052_cascade_penalty_squared.bypass"
# "B053_three_level_cascade.bypass"
# "B054_apc_cam_product_gate.bypass"
# "B055_lpmr_diff_gate.bypass"
# "B056_rq_pq_combined_pressure.bypass"
)

# =========================================================
# FIXED BUILD SETTINGS
# =========================================================

BRANCH="perceptron"
LLC_REPLACEMENT="lru"
PGO_FILE="profiles_v4/PGO_4_SPEC_LLM.profdata"

# NO config_fast.ini
# If empty, script will auto-detect a UNIQUE file inc/Arch/*_<cores>c.h
# If there are multiple matches, set this manually.
ARCH_BASE="glc"

TRACES_DIR="./traces"
TRACE_PERCENT=25

# =========================================================
# PATHS
# =========================================================

if command -v readlink >/dev/null 2>&1; then
  SCRIPT_FILE="$(readlink -f "${BASH_SOURCE[0]}")"
else
  SCRIPT_FILE="${BASH_SOURCE[0]}"
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHAMPSIM_DIR="${ROOT}/champsim_v10"
TRACES_DIR_ABS="${ROOT}/${TRACES_DIR#./}"

OUT_ROOT_ABS="${ROOT}/${OUT_ROOT#./}"
RESULTS_FILE="${OUT_ROOT_ABS}/full_results2.csv"
FAILED_FILE="${OUT_ROOT_ABS}/failed_runs2.csv"
BIN_CACHE_ROOT="${OUT_ROOT_ABS}/bin_cache"
LOCK_ROOT="${OUT_ROOT_ABS}/locks"
RESULTS_LOCKDIR="${LOCK_ROOT}/results.lockdir"
FAILED_LOCKDIR="${LOCK_ROOT}/failed.lockdir"

mkdir -p "$OUT_ROOT_ABS" "$BIN_CACHE_ROOT" "$LOCK_ROOT" "$WORK_BASE"

RUN_ROOT="$(mktemp -d "${WORK_BASE}/run.$$.XXXXXX")"
echo "$$" > "${RUN_ROOT}/owner.pid"

if [[ ! -f "$RESULTS_FILE" ]]; then
  printf 'trace;model;l1_pf;l2_pf;l3_pf;num_cores;avg_ipc;mpki_l1;mpki_l2;mpki_l3;hitr_l1;hitr_l2;hitr_l3;mshr_l1;mshr_l2;mshr_l3;apc_l1;apc_l2;apc_l3;lpm_l1;lpm_l2;lpm_l3;camat_l1;camat_l2;camat_l3\n' > "$RESULTS_FILE"
fi

if [[ ! -f "$FAILED_FILE" ]]; then
  printf 'trace;model;l1_pf;l2_pf;l3_pf;cores;rc;logfile\n' > "$FAILED_FILE"
fi

# =========================================================
# HELPERS
# =========================================================

sanitize() {
  local s="$1"
  s="${s//\//_}"
  s="${s// /_}"
  s="${s//:/_}"
  echo "$s"
}

normalize_model_name() {
  local model="$1"
  if [[ "$model" == *.bypass ]]; then
    echo "$model"
  else
    echo "${model}.bypass"
  fi
}

current_max_procs() {
  local n
  n="$(
    grep -E '^MAX_CONCURRENT_PROCS=' "$SCRIPT_FILE" \
      | head -n1 \
      | cut -d= -f2 \
      | tr -d '[:space:]'
  )"

  [[ "$n" =~ ^[0-9]+$ ]] || n=1
  (( n > 0 )) || n=1
  echo "$n"
}

count_running_jobs() {
  jobs -pr | wc -l | tr -d '[:space:]'
}

available_kb_on_path() {
  local p="$1"
  df -Pk "$p" | awk 'NR==2 {print $4}'
}

wait_for_work_space() {
  local free_kb
  while true; do
    free_kb="$(available_kb_on_path "$WORK_BASE")"
    [[ "$free_kb" =~ ^[0-9]+$ ]] || free_kb=0

    if (( free_kb >= MIN_FREE_KB_WORK )); then
      break
    fi

    echo "[WAIT] low free space on WORK_BASE fs: ${free_kb} KB < ${MIN_FREE_KB_WORK} KB"
    sleep "$POLL_SEC"
  done
}

acquire_lockdir() {
  local d="$1"
  while ! mkdir "$d" 2>/dev/null; do
    sleep 0.2
  done
}

release_lockdir() {
  local d="$1"
  rmdir "$d" 2>/dev/null || true
}

cleanup_stale_temp_during_run() {
  local stale_build_min stale_run_min
  local d owner_pid

  stale_build_min=$(( (BUILD_STALE_SEC + 59) / 60 ))
  stale_run_min=$(( (RUN_STALE_SEC + 59) / 60 ))
  (( stale_build_min >= 1 )) || stale_build_min=1
  (( stale_run_min >= 1 )) || stale_run_min=1

  # 1) Clean stale build dirs from THIS run if they are not active anymore
  while IFS= read -r d; do
    [[ -n "$d" ]] || continue
    [[ -e "$d/.active_build" ]] && continue
    rm -rf "$d" 2>/dev/null || true
  done < <(find "$RUN_ROOT" -maxdepth 1 -type d -name 'build.*' -mmin +"$stale_build_min" 2>/dev/null)

  # 2) Clean stale orphan run dirs from previous crashed launches if owner pid is dead
  while IFS= read -r d; do
    [[ -n "$d" ]] || continue
    [[ "$d" == "$RUN_ROOT" ]] && continue

    owner_pid=""
    if [[ -f "$d/owner.pid" ]]; then
      owner_pid="$(tr -d '[:space:]' < "$d/owner.pid" 2>/dev/null || true)"
    fi

    if [[ -n "$owner_pid" && "$owner_pid" =~ ^[0-9]+$ ]]; then
      if kill -0 "$owner_pid" 2>/dev/null; then
        continue
      fi
    fi

    rm -rf "$d" 2>/dev/null || true
  done < <(find "$WORK_BASE" -maxdepth 1 -type d -name 'run.*' -mmin +"$stale_run_min" 2>/dev/null)
}

wait_for_slot() {
  local running max

  while true; do
    cleanup_stale_temp_during_run

    running="$(count_running_jobs)"
    max="$(current_max_procs)"

    [[ "$running" =~ ^[0-9]+$ ]] || running=0
    [[ "$max" =~ ^[0-9]+$ ]] || max=1

    if (( running < max )); then
      break
    fi

    sleep "$POLL_SEC"
  done
}

cleanup_all() {
  trap - EXIT INT TERM HUP QUIT

  jobs -pr | xargs -r kill 2>/dev/null || true
  wait 2>/dev/null || true

  # remove only THIS run root
  rm -rf "$RUN_ROOT" 2>/dev/null || true
}
trap cleanup_all EXIT INT TERM HUP QUIT

resolve_trace_file() {
  local trace_hint="$1"
  local found=""

  if [[ -f "$trace_hint" ]]; then
    echo "$trace_hint"
    return 0
  fi

  found="$(find "$TRACES_DIR_ABS" -maxdepth 1 -type f -name "*${trace_hint}*.xz" -print -quit 2>/dev/null || true)"
  [[ -n "$found" ]] || return 1

  echo "$found"
}

auto_detect_arch_base() {
  local cores="$1"
  local matches=()
  local f base

  while IFS= read -r f; do
    matches+=("$f")
  done < <(find "$CHAMPSIM_DIR/inc/Arch" -maxdepth 1 -type f -name "*_${cores}c.h" | sort)

  if (( ${#matches[@]} == 1 )); then
    base="$(basename "${matches[0]}")"
    base="${base%_${cores}c.h}"
    echo "$base"
    return 0
  fi

  return 1
}

resolve_arch_base() {
  local cores="$1"

  if [[ -n "$ARCH_BASE" ]]; then
    echo "$ARCH_BASE"
    return 0
  fi

  auto_detect_arch_base "$cores"
}

compute_trace_window() {
  local trace_file="$1"
  local base trace_name trace_count simTraces_num percent toWarmUp toSim

  if (( DO_MIN_SIM == 1 )); then
    echo "${FAST_WARMUP};${FAST_SIM}"
    return 0
  fi

  base="$(basename "$trace_file")"
  trace_name="${base%.champsimtrace.xz}"
  trace_name="${trace_name%.xz}"

  case "$trace_name" in
    [0-9][0-9][0-9].*)
      echo "50000000;200000000"
      return 0
      ;;
    LLM*)
      trace_count="$(echo "$trace_name" | sed -n 's/.*_\([0-9][0-9]*\)M.*/\1/p')"
      if [[ -z "$trace_count" || ! "$trace_count" =~ ^[0-9]+$ ]]; then
        echo "500000000;200000000"
        return 0
      fi

      simTraces_num=$((trace_count * 1000000))
      percent=$(( (simTraces_num * TRACE_PERCENT) / 100 ))
      toWarmUp=$(( (percent / 1000000) * 1000000 ))
      toSim=$(( simTraces_num - toWarmUp - toWarmUp ))

      (( toWarmUp >= 0 )) || toWarmUp=0
      (( toSim >= 0 )) || toSim=0

      echo "${toWarmUp};${toSim}"
      return 0
      ;;
    *)
      echo "500000000;200000000"
      return 0
      ;;
  esac
}

copy_source_tree_for_build() {
  local dst="$1"

  # Broad source copy, but EXCLUDING heavy/runtime dirs.
  # This avoids missing source-side dirs while still avoiding trace copies.
  rsync -a \
    --exclude '/traces/***' \
    --exclude '/bin/***' \
    --exclude '/obj/***' \
    --exclude '/output*/***' \
    --exclude '/.git/***' \
    --exclude '*.o' \
    --exclude '*.d' \
    --exclude '*.bak' \
    --exclude '*.tmp' \
    "$CHAMPSIM_DIR/" "$dst/"
}

extract_metrics() {
gawk '
function add_avg(key, val) {
    SUM[key] += val + 0
    CNT[key] += 1
}
function avg(key) {
    return (CNT[key] ? SUM[key] / CNT[key] : 0)
}
function fmt(x) {
    return sprintf("%.5f", x)
}

/^NUM_CPUS[ \t]+[0-9]+/ {
    num_cores = $2 + 0
}

/^FINAL ROI CORE AVG IPC:/ {
    if (match($0, /;[ \t]*([0-9.]+)[ \t]*;/, m))
        avg_ipc = m[1] + 0
}

/^Core_[0-9]+_IPC[ \t]+/ {
    if (avg_ipc == 0)
        avg_ipc = $2 + 0
}

/L1D;_.*;MPKI;/ {
    if (match($0, /;MPKI;[ \t]*([0-9.]+);/, m)) add_avg("mpki_l1", m[1])
}
/L2C;_.*;MPKI;/ {
    if (match($0, /;MPKI;[ \t]*([0-9.]+);/, m)) add_avg("mpki_l2", m[1])
}
/LLC;_.*;MPKI;/ {
    if (match($0, /;MPKI;[ \t]*([0-9.]+);/, m)) add_avg("mpki_l3", m[1])
}

/L1D_total_HitR:/ {
    if (match($0, /L1D_total_HitR:[ \t]*([0-9.]+)/, m)) add_avg("hitr_l1", m[1])
}
/L2C_total_HitR:/ {
    if (match($0, /L2C_total_HitR:[ \t]*([0-9.]+)/, m)) add_avg("hitr_l2", m[1])
}
/LLC_total_HitR:/ {
    if (match($0, /LLC_total_HitR:[ \t]*([0-9.]+)/, m)) add_avg("hitr_l3", m[1])
}

/L1D;_.*;LOAD_MSHR_cap;/ {
    if (match($0, /;LOAD_MSHR_cap;[ \t]*([0-9.]+);/, m)) add_avg("mshr_l1", m[1])
}
/L2C;_.*;LOAD_MSHR_cap;/ {
    if (match($0, /;LOAD_MSHR_cap;[ \t]*([0-9.]+);/, m)) add_avg("mshr_l2", m[1])
}
/LLC;_.*;LOAD_MSHR_cap;/ {
    if (match($0, /;LOAD_MSHR_cap;[ \t]*([0-9.]+);/, m)) add_avg("mshr_l3", m[1])
}

/L1D;_.*;APC;/ {
    if (match($0, /;APC;[ \t]*([0-9.]+);/, m)) add_avg("apc_l1", m[1])
}
/L2C;_.*;APC;/ {
    if (match($0, /;APC;[ \t]*([0-9.]+);/, m)) add_avg("apc_l2", m[1])
}
/LLC;_.*;APC;/ {
    if (match($0, /;APC;[ \t]*([0-9.]+);/, m)) add_avg("apc_l3", m[1])
}

/L1D;_.*;LPM;/ {
    if (match($0, /;LPM;[ \t]*([0-9.]+);/, m)) add_avg("lpm_l1", m[1])
}
/L2C;_.*;LPM;/ {
    if (match($0, /;LPM;[ \t]*([0-9.]+);/, m)) add_avg("lpm_l2", m[1])
}
/LLC;_.*;LPM;/ {
    if (match($0, /;LPM;[ \t]*([0-9.]+);/, m)) add_avg("lpm_l3", m[1])
}

/L1D;_.*;C-AMAT;/ {
    if (match($0, /;C-AMAT;[ \t]*([0-9.]+);/, m)) add_avg("camat_l1", m[1])
}
/L2C;_.*;C-AMAT;/ {
    if (match($0, /;C-AMAT;[ \t]*([0-9.]+);/, m)) add_avg("camat_l2", m[1])
}
/LLC;_.*;C-AMAT;/ {
    if (match($0, /;C-AMAT;[ \t]*([0-9.]+);/, m)) add_avg("camat_l3", m[1])
}

END {
    printf "%d;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\n",
        num_cores,
        fmt(avg_ipc),
        fmt(avg("mpki_l1")),
        fmt(avg("mpki_l2")),
        fmt(avg("mpki_l3")),
        fmt(avg("hitr_l1")),
        fmt(avg("hitr_l2")),
        fmt(avg("hitr_l3")),
        fmt(avg("mshr_l1")),
        fmt(avg("mshr_l2")),
        fmt(avg("mshr_l3")),
        fmt(avg("apc_l1")),
        fmt(avg("apc_l2")),
        fmt(avg("apc_l3")),
        fmt(avg("lpm_l1")),
        fmt(avg("lpm_l2")),
        fmt(avg("lpm_l3")),
        fmt(avg("camat_l1")),
        fmt(avg("camat_l2")),
        fmt(avg("camat_l3"))
}
' "$1"
}

append_result_row() {
  local trace="$1"
  local model="$2"
  local l1="$3"
  local l2="$4"
  local l3="$5"
  local logfile="$6"
  local row

  row="$(extract_metrics "$logfile")"

  acquire_lockdir "$RESULTS_LOCKDIR"
  printf '%s;%s;%s;%s;%s;%s\n' \
    "$trace" "$model" "$l1" "$l2" "$l3" "$row" >> "$RESULTS_FILE"
  release_lockdir "$RESULTS_LOCKDIR"
}

append_failed_row() {
  local trace="$1"
  local model="$2"
  local l1="$3"
  local l2="$4"
  local l3="$5"
  local cores="$6"
  local rc="$7"
  local logfile="$8"

  acquire_lockdir "$FAILED_LOCKDIR"
  printf '%s;%s;%s;%s;%s;%s;%s;%s\n' \
    "$trace" "$model" "$l1" "$l2" "$l3" "$cores" "$rc" "$logfile" >> "$FAILED_FILE"
  release_lockdir "$FAILED_LOCKDIR"
}


resolve_model_file() {
    local model="$1"
    local normalized
    local found

    if [[ "$model" == *.bypass ]]; then
        normalized="$model"
    else
        normalized="${model}.bypass"
    fi

    found="$(find "$CHAMPSIM_DIR/src/ByP_Models" -type f -name "$normalized" -print -quit 2>/dev/null || true)"

    [[ -n "$found" ]] || return 1
    echo "$found"
}

build_binary_for_cfg() {
  local arch_base="$1"
  local cores="$2"
  local l1_pf="$3"
  local l2_pf="$4"
  local l3_pf="$5"
  local model_file="$6"
  local cached_bin="$7"
  local logfile="$8"

  local core_uarch="${arch_base}_${cores}c"
  local build_key builddir rc=0

  local model_src="$(resolve_model_file "$model_file")" || {
        echo "[ERROR] missing bypass model via find: $model_file" >> "$logfile"
        return 207
    }
  local arch_src="$CHAMPSIM_DIR/inc/Arch/${core_uarch}.h"
  local branch_src="$CHAMPSIM_DIR/branch/${BRANCH}.bpred"
  local l1_src="$CHAMPSIM_DIR/prefetcher/${l1_pf}.l1d_pref"
  local l2_src="$CHAMPSIM_DIR/prefetcher/${l2_pf}.l2c_pref"
  local l3_src="$CHAMPSIM_DIR/prefetcher/${l3_pf}.llc_pref"
  local repl_src="$CHAMPSIM_DIR/replacement/${LLC_REPLACEMENT}.llc_repl"
  local pgo_src="$CHAMPSIM_DIR/${PGO_FILE}"

  [[ -f "$arch_src"  ]] || { echo "[ERROR] missing arch file: $arch_src" >> "$logfile"; return 201; }
  [[ -f "$branch_src" ]] || { echo "[ERROR] missing branch predictor: $branch_src" >> "$logfile"; return 202; }
  [[ -f "$l1_src"    ]] || { echo "[ERROR] missing L1 prefetcher: $l1_src" >> "$logfile"; return 203; }
  [[ -f "$l2_src"    ]] || { echo "[ERROR] missing L2 prefetcher: $l2_src" >> "$logfile"; return 204; }
  [[ -f "$l3_src"    ]] || { echo "[ERROR] missing LLC prefetcher: $l3_src" >> "$logfile"; return 205; }
  [[ -f "$repl_src"  ]] || { echo "[ERROR] missing replacement policy: $repl_src" >> "$logfile"; return 206; }
  [[ -f "$model_src" ]] || { echo "[ERROR] missing bypass model: $model_src" >> "$logfile"; return 207; }
  [[ -f "$pgo_src"   ]] || { echo "[ERROR] missing PGO file: $pgo_src" >> "$logfile"; return 208; }

  wait_for_work_space

  build_key="$(sanitize "${arch_base}_${cores}_${l1_pf}_${l2_pf}_${l3_pf}_${model_file}_${BRANCH}_${LLC_REPLACEMENT}_${PGO_FILE}")"
  builddir="$(mktemp -d "${RUN_ROOT}/build.${build_key}.XXXXXX")"
  : > "${builddir}/.active_build"

  {
    echo "[BUILD] isolated build dir: $builddir"
    echo "[BUILD] arch=${core_uarch}"
    echo "[BUILD] L1=${l1_pf} L2=${l2_pf} L3=${l3_pf}"
    echo "[BUILD] model=${model_file}"
    echo "[BUILD] make -j${BUILD_JOBS} run_clang"
  } >> "$logfile"

    if [ "$DEBUG_LEVEL" -eq 2 ]; then
        copy_source_tree_for_build "$builddir"
    else
        copy_source_tree_for_build "$builddir" >/dev/null 2>&1
    fi

  cp "$arch_src"   "$builddir/inc/defs.h"                     || { rm -f "${builddir}/.active_build"; return 210; }
  cp "$branch_src" "$builddir/branch/branch_predictor.cc"     || { rm -f "${builddir}/.active_build"; return 211; }
  cp "$l1_src"     "$builddir/prefetcher/l1d_prefetcher.cc"   || { rm -f "${builddir}/.active_build"; return 212; }
  cp "$l2_src"     "$builddir/prefetcher/l2c_prefetcher.cc"   || { rm -f "${builddir}/.active_build"; return 213; }
  cp "$l3_src"     "$builddir/prefetcher/llc_prefetcher.cc"   || { rm -f "${builddir}/.active_build"; return 214; }
  cp "$repl_src"   "$builddir/replacement/llc_replacement.cc" || { rm -f "${builddir}/.active_build"; return 215; }
  cp "$model_src"  "$builddir/src/ooo_ByP_Model.cc"           || { rm -f "${builddir}/.active_build"; return 216; }

  sed -i.bak "s/^#define NUM_CPUS [0-9][0-9]*/#define NUM_CPUS ${cores}/" "$builddir/inc/champsim.h" || { rm -f "${builddir}/.active_build"; return 217; }

  echo "[BUILD] Loaded bypass model: ${model_file}" >> "$logfile"

    if [ "$DEBUG_LEVEL" -eq 2 ]; then
    (
        cd "$builddir" || exit 218
        make -j"$BUILD_JOBS" run_clang
    )
    rc=$?
    else
    (
        cd "$builddir" || exit 218
        make -j"$BUILD_JOBS" run_clang 2>&1 | awk '
            {
                buf[NR%20]=$0
            }
            /[Ee]rror/ {
                for (i=NR-4; i<=NR+8; i++) {
                    if (i>0 && buf[i%20]!="") print buf[i%20]
                }
            }
        ' >"$logfile"
    )
    rc=${PIPESTATUS[0]}
    fi

  rm -f "${builddir}/.active_build" 2>/dev/null || true

  if (( rc != 0 )); then
    if (( KEEP_FAILED_BUILD_DIRS == 1 )); then
      echo "[ERROR] build failed, kept temporarily for debug: $builddir" >> "$logfile"
    else
      rm -rf "$builddir" 2>/dev/null || true
    fi
    return "$rc"
  fi

  [[ -f "$builddir/bin/champsim" ]] || {
    echo "[ERROR] build finished but binary missing: $builddir/bin/champsim" >> "$logfile"
    return 219
  }

  mkdir -p "$(dirname "$cached_bin")"
  cp "$builddir/bin/champsim" "$cached_bin" || return 220
  chmod +x "$cached_bin" || true

  echo "[BUILD] cached binary: $cached_bin" >> "$logfile"

  rm -rf "$builddir" 2>/dev/null || true
  return 0
}

ensure_binary() {
  local arch_base="$1"
  local cores="$2"
  local l1_pf="$3"
  local l2_pf="$4"
  local l3_pf="$5"
  local model_file="$6"
  local logfile="$7"

  local build_key cache_dir cached_bin lockdir rc=0

  build_key="$(sanitize "${arch_base}_${cores}_${l1_pf}_${l2_pf}_${l3_pf}_${model_file}_${BRANCH}_${LLC_REPLACEMENT}_${PGO_FILE}")"
  cache_dir="${BIN_CACHE_ROOT}/${build_key}"
  cached_bin="${cache_dir}/champsim"
  lockdir="${cache_dir}.lockdir"

  if [[ -x "$cached_bin" ]]; then
    echo "[BUILD CACHE HIT] $cached_bin" >> "$logfile"
    echo "$cached_bin"
    return 0
  fi

  mkdir -p "$cache_dir"
  acquire_lockdir "$lockdir"

  if [[ -x "$cached_bin" ]]; then
    echo "[BUILD CACHE HIT after lock] $cached_bin" >> "$logfile"
    release_lockdir "$lockdir"
    echo "$cached_bin"
    return 0
  fi

  build_binary_for_cfg "$arch_base" "$cores" "$l1_pf" "$l2_pf" "$l3_pf" "$model_file" "$cached_bin" "$logfile"
  rc=$?

  release_lockdir "$lockdir"

  (( rc == 0 )) || return "$rc"
  [[ -x "$cached_bin" ]] || return 221

  echo "$cached_bin"
  return 0
}

run_one() {
  local cores="$1"
  local model_name_raw="$2"
  local trace_hint="$3"
  local l1_pf="$4"
  local l2_pf="$5"
  local l3_pf="$6"

  local model_file model_tag trace_tag out_dir logfile
  local trace_file trace_base arch_base cached_bin
  local warmup sim rc build_rc
  local trace_args=()
  local i

  model_file="$(normalize_model_name "$model_name_raw")"
  model_tag="$(sanitize "$model_file")"
  trace_tag="$(sanitize "$trace_hint")"

  out_dir="${OUT_ROOT_ABS}/core${cores}"
  mkdir -p "$out_dir"
  logfile="${out_dir}/L1-${l1_pf}-L2-${l2_pf}-L3-${l3_pf}-${trace_tag}-${model_tag}.log"

  {
    echo "[START] cores=${cores} model=${model_file} trace=${trace_hint} L1=${l1_pf} L2=${l2_pf} L3=${l3_pf}"
    echo "[CFG] DEBUG_LEVEL=${DEBUG_LEVEL} DO_MIN_SIM=${DO_MIN_SIM} BUILD_JOBS=${BUILD_JOBS}"
    echo "[CFG] current MAX_CONCURRENT_PROCS=$(current_max_procs)"
  } > "$logfile"

  trace_file="$(resolve_trace_file "$trace_hint")" || {
    echo "[ERROR] trace not found for hint: ${trace_hint}" >> "$logfile"
    append_failed_row "$trace_hint" "$model_file" "$l1_pf" "$l2_pf" "$l3_pf" "$cores" 301 "$logfile"
    echo "[FAIL] model=${model_file} trace=${trace_hint} rc=301"
    return 301
  }

  arch_base="$(resolve_arch_base "$cores")" || {
    echo "[ERROR] could not resolve ARCH_BASE for cores=${cores}. Set ARCH_BASE explicitly in script." >> "$logfile"
    append_failed_row "$trace_hint" "$model_file" "$l1_pf" "$l2_pf" "$l3_pf" "$cores" 302 "$logfile"
    echo "[FAIL] model=${model_file} trace=${trace_hint} rc=302"
    return 302
  }

  trace_base="$(basename "$trace_file")"
  IFS=';' read -r warmup sim <<< "$(compute_trace_window "$trace_file")"

  {
    echo "[RUN] trace file: $trace_file"
    echo "[RUN] arch base: $arch_base"
    echo "[RUN] warmup=${warmup} sim=${sim}"
  } >> "$logfile"

  cached_bin="$(ensure_binary "$arch_base" "$cores" "$l1_pf" "$l2_pf" "$l3_pf" "$model_file" "$logfile")"
  build_rc=$?
  if (( build_rc != 0 )); then
    append_failed_row "$trace_hint" "$model_file" "$l1_pf" "$l2_pf" "$l3_pf" "$cores" "$build_rc" "$logfile"
    echo "[FAIL] model=${model_file} trace=${trace_hint} rc=${build_rc}"
    return "$build_rc"
  fi

  for (( i = 0; i < cores; i++ )); do
    trace_args+=("$trace_file")
  done

#   {
#     # echo "[RUN] binary: $cached_bin"
#     # echo "[RUN] command: $cached_bin -warmup $warmup -simulation_instructions $sim -traces ${trace_args[*]}"
#   } >> "$logfile"

  "$cached_bin" -warmup "$warmup" -simulation_instructions "$sim" \
    --db "$DB_FNAME" --arch "$arch_base" --bypass "$model_file" \
    --pf_l1 "$l1_pf" --pf_l2 "$l2_pf" --pf_l3 "$l3_pf" \
    -traces "${trace_args[@]}" >> "$logfile" 2>&1
  rc=$?

  if (( rc == 0 )); then
    append_result_row "$trace_hint" "$model_file" "$l1_pf" "$l2_pf" "$l3_pf" "$logfile"
    echo "[DONE] model=${model_file} trace=${trace_base} L1=${l1_pf} L2=${l2_pf} L3=${l3_pf}"
  else
    append_failed_row "$trace_hint" "$model_file" "$l1_pf" "$l2_pf" "$l3_pf" "$cores" "$rc" "$logfile"
    echo "[FAIL] model=${model_file} trace=${trace_base} rc=${rc}"
  fi

  return "$rc"
}

# =========================================================
# SANITY CHECKS
# =========================================================

[[ -d "$CHAMPSIM_DIR" ]] || { echo "[ERROR] champsim dir not found: $CHAMPSIM_DIR"; exit 1; }
[[ -d "$TRACES_DIR_ABS" ]] || { echo "[ERROR] traces dir not found: $TRACES_DIR_ABS"; exit 1; }
[[ -f "$CHAMPSIM_DIR/$PGO_FILE" ]] || { echo "[ERROR] missing PGO file: $CHAMPSIM_DIR/$PGO_FILE"; exit 1; }

if ! command -v rsync >/dev/null 2>&1; then
  echo "[ERROR] rsync not found"
  exit 1
fi

if ! command -v gawk >/dev/null 2>&1; then
  echo "[ERROR] gawk not found"
  exit 1
fi

# =========================================================
# MAIN
# =========================================================

totRunCnt=0
for cores in "${CORE_LIST[@]}"; do
  for model_name in "${MODEL_LIST[@]}"; do
    for trace in "${TRACE_LIST[@]}"; do
      for pf_spec in "${PREFETCH_LIST[@]}"; do
        ((totRunCnt++))
      done
    done
  done
done

echo "TOTAL RUNS: $totRunCnt"

currRunCnt=0
for cores in "${CORE_LIST[@]}"; do
  for model_name in "${MODEL_LIST[@]}"; do
    for trace in "${TRACE_LIST[@]}"; do
      for pf_spec in "${PREFETCH_LIST[@]}"; do
        ((currRunCnt++))

        read -r l1_pf l2_pf l3_pf <<< "$pf_spec"
        [[ -n "${l1_pf:-}" ]] || { echo "[ERROR] bad PREFETCH_LIST item: '$pf_spec'"; exit 1; }
        [[ -n "${l2_pf:-}" ]] || { echo "[ERROR] bad PREFETCH_LIST item: '$pf_spec'"; exit 1; }
        [[ -n "${l3_pf:-}" ]] || l3_pf="$LLC_PREFETCHER_DEFAULT"

        wait_for_slot

        (
          run_one "$cores" "$model_name" "$trace" "$l1_pf" "$l2_pf" "$l3_pf"
        ) &
        echo "STARTED: $currRunCnt out of $totRunCnt | max_now=$(current_max_procs) | running_now=$(count_running_jobs) | $(grep 'cout << ' "$(resolve_model_file "$model_name")")"

        sleep "$LAUNCH_DELAY_SEC"
      done
    done
  done
done

wait || true
echo "DONE"
echo "Results : ${RESULTS_FILE}"
echo "Failures: ${FAILED_FILE}"
echo "Bin cache: ${BIN_CACHE_ROOT}"