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
 *   c_amat_val = ω(l) / α(l)                              [eq 30]
 *   apc_val    = α(l) / ω(l)                               [eq 32]
 *   lpmr_val   = ω(l) / (IC × CPIexe)                      [eq 52]
 *
 * Two MSHR modes (compile switch LPM_STRICT_MISS):
 *   OFF: MSHR.occupancy > 0. Includes returned-not-filled.
 *   ON:  INFLIGHT entries only. Paper-exact: miss ends at data return.
 *
 * Two LPMR methods:
 *   get_LPMR_std(): Paper standard. L1D as reference.
 *   get_LPMR_byp(): Bypass-corrected. Subtracts l1d_m_byp.
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

#define LPM_NUM_TYPES 7
#define LPM_L1D       4
#define LPM_L2C       5
#define LPM_LLC       6

#define LPM_CLASS_E 0
#define LPM_CLASS_H 1
#define LPM_CLASS_M 2
#define LPM_CLASS_X 3

struct LPM_Tracker {
    /* --- cycle counters (sim-style: reset at warmup, accumulate post-warmup) --- */
    uint64_t h, m, x, e;

    /* --- ROI snapshot (frozen at sim end, used by final stats) --- */
    uint64_t roi_h, roi_m, roi_x, roi_e;
    uint64_t roi_l1d_m_byp;
    uint64_t roi_llc_m_byp;
    double   roi_c_amat_val;
    double   roi_apc_val;
    double   roi_lpmr_val;

    /* --- bypass correction (L2C only) --- */
    uint64_t l1d_m_byp;
    uint64_t llc_m_byp;

    /* --- cached metrics (updated per lpm_operate) --- */
    double   c_amat_val;       /* ω/α            [eq 30] */
    double   apc_val;          /* α/ω            [eq 32] */
    double   lpmr_val;         /* ω/(IC×CPIexe)  [eq 52] */

    /* --- window-based counters (exponential decay every W_SIZE cycles) --- */
    static constexpr uint32_t W_SIZE = 512;
    uint64_t w_h, w_m, w_x, w_e;
    uint32_t w_tick;
    double   w_c_amat_val;     /* windowed ω/α   */
    double   w_apc_val;        /* windowed α/ω   */
    double   w_lpmr_val;       /* windowed ω/ideal */

    /* --- state --- */
    uint8_t  last_class;

    void init() {
        h = m = x = e = l1d_m_byp = llc_m_byp = 0;
        roi_h = roi_m = roi_x = roi_e = 0;
        roi_l1d_m_byp = roi_llc_m_byp = 0;
        roi_c_amat_val = roi_apc_val = roi_lpmr_val = 0.0;
        c_amat_val = apc_val = lpmr_val = 0.0;
        w_h = w_m = w_x = w_e = 0;
        w_tick = 0;
        w_c_amat_val = w_apc_val = w_lpmr_val = 0.0;
        last_class = LPM_CLASS_E;
    }

    /* Reset sim counters at warmup (like reset_cache_stats does for sim_miss) */
    void reset_sim() {
        h = m = x = e = l1d_m_byp = llc_m_byp = 0;
        c_amat_val = apc_val = lpmr_val = 0.0;
        /* NOTE: w_ counters NOT reset — they are windowed/decay, not cumulative */
    }

    /* Snapshot live counters into roi_ at simulation end */
    void record_roi() {
        roi_h = h; roi_m = m; roi_x = x; roi_e = e;
        roi_l1d_m_byp = l1d_m_byp;
        roi_llc_m_byp = llc_m_byp;
        roi_c_amat_val = c_amat_val;
        roi_apc_val = apc_val;
        roi_lpmr_val = lpmr_val;
    }

