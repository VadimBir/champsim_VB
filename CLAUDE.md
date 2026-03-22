# ChampSim L2 + LLC Bypass — Claude Code Execution Plan
**Scope:** Refactor L1 bypass infrastructure → implement L2 bypass → implement LLC bypass → DRAM return chain  
**Constraint:** NEVER merge L1/L2/LLC logic. Each bypass level = independent, separable, guarded block.  
**Equivalency gate:** `./qcheck_consistency.sh` MUST pass (both tests) before advancing stage. 2-min timeout → backup → diagnose → stop+report.

---

## STAGE 0 — Discovery (READ BEFORE ANY EDIT)

**Files to read in full before writing a single line:**
1. `inc/cache.h` — locate `BYPASS_L1D_Cache`, `cache_type`, `IS_L1D/IS_L2C/IS_LLC` defines, `upper_level_dcache` declaration
2. `inc/block.h` — confirm `uint8_t bypassed_levels` field on `PACKET`
3. `src/cache.cc` — locate `#define BYPASS_L1_LOGIC` region; map ALL 13 bypass-case branches for L1; identify exact lines where `add_rq()` is called downstream and where MSHR insertion is skipped
4. `src/dram_controller.cc` (or equivalent) — locate `upper_level_dcache[op_cpu]->return_data(...)` line; confirm `MEMORY_CONTROLLER` class, confirm `upper_level_dcache` chain depth

**Map and record:**
- Exact symbol name for `BYPASS_L1D_Cache` (bool, member of which struct/class?)
- Exact symbol name for `is_bypassing` target (where will it live — same struct?)
- All 13 L1 bypass cases: list each as one-line description (what condition → what action)
- Whether `bypassed_levels` is zeroed on PACKET construction
- Whether `upper_level_dcache[cpu]` is CACHE* or MEMORY*, and whether LLC's `upper_level_dcache` points to L2 and L2's points to L1D

**STOP if any of the above is ambiguous — report findings, do not proceed.**

---

## STAGE 1 — Refactor (Prepare Infrastructure)

### 1.1 `inc/cache.h`

**Replace:**
```cpp
bool BYPASS_L1D_Cache = false;
```
**With:**
```cpp
uint8_t is_bypassing = 0;   // bit0=L1 bit1=L2 bit2=LLC
```

**Add macro block immediately after (inside cache.h, NOT in a .cc):**
```cpp
// Bypass bit encoding: L1=1, L2=2, LLC=4
#define BYP_L1_BIT   1u
#define BYP_L2_BIT   2u
#define BYP_LLC_BIT  4u

#define BYPASS_L1(pkt)     ((pkt).bypassed_levels |= BYP_L1_BIT)
#define BYPASS_L2(pkt)     ((pkt).bypassed_levels |= BYP_L2_BIT)
#define BYPASS_LLC(pkt)    ((pkt).bypassed_levels |= BYP_LLC_BIT)
#define UNBYPASS_L1(pkt)   ((pkt).bypassed_levels &= ~BYP_L1_BIT)
#define UNBYPASS_L2(pkt)   ((pkt).bypassed_levels &= ~BYP_L2_BIT)
#define UNBYPASS_LLC(pkt)  ((pkt).bypassed_levels &= ~BYP_LLC_BIT)
#define isBYPASSING_L1(pkt)  ((pkt).bypassed_levels & BYP_L1_BIT)
#define isBYPASSING_L2(pkt)  ((pkt).bypassed_levels & BYP_L2_BIT)
#define isBYPASSING_LLC(pkt) ((pkt).bypassed_levels & BYP_LLC_BIT)
```

**Note:** Macros take `pkt` as argument to avoid ambiguity when called from DRAM context where no implicit `this` applies.

