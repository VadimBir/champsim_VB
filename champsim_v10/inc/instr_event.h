#include "defs.h"
#include "champsim.h"
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cassert>
#pragma once


// We are using Linux permission numbering schema, (1 2 4) and use addition to set the given flag.
// this allows to encode needed bits via simple addition and check can be simple.
// is_mem (bit 0)
#define IS_MEMORY_t          1
// reg_ready (bit 1)
#define REG_READY_t          2
// fetched (bits 2-3): INFLIGHT=1, COMPLETE=2
#define INFLIGHT_fetch_t     4   // 1 << 2
#define COMPLETE_fetch_t     8   // 2 << 2
// scheduled (bits 4-5): INFLIGHT=1, COMPLETE=2
#define INFLIGHT_schedule_t  16  // 1 << 4
#define COMPLETE_schedule_t  32  // 2 << 4
// leftover (bits 6-7): INFLIGHT=1, COMPLETE=2
#define INFLIGHT_leftover_t  64  // 1 << 6
#define COMPLETE_leftover_t  128 // 2 << 6

union instr_events {
    uint64_t raw[NUM_CPUS][ROB_SIZE];
    struct {
        uint8_t is_mem      : 1;  // bit 0  
        uint8_t reg_ready   : 1;  // bit 1 
        uint8_t fetched     : 2;  // bit 2-3 - INFLIGHT, COMPLETED
        uint8_t scheduled   : 2;  // bits 4-5 - INFLIGHT, COMPLETED
        uint8_t leftover    : 2;  // bits 6-7
        uint64_t event_cycle: 56; 
    } entries[NUM_CPUS][ROB_SIZE];
};
struct alignas(64) MemSchedHeap {
    alignas(64) uint64_t heap[NUM_CPUS][ROB_SIZE];
    alignas(64) uint16_t hpos[NUM_CPUS][ROB_SIZE];
    uint16_t sz[NUM_CPUS];
    uint8_t  rob_wrap_count[NUM_CPUS];  // {0,1} between head resets, uint8_t sufficient

    static constexpr uint8_t  ROB_BITS  = 9;   // log2(512)
    static constexpr uint8_t  NORM_BITS = 10;  // 9+1 to encode wrap
    static constexpr uint16_t INVALID   = 0xFFFF;

    // key = (barrier<<63) | (ev_cycle<<10) | norm_idx
    // barrier=1 → key_cycle >> any curr → break fires immediately
    // norm_idx low 9 bits = rob_idx (recoverable)
    __attribute__((always_inline))
    static inline uint64_t make_key(uint8_t barrier, uint64_t ev_cycle, uint16_t norm_idx) noexcept {
        return ((uint64_t)barrier << 63) | (ev_cycle << NORM_BITS) | (norm_idx & 0x3FF);
    }
    __attribute__((always_inline))
    static inline uint16_t key_rob(uint64_t k)   noexcept { return static_cast<uint16_t>(k & 0x1FF); }
    __attribute__((always_inline))
    static inline uint64_t key_cycle(uint64_t k) noexcept { return k >> NORM_BITS; }

    // norm_idx at push time only — do NOT recompute at update_key
    __attribute__((always_inline))
    inline uint16_t norm(const uint8_t cpu, const uint16_t rob_idx) const noexcept {
        return static_cast<uint16_t>((rob_wrap_count[cpu] * ROB_SIZE + rob_idx) & 0x3FF);
    }
    // Extract stored norm from existing key — use in ALL update_key calls
    __attribute__((always_inline))
    inline uint16_t stored_norm(const uint8_t cpu, const uint16_t rob_idx) const noexcept {
        const uint16_t i = hpos[cpu][rob_idx];
        return (i != INVALID) ? static_cast<uint16_t>(heap[cpu][i] & 0x3FF)
                              : norm(cpu, rob_idx);
    }

    void clear(const uint8_t cpu) noexcept {
        sz[cpu] = 0;
        rob_wrap_count[cpu] = 0;
        memset(hpos[cpu], 0xFF, ROB_SIZE * sizeof(uint16_t));
    }
    inline bool empty(const uint8_t cpu) const noexcept { return sz[cpu] == 0; }

