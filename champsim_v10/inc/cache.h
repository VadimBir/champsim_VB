#ifndef CACHE_H
#define CACHE_H

#include "memory_class.h"
#include "defs.h"

// PAGE
extern uint32_t PAGE_TABLE_LATENCY, SWAP_LATENCY;

// CACHE TYPE
#define IS_ITLB 0
#define IS_DTLB 1
#define IS_STLB 2
#define IS_L1I  3
#define IS_L1D  4
#define IS_L2C  5
#define IS_LLC  6

// normalize to bit index: 0,1,2
#define CACHE_LVL_BASE IS_L1D

void print_cache_config();

class CACHE : public MEMORY {
  public:
    int cpu;
    const string NAME;
    const uint32_t NUM_SET, NUM_WAY, NUM_LINE, WQ_SIZE, RQ_SIZE, PQ_SIZE, MSHR_SIZE;
    uint32_t LATENCY;
    BLOCK **block;
    int fill_level;
    uint32_t MAX_READ, MAX_FILL;
    uint32_t reads_available_this_cycle;
    uint8_t cache_type;

    // prefetch stats
    uint64_t pf_requested,
             pf_issued,
             pf_useful,
             pf_useless,
	         pf_late,
             pf_fill;

    // queues
    PACKET_QUEUE WQ{NAME + "_WQ", WQ_SIZE}, // write queue
                 RQ{NAME + "_RQ", RQ_SIZE}, // read queue
                 PQ{NAME + "_PQ", PQ_SIZE}, // prefetch queue
                 MSHR{NAME + "_MSHR", MSHR_SIZE}, // MSHR
                 PROCESSED{NAME + "_PROESSED", ROB_SIZE}; // processed queue
    // PACKET_QUEUE ByPassed_DATA{NAME + "_ByPassed_DATA", ROB_SIZE}; // VB CUSTOM BYPASS QUEUE

    uint64_t sim_access[NUM_CPUS][NUM_TYPES],
             sim_hit[NUM_CPUS][NUM_TYPES],
             sim_miss[NUM_CPUS][NUM_TYPES],
             roi_access[NUM_CPUS][NUM_TYPES],
             roi_hit[NUM_CPUS][NUM_TYPES],
             roi_miss[NUM_CPUS][NUM_TYPES];

    uint64_t total_miss_latency;

    // Bypass-aware load counters (parallel to sim_*, adds bypass as 3rd category)
    // wByP = "with Bypass": access = hit + miss + bypassed
    uint64_t sim_access_wByP[NUM_CPUS];   // sim_access[cpu][LOAD] + ByP_issued
    uint64_t sim_hit_wByP[NUM_CPUS];      // same as sim_hit[cpu][LOAD]
    uint64_t sim_miss_wByP[NUM_CPUS];     // same as sim_miss[cpu][LOAD]
    uint64_t sim_byp_wByP[NUM_CPUS];      // = total_ByP_issued (duplicated for clean accounting)
    uint64_t roi_access_wByP[NUM_CPUS];   // ROI snapshot of sim_access_wByP
    uint64_t roi_hit_wByP[NUM_CPUS];      // ROI snapshot of sim_hit_wByP
    uint64_t roi_miss_wByP[NUM_CPUS];     // ROI snapshot of sim_miss_wByP
    uint64_t roi_byp_wByP[NUM_CPUS];      // ROI snapshot of sim_byp_wByP

// VB: CCUSTOM CODE START
    // Cumulative post-warmup (for final stats, reset at warmup like sim_miss)
    uint64_t total_ByP_issued[NUM_CPUS];  // bypass model said yes
    uint64_t total_ByP_req[NUM_CPUS];     // bypass eligible (new miss + room)
    // Interval (for heartbeat, reset after each heartbeat print)
    uint64_t ByP_issued[NUM_CPUS];
    uint64_t ByP_req[NUM_CPUS];
    bool FORCE_ALL_HITS; // VB CUSTOM
    uint8_t is_bypassing = 0;   // bit0=L1 bit1=L2 bit2=LLC

// Bypass bit encoding for CACHE::is_bypassing config flag: L1=1, L2=2, LLC=4
#define BYP_L1_BIT   1u
#define BYP_L2_BIT   2u
#define BYP_LLC_BIT  4u

// Bypass per-level flags (separate variables, no cross-level interference)
#define BYPASS_L1(pkt)     ((pkt).l1_bypassed = 1)
#define BYPASS_L2(pkt)     ((pkt).l2_bypassed = 1)
#define BYPASS_LLC(pkt)    ((pkt).llc_bypassed = 1)
#define UNBYPASS_L1(pkt)   ((pkt).l1_bypassed = 0)
#define UNBYPASS_L2(pkt)   ((pkt).l2_bypassed = 0)
#define UNBYPASS_LLC(pkt)  ((pkt).llc_bypassed = 0)
#define isBYPASSING_L1(pkt)  ((pkt).l1_bypassed)
#define isBYPASSING_L2(pkt)  ((pkt).l2_bypassed)
#define isBYPASSING_LLC(pkt) ((pkt).llc_bypassed)
    
    // bool BYPASS_L1D_OnNewMiss = false; // VB CUSTOM
    bool BYPASS_L1D_on_MSHR_cap = false; // VB CUSTOM
    int probe_mshr(PACKET *packet) const;

    void set_force_all_hits(bool toEnable); // VB CUSTOM
// VB: CUSTOM CODE END 