### 1.2 `src/cache.cc`
```

**Replace ALL existing uses of `BYPASS_L1D_Cache` with `is_bypassing`:**
- Where it was SET to true → `cache.is_bypassing |= BYP_L1_BIT` (or use macro on packet if it's per-packet)
- **CRITICAL:** Determine scope — is `BYPASS_L1D_Cache` a CACHE member (per-cache-object config flag) or a per-packet flag?  
  - If per-cache-object config: keep `is_bypassing` as CACHE member, bit0 = "L1 bypass enabled for this object"  
  - If per-packet: it lives on PACKET, but that's already `bypassed_levels`  
  - **These are TWO different things** — "is bypass enabled for this level" (config) vs "did this packet get bypassed at level X" (state). Do NOT conflate.
  - Report which interpretation current code uses before proceeding.

**Wrap ALL existing 13 L1 bypass cases:**
```cpp
#ifdef BYPASS_L1_LOGIC
  // ... existing case N ...
#endif
```
Each case wrapped independently. Do NOT batch-wrap all 13 in one `#ifdef` block.

**Replace all `BYPASS_L1_LOGIC` guards with `BYPASS_L1_LOGIC`.**

### 1.3 Verify `inc/block.h`
Confirm `bypassed_levels` zero-initialized on PACKET construction. If not, add explicit `bypassed_levels = 0` in PACKET constructor or default member init.

### 1.4 Equivalency Gate — Stage 1
```
./qcheck_consistency.sh
```
- Test 1 (no bypass) PASS = original logic preserved  
- Test 2 (bypass) PASS = L1 bypass still functional after refactor  
- FAIL on either → diagnose, fix, re-run. Do NOT advance.

### 1.5 Backup
```bash
cp inc/cache.h   inc/cache.h.BK.ByPL1
cp inc/block.h   inc/block.h.BK.ByPL1
cp src/cache.cc  src/cache.cc.BK.ByPL1
cp src/dram_controller.cc src/dram_controller.cc.BK.ByPL1
```
(adjust filename if DRAM file differs)

---

## STAGE 2 — L2 Bypass Implementation

### 2.0 Pre-implementation Questions (answer BEFORE writing code)

Q1: In L1 bypass, when L1 miss → bypass → `L2.add_rq()` is called: does the packet carry `isBYPASSING_L1` set at that point? Confirm by tracing `bypassed_levels` value at L2 `read_rq` intake.  
Q2: In L2 `handle_read_rq()` (or equivalent), is `cache_type == IS_L2C` the correct discriminant to isolate L2-only path?  
Q3: Does L2 have its own `upper_level_dcache` pointer to L1D? Confirm.  
Q4: In DRAM return: does `upper_level_dcache[cpu]` on LLC point to L2, and `upper_level_dcache[cpu]` on L2 point to L1D? Trace chain.  

**Do not write L2 logic until all 4 are confirmed.**

### 2.1 `src/cache.cc` — L2 Bypass Block

Uncomment `#define BYPASS_L2_LOGIC`.

Inside `cache.cc`, in the read-queue handler, locate the miss-handling path. Add L2 bypass block:

```cpp
#ifdef BYPASS_L2_LOGIC
if (cache_type == IS_L2C) {
    // Bypass decision: mirror L1 bypass decision logic exactly, adapted for L2
    // CONDITION: new miss (not already in MSHR), bypass policy says bypass L2
    // ACTION: set BYPASS_L2 on packet, call lower_level->add_rq() WITHOUT inserting MSHR
    // IDENTICAL PATTERN to L1 bypass: no MSHR insertion, packet forwarded downstream
    //
    // 13 cases from L1: re-enumerate which apply at L2 level:
    // [Claude Code: list each of the 13 L1 cases, state YES/NO applies to L2, reason]
    // Only implement cases confirmed applicable.
}
#endif
```

**Structural mirror requirement:**  
L1 bypass skips L1 MSHR → calls L2.add_rq()  
L2 bypass skips L2 MSHR → calls LLC.add_rq() (or DRAM.add_rq() if LLC also bypassed — but that is LLC logic, NOT here)  
**At this stage: L2 bypass → LLC.add_rq() only. DRAM path is Stage 3.**

