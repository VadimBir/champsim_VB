/*
 * lpm_tracker.h — Layered Performance Matching (LPM) cycle tracker
 *
 * Based on: Liu, Espina, Sun. "A Study on Modeling and Optimization
 * of Memory Systems." JCST 36(1), 2021. DOI:10.1007/s11390-021-0771-8
 *
 * 4 cycle categories per cache level per CPU:
 *   h — pure hit cycles     (only hit-access activity)     [Section 3]
 *   m — pure miss cycles    (only miss-access activity)    [Section 3]
 *   x — mixed cycles        (both hit and miss overlap)    [Section 3]
 *   e — inactive cycles     (no memory activity)           [Section 3]
 *
 * Cached metrics updated every lpm_operate() call:
 *   camat_activeMemCyDivAccesses_ratio = ω(l) / α(l)                              [eq 30]
 *   apc_accessesDivActiveMemCy_ratio    = α(l) / ω(l)                               [eq 32]
 *   lpmr_activeMemCyDivIdealCy_ratio   = ω(l) / (IC × CPIexe)                      [eq 52]
 *
 * Two MSHR modes (compile switch LPM_STRICT_MISS):
 *   OFF: MSHR.occupancy > 0. Includes returned-not-filled.
 *   ON:  INFLIGHT entries only. Paper-exact: miss ends at data return.
 *
 * Two LPMR methods:
 *   get_LPMR_std(): Paper standard. L1D as reference.
 *   get_LPMR_byp(): Bypass-corrected. Subtracts m_byp_l1d_pureMiss_cy.
 */

#ifndef LPM_TRACKER_H
#define LPM_TRACKER_H

#include "memory_class.h"
#include "cache.h"
#include "defs.h"
#include <cstdint>
#include <cstdio>

/* Uncomment for paper-strict miss detection (inflight MSHR only) */
#define LPM_STRICT_MISS

#define LPM_NUM_TYPES 8
#define LPM_L1D       4
#define LPM_L2C       5
#define LPM_LLC       6
#define LPM_DRAM      7

#define LPM_CLASS_E 0
#define LPM_CLASS_H 1
#define LPM_CLASS_M 2
#define LPM_CLASS_X 3

struct LPM_Tracker {
    /* --- cycle counters (sim-style: reset at warmup, accumulate post-warmup) --- */
    uint64_t h_pureHit_cy, m_pureMiss_cy, x_overlap_cy, e_idle_cy;

    /* --- ROI snapshot (frozen at sim end, used by final stats) --- */
    uint64_t roi_h_pureHit_cy, roi_m_pureMiss_cy, roi_x_overlap_cy, roi_e_idle_cy;
    uint64_t roi_m_byp_l1d_pureMiss_cy;
    uint64_t roi_m_byp_llc_pureMiss_cy;
    double   roi_camat_activeMemCyDivAccesses_ratio;
    double   roi_apc_accessesDivActiveMemCy_ratio;
    double   roi_lpmr_activeMemCyDivIdealCy_ratio;
    double   roi_mst_pureMissCyDivAccesses_ratio;          /* m/α            [eq 18] */

    /* --- bypass correction (L2C only) --- */
    uint64_t m_byp_l1d_pureMiss_cy;
    uint64_t m_byp_llc_pureMiss_cy;

    /* --- cached metrics (updated per lpm_operate) --- */
    double   camat_activeMemCyDivAccesses_ratio;       /* ω/α            [eq 30] */
    double   apc_accessesDivActiveMemCy_ratio;          /* α/ω            [eq 32] */
    double   lpmr_activeMemCyDivIdealCy_ratio;         /* ω/(IC×CPIexe)  [eq 52] */
    double   mst_pureMissCyDivAccesses_ratio;           /* m/α            [eq 18] */

    /* --- window-based counters (exponential decay every W_SIZE cycles) --- */
    static constexpr uint32_t W_SIZE = 512;
    uint64_t w_h, w_m, w_x, w_e;
    uint32_t w_tick;
    double   w_c_amat_val;     /* windowed ω/α   */
    double   w_apc_val;        /* windowed α/ω   */
    double   w_lpmr_val;       /* windowed ω/ideal */
    double   w_mst_val;        /* windowed m/α   */

    /* --- state --- */
    uint8_t  last_class;