    // uint8_t get_bypass_bit();
    uint8_t return_data_bypass_forward_destination(PACKET *packet);
    // uint8_t return_data_bypass_do_forward(PACKET *packet, uint8_t bypass_destination);
    // uint8_t return_data_to_cpu(PACKET *packet);
    int check_mshr_fully(PACKET *packet);
    // bool should_bypass_current_level(PACKET *packet);
    // uint8_t get_bypass_destination(PACKET *packet);
    // void forward_bypassed_data(PACKET *packet);

    // bool should_bypass_to_lower();
    // CACHE* get_cache_at_level(const char* level_name);
    // bool handle_bypass();
// VB: CUSTOM BYPASS FUNCS END
    // void handle_read_pf_on_load(uint16_t read_cpu, int index, uint32_t set, int way);
    

    // constructor
    CACHE(string v1, uint32_t v2, int v3, uint32_t v4, uint32_t v5, uint32_t v6, uint32_t v7, uint32_t v8) 
        : NAME(v1), NUM_SET(v2), NUM_WAY(v3), NUM_LINE(v4), WQ_SIZE(v5), RQ_SIZE(v6), PQ_SIZE(v7), MSHR_SIZE(v8),
        FORCE_ALL_HITS(false) { // VB CUSTOM NOT FORCE ALL HITS

        LATENCY = 0;

        // cache block
        block = new BLOCK* [NUM_SET];
        for (uint32_t i=0; i<NUM_SET; i++) {
            block[i] = new BLOCK[NUM_WAY]; 

            for (uint32_t j=0; j<NUM_WAY; j++) {
                block[i][j].lru = j;
            }
        }

        for (uint32_t i=0; i<NUM_CPUS; i++) {
            upper_level_icache[i] = NULL;
            upper_level_dcache[i] = NULL;

            for (uint32_t j=0; j<NUM_TYPES; j++) {
                sim_access[i][j] = 0;
                sim_hit[i][j] = 0;
                sim_miss[i][j] = 0;
                roi_access[i][j] = 0;
                roi_hit[i][j] = 0;
                roi_miss[i][j] = 0;
            }
        }

        total_miss_latency = 0;
        for (uint32_t i=0; i<NUM_CPUS; i++) {
            total_ByP_issued[i] = 0;
            total_ByP_req[i] = 0;
            ByP_issued[i] = 0;
            ByP_req[i] = 0;
            sim_access_wByP[i] = 0;
            sim_hit_wByP[i] = 0;
            sim_miss_wByP[i] = 0;
            sim_byp_wByP[i] = 0;
            roi_access_wByP[i] = 0;
            roi_hit_wByP[i] = 0;
            roi_miss_wByP[i] = 0;
            roi_byp_wByP[i] = 0;
        }
            lower_level = NULL;
            extra_interface = NULL;
            fill_level = -1;
            MAX_READ = 1;
            MAX_FILL = 1;

            pf_requested = 0;
            pf_issued = 0;
            pf_useful = 0;
            pf_useless = 0;
            pf_late = 0;
            pf_fill = 0;
    };

    // destructor
    ~CACHE() {
        for (uint32_t i=0; i<NUM_SET; i++)
            delete[] block[i];
        delete[] block;
    };

    // functions
    int  add_rq(PACKET *packet),
         add_wq(PACKET *packet),
         add_pq(PACKET *packet);



    void return_data(PACKET *packet),
         operate(),
         increment_WQ_FULL(uint64_t address);

    uint32_t get_occupancy(uint8_t queue_type, uint64_t address) const;
    uint32_t get_size(uint8_t queue_type, uint64_t address) const;