### 2.2 `src/cache.cc` or `src/dram_controller.cc` — L2 Return Path

On LLC MSHR complete / LLC fill, before returning to L2:  
```cpp
#ifdef BYPASS_L2_LOGIC
if (isBYPASSING_L2(packet)) {
    // Do NOT fill L2 cache
    // Check L1 bypass state:
    if (isBYPASSING_L1(packet)) {
        // Skip L1 fill — return directly to CPU (L1 processed)
        upper_level_dcache[cpu]->return_data(&packet);  // this is L1D->return_data? confirm chain
    } else {
        // Fill L1: call L1D return_data normally
        upper_level_dcache[cpu]->return_data(&packet);
    }
    return;  // skip normal L2 fill
}
#endif
```

**CRITICAL:** Confirm exact call chain. If LLC's `upper_level_dcache` IS L2 and L2's `upper_level_dcache` IS L1D, then:  
- Normal: LLC → L2.return_data() → L2 fills → L2 calls L1D.return_data()  
- L2 bypassed: LLC → skip L2 fill → call L2's `upper_level_dcache` (=L1D) directly  
Implement exactly this, no abstraction shortcuts.

### 2.3 Equivalency Gate — Stage 2
```
./qcheck_consistency.sh
```
- Test 1 PASS: no-bypass path unaffected  
- Test 2 PASS: L1 bypass still works (L2 bypass independently gated)  
- Additional: if test suite has L2 bypass test, run it  
- If Test 2 non-equivalent but finishes → STOP, report delta, await instruction  

### 2.4 Backup
```bash
cp inc/cache.h   inc/cache.h.BK.ByPL1L2
cp inc/block.h   inc/block.h.BK.ByPL1L2
cp src/cache.cc  src/cache.cc.BK.ByPL1L2
cp src/dram_controller.cc src/dram_controller.cc.BK.ByPL1L2
```

---

## STAGE 3 — LLC Bypass Implementation

### 3.0 Pre-implementation Questions

Q1: LLC miss → bypass → DRAM.add_rq(): does `MEMORY_CONTROLLER` have `add_rq()` accepting PACKET? Confirm signature.  
Q2: In DRAM `return_data()` (or wherever `upper_level_dcache[op_cpu]->return_data(...)` is called): is `op_cpu` the correct CPU index? Confirm multi-core safety.  
Q3: Can DRAM access LLC's `upper_level_dcache` pointer (=L2), and L2's `upper_level_dcache` (=L1D)? Or must bypass levels be encoded purely on `bypassed_levels` field?  
Q4: The DRAM bypass return chain requires 3-level conditional. Confirm `bypassed_levels` on the PACKET is still intact when DRAM returns it (not zeroed anywhere in transit).  

### 3.1 `src/cache.cc` — LLC Bypass Block

Uncomment `#define BYPASS_LLC_LOGIC`.

```cpp
#ifdef BYPASS_LLC_LOGIC
if (cache_type == IS_LLC) {
    // New miss + bypass policy → set BYPASS_LLC, call DRAM.add_rq(), skip LLC MSHR
    // Mirror: same 13-case analysis applied at LLC level
    // [Claude Code: enumerate applicable cases]
}
#endif
```

### 3.2 `src/dram_controller.cc` — DRAM Return Chain

Locate:
```cpp
upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);
```