    void init() {
        h_pureHit_cy = m_pureMiss_cy = x_overlap_cy = e_idle_cy = m_byp_l1d_pureMiss_cy = m_byp_llc_pureMiss_cy = 0;
        roi_h_pureHit_cy = roi_m_pureMiss_cy = roi_x_overlap_cy = roi_e_idle_cy = 0;
        roi_m_byp_l1d_pureMiss_cy = roi_m_byp_llc_pureMiss_cy = 0;
        roi_camat_activeMemCyDivAccesses_ratio = roi_apc_accessesDivActiveMemCy_ratio = roi_lpmr_activeMemCyDivIdealCy_ratio = 0.0;
        roi_mst_pureMissCyDivAccesses_ratio = 0.0;
        camat_activeMemCyDivAccesses_ratio = apc_accessesDivActiveMemCy_ratio = lpmr_activeMemCyDivIdealCy_ratio = 0.0;
        mst_pureMissCyDivAccesses_ratio = 0.0;
        w_h = w_m = w_x = w_e = 0;
        w_tick = 0;
        w_c_amat_val = w_apc_val = w_lpmr_val = w_mst_val = 0.0;
        last_class = LPM_CLASS_E;
    }

    /* Reset sim counters at warmup (like reset_cache_stats does for sim_miss) */
    void reset_sim() {
        h_pureHit_cy = m_pureMiss_cy = x_overlap_cy = e_idle_cy = m_byp_l1d_pureMiss_cy = m_byp_llc_pureMiss_cy = 0;
        camat_activeMemCyDivAccesses_ratio = apc_accessesDivActiveMemCy_ratio = lpmr_activeMemCyDivIdealCy_ratio = 0.0;
        mst_pureMissCyDivAccesses_ratio = 0.0;
        /* NOTE: w_ counters NOT reset — they are windowed/decay, not cumulative */
    }

    /* Snapshot live counters into roi_ at simulation end */
    void record_roi() {
        roi_h_pureHit_cy = h_pureHit_cy; roi_m_pureMiss_cy = m_pureMiss_cy; roi_x_overlap_cy = x_overlap_cy; roi_e_idle_cy = e_idle_cy;
        roi_m_byp_l1d_pureMiss_cy = m_byp_l1d_pureMiss_cy;
        roi_m_byp_llc_pureMiss_cy = m_byp_llc_pureMiss_cy;
        roi_camat_activeMemCyDivAccesses_ratio = camat_activeMemCyDivAccesses_ratio;
        roi_apc_accessesDivActiveMemCy_ratio = apc_accessesDivActiveMemCy_ratio;
        roi_lpmr_activeMemCyDivIdealCy_ratio = lpmr_activeMemCyDivIdealCy_ratio;
        roi_mst_pureMissCyDivAccesses_ratio = mst_pureMissCyDivAccesses_ratio;
    }

    inline void tick(bool ha, bool ma) {
        uint8_t cls = ((uint8_t)ha << 1) | (uint8_t)ma;
        e_idle_cy += (cls == 0);
        m_pureMiss_cy += (cls == 1);
        h_pureHit_cy += (cls == 2);
        x_overlap_cy += (cls == 3);
        static const uint8_t map[4] = {
            LPM_CLASS_E, LPM_CLASS_M, LPM_CLASS_H, LPM_CLASS_X
        };
        last_class = map[cls];
        /* window tick — add to windowed counters, halve on boundary */
        w_e += (cls == 0);
        w_m += (cls == 1);
        w_h += (cls == 2);
        w_x += (cls == 3);
        if (++w_tick >= W_SIZE) {
            w_h >>= 1; w_m >>= 1; w_x >>= 1; w_e >>= 1;
            w_tick = 0;
        }
    }

    inline void tick_byp(bool ha, bool ma,
                         bool has_byp_miss, bool upper_pure_miss) {
        tick(ha, ma);
        m_byp_l1d_pureMiss_cy += (last_class == LPM_CLASS_M)
               & has_byp_miss
               & (!upper_pure_miss);
    }

    inline void tick_llc_byp(bool ha, bool ma,
                              bool has_byp_miss) {
        tick(ha, ma);
        m_byp_llc_pureMiss_cy += (last_class == LPM_CLASS_M)
               & has_byp_miss;
    }

