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
 *   get_LPMR_byp(): Bypass-corrected. Subtracts m_byp.
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
    /* --- cycle counters --- */
    uint64_t h, m, x, e;

    /* --- bypass correction (L2C only) --- */
    uint64_t m_byp;

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
        h = m = x = e = m_byp = 0;
        c_amat_val = apc_val = lpmr_val = 0.0;
        w_h = w_m = w_x = w_e = 0;
        w_tick = 0;
        w_c_amat_val = w_apc_val = w_lpmr_val = 0.0;
        last_class = LPM_CLASS_E;
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
                         bool has_byp_miss, bool l1d_pure_miss) {
        tick(ha, ma);
        m_byp += (last_class == LPM_CLASS_M)
               & has_byp_miss
               & (!l1d_pure_miss);
    }

    /* Update cached metrics. Called after tick().
     * alpha:    total accesses at this level (from sim_access)
     * ideal:    IC×CPIexe = e(L1D)+h(L1D)+x(L1D)               */
    inline void update_metrics(uint64_t alpha, uint64_t ideal) {
        uint64_t w = omega();
        c_amat_val = alpha ? (double)w / alpha  : 0.0;
        apc_val    = w     ? (double)alpha / w  : 0.0;
        lpmr_val   = ideal ? (double)w / ideal  : 0.0;
    }

    /* Update windowed metrics. w_alpha_proxy = w_h+w_m+w_x (windowed access proxy).
     * w_ideal = windowed L1D ideal cycles (e+h+x from L1D window counters).      */
    inline void update_windowed_metrics(uint64_t w_alpha_proxy, uint64_t w_ideal) {
        uint64_t ww = w_omega();
        w_c_amat_val = w_alpha_proxy ? (double)ww / w_alpha_proxy : 0.0;
        w_apc_val    = ww            ? (double)w_alpha_proxy / ww  : 0.0;
        w_lpmr_val   = w_ideal       ? (double)ww / w_ideal        : 0.0;
    }

    inline uint64_t omega()   const { return h + m + x; }
    inline uint64_t n_total() const { return e + h + m + x; }
    inline uint64_t w_omega() const { return w_h + w_m + w_x; }

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

/*-----------------------------------------------------------------
 * lpm_operate() — call from CACHE::operate() BEFORE handle_*
 *
 * Parameters:
 *   cpu, cache_type:  from CACHE object
 *   hit_active:       (RQ.occ | WQ.occ | PQ.occ) > 0
 *   miss_active:      MSHR has active misses (mode-dependent)
 *   alpha:            sum of sim_access[cpu][0..NUM_TYPES-1]
 *   has_byp_miss:     (L2C only) any MSHR entry with bypassed_levels>0
 *
 * Ticks counter, updates cached c_amat/apc/lpmr.
 * LPMR denominator: IC×CPIexe = e(L1D)+h(L1D)+x(L1D)  [eq 4,18]
 *-----------------------------------------------------------------*/
inline void lpm_operate(int cpu, uint8_t cache_type,
                        bool hit_active, bool miss_active,
                        uint64_t alpha, bool has_byp_miss) {
#ifdef BYPASS_LOGIC
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
    lpm[cpu][cache_type].update_metrics(alpha, ideal);

    /* windowed metrics */
    uint64_t w_ideal       = l1d.w_e + l1d.w_h + l1d.w_x;
    LPM_Tracker& ct        = lpm[cpu][cache_type];
    uint64_t w_alpha_proxy = ct.w_h + ct.w_m + ct.w_x;
    ct.update_windowed_metrics(w_alpha_proxy, w_ideal);
}

/*-----------------------------------------------------------------
 * Standalone accessors (for final stats / cross-module queries)
 *-----------------------------------------------------------------*/
inline double lpm_c_amat(int cpu, uint8_t ct, uint64_t alpha) {
    return alpha ? (double)lpm[cpu][ct].omega() / alpha : 0.0;
}
inline double lpm_apc(int cpu, uint8_t ct, uint64_t alpha) {
    uint64_t w = lpm[cpu][ct].omega();
    return w ? (double)alpha / w : 0.0;
}
inline double lpm_mst(int cpu, uint64_t alpha_l1d) {
    return alpha_l1d ? (double)lpm[cpu][LPM_L1D].m / alpha_l1d : 0.0;
}

