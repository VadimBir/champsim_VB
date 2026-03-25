#include "cache.h"
#include "lpm_tracker.h"

#ifndef L1D_type
#define L1D_type  4
#define L2C_type  5
#define LLC_type  6
#endif



// USEFUL VARS:  usage example
//  * Cached metrics available without sim_access:
//  *   lpm[cpu][IS_L1D].c_amat_val   ← ω(L1D)/α(L1D)
//  *   lpm[cpu][IS_L1D].apc_val      ← α(L1D)/ω(L1D)
//  *   lpm[cpu][IS_L1D].lpmr_val     ← ω(L1D)/(IC×CPIexe)
//  *   lpm[cpu][IS_L2C].mu()         ← miss-cycle fraction
//  *   lpm[cpu][IS_L2C].kappa()      ← pure-miss fraction
//  *
//  * Also raw counters:
//  *   lpm[cpu][IS_L1D].h, .m, .x, .e
//  *   lpm[cpu][IS_L1D].omega()

// L2C->MSHR.occupancy      L2C->MSHR.SIZE
// L2C->RQ.occupancy        L2C->RQ.SIZE
// L2C->PQ.occupancy        L2C->PQ.SIZE
// L2C->MSHR.occupancy      L2C->MSHR.SIZE
// L2C->RQ.occupancy        L2C->RQ.SIZE
// L2C->PQ.occupancy        L2C->PQ.SIZE

// CACHE TYPEs




static bool l2_bypass_init = false;

#define SHALL_L2C_BYPASS_DEFINED
inline bool l2c_bypass_operate(int cpu, CACHE *L1D, CACHE *L2C, CACHE *LLC) {
    if (l2_bypass_init == false){
        cout << "Bypass on WINDOW: \navailable = (double)free * camat\nrq_unmatch = 1.0 - (lpmr_l2c / lpmr_l1d) \npq_unmatch = 1.0 - (lpmr_llc / lpmr_l2c) \nimmediate = (double)L2C->RQ.occupancy * rq_unmatch * camat + (double)L2C->PQ.occupancy * pq_unmatch * camat \nfuture = apc_l1d * (lpmr_l2c / lpmr_l1d) * camat + camat;" << endl;
        
        l2_bypass_init = true;
    }

    if (LLC->MSHR.occupancy < LLC->MSHR.SIZE/2 && L2C->MSHR.occupancy < L2C->MSHR.SIZE/2)
        return true;
    return false;
}