    /* Update cached metrics. Called after tick().
     * α:    total accesses at this level (from sim_access)
     * ideal:    IC×CPIexe = e(L1D)+h(L1D)+x(L1D)               */
    inline void update_metrics(uint64_t α, uint64_t ideal) {
        uint64_t w = ω_numMemActiveCy();
        camat_activeMemCyDivAccesses_ratio = α ? (double)w / α  : 0.0;
        apc_accessesDivActiveMemCy_ratio    = w     ? (double)α / w  : 0.0;
        lpmr_activeMemCyDivIdealCy_ratio   = ideal ? (double)w / ideal  : 0.0;
        mst_pureMissCyDivAccesses_ratio     = α ? (double)m_pureMiss_cy / α : 0.0;
    }

    /* Update windowed metrics. w_α_proxy = w_h+w_m+w_x (windowed access proxy).
     * w_ideal = windowed L1D ideal cycles (e+h+x from L1D window counters).      */
    inline void update_windowed_metrics(uint64_t w_α_proxy, uint64_t w_ideal) {
        uint64_t ww = w_ω_numMemActiveCy();
        w_c_amat_val = w_α_proxy ? (double)ww / w_α_proxy : 0.0;
        w_apc_val    = ww            ? (double)w_α_proxy / ww  : 0.0;
        w_lpmr_val   = w_ideal       ? (double)ww / w_ideal        : 0.0;
        w_mst_val    = w_α_proxy ? (double)w_m / w_α_proxy : 0.0;
    }

    inline uint64_t ω_numMemActiveCy()   const { return h_pureHit_cy + m_pureMiss_cy + x_overlap_cy; }
    inline uint64_t num_totalCy() const { return e_idle_cy + h_pureHit_cy + m_pureMiss_cy + x_overlap_cy; }
    inline uint64_t w_ω_numMemActiveCy() const { return w_h + w_m + w_x; }

    /* ROI-based accessors (from snapshot) */
    inline uint64_t roi_ω_numMemActiveCy()   const { return roi_h_pureHit_cy + roi_m_pureMiss_cy + roi_x_overlap_cy; }
    inline uint64_t roi_num_totalCy() const { return roi_e_idle_cy + roi_h_pureHit_cy + roi_m_pureMiss_cy + roi_x_overlap_cy; }
    inline double roi_µ_sumMissOverlapCyDivMemActiveCy_ratio()    const { uint64_t w=roi_ω_numMemActiveCy(); return w ? (double)(roi_m_pureMiss_cy+roi_x_overlap_cy)/w : 0.0; }
    inline double roi_κ_pureMissCyDivSumMissOverlapCy_ratio() const { uint64_t d=roi_m_pureMiss_cy+roi_x_overlap_cy; return d ? (double)roi_m_pureMiss_cy/d        : 0.0; }
    inline double roi_φ_sumPureHitOverlapCyDivMemActiveCy_ratio()   const { uint64_t w=roi_ω_numMemActiveCy(); return w ? (double)(roi_h_pureHit_cy+roi_x_overlap_cy)/w : 0.0; }

    inline double µ_sumMissOverlapCyDivMemActiveCy_ratio()      const { uint64_t w=ω_numMemActiveCy();   return w ? (double)(m_pureMiss_cy+x_overlap_cy)/w   : 0.0; }
    inline double κ_pureMissCyDivSumMissOverlapCy_ratio()   const { uint64_t d=m_pureMiss_cy+x_overlap_cy;       return d ? (double)m_pureMiss_cy/d       : 0.0; }
    inline double φ_sumPureHitOverlapCyDivMemActiveCy_ratio()     const { uint64_t w=ω_numMemActiveCy();   return w ? (double)(h_pureHit_cy+x_overlap_cy)/w   : 0.0; }
    inline double w_µ_sumMissOverlapCyDivMemActiveCy_ratio()    const { uint64_t w=w_ω_numMemActiveCy(); return w ? (double)(w_m+w_x)/w : 0.0; }
    inline double w_κ_pureMissCyDivSumMissOverlapCy_ratio() const { uint64_t d=w_m+w_x;  return d ? (double)w_m/d     : 0.0; }
    inline double w_φ_sumPureHitOverlapCyDivMemActiveCy_ratio()   const { uint64_t w=w_ω_numMemActiveCy(); return w ? (double)(w_h+w_x)/w : 0.0; }
};