    inline void tick(bool ha, bool ma) {
        uint8_t cls = ((uint8_t)ha << 1) | (uint8_t)ma;
        e += (cls == 0);
        m += (cls == 1);
        h += (cls == 2);
        x += (cls == 3);
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
        l1d_m_byp += (last_class == LPM_CLASS_M)
               & has_byp_miss
               & (!upper_pure_miss);
    }

    inline void tick_llc_byp(bool ha, bool ma,
                              bool has_byp_miss) {
        tick(ha, ma);
        llc_m_byp += (last_class == LPM_CLASS_M)
               & has_byp_miss;
    }

    /* Update cached metrics. Called after tick().
     * α:    total accesses at this level (from sim_access)
     * ideal:    IC×CPIexe = e(L1D)+h(L1D)+x(L1D)               */
    inline void update_metrics(uint64_t α, uint64_t ideal) {
        uint64_t w = omega();
        c_amat_val = α ? (double)w / α  : 0.0;
        apc_val    = w     ? (double)α / w  : 0.0;
        lpmr_val   = ideal ? (double)w / ideal  : 0.0;
    }

    /* Update windowed metrics. w_α_proxy = w_h+w_m+w_x (windowed access proxy).
     * w_ideal = windowed L1D ideal cycles (e+h+x from L1D window counters).      */
    inline void update_windowed_metrics(uint64_t w_α_proxy, uint64_t w_ideal) {
        uint64_t ww = w_omega();
        w_c_amat_val = w_α_proxy ? (double)ww / w_α_proxy : 0.0;
        w_apc_val    = ww            ? (double)w_α_proxy / ww  : 0.0;
        w_lpmr_val   = w_ideal       ? (double)ww / w_ideal        : 0.0;
    }

    inline uint64_t omega()   const { return h + m + x; }
    inline uint64_t n_total() const { return e + h + m + x; }
    inline uint64_t w_omega() const { return w_h + w_m + w_x; }

    /* ROI-based accessors (from snapshot) */
    inline uint64_t roi_omega()   const { return roi_h + roi_m + roi_x; }
    inline uint64_t roi_n_total() const { return roi_e + roi_h + roi_m + roi_x; }
    inline double roi_mu()    const { uint64_t w=roi_omega(); return w ? (double)(roi_m+roi_x)/w : 0.0; }
    inline double roi_kappa() const { uint64_t d=roi_m+roi_x; return d ? (double)roi_m/d        : 0.0; }
    inline double roi_phi()   const { uint64_t w=roi_omega(); return w ? (double)(roi_h+roi_x)/w : 0.0; }

    inline double mu()      const { uint64_t w=omega();   return w ? (double)(m+x)/w   : 0.0; }
    inline double kappa()   const { uint64_t d=m+x;       return d ? (double)m/d       : 0.0; }
    inline double phi()     const { uint64_t w=omega();   return w ? (double)(h+x)/w   : 0.0; }
    inline double w_mu()    const { uint64_t w=w_omega(); return w ? (double)(w_m+w_x)/w : 0.0; }
    inline double w_kappa() const { uint64_t d=w_m+w_x;  return d ? (double)w_m/d     : 0.0; }
    inline double w_phi()   const { uint64_t w=w_omega(); return w ? (double)(w_h+w_x)/w : 0.0; }
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
    uint64_t ideal = l1d.e + l1d.h + l1d.x;
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
    return α ? (double)lpm[cpu][ct].omega() / α : 0.0;
}
inline double lpm_apc(int cpu, uint8_t ct, uint64_t α) {
    uint64_t w = lpm[cpu][ct].omega();
    return w ? (double)α / w : 0.0;
}
inline double lpm_mst(int cpu, uint64_t α_l1d) {
    return α_l1d ? (double)lpm[cpu][LPM_L1D].m / α_l1d : 0.0;
}

/* Standard LPMR [eq 52] — live (sim-style, post-warmup cumulative) */
inline double get_LPMR_std(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t ideal = ref.e + ref.h + ref.x;
    return ideal ? (double)lpm[cpu][ct].omega() / ideal : 0.0;
}

