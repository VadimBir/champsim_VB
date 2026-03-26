#ifndef DRAM_H
#define DRAM_H

#include "memory_class.h"

// DRAM configuration
#include "defs.h"

extern uint32_t DRAM_MTPS, DRAM_DBUS_RETURN_TIME;


void print_dram_config();

// DRAM
class MEMORY_CONTROLLER : public MEMORY {
  public:
    const string NAME;

    DRAM_ARRAY dram_array[DRAM_CHANNELS][DRAM_RANKS][DRAM_BANKS];
    uint64_t dbus_cycle_available[DRAM_CHANNELS], dbus_cycle_congested[DRAM_CHANNELS], dbus_congested[NUM_TYPES+1][NUM_TYPES+1];
    uint64_t bank_cycle_available[DRAM_CHANNELS][DRAM_RANKS][DRAM_BANKS];
    uint8_t  do_write, write_mode[DRAM_CHANNELS]; 
    uint32_t processed_writes, scheduled_reads[DRAM_CHANNELS], scheduled_writes[DRAM_CHANNELS];
    int fill_level;

    BANK_REQUEST bank_request[DRAM_CHANNELS][DRAM_RANKS][DRAM_BANKS];

    uint64_t sim_read_access[NUM_CPUS];  // post-warmup LOAD completions per CPU (analogous to sim_access[cpu][LOAD] in CACHE)

    // queues
    PACKET_QUEUE WQ[DRAM_CHANNELS];
    PACKET_QUEUE RQ[DRAM_CHANNELS];

    // // constructor
    // MEMORY_CONTROLLER(const std::string &v1) : NAME(v1.c_str())
    // {
    //     for (uint32_t i = 0; i < NUM_TYPES + 1; i++)
    //         for (uint32_t j = 0; j < NUM_TYPES + 1; j++)
    //             dbus_congested[i][j] = 0;

    //     do_write = 0;
    //     processed_writes = 0;

    //     for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
    //         dbus_cycle_available[i] = 0;
    //         dbus_cycle_congested[i] = 0;
    //         write_mode[i] = 0;
    //         scheduled_reads[i] = 0;
    //         scheduled_writes[i] = 0;

    //         for (uint32_t j = 0; j < DRAM_RANKS; j++)
    //             for (uint32_t k = 0; k < DRAM_BANKS; k++)
    //                 bank_cycle_available[i][j][k] = 0;

    //         // WQ[i] = PACKET_QUEUE("DRAM_WQ" + std::to_string(i), DRAM_WQ_SIZE);
    //         // RQ[i] = PACKET_QUEUE("DRAM_RQ" + std::to_string(i), DRAM_RQ_SIZE);
    //         new (&WQ[i]) PACKET_QUEUE(
    //             std::string("DRAM_WQ") + std::to_string(i),
    //             DRAM_WQ_SIZE
    //         );

    //         new (&RQ[i]) PACKET_QUEUE(
    //             std::string("DRAM_RQ") + std::to_string(i),
    //             DRAM_RQ_SIZE
    //         );

    //     }

    //     fill_level = FILL_DRAM;
    // }

    // constructor
    MEMORY_CONTROLLER(const std::string &v1) : NAME(v1.c_str())
    {
        for (uint32_t i = 0; i < NUM_TYPES + 1; i++)
            for (uint32_t j = 0; j < NUM_TYPES + 1; j++)
                dbus_congested[i][j] = 0;

        do_write = 0;
        processed_writes = 0;

        for (uint32_t i = 0; i < DRAM_CHANNELS; i++) {
            dbus_cycle_available[i] = 0;
            dbus_cycle_congested[i] = 0;
            write_mode[i] = 0;
            scheduled_reads[i] = 0;
            scheduled_writes[i] = 0;

            for (uint32_t j = 0; j < DRAM_RANKS; j++)
                for (uint32_t k = 0; k < DRAM_BANKS; k++)
                    bank_cycle_available[i][j][k] = 0;

            {
                char buf[32];
                int n = std::snprintf(buf, sizeof(buf), "DRAM_WQ%u", i);
                new (&WQ[i]) PACKET_QUEUE(std::string(buf, (n>0 ? n : 0)), DRAM_WQ_SIZE);

                n = std::snprintf(buf, sizeof(buf), "DRAM_RQ%u", i);
                new (&RQ[i]) PACKET_QUEUE(std::string(buf, (n>0 ? n : 0)), DRAM_RQ_SIZE);
            }

        }

        fill_level = FILL_DRAM;
        for (uint32_t c = 0; c < NUM_CPUS; c++) sim_read_access[c] = 0;
    };