/* Global storage: lpm[cpu][cache_type] */
extern LPM_Tracker lpm[NUM_CPUS][LPM_NUM_TYPES];

inline void lpm_init() {
    for (int c = 0; c < NUM_CPUS; c++)
        for (int t = 0; t < LPM_NUM_TYPES; t++)
            lpm[c][t].init();
}

/* Reset sim counters at warmup (like reset_cache_stats) */
inline void lpm_reset_sim(int cpu) {
    for (int t = 0; t < LPM_NUM_TYPES; t++)
        lpm[cpu][t].reset_sim();
}

/* Snapshot sim → roi at simulation end (like record_roi_stats) */
inline void lpm_record_roi(int cpu) {
    for (int t = 0; t < LPM_NUM_TYPES; t++)
        lpm[cpu][t].record_roi();
}

/*-----------------------------------------------------------------
 * lpm_operate() — call from CACHE::operate() BEFORE handle_*
 *
 * Parameters:
 *   cpu, cache_type:  from CACHE object
 *   hit_active:       (RQ.occ | WQ.occ | PQ.occ) > 0
 *   miss_active:      MSHR has active misses (mode-dependent)
 *   α:            sum of sim_access[cpu][0..NUM_TYPES-1]
 *   has_byp_miss:     (L2C only) any MSHR entry with l1_bypassed|l2_bypassed|llc_bypassed>0
 *
 * Ticks counter, updates cached c_amat/apc/lpmr.
 * LPMR denominator: IC×CPIexe = e(L1D)+h(L1D)+x(L1D)  [eq 4,18]
 *-----------------------------------------------------------------*/
inline void lpm_operate(int cpu, uint8_t cache_type,
                        bool hit_active, bool miss_active,
                        uint64_t α, bool has_byp_miss) {
#ifdef BYPASS_LLC_LOGIC
    if (cache_type == IS_LLC) {
        lpm[cpu][cache_type].tick_llc_byp(hit_active, miss_active, has_byp_miss);
    } else
#endif
#ifdef BYPASS_L1_LOGIC
    if (cache_type == IS_L2C) {
        bool l1d_pm = (lpm[cpu][LPM_L1D].last_class == LPM_CLASS_M);
        lpm[cpu][cache_type].tick_byp(hit_active, miss_active,
                                      has_byp_miss, l1d_pm);
    } else
#endif
    {
        lpm[cpu][cache_type].tick(hit_active, miss_active);
    }

    const LPM_Tracker& l1d = lpm[cpu][LPM_L1D];
    uint64_t ideal = l1d.e_idle_cy + l1d.h_pureHit_cy + l1d.x_overlap_cy;
    lpm[cpu][cache_type].update_metrics(α, ideal);

    /* windowed metrics */
    uint64_t w_ideal       = l1d.w_e + l1d.w_h + l1d.w_x;
    LPM_Tracker& ct        = lpm[cpu][cache_type];
    uint64_t w_α_proxy = ct.w_h + ct.w_m + ct.w_x;
    ct.update_windowed_metrics(w_α_proxy, w_ideal);
}

/*-----------------------------------------------------------------
 * Standalone accessors (for final stats / cross-module queries)
 *-----------------------------------------------------------------*/
inline double lpm_c_amat(int cpu, uint8_t ct, uint64_t α) {
    return α ? (double)lpm[cpu][ct].ω_numMemActiveCy() / α : 0.0;
}
inline double lpm_apc(int cpu, uint8_t ct, uint64_t α) {
    uint64_t w = lpm[cpu][ct].ω_numMemActiveCy();
    return w ? (double)α / w : 0.0;
}
inline double lpm_mst(int cpu, uint8_t ct, uint64_t α) {
    return α ? (double)lpm[cpu][ct].m_pureMiss_cy / α : 0.0;
}

/*-----------------------------------------------------------------
 * Recursive C-AMAT [eq 31] — the real end-to-end memory cost
 *
 * rCAMAT = C-AMAT(L1) + µ(L1)·κ(L1)·C-AMAT(L2)
 *        + µ(L1)·κ(L1)·µ(L2)·κ(L2)·C-AMAT(LLC)
 *
 * Where µ·κ = m/ω = pure-miss fraction of active cycles
 *-----------------------------------------------------------------*/