    __attribute__((always_inline))
    inline void push(const uint8_t cpu, const uint64_t key) noexcept {
        uint16_t i = sz[cpu]++;
        heap[cpu][i] = key;
        hpos[cpu][key_rob(key)] = i;
        sift_up(cpu, i);
    }

    __attribute__((always_inline))
    inline void pop(const uint8_t cpu) noexcept {
        hpos[cpu][key_rob(heap[cpu][0])] = INVALID;
        const uint16_t n = --sz[cpu];
        if (n > 0) {
            heap[cpu][0] = heap[cpu][n];
            hpos[cpu][key_rob(heap[cpu][0])] = 0;
            sift_down(cpu, 0);
        }
    }

    inline void remove(const uint8_t cpu, const uint16_t rob_idx) noexcept {
        const uint16_t i = hpos[cpu][rob_idx];
        if (i == INVALID) return;
        hpos[cpu][rob_idx] = INVALID;
        const uint16_t n = --sz[cpu];
        if (i < n) {
            heap[cpu][i] = heap[cpu][n];
            hpos[cpu][key_rob(heap[cpu][i])] = i;
            if (i > 0 && heap[cpu][(i-1)>>1] > heap[cpu][i]) sift_up(cpu, i);
            else                                               sift_down(cpu, i);
        }
    }

    __attribute__((always_inline))
    inline void update_key(const uint8_t cpu, const uint16_t rob_idx, const uint64_t new_key) noexcept {
        const uint16_t i = hpos[cpu][rob_idx];
        if (i == INVALID) return;
        const uint64_t old = heap[cpu][i];
        heap[cpu][i] = new_key;
        if      (new_key < old) sift_up(cpu, i);
        else if (new_key > old) sift_down(cpu, i);
    }

    inline void on_tail_wrap(const uint8_t cpu) noexcept { rob_wrap_count[cpu]++; }

    // Trigger: ROB.head just became 0.
    // All remaining heap entries have rob_idx ∈ [0, tail-1] → no offset needed.
    // O(n) key fixup + O(n) heapify. Amortized O(1) per retirement.
    void on_head_reset_to_zero(const uint8_t cpu) noexcept {
        rob_wrap_count[cpu] = 0;
        const uint16_t n = sz[cpu];
        for (uint16_t i = 0; i < n; ++i) {
            const uint64_t k = heap[cpu][i];
            const uint16_t rob_idx = static_cast<uint16_t>(k & 0x1FF);
            // preserve barrier (bit63) + ev_cycle (bits62..10), replace low 10 bits with rob_idx
            heap[cpu][i] = (k & ~uint64_t(0x3FF)) | rob_idx;
        }
        heapify(cpu);
    }

private:
    inline void sift_up(const uint8_t cpu, uint16_t i) noexcept {
        while (i > 0) {
            const uint16_t p = (i-1) >> 1;
            if (heap[cpu][p] <= heap[cpu][i]) break;
            uint64_t tmp = heap[cpu][i]; heap[cpu][i] = heap[cpu][p]; heap[cpu][p] = tmp;
            hpos[cpu][key_rob(heap[cpu][i])] = i;
            hpos[cpu][key_rob(heap[cpu][p])] = p;
            i = p;
        }
    }
    inline void sift_down(const uint8_t cpu, uint16_t i) noexcept {
        const uint16_t n = sz[cpu];
        while (true) {
            const uint16_t l=(i<<1)+1, r=l+1;
            uint16_t m = i;
            if (l < n && heap[cpu][l] < heap[cpu][m]) m = l;
            if (r < n && heap[cpu][r] < heap[cpu][m]) m = r;
            if (m == i) break;
            uint64_t tmp = heap[cpu][i]; heap[cpu][i] = heap[cpu][m]; heap[cpu][m] = tmp;
            hpos[cpu][key_rob(heap[cpu][i])] = i;
            hpos[cpu][key_rob(heap[cpu][m])] = m;
            i = m;
        }
    }
    void heapify(const uint8_t cpu) noexcept {
        const uint16_t n = sz[cpu];
        if (n < 2) return;
        for (int16_t i = (n>>1)-1; i >= 0; --i)
            sift_down(cpu, static_cast<uint16_t>(i));
        // hpos[] maintained by sift_down swaps ✓
    }
};

extern MemSchedHeap mem_sched_heap;
extern instr_events rob_events;