    // // queues
    // PACKET_QUEUE WQ[DRAM_CHANNELS], RQ[DRAM_CHANNELS];

    
    // // constructor
    // MEMORY_CONTROLLER(string v1) : NAME (v1) {
    //     for (uint32_t i=0; i<NUM_TYPES+1; i++) {
    //         for (uint32_t j=0; j<NUM_TYPES+1; j++) {
    //             dbus_congested[i][j] = 0;
    //         }
    //     }
    //     do_write = 0;
    //     processed_writes = 0;
    //     for (uint32_t i=0; i<DRAM_CHANNELS; i++) {
    //         dbus_cycle_available[i] = 0;
    //         dbus_cycle_congested[i] = 0;
    //         write_mode[i] = 0;
    //         scheduled_reads[i] = 0;
    //         scheduled_writes[i] = 0;

    //         for (uint32_t j=0; j<DRAM_RANKS; j++) {
    //             for (uint32_t k=0; k<DRAM_BANKS; k++)
    //                 bank_cycle_available[i][j][k] = 0;
    //         }

    //         WQ[i].NAME = "DRAM_WQ" + to_string(i);
    //         WQ[i].SIZE = DRAM_WQ_SIZE;
    //         WQ[i].entry = new PACKET [DRAM_WQ_SIZE];

    //         RQ[i].NAME = "DRAM_RQ" + to_string(i);
    //         RQ[i].SIZE = DRAM_RQ_SIZE;
    //         RQ[i].entry = new PACKET [DRAM_RQ_SIZE];
    //     }

    //     fill_level = FILL_DRAM;
    // };
    void quickReset(){
        for (uint32_t i=0; i<NUM_TYPES+1; i++) {
            for (uint32_t j=0; j<NUM_TYPES+1; j++) {
                dbus_congested[i][j] = 0;
            }
        }
        do_write = 0;
        processed_writes = 0;
        for (uint32_t i=0; i<DRAM_CHANNELS; i++) {
            dbus_cycle_available[i] = 0;
            dbus_cycle_congested[i] = 0;
            write_mode[i] = 0;
            scheduled_reads[i] = 0;
            scheduled_writes[i] = 0;

            for (uint32_t j=0; j<DRAM_RANKS; j++) {
                for (uint32_t k=0; k<DRAM_BANKS; k++)
                    bank_cycle_available[i][j][k] = 0;
            }

            WQ[i].quick_reset();
            RQ[i].quick_reset();
        }
        for (uint32_t c = 0; c < NUM_CPUS; c++) sim_read_access[c] = 0;
    }

    // destructor
    ~MEMORY_CONTROLLER() {

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

    void schedule(PACKET_QUEUE *queue), process(PACKET_QUEUE *queue),
         update_schedule_cycle(PACKET_QUEUE *queue),
         update_process_cycle(PACKET_QUEUE *queue),
         reset_remain_requests(PACKET_QUEUE *queue, uint32_t channel);

    constexpr uint32_t dram_get_channel(uint64_t address) const;
    constexpr uint32_t dram_get_rank   (uint64_t address) const;
    constexpr uint32_t dram_get_bank   (uint64_t address) const;
    constexpr uint32_t dram_get_row    (uint64_t address) const;
    constexpr uint32_t dram_get_column (uint64_t address) const;
    uint32_t           drc_check_hit (uint64_t address, uint32_t cpu, uint32_t channel, uint32_t rank, uint32_t bank, uint32_t row);

    uint64_t get_bank_earliest_cycle();

    int check_dram_queue(PACKET_QUEUE *queue, PACKET *packet);
};

#endif