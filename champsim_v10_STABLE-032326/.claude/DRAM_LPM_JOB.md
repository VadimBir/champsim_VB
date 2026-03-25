# JOB: Add LPM Tracking to DRAM Controller

## Goal
Add LPM (Layered Performance Matching) cycle classification to the DRAM controller, so DRAM gets the same h/m/x/e cycle tracking as L1D/L2C/LLC. This enables a DRAM column in heartbeat stats (APC, LPM, cAMT) and a true 5-level recursive C-AMAT.

## CONSTRAINTS — READ CAREFULLY
- **NEVER touch files in archive/ directory**
- **NEVER modify bypass model files (.l1_bypass, .l2_bypass, .llc_bypass, .bypass) without permission**
- **NEVER read DEBUG*.txt files**
- **DO NOT modify lpm_tracker.h windowed (w_*) counters**
- **DO NOT modify existing L1D/L2C/LLC LPM logic in cache.cc**
- **DO NOT change any existing sim_access/sim_hit/sim_miss logic**
- Present your FULL PLAN first. Do NOT edit any file until the plan is approved.

## Context: How LPM works for caches (in cache.cc CACHE::operate())

The LPM tracker classifies each cycle into 4 categories:
- h (pure hit): only hit-access activity (queues have requests, no MSHR misses)
- m (pure miss): only miss-access activity (MSHR has inflight misses, no queue requests)
- x (overlap): both hit and miss activity simultaneously
- e (idle): no memory activity

For each cache level, every cycle inside `CACHE::operate()`:
1. Compute `hit_active = (RQ.occupancy | WQ.occupancy | PQ.occupancy) > 0`
2. Compute `miss_active = MSHR has inflight entries` (under LPM_STRICT_MISS: entries with returned != COMPLETED)
3. Compute `α = sum of sim_access[cpu][0..NUM_TYPES-1]` (total accesses)
4. Call `lpm_operate(cpu, cache_type, hit_active, miss_active, α, has_byp)`

The `lpm_operate()` function in lpm_tracker.h:
- Calls `tick(hit_active, miss_active)` which increments h/m/x/e
- Calls `update_metrics(α, ideal)` which computes camat=ω/α, apc=α/ω, lpmr=ω/ideal
- Where ω = h+m+x (active memory cycles), ideal = e(L1D)+h(L1D)+x(L1D)

## What to implement

### 1. Define DRAM LPM type constant (lpm_tracker.h)

Currently: `LPM_NUM_TYPES 7`, types 0-6 (ITLB..LLC).
Add: `#define LPM_DRAM 7` and change `LPM_NUM_TYPES` to 8.

### 2. Add DRAM LPM tick in MEMORY_CONTROLLER::operate() (dram_controller.cc)

Add `#include "lpm_tracker.h"` at top of dram_controller.cc.

At the START of `MEMORY_CONTROLLER::operate()`, BEFORE the channel loop, add per-CPU LPM tick:

```cpp
/* LPM tick for DRAM — one tick per CPU per cycle */
for (uint32_t cpu = 0; cpu < NUM_CPUS; cpu++) {
    if (!warmup_complete[cpu]) continue;

    // hit_active: DRAM has pending requests for this CPU
    bool hit_active = false;
    for (uint32_t ch = 0; ch < DRAM_CHANNELS; ch++) {
        for (uint32_t j = 0; j < RQ[ch].SIZE; j++) {
            if (RQ[ch].entry[j].address && RQ[ch].entry[j].cpu == cpu) {
                hit_active = true; break;
            }
        }
        if (hit_active) break;
        for (uint32_t j = 0; j < WQ[ch].SIZE; j++) {
            if (WQ[ch].entry[j].address && WQ[ch].entry[j].cpu == cpu) {
                hit_active = true; break;
            }
        }
        if (hit_active) break;
    }

    // miss_active: any bank is working on a request for this CPU
    bool miss_active = false;
    for (uint32_t ch = 0; ch < DRAM_CHANNELS && !miss_active; ch++)
        for (uint32_t r = 0; r < DRAM_RANKS && !miss_active; r++)
            for (uint32_t b = 0; b < DRAM_BANKS && !miss_active; b++)
                if (bank_request[ch][r][b].working &&
                    bank_request[ch][r][b].cycle_available > current_core_cycle[cpu])
                    miss_active = true;

    // α: use RQ ROW_BUFFER_HIT + ROW_BUFFER_MISS summed across channels as access proxy
    // (DRAM has no sim_access array, so use completed request count)
    uint64_t α = 0;
    for (uint32_t ch = 0; ch < DRAM_CHANNELS; ch++)
        α += RQ[ch].ROW_BUFFER_HIT + RQ[ch].ROW_BUFFER_MISS;
    // NOTE: this is total across all CPUs. For per-CPU, we'd need per-CPU counters.
    // For now this is acceptable — DRAM is shared.

    lpm[cpu][LPM_DRAM].tick(hit_active, miss_active);

    const LPM_Tracker& l1d = lpm[cpu][LPM_L1D];
    uint64_t ideal = l1d.e_idle_cy + l1d.h_pureHit_cy + l1d.x_overlap_cy;
    lpm[cpu][LPM_DRAM].update_metrics(α, ideal);
}
```

### 3. Update heartbeat in main.cc

Add `#define DRAM_type 7` near the existing L1D_type/L2C_type/LLC_type defines (or use LPM_DRAM).

In the heartbeat print section, add DRAM as 5th value after LLC for APC, LPM, cAMT:
- After LLC APC: add `lpm[i][LPM_DRAM].apc_accessesDivActiveMemCy_ratio`
- After LLC LPM: add `lpm[i][LPM_DRAM].lpmr_activeMemCyDivIdealCy_ratio`
- After LLC cAMT: add `lpm[i][LPM_DRAM].camat_activeMemCyDivAccesses_ratio`

Order should be: CPU L1 L2 LLC DRAM

### 4. Update recursive C-AMAT to include DRAM (lpm_tracker.h)

Update `get_recursive_camat()` and `get_recursive_camat_roi()`:
```
rCAMAT = C-AMAT(L1) + µκ(L1)·C-AMAT(L2) + µκ(L1)·µκ(L2)·C-AMAT(LLC) + µκ(L1)·µκ(L2)·µκ(LLC)·C-AMAT(DRAM)
```

Update `get_LPMR_global_std/byp/roi_std/roi_byp` to multiply by LPMR(DRAM) too.

### 5. Update final stats lpm_print() (lpm_tracker.h)

Add "DRAM" to the `nm[]` array and ensure the loop covers LPM_DRAM (index 7).

### 6. Update final stats in main.cc

The semicolon-delimited final stats block that prints APC/LPM/C-AMAT per cache level — add DRAM lines there too.

## Files to modify (ONLY these)
1. `inc/lpm_tracker.h` — LPM_DRAM constant, LPM_NUM_TYPES, recursive camat, global LPMR, lpm_print
2. `src/dram_controller.cc` — add include, add LPM tick in operate()
3. `src/main.cc` — heartbeat DRAM column, final stats DRAM lines

## DO NOT modify
- `inc/cache.h`
- `inc/block.h`
- `src/cache.cc` (existing LPM logic)
- Any file in archive/
- Any .bypass model files
- DEBUG*.txt files

## Verification
After implementing, report all changes made with exact line numbers. Do NOT run builds or tests — I will do that.
