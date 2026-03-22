# Plan: Rewrite LLC Bypass — Mirror L1 Base Logic Exactly

## Problem
The current LLC bypass code has custom interceptors in `return_data()` (lines 2034-2081 in cache.cc) and complex orphan-handling logic that DOES NOT exist in the original working L1 bypass code. This introduced double-return bugs and deadlocks.

## Principle
**The L1 bypass works because it's simple:**
1. L1D skip MSHR → send to L2C
2. L2C has the MSHR → handles everything
3. `handle_fill` at L2C: `fill_level < fill_level` check handles return_data up, then `BYPASS_LOGIC` PROCESSED block handles bypass completion
4. `return_data` has NO interceptor — standard MSHR lookup
5. `check_mshr` has simple UNBYPASS logic
6. `merge_with_prefetch` saves/restores bypass + fill_level

**For LLC bypass, the IDENTICAL mirror:**
1. LLC skip MSHR → send to DRAM
2. L2C STILL has the MSHR → handles everything
3. DRAM must return to L2C directly (skip LLC) — same as CPU skips L1D for L1 bypass
4. `handle_fill` at L2C: already works via existing `fill_level` check + `BYPASS_LOGIC` PROCESSED
5. NO LLC interceptor needed in `return_data`

## Key Insight
L1 bypass: CPU never calls L1D.return_data for bypass packets. L2C handles it.
LLC bypass: DRAM must never call LLC.return_data for bypass packets. L2C handles it.

The ONLY code change needed outside cache.cc is in `dram_controller.cc` — skip LLC on bypass return.

## Steps

### Step 1: Clean `dram_controller.cc` line 375
**Current:**
```cpp
upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);
```
**Change to:**
```cpp
#ifdef BYPASS_LLC_LOGIC
if (queue->entry[request_index].llc_bypassed) {
    // LLC was bypassed: skip LLC, return directly to L2C
    // This mirrors L1 bypass: CPU skips L1D, sends to L2C directly
    upper_level_dcache[op_cpu]->upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);
} else
#endif
    upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);
```
- `upper_level_dcache[op_cpu]` = LLC
- `LLC->upper_level_dcache[op_cpu]` = L2C
- So bypass packets go directly to L2C.return_data(), bypassing LLC entirely

### Step 2: REMOVE LLC interceptor from `cache.cc` return_data()
**Delete lines 2034-2082** (the entire `#ifdef BYPASS_LLC_LOGIC` block in return_data).
This code should NOT exist — the original L1 bypass has NO interceptor in return_data.

### Step 3: REMOVE L2 interceptor from `cache.cc` return_data()
**Delete lines 2083-2111** (the entire `#ifdef BYPASS_L2_LOGIC` block in return_data).
Same reason — not in original code, L2 bypass is disabled anyway.

### Step 4: Verify handle_fill LLC_BYPASS block (lines 219-256)
The `#ifdef LLC_BYPASS` block in handle_fill — this is a DIFFERENT feature (LLC victim bypass, not our MSHR bypass). Leave it alone.

### Step 5: Handle orphaned LLC MSHRs
**Problem:** If a prefetch creates an LLC MSHR for the same address that gets LLC-bypassed, and DRAM merges them, the LLC MSHR may get orphaned (no return data).

**Scenario A — bypass enters DRAM first, prefetch merges:** DRAM returns with `llc_bypassed=1` → skips LLC → LLC MSHR orphaned.

**Solution:** In LLC `handle_prefetch`, when a prefetch misses at LLC, check if address is already in DRAM RQ with `llc_bypassed=1`. If so, DON'T create LLC MSHR — it would be orphaned.

**Alternative simpler solution:** Just don't worry about it. The orphaned LLC MSHR will sit there with `returned=INFLIGHT` forever. When another request for the same address comes, it'll merge with the MSHR and get serviced normally. The MSHR slot is wasted but won't deadlock unless ALL LLC MSHRs get orphaned (extremely unlikely).

**Even simpler:** In DRAM add_rq merge path, if existing entry has `llc_bypassed=1` and new packet has `llc_bypassed=0`, CLEAR the `llc_bypassed` flag so DRAM returns via normal LLC path. This services the LLC MSHR.

### Step 6: Verify no other references
Grep for `isBYPASSING_LLC\|llc_bypassed` in cache.cc. Remove any logic that doesn't mirror the L1 pattern exactly.

The ONLY places `llc_bypassed` should appear:
1. `handle_read` LLC bypass entry: `BYPASS_LLC(RQ), fill=FILL_LLC, lower_level->add_rq()`
2. `merge_with_prefetch`: save/restore (already done)
3. `block.h`: field declaration + zero init + operator<< print

## Risk: Orphaned LLC MSHRs
The orphan problem only occurs when:
1. LLC bypass fires for address X (no LLC MSHR)
2. Prefetch creates LLC MSHR for address X
3. Both go to DRAM
4. DRAM merges them (bypass packet was first)
5. DRAM returns with `llc_bypassed=1` → skips LLC → LLC MSHR orphaned

Mitigation options (pick ONE):
- **A)** In DRAM merge, clear `llc_bypassed` if new packet doesn't have it (enable `DO_LVL_BYPASS_ON_FULL_MSHR` or similar)
- **B)** In LLC `check_mshr` during return_data (normal path), if LLC MSHR has an address that just returned, mark it COMPLETED too
- **C)** Accept orphans — they waste MSHR slots but don't deadlock. When LLC MSHR is full from orphans, new requests stall temporarily until orphans are evicted (timeout/age out). If no eviction mechanism → risk of deadlock.
- **D)** BEST: In DRAM add_rq, when merging and existing has `llc_bypassed=1` but new has `llc_bypassed=0`, clear `llc_bypassed` on the existing entry. One line change.

## Files to modify
1. `champsim_v10/src/dram_controller.cc` — line 375 (DRAM return)
2. `champsim_v10/src/cache.cc` — DELETE return_data interceptors, keep everything else
3. `champsim_v10/src/dram_controller.cc` — line ~510 area (DRAM merge, for orphan fix option D)

## What NOT to touch
- handle_fill — already correct (L1 bypass PROCESSED block works for LLC bypass too)
- check_mshr — L1 bypass UNBYPASS logic already correct
- merge_with_prefetch — already saves/restores all 3 bypass flags
- handle_read bypass entry (line 983-998) — already correct
- block.h — already has all 3 fields
- cache.h — already has all macros
