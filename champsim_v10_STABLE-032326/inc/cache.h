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
    int probe_mshr(PACKET *packet);

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
    void handle_read_hit(uint16_t read_cpu, int index, uint32_t set, int way);
    void handle_read_miss(uint16_t read_cpu, int index, uint32_t set, int way);
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

    uint32_t get_occupancy(uint8_t queue_type, uint64_t address),
             get_size(uint8_t queue_type, uint64_t address);

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
    
    uint32_t get_set(uint64_t address),
             get_way(uint64_t address, uint32_t set),
             find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
             llc_find_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type),
             lru_victim(uint32_t cpu, uint64_t instr_id, uint32_t set, const BLOCK *current_set, uint64_t ip, uint64_t full_addr, uint32_t type);
};

#endif