inline double get_recursive_camat(int cpu) {
    const LPM_Tracker& l1 = lpm[cpu][LPM_L1D];
    const LPM_Tracker& l2 = lpm[cpu][LPM_L2C];
    const LPM_Tracker& llc = lpm[cpu][LPM_LLC];
    const LPM_Tracker& dram = lpm[cpu][LPM_DRAM];
    double camat_l1 = l1.camat_activeMemCyDivAccesses_ratio;
    double camat_l2 = l2.camat_activeMemCyDivAccesses_ratio;
    double camat_llc = llc.camat_activeMemCyDivAccesses_ratio;
    double camat_dram = dram.camat_activeMemCyDivAccesses_ratio;
    double mk_l1 = l1.µ_sumMissOverlapCyDivMemActiveCy_ratio() * l1.κ_pureMissCyDivSumMissOverlapCy_ratio();
    double mk_l2 = l2.µ_sumMissOverlapCyDivMemActiveCy_ratio() * l2.κ_pureMissCyDivSumMissOverlapCy_ratio();
    double mk_llc = llc.µ_sumMissOverlapCyDivMemActiveCy_ratio() * llc.κ_pureMissCyDivSumMissOverlapCy_ratio();
    return camat_l1 + mk_l1 * camat_l2 + mk_l1 * mk_l2 * camat_llc + mk_l1 * mk_l2 * mk_llc * camat_dram;
}

inline double get_recursive_camat_roi(int cpu) {
    const LPM_Tracker& l1 = lpm[cpu][LPM_L1D];
    const LPM_Tracker& l2 = lpm[cpu][LPM_L2C];
    const LPM_Tracker& llc = lpm[cpu][LPM_LLC];
    const LPM_Tracker& dram = lpm[cpu][LPM_DRAM];
    double camat_l1 = l1.roi_camat_activeMemCyDivAccesses_ratio;
    double camat_l2 = l2.roi_camat_activeMemCyDivAccesses_ratio;
    double camat_llc = llc.roi_camat_activeMemCyDivAccesses_ratio;
    double camat_dram = dram.roi_camat_activeMemCyDivAccesses_ratio;
    double mk_l1 = l1.roi_µ_sumMissOverlapCyDivMemActiveCy_ratio() * l1.roi_κ_pureMissCyDivSumMissOverlapCy_ratio();
    double mk_l2 = l2.roi_µ_sumMissOverlapCyDivMemActiveCy_ratio() * l2.roi_κ_pureMissCyDivSumMissOverlapCy_ratio();
    double mk_llc = llc.roi_µ_sumMissOverlapCyDivMemActiveCy_ratio() * llc.roi_κ_pureMissCyDivSumMissOverlapCy_ratio();
    return camat_l1 + mk_l1 * camat_l2 + mk_l1 * mk_l2 * camat_llc + mk_l1 * mk_l2 * mk_llc * camat_dram;
}

/* Recursive APC = 1 / recursive C-AMAT */
inline double get_recursive_apc(int cpu) {
    double rc = get_recursive_camat(cpu);
    return rc > 0.0 ? 1.0 / rc : 0.0;
}
inline double get_recursive_apc_roi(int cpu) {
    double rc = get_recursive_camat_roi(cpu);
    return rc > 0.0 ? 1.0 / rc : 0.0;
}

/* Standard LPMR [eq 52] — live (sim-style, post-warmup cumulative) */
inline double get_LPMR_std(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t ideal = ref.e_idle_cy + ref.h_pureHit_cy + ref.x_overlap_cy;
    return ideal ? (double)lpm[cpu][ct].ω_numMemActiveCy() / ideal : 0.0;
}

/* Bypass-corrected LPMR — live */
inline double get_LPMR_byp(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t app = ref.e_idle_cy + ref.h_pureHit_cy + ref.x_overlap_cy;
    uint64_t cor = lpm[cpu][LPM_L1D].m_byp_l1d_pureMiss_cy;
    uint64_t ideal = (app > cor) ? (app - cor) : 1;
    return (double)lpm[cpu][ct].ω_numMemActiveCy() / ideal;
}

