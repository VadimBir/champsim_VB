# Stage 0 — Discovery Report

## Pointer Chain (confirmed from main.cc:1384-1433)
- `L1D.lower_level = L2C`
- `L2C.upper_level_dcache[i] = L1D`
- `L2C.lower_level = LLC`
- `LLC.upper_level_dcache[i] = L2C`
- `LLC.lower_level = DRAM`
- `DRAM.upper_level_dcache[i] = LLC`

**Chain**: DRAM→LLC→L2C→L1D (return path via `upper_level_dcache`)

## `BYPASS_L1D_Cache` scope
- **Per-cache-object config flag** (`bool` member of `CACHE` class, cache.h:63)
- Set by L2C prefetcher (e.g., `no_ByP.l2c_pref`, `next_line_ByP.l2c_pref`)
- Used in `ooo_cpu.cc:1457-1479` to decide if L1D is bypassed at OOO dispatch
- **NOT per-packet**. Per-packet state = `bypassed_levels` on PACKET

## `bypassed_levels` zero-initialization
- `uint8_t bypassed_levels = 0;` (block.h:113) — default member init ✓
- `quickReset()` sets `bypassed_levels = 0;` (block.h:254) ✓
- `fast_copy_packet()` explicitly copies `bypassed_levels` (block.h:224) ✓

## `upper_level_dcache` type
- Declared as `MEMORY *upper_level_dcache[NUM_CPUS]` in `memory_class.h:23`
- At runtime, points to CACHE objects (cast needed for CACHE-specific methods)

## Active L1 Bypass Mechanism (`BYPASS_L1D_OnNewMiss`)
Location: `cache.cc:917-931` in `handle_read()`.
- **Trigger**: L1D cache miss, no existing MSHR entry (`mshr_index == -1`), lower level RQ has room, `shall_L1D_Bypass_OnCacheMissedMSHRcap()` returns true, warmup complete.
- **Action**: Sets `bypassed_levels = 1`, `fill_level = FILL_L2`, calls `lower_level->add_rq()` (= L2C). **No L1D MSHR insertion**.

## 13 Bypass Cases (all under `#ifdef BYPASS_LOGIC`)

### handle_fill() — L2C fill path
1. **L2C MSHR bypass complete** (cache.cc:377): L2C MSHR entry with `type==LOAD && bypassed_levels==1` → add to L2C PROCESSED queue (not L1D). Packet goes to CPU via L2C processed path.

### merge_with_prefetch()
2. **merge_with_prefetch guard** (cache.cc:480): commented-out save of `prior_bypassed` — currently no active bypass logic here, just commented remnants.

### handle_read() — L2C hit for bypassed packet
3. **L2C read hit for bypassed LOAD** (cache.cc:833-861): When L2C has a cache hit for a packet with `bypassed_levels==1 && type==LOAD && !instruction` → add to L2C PROCESSED queue directly.

### handle_read() — MSHR merge mismatch
4. **L2C MSHR bypass mismatch** (cache.cc:1091-1168): When merging RQ entry into existing MSHR at L2C and `bypassed_levels` differ → push bypass LQ deps to L1D MSHR, reset both to `bypassed_levels=0`, set `fill_level=1`.
5. **PREFETCH sub-case** (cache.cc:1131-1135): If MSHR is PREFETCH type with lower fill_level → reset bypass on RQ entry.

### handle_prefetch() — PQ merge with bypass MSHR
6. **PQ↔bypass MSHR fill_level conflict** (cache.cc:1362-1375): PQ wants lower fill_level but MSHR is bypass LOAD → check if L1D has MSHR for this address; if yes, downgrade; if no, keep bypass.

### operate() — bypass detection for LPM
7. **L2C bypass detection** (cache.cc:1453-1459): Scan MSHR for any `bypassed_levels > 0` entries, set `has_byp` flag for LPM tracker.

### add_rq() — WQ hit for bypassed packet
8. **L2C WQ hit for bypassed LOAD** (cache.cc:1647-1661): When bypassed LOAD hits in L2C WQ → add to L2C PROCESSED.

### add_rq() — RQ merge RFO bypass mismatch
9. **L2C RQ RFO merge mismatch** (cache.cc:1699-1708): RFO merges into existing RQ entry at L2C with different bypass → reset bypass, update fill_level.

### add_rq() — RQ merge LOAD bypass mismatch
10. **L2C RQ LOAD merge mismatch** (cache.cc:1724-1739): LOAD merges into existing RQ entry at L2C with different bypass → reset both to 0.

### return_data() — L1D dep propagation
11. **L1D PREFETCH→LOAD promotion** (cache.cc:2006-2026): When L1D MSHR has PREFETCH and returning packet is LOAD → promote type, copy deps, remove self from dep set.
12. **L1D LOAD→LOAD dep merge** (cache.cc:2028-2033): Merge LQ deps from returning packet into L1D MSHR.
13. **L1D LOAD→RFO / RFO→RFO merges** (cache.cc:2034-2060): Merge LQ/SQ deps from returning packet.

### check_mshr() — bypass downgrade
14. **check_mshr bypass downgrade** (cache.cc:2144-2148): If MSHR has `bypassed_levels==1` and incoming packet has `bypassed_levels==0` and is LOAD → reset MSHR bypass, update fill_level.

## Defines location
- `BYPASS_LOGIC`, `BYPASS_L1D_OnNewMiss`, `BYPASS_SANITY_CHECK` defined in `champsim.h:32-35`
- `BYPASS_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS` commented out in `champsim.h:36`

## Files using BYPASS_LOGIC guards
- `src/cache.cc` (main bypass logic)
- `src/ooo_cpu.cc` (dispatch bypass path)
- `src/dram_controller.cc` (EQUIVALENCY variant only)
- `src/block.cc` (EQUIVALENCY variant only)
- `inc/lpm_tracker.h` (LPM bypass detection)