    int  check_hit(PACKET *packet),
         invalidate_entry(uint64_t inval_addr),
         check_mshr(PACKET *packet),
         prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, int prefetch_fill_level, uint32_t prefetch_metadata);
        //  kpc_prefetch_line(uint64_t base_addr, uint64_t pf_addr, int prefetch_fill_level, int delta, int depth, int signature, int confidence, uint32_t prefetch_metadata);

    void handle_fill(),
         handle_writeback(),
         handle_read(),
         handle_prefetch();

    // VB: JOB IS TO HANDLE MERGES FOR ANY QUEUE BUT ONLY FOR PREFETCH PACKETS
    void merge_with_prefetch(PACKET &mshr_packet, PACKET &queue_packet);

    void add_mshr(PACKET *packet),
         update_fill_cycle(),
         llc_initialize_replacement(),
         update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit),
         llc_update_replacement_state(uint32_t cpu, uint32_t set, uint32_t way, uint64_t full_addr, uint64_t ip, uint64_t victim_addr, uint32_t type, uint8_t hit),
         lru_update(uint32_t set, uint32_t way),
         fill_cache(uint32_t set, uint32_t way, PACKET *packet),
         replacement_final_stats(),
         llc_replacement_final_stats(),
         //prefetcher_initialize(),
         l1d_prefetcher_initialize(),
         l2c_prefetcher_initialize(),
         llc_prefetcher_initialize(),
         prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type),
         l1d_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type),
         prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr),
         l1d_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in),
         //prefetcher_final_stats(),
         l1d_prefetcher_final_stats(),
         l2c_prefetcher_final_stats(),
         llc_prefetcher_final_stats();

    uint32_t l2c_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in),
         llc_prefetcher_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in),
         l2c_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in),
         llc_prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in);

    void prefetcher_feedback(uint64_t &pref_gen, uint64_t &pref_fill, uint64_t &pref_used, uint64_t &pref_late);
    
    inline void return_to_upper_level(PACKET& packet) {
        if (packet.instruction)
            upper_level_icache[packet.cpu]->return_data(&packet);
        else
            upper_level_dcache[packet.cpu]->return_data(&packet);
    }

    // ---------------------------------------------------------------
    // handle_read hit-path inline helpers
    // ---------------------------------------------------------------

    // Routes hit packet to PROCESSED queue for TLB/L1I/L1D cache types.
    // Sets instruction_pa / data_pa / data fields for TLBs before queuing.
    inline void handle_read_hit_processed(int index, uint32_t set, uint32_t way) {
        if (cache_type == IS_ITLB) {
            RQ.entry[index].instruction_pa = block[set][way].data;
            if (PROCESSED.occupancy < PROCESSED.SIZE) {
                char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                for (size_t i = 0; i < sizeof(PACKET); i += 64)
                    __builtin_prefetch(dst + i, 1, 3);
                PROCESSED.add_queue(&RQ.entry[index]);
            } else { cerr << "ITLB PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
        else if (cache_type == IS_DTLB) {
            RQ.entry[index].data_pa = block[set][way].data;
            if (PROCESSED.occupancy < PROCESSED.SIZE) {
                char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                for (size_t i = 0; i < sizeof(PACKET); i += 64)
                    __builtin_prefetch(dst + i, 1, 3);
                PROCESSED.add_queue(&RQ.entry[index]);
            } else { cerr << "DTLB PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
        else if (cache_type == IS_STLB) {
            RQ.entry[index].data = block[set][way].data;
        }
        else if (cache_type == IS_L1I) {
            if (PROCESSED.occupancy < PROCESSED.SIZE) {
                char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                for (size_t i = 0; i < sizeof(PACKET); i += 64)
                    __builtin_prefetch(dst + i, 1, 3);
                PROCESSED.add_queue(&RQ.entry[index]);
            } else { cerr << "L1I PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
        else if ((cache_type == IS_L1D) && (RQ.entry[index].type != PREFETCH)) {
            if (PROCESSED.occupancy < PROCESSED.SIZE) {
                char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                for (size_t i = 0; i < sizeof(PACKET); i += 64)
                    __builtin_prefetch(dst + i, 1, 3);
                PROCESSED.add_queue(&RQ.entry[index]);
            } else { cerr << "L1D PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
    }

    // Handles bypass hit returns:
    //   L1 bypass: packet hit at L2C with l1_bypassed==1 → route via L2C PROCESSED (→CPU).
    //   L2 bypass: packet hit at LLC with l2_bypassed==1 → double-hop return directly to L1D.
    // Returns true if a bypass return was performed (caller skips normal return path).
    inline bool handle_read_hit_bypass_return(uint16_t read_cpu, int index) {
#ifdef BYPASS_L1_LOGIC
        if ((cache_type == IS_L2C) && (RQ.entry[index].type == LOAD
                && RQ.entry[index].l1_bypassed == 1 && !RQ.entry[index].instruction)) {
            if (PROCESSED.occupancy < PROCESSED.SIZE) {
                char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                for (size_t i = 0; i < sizeof(PACKET); i += 64)
                    __builtin_prefetch(dst + i, 1, 3);
                PROCESSED.add_queue(&RQ.entry[index]);
            } else { cerr << "L2C PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
            return true;
        }
#endif
#ifdef BYPASS_L2_LOGIC
        if ((cache_type == IS_LLC) && (RQ.entry[index].type == LOAD
                && RQ.entry[index].l2_bypassed == 1 && !RQ.entry[index].instruction)) {
            upper_level_dcache[read_cpu]->upper_level_dcache[read_cpu]->return_data(&RQ.entry[index]);
            return true;
        }
#endif
        return false;
    }

    // Notifies prefetcher of a read hit on LOAD type.
    // Dispatches to l1d / l2c / llc prefetcher_operate per cache level.
    inline void handle_read_hit_pf_operate(uint16_t read_cpu, int index, uint32_t set, uint32_t way) {
        if (RQ.entry[index].type == LOAD) {
            if (cache_type == IS_L1D)
                l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type);
            else if (cache_type == IS_L2C)
                l2c_prefetcher_operate(block[set][way].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
            else if (cache_type == IS_LLC) {
                cpu = read_cpu;
                llc_prefetcher_operate(block[set][way].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
                cpu = 0;
            }
        }
    }

    // Updates replacement state (LRU) for the hit way.
    inline void handle_read_hit_replacement(uint16_t read_cpu, int index, uint32_t set, uint32_t way) {
        if (cache_type == IS_LLC)
            llc_update_replacement_state(read_cpu, set, way, block[set][way].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 1);
        else
            update_replacement_state(read_cpu, set, way, block[set][way].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 1);
    }

    // Increments sim_hit / sim_access counters, and wByP load counters.
    inline void handle_read_hit_stats(uint16_t read_cpu, int index) {
        sim_hit[read_cpu][RQ.entry[index].type]++;
        sim_access[read_cpu][RQ.entry[index].type]++;
        if (RQ.entry[index].type == LOAD) {
            sim_hit_wByP[read_cpu]++;
            sim_access_wByP[read_cpu]++;
        }
    }

    // If fill_level < this cache's fill_level, returns data to the upper level.
    inline void handle_read_hit_return(int index) {
        if (RQ.entry[index].fill_level < fill_level)
            return_to_upper_level(RQ.entry[index]);
    }

    // Increments pf_useful on prefetch hit, clears prefetch bit, sets used=1.
    // Increments HIT/ACCESS counters and removes entry from RQ.
    inline void handle_read_hit_pf_useful_and_remove(int index, uint32_t set, uint32_t way) {
        if (block[set][way].prefetch) {
            pf_useful++;
            block[set][way].prefetch = 0;
        }
        block[set][way].used = 1;
        HIT[RQ.entry[index].type]++;
        ACCESS[RQ.entry[index].type]++;
        RQ.remove_queue(&RQ.entry[index]);
        reads_available_this_cycle--;
    }

    // ---------------------------------------------------------------
    // handle_read miss-path inline helpers
    // ---------------------------------------------------------------

    // Checks bypass conditions for L1D/L2C/LLC on a new miss.
    // Increments bypass opportunity counters, sets bypass flag on packet,
    // calls lower_level->add_rq(), and returns true if bypass was taken.
    // NOTE: defined out-of-line in cache.cc (bypass model funcs only visible there).
    bool handle_read_miss_bypass(uint16_t read_cpu, int index, int mshr_index);

    // Handles new LLC miss: checks DRAM RQ capacity, calls add_mshr + lower_level->add_rq.
    // Sets miss_handled=0 if DRAM RQ is full.
    inline void handle_read_miss_new_llc(int index, uint8_t& miss_handled) {
        if (lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address)) {
            miss_handled = 0;
        } else {
            add_mshr(&RQ.entry[index]);
            if (lower_level)
                lower_level->add_rq(&RQ.entry[index]);
        }
    }

    // Handles new non-LLC miss: checks lower-level RQ capacity, calls add_mshr + lower_level->add_rq.
    // Special case: IS_STLB with no lower_level emulates page table walk via va_to_pa.
    // Sets miss_handled=0 if lower RQ is full.
    inline void handle_read_miss_new_other(uint16_t read_cpu, int index, uint8_t& miss_handled) {
        if (lower_level && lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address)) {
            miss_handled = 0;
        } else {
            add_mshr(&RQ.entry[index]);
            if (lower_level)
                lower_level->add_rq(&RQ.entry[index]);
            else {
                if (cache_type == IS_STLB) {
                    uint64_t pa = va_to_pa(read_cpu, RQ.entry[index].instr_id, RQ.entry[index].full_addr, RQ.entry[index].address);
                    RQ.entry[index].data = pa >> LOG2_PAGE_SIZE;
                    RQ.entry[index].event_cycle = current_core_cycle[read_cpu];
                    return_data(&RQ.entry[index]);
                }
            }
        }
    }

    // Handles MSHR-full stall: sets miss_handled=0, increments STALL counter.
    inline void handle_read_miss_mshr_full(int index, uint8_t& miss_handled) {
        miss_handled = 0;
        STALL[RQ.entry[index].type]++;
    }

    // Merges dependency indices from in-flight RQ entry into existing MSHR entry.
    // Handles RFO (sq/lq), instruction (rob), and data load (lq+sq) cases.
    inline void handle_read_miss_inflight_merge_deps(int index, int mshr_index) {
        if (RQ.entry[index].type == RFO) {
            if (RQ.entry[index].l1_bypassed)
                assert(0&&"RFO BYPASS NOT EXPECTED ... ");
            if (RQ.entry[index].tlb_access) {
                uint16_t sq_index = RQ.entry[index].sq_index;
                MSHR.entry[mshr_index].store_merged = 1;
                MSHR.entry[mshr_index].sq_index_depend_on_me.insert(sq_index);
                MSHR.entry[mshr_index].sq_index_depend_on_me.join(RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
            }
            if (RQ.entry[index].load_merged) {
                MSHR.entry[mshr_index].load_merged = 1;
                MSHR.entry[mshr_index].lq_index_depend_on_me.join(RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
            }
        } else {
            if (RQ.entry[index].instruction) {
                uint16_t rob_index = RQ.entry[index].rob_index;
                MSHR.entry[mshr_index].instr_merged = 1;
                MSHR.entry[mshr_index].rob_index_depend_on_me.insert(rob_index);
                if (RQ.entry[index].instr_merged)
                    MSHR.entry[mshr_index].rob_index_depend_on_me.join(RQ.entry[index].rob_index_depend_on_me, ROB_SIZE);
            } else {
                uint16_t lq_index = RQ.entry[index].lq_index;
                MSHR.entry[mshr_index].load_merged = 1;
                MSHR.entry[mshr_index].lq_index_depend_on_me.insert(lq_index);
                MSHR.entry[mshr_index].lq_index_depend_on_me.join(RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
                if (RQ.entry[index].store_merged) {
                    MSHR.entry[mshr_index].store_merged = 1;
                    MSHR.entry[mshr_index].sq_index_depend_on_me.join(RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
                }
            }
        }
    }

    // Updates MSHR fill_level if incoming RQ entry has a shallower fill target.
    inline void handle_read_miss_inflight_fill_level(int index, int mshr_index) {
        if (RQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
            MSHR.entry[mshr_index].fill_level = RQ.entry[index].fill_level;
    }

    // Resolves L1 bypass mismatch at L2C: if l1_bypassed differs between RQ and MSHR,
    // injects LQ deps into L1D MSHR and clears l1_bypassed to use normal fill path.
    inline void handle_read_miss_inflight_bypass_l1_mismatch(int index, int mshr_index) {
#ifdef BYPASS_L1_LOGIC
        if (cache_type == IS_L2C) {
            if (RQ.entry[index].l1_bypassed != MSHR.entry[mshr_index].l1_bypassed) {
                if (MSHR.entry[mshr_index].type != PREFETCH) {
                    auto *l1d = (CACHE *) this->upper_level_dcache[cpu];
                    bool found_l1d_mshr = false;
                    for (uint16_t m = 0; m < l1d->MSHR_SIZE; m++) {
                        if (l1d->MSHR.entry[m].address == MSHR.entry[mshr_index].address) {
                            found_l1d_mshr = true;
                            l1d->MSHR.entry[m].load_merged = 1;
                            l1d->MSHR.entry[m].lq_index_depend_on_me.insert(RQ.entry[index].lq_index);
                            if (RQ.entry[index].load_merged) {
                                ITERATE_SET(dep, RQ.entry[index].lq_index_depend_on_me, LQ_SIZE) {
                                    l1d->MSHR.entry[m].lq_index_depend_on_me.insert(dep);
                                }
                            }
                            l1d->MSHR.entry[m].lq_index_depend_on_me.remove(l1d->MSHR.entry[m].lq_index);
                            break;
                        }
                    }
                    if (found_l1d_mshr) {
                        RQ.entry[index].l1_bypassed = 0;
                        MSHR.entry[mshr_index].l1_bypassed = 0;
                        MSHR.entry[mshr_index].fill_level = 1;
                    }
                } else if (MSHR.entry[mshr_index].fill_level < fill_level) {
                    RQ.entry[index].l1_bypassed = 0;
                }
            }
        }
#endif
    }

    // Resolves L2 bypass mismatch at LLC: if l2_bypassed differs between RQ and MSHR,
    // injects LQ deps into L2C MSHR and clears l2_bypassed to use normal fill path.
    inline void handle_read_miss_inflight_bypass_l2_mismatch(int index, int mshr_index) {
#ifdef BYPASS_L2_LOGIC
        if (cache_type == IS_LLC) {
            if (RQ.entry[index].l2_bypassed != MSHR.entry[mshr_index].l2_bypassed) {
                if (MSHR.entry[mshr_index].type != PREFETCH) {
                    auto *l2c = (CACHE *) this->upper_level_dcache[cpu];
                    bool found_l2c_mshr = false;
                    for (uint16_t m = 0; m < l2c->MSHR_SIZE; m++) {
                        if (l2c->MSHR.entry[m].address == MSHR.entry[mshr_index].address) {
                            found_l2c_mshr = true;
                            l2c->MSHR.entry[m].load_merged = 1;
                            l2c->MSHR.entry[m].lq_index_depend_on_me.insert(RQ.entry[index].lq_index);
                            if (RQ.entry[index].load_merged) {
                                ITERATE_SET(dep, RQ.entry[index].lq_index_depend_on_me, LQ_SIZE) {
                                    l2c->MSHR.entry[m].lq_index_depend_on_me.insert(dep);
                                }
                            }
                            l2c->MSHR.entry[m].lq_index_depend_on_me.remove(l2c->MSHR.entry[m].lq_index);
                            break;
                        }
                    }
                    if (found_l2c_mshr) {
                        RQ.entry[index].l2_bypassed = 0;
                        MSHR.entry[mshr_index].l2_bypassed = 0;
                        MSHR.entry[mshr_index].fill_level = FILL_L2;
                    }
                } else if (MSHR.entry[mshr_index].fill_level < fill_level) {
                    RQ.entry[index].l2_bypassed = 0;
                }
            }
        }
#endif
    }

    // Handles late prefetch takeover: increments pf_late, calls merge_with_prefetch,
    // then restores bypass flags from RQ entry onto MSHR (merge_with_prefetch overwrites them).
    inline void handle_read_miss_inflight_prefetch_takeover(int index, int mshr_index) {
        pf_late++;
        merge_with_prefetch(MSHR.entry[mshr_index], RQ.entry[index]);
#ifdef BYPASS_L1_LOGIC
        if (RQ.entry[index].l1_bypassed)
            MSHR.entry[mshr_index].l1_bypassed = 1;
#endif
#ifdef BYPASS_L2_LOGIC
        if (RQ.entry[index].l2_bypassed)
            MSHR.entry[mshr_index].l2_bypassed = 1;
#endif
#ifdef BYPASS_LLC_LOGIC
        if (RQ.entry[index].llc_bypassed)
            MSHR.entry[mshr_index].llc_bypassed = 1;
#endif
    }

    // For non-prefetch in-flight MSHR: inserts rob/lq/sq index into MSHR dep set per packet type.
    inline void handle_read_miss_inflight_non_prefetch_merge(int index, int mshr_index) {
        if (RQ.entry[index].instruction) {
            MSHR.entry[mshr_index].rob_index_depend_on_me.insert(RQ.entry[index].rob_index);
            MSHR.entry[mshr_index].instr_merged = 1;
        } else if (RQ.entry[index].type == LOAD) {
            MSHR.entry[mshr_index].lq_index_depend_on_me.insert(RQ.entry[index].lq_index);
            MSHR.entry[mshr_index].load_merged = 1;
        } else if (RQ.entry[index].type == RFO) {
            MSHR.entry[mshr_index].sq_index_depend_on_me.insert(RQ.entry[index].sq_index);
            MSHR.entry[mshr_index].store_merged = 1;
        }
    }

    // Notifies prefetcher of a read miss on LOAD type.
    inline void handle_read_miss_handled_pf_operate(uint16_t read_cpu, int index) {
        if (RQ.entry[index].type == LOAD) {
            if (cache_type == IS_L1D)
                l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type);
            if (cache_type == IS_L2C)
                l2c_prefetcher_operate(RQ.entry[index].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
            if (cache_type == IS_LLC) {
                cpu = read_cpu;
                llc_prefetcher_operate(RQ.entry[index].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
                cpu = 0;
            }
        }
    }

    // Increments MISS/ACCESS counters and removes entry from RQ.
    inline void handle_read_miss_handled_stats_remove(int index) {
        MISS[RQ.entry[index].type]++;
        ACCESS[RQ.entry[index].type]++;
        RQ.remove_queue(&RQ.entry[index]);
        reads_available_this_cycle--;
    }

    // ---------------------------------------------------------------
    // handle_writeback hit-path inline helpers
    // ---------------------------------------------------------------

    // Updates replacement state, collects sim_hit/sim_access, marks block dirty,
    // copies TLB data fields, checks fill_level and returns to upper level, removes from WQ.
    inline void handle_writeback_hit(uint16_t writeback_cpu, int index, uint32_t set, uint32_t way) {
        if (cache_type == IS_LLC)
            llc_update_replacement_state(writeback_cpu, set, way, block[set][way].full_addr, WQ.entry[index].ip, 0, WQ.entry[index].type, 1);
        else
            update_replacement_state(writeback_cpu, set, way, block[set][way].full_addr, WQ.entry[index].ip, 0, WQ.entry[index].type, 1);
        sim_hit[writeback_cpu][WQ.entry[index].type]++;
        sim_access[writeback_cpu][WQ.entry[index].type]++;
        block[set][way].dirty = 1;
        if (cache_type == IS_ITLB)
            WQ.entry[index].instruction_pa = block[set][way].data;
        else if (cache_type == IS_DTLB)
            WQ.entry[index].data_pa = block[set][way].data;
        else if (cache_type == IS_STLB)
            WQ.entry[index].data = block[set][way].data;
        if (WQ.entry[index].fill_level < fill_level)
            return_to_upper_level(WQ.entry[index]);
        HIT[WQ.entry[index].type]++;
        ACCESS[WQ.entry[index].type]++;
        WQ.remove_queue(&WQ.entry[index]);
    }

    // ---------------------------------------------------------------
    // handle_writeback miss-path inline helpers (L1D RFO path)
    // ---------------------------------------------------------------

    // New RFO miss: checks lower RQ capacity, calls add_mshr + lower->add_rq.
    // Sets miss_handled=0 if lower RQ is full.
    inline void handle_writeback_miss_new(int index, uint8_t& miss_handled) {
        if (cache_type == IS_LLC) {
            if (lower_level->get_occupancy(1, WQ.entry[index].address) == lower_level->get_size(1, WQ.entry[index].address))
                miss_handled = 0;
            else {
                add_mshr(&WQ.entry[index]);
                lower_level->add_rq(&WQ.entry[index]);
            }
        } else {
            if (lower_level && lower_level->get_occupancy(1, WQ.entry[index].address) == lower_level->get_size(1, WQ.entry[index].address))
                miss_handled = 0;
            else {
                add_mshr(&WQ.entry[index]);
                lower_level->add_rq(&WQ.entry[index]);
            }
        }
    }

    // MSHR full stall for writeback path.
    inline void handle_writeback_miss_mshr_full(int index, uint8_t& miss_handled) {
        miss_handled = 0;
        STALL[WQ.entry[index].type]++;
    }

    // In-flight MSHR merge for writeback: updates fill_level, merges prefetch if needed.
    inline void handle_writeback_miss_inflight(int index, int mshr_index) {
        if (WQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
            MSHR.entry[mshr_index].fill_level = WQ.entry[index].fill_level;
        if (MSHR.entry[mshr_index].type == PREFETCH)
            merge_with_prefetch(MSHR.entry[mshr_index], WQ.entry[index]);
        MSHR_MERGED[WQ.entry[index].type]++;
    }

    // Increments MISS/ACCESS and removes entry from WQ.
    inline void handle_writeback_miss_handled_stats_remove(int index) {
        MISS[WQ.entry[index].type]++;
        ACCESS[WQ.entry[index].type]++;
        WQ.remove_queue(&WQ.entry[index]);
    }

    // ---------------------------------------------------------------
    // handle_writeback non-L1D miss path (writeback eviction into lower level)
    // ---------------------------------------------------------------

    // Finds victim way for a writeback miss (non-L1D). Returns the way index.
    inline uint32_t handle_writeback_find_victim(uint32_t writeback_cpu, int index, uint32_t set) {
        if (cache_type == IS_LLC)
            return llc_find_victim(writeback_cpu, WQ.entry[index].instr_id, set, block[set], WQ.entry[index].ip, WQ.entry[index].full_addr, WQ.entry[index].type);
        return find_victim(writeback_cpu, WQ.entry[index].instr_id, set, block[set], WQ.entry[index].ip, WQ.entry[index].full_addr, WQ.entry[index].type);
    }

    // Checks dirty eviction: if block dirty and lower WQ has room, constructs and sends writeback packet.
    // Sets do_fill=0 and increments STALL if lower WQ is full.
    inline void handle_writeback_evict_dirty(uint32_t writeback_cpu, int index, uint32_t set, uint32_t way, uint8_t& do_fill) {
        if (block[set][way].dirty && lower_level) {
            if (lower_level->get_occupancy(2, block[set][way].address) == lower_level->get_size(2, block[set][way].address)) {
                do_fill = 0;
                lower_level->increment_WQ_FULL(block[set][way].address);
                STALL[WQ.entry[index].type]++;
            } else {
                PACKET writeback_packet;
                writeback_packet.fill_level = fill_level << 1;
                writeback_packet.cpu = writeback_cpu;
                writeback_packet.address = block[set][way].address;
                writeback_packet.full_addr = block[set][way].full_addr;
                writeback_packet.data = block[set][way].data;
                writeback_packet.instr_id = WQ.entry[index].instr_id;
                writeback_packet.ip = 0;
                writeback_packet.type = WRITEBACK;
                writeback_packet.event_cycle = current_core_cycle[writeback_cpu];
                lower_level->add_wq(&writeback_packet);
            }
        }
    }

    // Calls per-type prefetcher fill hook, updates replacement, collects stats,
    // calls fill_cache, marks dirty, checks fill_level, increments MISS/ACCESS, removes from WQ.
    inline void handle_writeback_do_fill(uint32_t writeback_cpu, int index, uint32_t set, uint32_t way) {
        if (cache_type == IS_L1D)
            l1d_prefetcher_cache_fill(WQ.entry[index].full_addr, set, way, 0, block[set][way].address<<LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
        else if (cache_type == IS_L2C)
            WQ.entry[index].pf_metadata = l2c_prefetcher_cache_fill(WQ.entry[index].address<<LOG2_BLOCK_SIZE, set, way, 0, block[set][way].address<<LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
        if (cache_type == IS_LLC) {
            cpu = writeback_cpu;
            WQ.entry[index].pf_metadata = llc_prefetcher_cache_fill(WQ.entry[index].address<<LOG2_BLOCK_SIZE, set, way, 0, block[set][way].address<<LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
            cpu = 0;
        }
        if (cache_type == IS_LLC)
            llc_update_replacement_state(writeback_cpu, set, way, WQ.entry[index].full_addr, WQ.entry[index].ip, block[set][way].full_addr, WQ.entry[index].type, 0);
        else
            update_replacement_state(writeback_cpu, set, way, WQ.entry[index].full_addr, WQ.entry[index].ip, block[set][way].full_addr, WQ.entry[index].type, 0);
        sim_miss[writeback_cpu][WQ.entry[index].type]++;
        sim_access[writeback_cpu][WQ.entry[index].type]++;
        fill_cache(set, way, &WQ.entry[index]);
        block[set][way].dirty = 1;
        if (WQ.entry[index].fill_level < fill_level)
            return_to_upper_level(WQ.entry[index]);
        MISS[WQ.entry[index].type]++;
        ACCESS[WQ.entry[index].type]++;
        WQ.remove_queue(&WQ.entry[index]);
    }

    // ---------------------------------------------------------------
    // handle_fill inline helpers
    // ---------------------------------------------------------------

    // Finds victim way for a fill. Returns the way index.
    inline uint32_t handle_fill_find_victim(uint16_t fill_cpu, uint16_t mshr_index, uint32_t set) {
        if (cache_type == IS_LLC)
            return llc_find_victim(fill_cpu, MSHR.entry[mshr_index].instr_id, set, block[set], MSHR.entry[mshr_index].ip, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].type);
        return find_victim(fill_cpu, MSHR.entry[mshr_index].instr_id, set, block[set], MSHR.entry[mshr_index].ip, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].type);
    }

    // Checks dirty eviction on fill: constructs writeback packet to lower WQ if dirty block evicted.
    // Sets do_fill=0 and increments STALL if lower WQ is full.
    inline void handle_fill_evict_dirty(uint16_t fill_cpu, uint16_t mshr_index, uint32_t set, uint32_t way, uint8_t& do_fill) {
        if (block[set][way].dirty && lower_level) {
            if (lower_level->get_occupancy(2, block[set][way].address) == lower_level->get_size(2, block[set][way].address)) {
                do_fill = 0;
                lower_level->increment_WQ_FULL(block[set][way].address);
                STALL[MSHR.entry[mshr_index].type]++;
            } else {
                PACKET writeback_packet;
                writeback_packet.fill_level = fill_level << 1;
                writeback_packet.cpu = fill_cpu;
                writeback_packet.address = block[set][way].address;
                writeback_packet.full_addr = block[set][way].full_addr;
                writeback_packet.data = block[set][way].data;
                writeback_packet.instr_id = MSHR.entry[mshr_index].instr_id;
                writeback_packet.ip = 0;
                writeback_packet.type = WRITEBACK;
                writeback_packet.event_cycle = current_core_cycle[fill_cpu];
                lower_level->add_wq(&writeback_packet);
            }
        }
    }

    // Calls per-type prefetcher cache fill hook for L1D/L2C/LLC.
    inline void handle_fill_pf_fill(uint16_t fill_cpu, uint16_t mshr_index, uint32_t set, uint32_t way) {
        if (cache_type == IS_L1D)
            l1d_prefetcher_cache_fill(MSHR.entry[mshr_index].full_addr, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, block[set][way].address<<LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
        if (cache_type == IS_L2C)
            MSHR.entry[mshr_index].pf_metadata = l2c_prefetcher_cache_fill(MSHR.entry[mshr_index].address<<LOG2_BLOCK_SIZE, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, block[set][way].address<<LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
        if (cache_type == IS_LLC) {
            cpu = fill_cpu;
            MSHR.entry[mshr_index].pf_metadata = llc_prefetcher_cache_fill(MSHR.entry[mshr_index].address<<LOG2_BLOCK_SIZE, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, block[set][way].address<<LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
            cpu = 0;
        }
    }

    // Updates replacement state for the filled way.
    inline void handle_fill_replacement(uint16_t fill_cpu, uint16_t mshr_index, uint32_t set, uint32_t way) {
        if (cache_type == IS_LLC)
            llc_update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, block[set][way].full_addr, MSHR.entry[mshr_index].type, 0);
        else
            update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, block[set][way].full_addr, MSHR.entry[mshr_index].type, 0);
    }

    // Increments sim_miss/sim_access and wByP counters.
    // wByP skipped for llc_bypassed: already counted at bypass-decision time in handle_read_miss_bypass.
    inline void handle_fill_stats(uint16_t fill_cpu, uint16_t mshr_index) {
        sim_miss[fill_cpu][MSHR.entry[mshr_index].type]++;
        sim_access[fill_cpu][MSHR.entry[mshr_index].type]++;
        if (MSHR.entry[mshr_index].type == LOAD
#ifdef BYPASS_LLC_LOGIC
            && !MSHR.entry[mshr_index].llc_bypassed
#endif
        ) {
            sim_miss_wByP[fill_cpu]++;
            sim_access_wByP[fill_cpu]++;
        }
    }

    // Calls fill_cache, marks dirty for L1D RFO.
    inline void handle_fill_cache_and_dirty(uint16_t mshr_index, uint32_t set, uint32_t way) {
        fill_cache(set, way, &MSHR.entry[mshr_index]);
        if (cache_type == IS_L1D && MSHR.entry[mshr_index].type == RFO)
            block[set][way].dirty = 1;
    }

    // If fill_level < this cache's fill_level, returns data to upper level.
    inline void handle_fill_return(uint16_t mshr_index) {
        if (MSHR.entry[mshr_index].fill_level < fill_level)
            return_to_upper_level(MSHR.entry[mshr_index]);
    }

    // Routes filled packet to PROCESSED queue for TLB/L1I/L1D types,
    // and handles bypass return paths for L2C (l1_bypassed→PROCESSED) and LLC (l2_bypassed→L1D).
    inline void handle_fill_processed_and_bypass_return(uint16_t fill_cpu, uint16_t mshr_index, uint32_t set, uint32_t way) {
        if (cache_type == IS_ITLB) {
            MSHR.entry[mshr_index].instruction_pa = block[set][way].data;
            if (PROCESSED.occupancy < PROCESSED.SIZE)
                PROCESSED.add_queue(&MSHR.entry[mshr_index]);
            else { cerr << "ITLB PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
        else if (cache_type == IS_DTLB) {
            MSHR.entry[mshr_index].data_pa = block[set][way].data;
            if (PROCESSED.occupancy < PROCESSED.SIZE)
                PROCESSED.add_queue(&MSHR.entry[mshr_index]);
            else { cerr << "DTLB PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
        else if (cache_type == IS_L1I) {
            if (PROCESSED.occupancy < PROCESSED.SIZE)
                PROCESSED.add_queue(&MSHR.entry[mshr_index]);
            else { cerr << "L1I PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
        else if ((cache_type == IS_L1D) && (MSHR.entry[mshr_index].type != PREFETCH)) {
            if (PROCESSED.occupancy < PROCESSED.SIZE)
                PROCESSED.add_queue(&MSHR.entry[mshr_index]);
            else { cerr << "L1D PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
        }
#ifdef BYPASS_L1_LOGIC
        else if ((cache_type == IS_L2C) && (MSHR.entry[mshr_index].type == LOAD) && MSHR.entry[mshr_index].l1_bypassed == 1) {
            if (PROCESSED.occupancy < PROCESSED.SIZE)
                PROCESSED.add_queue(&MSHR.entry[mshr_index]);
            else { cerr << "L2C PROCESSED FULL" << endl; assert(0&&"RETURN IS LOST FOREVER!!!! "); }
            // PF from L1D merged into this bypassed MSHR — complete L1D's prefetch MSHR.
            // Guard: only if L1D still has an MSHR for this address (may have been resolved by other path).
            // Use PREFETCH type to avoid promotion to LOAD at L1D (prevents double CPU signal).
            if (MSHR.entry[mshr_index].pf_merged_from_upper
                    && ((CACHE *)upper_level_dcache[fill_cpu])->probe_mshr(&MSHR.entry[mshr_index]) != -1) {
                PACKET pf_ret = MSHR.entry[mshr_index];
                pf_ret.type = PREFETCH;
                return_to_upper_level(pf_ret);
            }
        }
#endif
#ifdef BYPASS_L2_LOGIC
        else if ((cache_type == IS_LLC) && (MSHR.entry[mshr_index].type == LOAD) && MSHR.entry[mshr_index].l2_bypassed == 1) {
            upper_level_dcache[fill_cpu]->upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
            // PF from L2C merged into this bypassed MSHR — complete L2C's prefetch MSHR.
            // Guard: only if L2C still has an MSHR for this address.
            // L2C has no type-promotion, so the MSHR stays PREFETCH → fills cache, no L1D forward.
            if (MSHR.entry[mshr_index].pf_merged_from_upper
                    && ((CACHE *)upper_level_dcache[fill_cpu])->probe_mshr(&MSHR.entry[mshr_index]) != -1) {
                return_to_upper_level(MSHR.entry[mshr_index]);
            }
        }
#endif
    }

    // Updates miss latency stats, removes from MSHR, decrements num_returned, calls update_fill_cycle.
    inline void handle_fill_remove(uint16_t fill_cpu, uint16_t mshr_index) {
        if (warmup_complete[fill_cpu]) {
            uint64_t current_miss_latency = current_core_cycle[fill_cpu] - MSHR.entry[mshr_index].cycle_enqueued;
            total_miss_latency += current_miss_latency;
        }
        MSHR.remove_queue(&MSHR.entry[mshr_index]);
        MSHR.num_returned--;
        update_fill_cycle();
    }

    constexpr uint32_t get_set(uint64_t address);
    uint32_t get_way(uint64_t address, uint32_t set) const,
             find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
             llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
             lru_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type);
};

#endif