/* Bypass-corrected LPMR — live */
inline double get_LPMR_byp(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t app = ref.e + ref.h + ref.x;
    uint64_t cor = lpm[cpu][LPM_L1D].l1d_m_byp;
    uint64_t ideal = (app > cor) ? (app - cor) : 1;
    return (double)lpm[cpu][ct].omega() / ideal;
}

/* ROI-based LPMR — from snapshot at sim end */
inline double get_LPMR_roi_std(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t ideal = ref.roi_e + ref.roi_h + ref.roi_x;
    return ideal ? (double)lpm[cpu][ct].roi_omega() / ideal : 0.0;
}
inline double get_LPMR_roi_byp(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t app = ref.roi_e + ref.roi_h + ref.roi_x;
    uint64_t cor = lpm[cpu][LPM_L1D].roi_l1d_m_byp;
    uint64_t ideal = (app > cor) ? (app - cor) : 1;
    return (double)lpm[cpu][ct].roi_omega() / ideal;
}

inline double get_LPMR_global_std(int cpu) {
    return get_LPMR_std(cpu, LPM_L1D)
         * get_LPMR_std(cpu, LPM_L2C)
         * get_LPMR_std(cpu, LPM_LLC);
}
inline double get_LPMR_global_byp(int cpu) {
    return get_LPMR_byp(cpu, LPM_L1D)
         * get_LPMR_byp(cpu, LPM_L2C)
         * get_LPMR_byp(cpu, LPM_LLC);
}
inline double get_LPMR_global_roi_std(int cpu) {
    return get_LPMR_roi_std(cpu, LPM_L1D)
         * get_LPMR_roi_std(cpu, LPM_L2C)
         * get_LPMR_roi_std(cpu, LPM_LLC);
}
inline double get_LPMR_global_roi_byp(int cpu) {
    return get_LPMR_roi_byp(cpu, LPM_L1D)
         * get_LPMR_roi_byp(cpu, LPM_L2C)
         * get_LPMR_roi_byp(cpu, LPM_LLC);
}

/* Final stats print — uses ROI snapshot (post-warmup only) */
inline void lpm_print(int cpu) {
    static const char* nm[] = {"ITLB","DTLB","STLB","L1I ","L1D ","L2C ","LLC "};
    printf("\n=== LPM Cycle Classification  CPU %d (ROI) ===\n", cpu);
    printf("%-4s %12s %12s %12s %12s | %12s %8s %8s %8s | %10s %10s | %8s %8s\n",
           "Lvl", "h(hit)", "m(miss)", "x(mixed)", "e(idle)",
           "omega", "mu", "kappa", "phi", "LPMR_std", "LPMR_byp",
           "C-AMAT", "APC");
    for (int t = 0; t < LPM_NUM_TYPES; t++) {
        const LPM_Tracker& s = lpm[cpu][t];
        if (s.roi_n_total() == 0) continue;
        printf("%-4s %12lu %12lu %12lu %12lu | %12lu %8.4f %8.4f %8.4f",
               nm[t], s.roi_h, s.roi_m, s.roi_x, s.roi_e, s.roi_omega(),
               s.roi_mu(), s.roi_kappa(), s.roi_phi());
        if (t >= LPM_L1D)
            printf(" | %10.6f %10.6f | %8.4f %8.4f",
                   get_LPMR_roi_std(cpu, t), get_LPMR_roi_byp(cpu, t),
                   s.roi_c_amat_val, s.roi_apc_val);
        else
            printf(" | %10s %10s | %8s %8s", "n/a","n/a","n/a","n/a");
        printf("\n");
    }
    printf("Global LPMR_std: %.6f  LPMR_byp: %.6f\n",
           get_LPMR_global_roi_std(cpu), get_LPMR_global_roi_byp(cpu));
}

#endif /* LPM_TRACKER_H */