/* Standard LPMR [eq 52] */
inline double get_LPMR_std(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t ideal = ref.e + ref.h + ref.x;
    return ideal ? (double)lpm[cpu][ct].omega() / ideal : 0.0;
}

/* Bypass-corrected LPMR */
inline double get_LPMR_byp(int cpu, uint8_t ct) {
    const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
    uint64_t app = ref.e + ref.h + ref.x;
    uint64_t cor = lpm[cpu][LPM_L2C].m_byp;
    uint64_t ideal = (app > cor) ? (app - cor) : 1;
    return (double)lpm[cpu][ct].omega() / ideal;
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

inline void lpm_print(int cpu) {
    static const char* nm[] = {"ITLB","DTLB","STLB","L1I ","L1D ","L2C ","LLC "};
    printf("\n=== LPM Cycle Classification  CPU %d ===\n", cpu);
    printf("%-4s %12s %12s %12s %12s | %12s %8s %8s %8s | %10s %10s | %8s %8s\n",
           "Lvl", "h(hit)", "m(miss)", "x(mixed)", "e(idle)",
           "omega", "mu", "kappa", "phi", "LPMR_std", "LPMR_byp",
           "C-AMAT", "APC");
    for (int t = 0; t < LPM_NUM_TYPES; t++) {
        const LPM_Tracker& s = lpm[cpu][t];
        if (s.n_total() == 0) continue;
        printf("%-4s %12lu %12lu %12lu %12lu | %12lu %8.4f %8.4f %8.4f",
               nm[t], s.h, s.m, s.x, s.e, s.omega(),
               s.mu(), s.kappa(), s.phi());
        if (t >= LPM_L1D)
            printf(" | %10.6f %10.6f | %8.4f %8.4f",
                   get_LPMR_std(cpu, t), get_LPMR_byp(cpu, t),
                   s.c_amat_val, s.apc_val);
        else
            printf(" | %10s %10s | %8s %8s", "n/a","n/a","n/a","n/a");
        printf("\n");
    }
    if (lpm[cpu][LPM_L2C].m_byp > 0)
        printf("L2C m_byp: %lu\n", lpm[cpu][LPM_L2C].m_byp);
    printf("Global LPMR_std: %.6f  LPMR_byp: %.6f\n",
           get_LPMR_global_std(cpu), get_LPMR_global_byp(cpu));
}

#endif /* LPM_TRACKER_H */


/*
 * lpm_tracker.h — Drop-in compatible LPM cycle tracker
 *
 * IMPORTANT TRUTH:
 * This file preserves the SAME public fields, SAME function names,
 * SAME signatures, and SAME external call pattern as the original.
 *
 * Therefore:
 *   - It CAN correctly track binary cycle classes e/h/m/x.
 *   - It CAN correctly compute cumulative C-AMAT/APC/LPMR from omega and alpha.
 *   - It CAN fix windowed C-AMAT/APC by deriving window-alpha from cumulative alpha deltas.
 *   - It CANNOT become paper-exact for ci / ci^(h) / ci^(m), CH, Cm, CM, pAMP,
 *     alpha_M, rho_M, etc., because that information is not provided by the fixed API.
 */

// #ifndef LPM_TRACKER_H


// #define LPM_TRACKER_H

// #include "memory_class.h"
// #include "cache.h"
// #include "defs.h"
// #include <cstdint>
// #include <cstdio>

// /* Uncomment for paper-strict miss detection (inflight MSHR only) */
// #define LPM_STRICT_MISS

// #define LPM_NUM_TYPES 7
// #define LPM_L1D       4
// #define LPM_L2C       5
// #define LPM_LLC       6

// #define LPM_CLASS_E 0
// #define LPM_CLASS_H 1
// #define LPM_CLASS_M 2
// #define LPM_CLASS_X 3

// struct LPM_Tracker {
//     /* --- cycle counters --- */
//     uint64_t h, m, x, e;

//     /* --- bypass correction (L2C only) --- */
//     uint64_t m_byp;

//     /* --- cached metrics (updated per lpm_operate) --- */
//     double   c_amat_val;       /* omega/alpha            [eq 30] */
//     double   apc_val;          /* alpha/omega            [eq 32] */
//     double   lpmr_val;         /* omega/(IC*CPIexe)      [eq 52] */

//     /* --- window-based counters (exponential decay every W_SIZE cycles) --- */
//     static constexpr uint32_t W_SIZE = 512;
//     uint64_t w_h, w_m, w_x, w_e;
//     uint32_t w_tick;
//     double   w_c_amat_val;     /* windowed omega/alpha   */
//     double   w_apc_val;        /* windowed alpha/omega   */
//     double   w_lpmr_val;       /* windowed omega/ideal   */

//     /* --- state --- */
//     uint8_t  last_class;

//     /* --- INTERNAL bookkeeping (does not change external API) --- */
//     uint64_t last_alpha_total; /* last cumulative alpha seen */
//     uint64_t w_alpha;          /* decayed windowed access count derived from alpha deltas */

//     void init() {
//         h = m = x = e = m_byp = 0;
//         c_amat_val = apc_val = lpmr_val = 0.0;

//         w_h = w_m = w_x = w_e = 0;
//         w_tick = 0;
//         w_c_amat_val = w_apc_val = w_lpmr_val = 0.0;

//         last_class = LPM_CLASS_E;

//         last_alpha_total = 0;
//         w_alpha = 0;
//     }

//     inline void tick(bool ha, bool ma) {
//         uint8_t cls = (static_cast<uint8_t>(ha) << 1) | static_cast<uint8_t>(ma);

//         e += (cls == 0);
//         m += (cls == 1);
//         h += (cls == 2);
//         x += (cls == 3);

//         static const uint8_t map[4] = {
//             LPM_CLASS_E, LPM_CLASS_M, LPM_CLASS_H, LPM_CLASS_X
//         };
//         last_class = map[cls];

//         /* window tick */
//         w_e += (cls == 0);
//         w_m += (cls == 1);
//         w_h += (cls == 2);
//         w_x += (cls == 3);

//         if (++w_tick >= W_SIZE) {
//             w_h >>= 1;
//             w_m >>= 1;
//             w_x >>= 1;
//             w_e >>= 1;
//             w_alpha >>= 1;   /* keep access-count window aligned with cycle windows */
//             w_tick = 0;
//         }
//     }

//     inline void tick_byp(bool ha, bool ma,
//                          bool has_byp_miss, bool l1d_pure_miss) {
//         tick(ha, ma);

//         /* keep original intent, but use logical ops explicitly */
//         if ((last_class == LPM_CLASS_M) && has_byp_miss && (!l1d_pure_miss))
//             ++m_byp;
//     }

//     /* Update cached metrics. Called after tick().
//      * alpha: cumulative total accesses at this level
//      * ideal: IC*CPIexe proxy = e(L1D)+h(L1D)+x(L1D)
//      */
//     inline void update_metrics(uint64_t alpha, uint64_t ideal) {
//         /* Derive per-cycle access increment from cumulative alpha. */
//         uint64_t delta_alpha = (alpha >= last_alpha_total) ? (alpha - last_alpha_total)
//                                                            : alpha; /* counter reset / restart */
//         last_alpha_total = alpha;
//         w_alpha += delta_alpha;

//         uint64_t w = omega();
//         c_amat_val = alpha ? static_cast<double>(w) / static_cast<double>(alpha) : 0.0;
//         apc_val    = w     ? static_cast<double>(alpha) / static_cast<double>(w) : 0.0;
//         lpmr_val   = ideal ? static_cast<double>(w) / static_cast<double>(ideal) : 0.0;
//     }

//     /* Update windowed metrics.
//      * NOTE: signature preserved for drop-in compatibility.
//      * The passed w_alpha_proxy is intentionally ignored because using
//      * w_h+w_m+w_x as alpha proxy makes windowed C-AMAT/APC degenerate.
//      */
//     inline void update_windowed_metrics(uint64_t /*w_alpha_proxy*/, uint64_t w_ideal) {
//         uint64_t ww = w_omega();

//         w_c_amat_val = w_alpha ? static_cast<double>(ww) / static_cast<double>(w_alpha) : 0.0;
//         w_apc_val    = ww      ? static_cast<double>(w_alpha) / static_cast<double>(ww) : 0.0;
//         w_lpmr_val   = w_ideal ? static_cast<double>(ww) / static_cast<double>(w_ideal) : 0.0;
//     }

//     inline uint64_t omega()   const { return h + m + x; }
//     inline uint64_t n_total() const { return e + h + m + x; }
//     inline uint64_t w_omega() const { return w_h + w_m + w_x; }

//     inline double mu() const {
//         uint64_t w = omega();
//         return w ? static_cast<double>(m + x) / static_cast<double>(w) : 0.0;
//     }

//     inline double kappa() const {
//         uint64_t d = m + x;
//         return d ? static_cast<double>(m) / static_cast<double>(d) : 0.0;
//     }

//     inline double phi() const {
//         uint64_t w = omega();
//         return w ? static_cast<double>(h + x) / static_cast<double>(w) : 0.0;
//     }

//     inline double w_mu() const {
//         uint64_t w = w_omega();
//         return w ? static_cast<double>(w_m + w_x) / static_cast<double>(w) : 0.0;
//     }

//     inline double w_kappa() const {
//         uint64_t d = w_m + w_x;
//         return d ? static_cast<double>(w_m) / static_cast<double>(d) : 0.0;
//     }

//     inline double w_phi() const {
//         uint64_t w = w_omega();
//         return w ? static_cast<double>(w_h + w_x) / static_cast<double>(w) : 0.0;
//     }
// };

// /* Global storage: lpm[cpu][cache_type] */
// extern LPM_Tracker lpm[NUM_CPUS][LPM_NUM_TYPES];

// inline void lpm_init() {
//     for (int c = 0; c < NUM_CPUS; c++)
//         for (int t = 0; t < LPM_NUM_TYPES; t++)
//             lpm[c][t].init();
// }

// /*-----------------------------------------------------------------
//  * lpm_operate() — call from CACHE::operate() BEFORE handle_*
//  *
//  * Parameters:
//  *   cpu, cache_type:  from CACHE object
//  *   hit_active:       binary hit-side activity
//  *   miss_active:      binary miss-side activity
//  *   alpha:            cumulative accesses at this level
//  *   has_byp_miss:     (L2C only) any MSHR entry with bypassed_levels>0
//  *
//  * NOTE:
//  *   This remains a binary-activity tracker.
//  *   It is NOT paper-exact ci/ch/cm accounting, because current API
//  *   does not expose those quantities.
//  *-----------------------------------------------------------------*/
// inline void lpm_operate(int cpu, uint8_t cache_type,
//                         bool hit_active, bool miss_active,
//                         uint64_t alpha, bool has_byp_miss) {
// #ifdef BYPASS_LOGIC
//     if (cache_type == IS_L2C) {
//         bool l1d_pm = (lpm[cpu][LPM_L1D].last_class == LPM_CLASS_M);
//         lpm[cpu][cache_type].tick_byp(hit_active, miss_active,
//                                       has_byp_miss, l1d_pm);
//     } else
// #endif
//     {
//         lpm[cpu][cache_type].tick(hit_active, miss_active);
//     }

//     const LPM_Tracker& l1d = lpm[cpu][LPM_L1D];
//     uint64_t ideal = l1d.e + l1d.h + l1d.x;
//     lpm[cpu][cache_type].update_metrics(alpha, ideal);

//     /* windowed metrics */
//     uint64_t w_ideal       = l1d.w_e + l1d.w_h + l1d.w_x;
//     LPM_Tracker& ct        = lpm[cpu][cache_type];

//     /* Preserved for compatibility; no longer used internally. */
//     uint64_t w_alpha_proxy = ct.w_h + ct.w_m + ct.w_x;

//     ct.update_windowed_metrics(w_alpha_proxy, w_ideal);
// }

// /*-----------------------------------------------------------------
//  * Standalone accessors (for final stats / cross-module queries)
//  *-----------------------------------------------------------------*/
// inline double lpm_c_amat(int cpu, uint8_t ct, uint64_t alpha) {
//     return alpha ? static_cast<double>(lpm[cpu][ct].omega()) / static_cast<double>(alpha) : 0.0;
// }

// inline double lpm_apc(int cpu, uint8_t ct, uint64_t alpha) {
//     uint64_t w = lpm[cpu][ct].omega();
//     return w ? static_cast<double>(alpha) / static_cast<double>(w) : 0.0;
// }

// inline double lpm_mst(int cpu, uint64_t alpha_l1d) {
//     return alpha_l1d ? static_cast<double>(lpm[cpu][LPM_L1D].m) / static_cast<double>(alpha_l1d) : 0.0;
// }

// /* Standard LPMR [eq 52] */
// inline double get_LPMR_std(int cpu, uint8_t ct) {
//     const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
//     uint64_t ideal = ref.e + ref.h + ref.x;
//     return ideal ? static_cast<double>(lpm[cpu][ct].omega()) / static_cast<double>(ideal) : 0.0;
// }

// /* Bypass-corrected LPMR (custom heuristic, not paper-standard) */
// inline double get_LPMR_byp(int cpu, uint8_t ct) {
//     const LPM_Tracker& ref = lpm[cpu][LPM_L1D];
//     uint64_t app = ref.e + ref.h + ref.x;
//     uint64_t cor = lpm[cpu][LPM_L2C].m_byp;
//     uint64_t ideal = (app > cor) ? (app - cor) : 1;
//     return static_cast<double>(lpm[cpu][ct].omega()) / static_cast<double>(ideal);
// }

// inline double get_LPMR_global_std(int cpu) {
//     return get_LPMR_std(cpu, LPM_L1D)
//          * get_LPMR_std(cpu, LPM_L2C)
//          * get_LPMR_std(cpu, LPM_LLC);
// }

// inline double get_LPMR_global_byp(int cpu) {
//     return get_LPMR_byp(cpu, LPM_L1D)
//          * get_LPMR_byp(cpu, LPM_L2C)
//          * get_LPMR_byp(cpu, LPM_LLC);
// }

// inline void lpm_print(int cpu) {
//     static const char* nm[] = {"ITLB","DTLB","STLB","L1I ","L1D ","L2C ","LLC "};

//     printf("\n=== LPM Cycle Classification  CPU %d ===\n", cpu);
//     printf("%-4s %12s %12s %12s %12s | %12s %8s %8s %8s | %10s %10s | %8s %8s | %8s %8s %8s\n",
//            "Lvl", "h(hit)", "m(miss)", "x(mixed)", "e(idle)",
//            "omega", "mu", "kappa", "phi", "LPMR_std", "LPMR_byp",
//            "C-AMAT", "APC", "wC-AMAT", "wAPC", "wLPMR");

//     for (int t = 0; t < LPM_NUM_TYPES; t++) {
//         const LPM_Tracker& s = lpm[cpu][t];
//         if (s.n_total() == 0)
//             continue;

//         printf("%-4s %12lu %12lu %12lu %12lu | %12lu %8.4f %8.4f %8.4f",
//                nm[t], s.h, s.m, s.x, s.e, s.omega(),
//                s.mu(), s.kappa(), s.phi());

//         if (t >= LPM_L1D) {
//             printf(" | %10.6f %10.6f | %8.4f %8.4f | %8.4f %8.4f %8.4f",
//                    get_LPMR_std(cpu, t), get_LPMR_byp(cpu, t),
//                    s.c_amat_val, s.apc_val,
//                    s.w_c_amat_val, s.w_apc_val, s.w_lpmr_val);
//         } else {
//             printf(" | %10s %10s | %8s %8s | %8s %8s %8s",
//                    "n/a", "n/a", "n/a", "n/a", "n/a", "n/a", "n/a");
//         }

//         printf("\n");
//     }

//     if (lpm[cpu][LPM_L2C].m_byp > 0)
//         printf("L2C m_byp: %lu\n", lpm[cpu][LPM_L2C].m_byp);

//     printf("Global LPMR_std: %.6f  LPMR_byp: %.6f\n",
//            get_LPMR_global_std(cpu), get_LPMR_global_byp(cpu));
// }

// #endif /* LPM_TRACKER_H */