Replace with:
```cpp
#if defined(BYPASS_LLC_LOGIC) || defined(BYPASS_L2_LOGIC)
{
    PACKET& pkt = queue->entry[request_index];
#ifdef BYPASS_LLC_LOGIC
    if (isBYPASSING_LLC(pkt)) {
#ifdef BYPASS_L2_LOGIC
        if (isBYPASSING_L2(pkt)) {
            if (isBYPASSING_L1(pkt)) {
                // All 3 bypassed: LLC.upper→L2.upper→L1D = CPU
                // Chain: LLC's upper_level_dcache = L2; L2's upper_level_dcache = L1D
                upper_level_dcache[op_cpu]                          // = LLC ptr? NO — in DRAM context upper_level_dcache IS LLC
                // MUST resolve exact pointer chain here — see Q3 above
                // Pseudocode: get_llc(op_cpu)->upper_level_dcache[op_cpu]->upper_level_dcache[op_cpu]->return_data(&pkt)
            } else {
                // LLC+L2 bypassed, L1 not: return to L1D
                // get_llc(op_cpu)->upper_level_dcache[op_cpu]->return_data(&pkt)
            }
        } else {
#endif
            // LLC bypassed, L2 not: return to L2
            // upper_level_dcache[op_cpu]->return_data(&pkt)  // = LLC's upper = L2
#ifdef BYPASS_L2_LOGIC
        }
#endif
        return;
    }
#endif
    // Default: no bypass active at LLC — normal path
    upper_level_dcache[op_cpu]->return_data(&pkt);
}
#else
upper_level_dcache[op_cpu]->return_data(&queue->entry[request_index]);
#endif
```

**NOTE:** Pseudocode stubs above MUST be replaced with real pointer dereferences after Q3 is answered. Do not leave pseudocode in final implementation.

**Edge case mandatory check:**  
- L2 bypass active, LLC bypass NOT active: DRAM not involved in L2 path (L2 miss → LLC, not DRAM). This DRAM block only triggers when packet came from LLC bypass. Confirm this invariant: if LLC not bypassed, packet never reaches DRAM directly from L2.  
- If invariant fails → re-examine Stage 2 routing.

### 3.3 Equivalency Gate — Stage 3
```
./qcheck_consistency.sh
```
Same pass criteria. Non-equivalent finish → STOP, report, await.

### 3.4 Backup
```bash
cp inc/cache.h   inc/cache.h.BK.ByPL1L2L3
cp inc/block.h   inc/block.h.BK.ByPL1L2L3
cp src/cache.cc  src/cache.cc.BK.ByPL1L2L3
cp src/dram_controller.cc src/dram_controller.cc.BK.ByPL1L2L3
```
Duplicate collision → append `_v2`.

---

## INVARIANTS — Enforce Throughout All Stages

| # | Invariant |
|---|-----------|
| I1 | `store_merged=1` NEVER on bypass packet |
| I2 | `fill_level` NOT overwritten during PREFETCH takeover of bypass packet |
| I3 | `merge_with_prefetch`: save deps BEFORE overwrite, not after |
| I4 | `check_mshr` and `check_dram_queue` MUST match on `bypassed_levels` same as cache-level check |
| I5 | `remove_queue()` advances head ONLY when removing head entry |
| I6 | `bypassed_levels` zero-initialized on every new PACKET |
| I7 | No MSHR insertion for any bypassed level — verified per level, per stage |
| I8 | L1/L2/LLC bypass logic blocks compile-independent via separate `#ifdef` guards |

---

## STOP CONDITIONS

- Any equivalency test FAIL → stop, report exact diff/error, do NOT advance  
- Any pre-implementation question unresolvable from code → stop, report, await  
- 2-min timeout on qcheck → backup current state, report timeout, stop  
- Any ambiguity in pointer chain for DRAM return → stop, report chain as traced, await confirmation  

---

## DELIVERABLES PER STAGE

| Stage | Deliverable |
|-------|-------------|
| 0 | Discovery report: 13 L1 cases listed, pointer chain confirmed, scope of `is_bypassing` vs `bypassed_levels` clarified |
| 1 | Refactored code + passing qcheck + `.BK.ByPL1` backups |
| 2 | L2 bypass + passing qcheck + `.BK.ByPL1L2` backups |
| 3 | LLC bypass + DRAM chain + passing qcheck + `.BK.ByPL1L2L3` backups |