/* ROI-based LPMR — from snapshot at sim end */
inline double get_LPMR_roi_std(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t ideal = ref.roi_e_idle_cy + ref.roi_h_pureHit_cy + ref.roi_x_overlap_cy;
    return ideal ? (double)lpm[cpu][ct].roi_ω_numMemActiveCy() / ideal : 0.0;
}
inline double get_LPMR_roi_byp(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t app = ref.roi_e_idle_cy + ref.roi_h_pureHit_cy + ref.roi_x_overlap_cy;
    uint64_t cor = lpm[cpu][LPM_L1D].roi_m_byp_l1d_pureMiss_cy;
    uint64_t ideal = (app > cor) ? (app - cor) : 1;
    return (double)lpm[cpu][ct].roi_ω_numMemActiveCy() / ideal;
}

inline double get_LPMR_global_std(int cpu) {
    return get_LPMR_std(cpu, LPM_L1D)
         * get_LPMR_std(cpu, LPM_L2C)
         * get_LPMR_std(cpu, LPM_LLC)
         * get_LPMR_std(cpu, LPM_DRAM);
}
inline double get_LPMR_global_byp(int cpu) {
    return get_LPMR_byp(cpu, LPM_L1D)
         * get_LPMR_byp(cpu, LPM_L2C)
         * get_LPMR_byp(cpu, LPM_LLC)
         * get_LPMR_byp(cpu, LPM_DRAM);
}
inline double get_LPMR_global_roi_std(int cpu) {
    return get_LPMR_roi_std(cpu, LPM_L1D)
         * get_LPMR_roi_std(cpu, LPM_L2C)
         * get_LPMR_roi_std(cpu, LPM_LLC)
         * get_LPMR_roi_std(cpu, LPM_DRAM);
}
inline double get_LPMR_global_roi_byp(int cpu) {
    return get_LPMR_roi_byp(cpu, LPM_L1D)
         * get_LPMR_roi_byp(cpu, LPM_L2C)
         * get_LPMR_roi_byp(cpu, LPM_LLC)
         * get_LPMR_roi_byp(cpu, LPM_DRAM);
}

/* Final stats print — uses ROI snapshot (post-warmup only) */
inline void lpm_print(int cpu) {
    static const char* nm[] = {"ITLB","DTLB","STLB","L1I ","L1D ","L2C ","LLC ","DRAM"};
    printf("\n=== LPM Cycle Classification  CPU %d (ROI) ===\n", cpu);
    printf("%-4s %12s %12s %12s %12s | %12s %8s %8s %8s | %10s %10s | %8s %8s %8s\n",
           "Lvl", "h(hit)", "m(miss)", "x(mixed)", "e(idle)",
           "omega", "mu", "kappa", "phi", "LPMR_std", "LPMR_byp",
           "C-AMAT", "APC", "MST");
    for (int t = 0; t < LPM_NUM_TYPES; t++) {
        const LPM_Tracker& s = lpm[cpu][t];
        if (s.roi_num_totalCy() == 0) continue;
        printf("%-4s %12lu %12lu %12lu %12lu | %12lu %8.4f %8.4f %8.4f",
               nm[t], s.roi_h_pureHit_cy, s.roi_m_pureMiss_cy, s.roi_x_overlap_cy, s.roi_e_idle_cy, s.roi_ω_numMemActiveCy(),
               s.roi_µ_sumMissOverlapCyDivMemActiveCy_ratio(), s.roi_κ_pureMissCyDivSumMissOverlapCy_ratio(), s.roi_φ_sumPureHitOverlapCyDivMemActiveCy_ratio());
        if (t >= LPM_L1D)
            printf(" | %10.6f %10.6f | %8.4f %8.4f %8.4f",
                   get_LPMR_roi_std(cpu, t), get_LPMR_roi_byp(cpu, t),
                   s.roi_camat_activeMemCyDivAccesses_ratio, s.roi_apc_accessesDivActiveMemCy_ratio,
                   s.roi_mst_pureMissCyDivAccesses_ratio);
        else
            printf(" | %10s %10s | %8s %8s %8s", "n/a","n/a","n/a","n/a","n/a");
        printf("\n");
    }
    printf("Global LPMR_std: %.6f  LPMR_byp: %.6f\n",
           get_LPMR_global_roi_std(cpu), get_LPMR_global_roi_byp(cpu));
}

#endif /* LPM_TRACKER_H */
