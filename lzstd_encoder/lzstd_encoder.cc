/**
 * lzstd_encoder - XZ -> ZSTD trace re-encoder
 *
 * Format: [4-byte compressed_size | MSB=partial_flag][ZSTD data]
 * Block size: 1536 instructions = 98304 bytes uncompressed
 * Compression: ZSTD level 18
 *
 * Threading: ring of N slots. Reader fills raw data round-robin.
 * Each slot has its own worker thread that compresses it.
 * Writer collects slots in order and writes.
 * No queues, no maps, no out-of-order complexity.
 *
 * Usage: ./lzstd_encoder <input.xz> <output.zstd>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <lzma.h>
#include <zstd.h>

static const int    COMPRESSION_LEVEL = 18;
static const size_t BATCH_SIZE        = 1;
static const size_t INSTR_SIZE        = 64;
static const size_t UNCOMPRESSED_SIZE = BATCH_SIZE * INSTR_SIZE; // 98304
static const size_t COMPRESSED_MAX    = ZSTD_COMPRESSBOUND(UNCOMPRESSED_SIZE);

struct input_instr {
    uint64_t ip;
    uint8_t  is_branch, branch_taken;
    uint8_t  destination_registers[2];
    uint8_t  source_registers[4];
    uint64_t destination_memory[2];
    uint64_t source_memory[4];
};
static_assert(sizeof(input_instr) == INSTR_SIZE, "");

// ============================================================================
// Slot states
// ============================================================================
enum SlotState { EMPTY, READY_TO_COMPRESS, COMPRESSED, DONE };

struct Slot {
    std::mutex              mtx;
    std::condition_variable cv;
    SlotState               state = EMPTY;

    uint8_t  raw[UNCOMPRESSED_SIZE];
    uint8_t  compressed[COMPRESSED_MAX];
    size_t   compressed_size = 0;
    size_t   instr_count = 0;
    bool     is_partial = false;
    bool     is_last = false;   // sentinel: no data, just signals worker to exit
};

// ============================================================================
// XZ reader
// ============================================================================
class XZReader {
    std::ifstream        f;
    lzma_stream          strm = LZMA_STREAM_INIT;
    uint8_t              inbuf[65536];
    bool                 initialized = false;
    bool                 eof_in = false;
public:
    ~XZReader() { close(); }

    bool open(const char* path) {
        f.open(path, std::ios::binary);
        if (!f.good()) { fprintf(stderr, "Cannot open: %s\n", path); return false; }
        lzma_ret r = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
        if (r != LZMA_OK) { fprintf(stderr, "lzma init: %d\n", r); return false; }
        initialized = true;
        strm.next_in = nullptr; strm.avail_in = 0;
        return true;
    }

    // Returns bytes read; 0 = EOF
    size_t read(uint8_t* buf, size_t want) {
        size_t got = 0;
        while (got < want) {
            strm.next_out  = buf + got;
            strm.avail_out = want - got;
            if (strm.avail_in == 0 && !eof_in) {
                f.read((char*)inbuf, sizeof(inbuf));
                size_t n = f.gcount();
                if (n > 0) { strm.next_in = inbuf; strm.avail_in = n; }
                else        { eof_in = true; }
            }
            lzma_action act = (eof_in && strm.avail_in == 0) ? LZMA_FINISH : LZMA_RUN;
            lzma_ret r = lzma_code(&strm, act);
            size_t made = (want - got) - strm.avail_out;
            got += made;
            if (r == LZMA_STREAM_END) { if (got == 0) return 0; continue; }
            if (r != LZMA_OK && r != LZMA_BUF_ERROR) { fprintf(stderr, "lzma err %d\n", r); break; }
            if (act == LZMA_FINISH && made == 0) break;
        }
        return got;
    }

    void close() {
        if (initialized) { lzma_end(&strm); initialized = false; }
        if (f.is_open()) f.close();
    }
};

// ============================================================================
// Worker: waits for READY_TO_COMPRESS, compresses, sets COMPRESSED
// ============================================================================
void worker(Slot* slot) {
    while (true) {
        {
            std::unique_lock<std::mutex> lk(slot->mtx);
            slot->cv.wait(lk, [&]{ return slot->state == READY_TO_COMPRESS; });
        }

        if (slot->is_last) {
            std::lock_guard<std::mutex> lk(slot->mtx);
            slot->state = COMPRESSED;
            slot->cv.notify_all();
            return;
        }

        size_t csz = ZSTD_compress(
            slot->compressed, COMPRESSED_MAX,
            slot->raw,        UNCOMPRESSED_SIZE,
            COMPRESSION_LEVEL
        );
        if (ZSTD_isError(csz)) {
            fprintf(stderr, "ZSTD error: %s\n", ZSTD_getErrorName(csz));
            exit(1);
        }

        {
            std::lock_guard<std::mutex> lk(slot->mtx);
            slot->compressed_size = csz;
            slot->state = COMPRESSED;
            slot->cv.notify_all();
        }
    }
}

// ============================================================================
// MAIN
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.xz> <output.zstd>\n", argv[0]);
        return 1;
    }

    unsigned int hw  = std::thread::hardware_concurrency();
    int          N   = (int)((hw > 2) ? hw - 4 : 1);  // N worker threads = N slots

    fprintf(stdout, "Input:      %s\n", argv[1]);
    fprintf(stdout, "Output:     %s\n", argv[2]);
    fprintf(stdout, "ZSTD level: %d\n", COMPRESSION_LEVEL);
    fprintf(stdout, "Batch:      %zu instructions / %zu bytes\n", BATCH_SIZE, UNCOMPRESSED_SIZE);
    fprintf(stdout, "Threads:    %d\n", N);
    fflush(stdout);

    // Allocate slots on heap (large raw[] inside)
    std::vector<Slot*> slots(N);
    for (int i = 0; i < N; i++) slots[i] = new Slot();

    // Start one worker per slot
    std::vector<std::thread> workers;
    for (int i = 0; i < N; i++)
        workers.emplace_back(worker, slots[i]);

    XZReader src;
    if (!src.open(argv[1])) return 1;

    std::ofstream out(argv[2], std::ios::binary);
    if (!out.good()) { fprintf(stderr, "Cannot create: %s\n", argv[2]); return 1; }

    size_t   total_batches = 0;
    uint64_t total_instrs  = 0;
    bool     reading       = true;

    // We pipeline: while reader fills slot[slot_idx] and triggers compression,
    // writer drains slot[read_idx] which was triggered N steps ago.
    // Simpler: just fill all, then drain all, in waves of N.

    while (reading) {
        // --- FILL WAVE: fill up to N slots ---
        int filled = 0;
        for (int i = 0; i < N && reading; i++) {
            Slot* s = slots[i];

            // Wait for slot to be EMPTY (writer already drained it)
            {
                std::unique_lock<std::mutex> lk(s->mtx);
                s->cv.wait(lk, [&]{ return s->state == EMPTY || s->state == DONE; });
                s->state = EMPTY;
            }

            size_t bytes = src.read(s->raw, UNCOMPRESSED_SIZE);
            size_t count = bytes / INSTR_SIZE;

            if (count == 0) {
                // EOF — signal this slot's worker to exit
                std::lock_guard<std::mutex> lk(s->mtx);
                s->is_last  = true;
                s->state    = READY_TO_COMPRESS;
                s->cv.notify_all();
                reading = false;
                break;
            }

            s->instr_count = count;
            s->is_partial  = (count < BATCH_SIZE);
            total_instrs  += count;

            if (s->is_partial) {
                memset(s->raw + bytes, 0, UNCOMPRESSED_SIZE - bytes);
                input_instr* last = reinterpret_cast<input_instr*>(
                    s->raw + (BATCH_SIZE - 1) * INSTR_SIZE);
                last->ip = 0xDEAD0000ULL | (count & 0xFFFF);
            }

            {
                std::lock_guard<std::mutex> lk(s->mtx);
                s->is_last = false;
                s->state   = READY_TO_COMPRESS;
                s->cv.notify_all();
            }
            filled++;

            if (s->is_partial) { reading = false; break; }
        }

        // --- DRAIN WAVE: collect compressed results in same order ---
        for (int i = 0; i < filled; i++) {
            Slot* s = slots[i];

            {
                std::unique_lock<std::mutex> lk(s->mtx);
                s->cv.wait(lk, [&]{ return s->state == COMPRESSED; });
            }

            uint32_t prefix = (uint32_t)s->compressed_size;
            if (s->is_partial) prefix |= 0x80000000u;
            out.write(reinterpret_cast<char*>(&prefix), sizeof(prefix));
            out.write(reinterpret_cast<char*>(s->compressed), s->compressed_size);
            if (!out.good()) { fprintf(stderr, "Write error\n"); exit(1); }

            total_batches++;
            if (total_batches % 50000 == 0){
                fprintf(stdout, "\r[%zu blocks | %zu M instrs | %.0f MB out]   ",
                    total_batches,
                    (size_t)(total_instrs / 1000000),
                    (double)out.tellp() / (1024*1024));
                    fflush(stdout);
            }

            {
                std::lock_guard<std::mutex> lk(s->mtx);
                s->state = DONE;
                s->cv.notify_all();
            }
        }
    }

    // Signal remaining workers (those not yet signaled) to exit
    for (int i = 0; i < N; i++) {
        Slot* s = slots[i];
        std::unique_lock<std::mutex> lk(s->mtx);
        if (s->state != READY_TO_COMPRESS && s->state != COMPRESSED) {
            s->is_last = true;
            s->state   = READY_TO_COMPRESS;
            s->cv.notify_all();
        }
    }

    for (auto& t : workers) t.join();

    src.close();
    out.close();

    std::ifstream in_sz(argv[1],  std::ios::binary | std::ios::ate);
    size_t in_size  = in_sz.tellg(); in_sz.close();
    std::ifstream out_sz(argv[2], std::ios::binary | std::ios::ate);
    size_t out_size = out_sz.tellg(); out_sz.close();

    fprintf(stdout, "\nDone: %zu blocks, %zu instructions\n", total_batches, total_instrs);
    fprintf(stdout, "Input:  %zu MB\n", in_size  / (1024*1024));
    fprintf(stdout, "Output: %zu MB\n", out_size / (1024*1024));
    if (in_size > 0)
        fprintf(stdout, "Ratio:  %.2f%%\n", 100.0 * out_size / in_size);

    for (int i = 0; i < N; i++) delete slots[i];
    return 0;
}
