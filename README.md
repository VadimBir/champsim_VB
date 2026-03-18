# ChampSim v10 — Bypass DSE Research Framework

ChampSim out-of-order CPU simulator with L1D cache bypass design space exploration (DSE).
Active sim stack: `champsim_v10` (was `champsim_v7_ByP_fast`).

---

## Quick Start

```bash
# Single run: 2 cores, no prefetch, LLM trace, debug on, bypass+cAMAT enabled
./quick_v10.sh -p 1 --L1 no --L2 no --trace LLM256.Pythia-70M_21M -d 1 -c 2 -bypca -ByPModel no

# See all options
./quick_v10.sh -h
```

---

## Active Scripts (root)

| Script | Purpose |
|--------|---------|
| `quick_v10.sh` | Main entry: build + run one config |
| `qbuildPrefetcher_v10.sh` | Build ChampSim binary with selected prefetcher |
| `qrun_champsim_v10.sh` | Run simulation (called by quick_v10) |
| `quickByP_v10.sh` | Parallel bypass model DSE runner |

### Key flags (`quick_v10.sh`)
| Flag | Description |
|------|-------------|
| `-p <N>` | Number of parallel runs |
| `--L1 <pf>` | L1 prefetcher (`no`, `nextline`, `visio`, …) |
| `--L2 <pf>` | L2 prefetcher |
| `--trace <name>` | Trace short name (see Traces below) |
| `-d <0/1>` | Debug output (1=verbose periodic stats) |
| `-c <N>` | CPU cores (1 or 2) |
| `-bypca` | Enable bypass + C-AMAT model |
| `-ByPModel <name>` | Bypass model name (`no` = baseline) |

---

## Traces

| Short name | Full trace | Type |
|------------|-----------|------|
| `LLM256` | `LLM256.Pythia-70M_21M` | LLM inference (fast, primary) |
| `mcf` | `429.mcf-22B` | SPEC (memory-intensive) |
| `xalan` | `483.xalancbmk-127B` | SPEC |
| `cactus` | `607.cactuBSSN_s-4004B` | SPEC |

Traces live in `traces/`.

---

## Bypass DSE (Automated)

Designs are `.cc` files in `CC/CC_DSE/designs/` named `*_D<number>.cc`.

```bash
# Run all queued designs (4 parallel, LLM trace)
./CC/CC_DSE/dse_queue_runner.sh LLM256 4

# Status
./CC/CC_DSE/view_status.sh
cat CC/CC_DSE/report.md

# Reset a design to re-run
./CC/CC_DSE/reset_queue.sh D05
```

Design lifecycle: `*.cc` → READY → BUILDING → RUNNING → DONE/FAILED

The only editable file per design is the inline bypass function:
```cpp
inline bool shall_L1D_Bypass_OnCacheMissedMSHRcap(int cpu, CACHE *L1D, CACHE *L2C, CACHE *LLC)
```

Baseline IPC: **~0.838** (LLM256 trace, 2 cores, no prefetch)
Best known IPC: **~0.65994** (D67/D92/D95, LPMR-gated budget model)

---

## Directory Structure

```
champsim_v10/          Active simulator (v10 = was v7_ByP_fast)
champsim_v7_ByP/       Previous bypass version (keep)
champsim_v7_ByP_fast copy/  Archive copies (keep)
CC/                    DSE automation (CC_DSE, CC_OUT_LOGS, runner scripts)
outputs/               Simulation output directories
sim_db/                Results DB + query scripts (CHAMPSIM_RESULTS.db)
docs/                  Design reports, agent guides, DSE analysis
traces/                Trace files
artifacts/             Legacy versioned scripts (v3/v5/v7)
config_fast.ini        Active config (arch, cores, prefetcher, trace settings)
REVERT.sh              Inverse mv commands to undo reorganization
IIT_C-AMAT.pdf         Reference paper (Liu, Espina, Sun 2021)
```

---

## Configuration (`config_fast.ini`)

```ini
arch=glc
NUM_CORES=2
prefetcher_L1=no
prefetcher_L2=no
prefetcher_L3=no
TRACE_PERCENT=20
tracesDirName=traces
champsimDirName=champsim_v7_ByP_fast   # legacy name, scripts use champsim_v10
isDebug=1
isProfile=0
doMinSim=0
```

All active scripts embed these as `${VAR:-default}` — ini file is optional override.

---

## Simulation Output

Periodic line every ~25K instructions:
```
C:0 I# 21000001 cy 24370345 IPC 1.91 IPCavg 0.83810 ByP 0.00 0.08 0.28
     APC 0.25 0.04 0.00 LPM 0.99 0.68 0.55 cAMT 0.25 0.04 0.00
     MSHR% 43 0 0 LoadH% 90 90 43
```
- `IPCavg` = cumulative IPC (primary metric, maximize)
- `MSHR%` = MSHR utilization per level — watch L2C > 0 (cascade risk)
- `ByP` = bypass rate per level

Final metric: `Core_0_IPC` in `CC/CC_OUT_LOGS/<design>_<trace>.log`
