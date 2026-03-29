# TASK: Complete LiteXZ Trace Reader Implementation + ZSTD Proof-of-Concept

## Current State

### litexz_encoder/ Directory
- `litexz_encoder.cc` - Working XZ encoder/decoder with batch processing (128 instr/block)
- `reencode.cpp` - Alternative re-encoder with XZReader/XZWriter classes
- `reencode` - Compiled binary exists

### ChampSim Integration (INCOMPLETE)
- `ooo_cpu.h` - Has `LiteXZReader` class defined (double-buffered reader) but NOT USED
- `ooo_cpu.h` - Has `XZReader` class (byte-by-byte reader) - CURRENTLY USED
- `main.cc` - Has trace format detection for `.liteXZ` files
- `ooo_cpu.cc` - `handle_branch()` only uses `xz_reader`, NO `lite_xz_reader` support

## Required Changes

### 1. ooo_cpu.h - LiteXZReader Fix (CRITICAL)
The current `LiteXZReader::fill_half_buffer()` expects a 4-byte size prefix that doesn't exist in current `.liteXZ` format files.

**Current code reads:**
```cpp
// Try to read one compressed block
// First, read a 4-byte size prefix (compressed block size)
uint32_t compressed_size = 0;
input_file.read(reinterpret_cast<char*>(&compressed_size), sizeof(compressed_size));
```

**Problem:** The `litexz_encoder.cc` creates blocks WITHOUT size prefixes - each block is a raw XZ stream.

**Fix Options:**
- Option A: Modify `litexz_encoder.cc` to add size prefixes
- Option B: Modify `LiteXZReader` to read fixed-size blocks or detect stream boundaries

**DECISION:** Use Option A (size prefix) - it allows efficient seeking and is more robust.

### 2. main.cc - Define REDUCE_LiteXZ (DONE-ish)
Current code already detects `.liteXZ` but doesn't define a macro for conditional compilation.

Need to add:
```cpp
#define REDUCE_LiteXZ  // When trace ends with .liteXZ
```

### 3. ooo_cpu.cc - handle_branch() LiteXZ Support
The `handle_branch()` function currently only checks `xz_reader.eof()` and `xz_reader.read()`.

Need to add conditional logic:
```cpp
#ifdef REDUCE_LiteXZ
if (is_lite_xz) {
    // Use lite_xz_reader.read() which returns one instruction at a time
    // but internally uses double-buffering for efficiency
} else
#endif
if (is_xz_trace) {
    // Existing XZReader code
}
```

### 4. litexz_encoder/ - ZSTD Proof-of-Concept
Create new `litezstd_encoder/` directory with:
- Same architecture as litexz_encoder but using ZSTD
- Batch size: 128 instructions (8192 bytes)
- Format: `.liteZST` files
- Block format: [4-byte uncompressed size][4-byte compressed size][compressed data]

## File Structure

```
champsim_VB/
├── litexz_encoder/
│   ├── litexz_encoder.cc      (MODIFY: add size prefix)
│   ├── reencode.cpp           (working reference)
│   └── Makefile
├── litezstd_encoder/          (NEW)
│   ├── litezstd_encoder.cc    (zstd version)
│   └── Makefile
└── champsim_v10/
    ├── inc/
    │   └── ooo_cpu.h           (MODIFY: fix LiteXZReader)
    └── src/
        ├── main.cc             (MODIFY: add #define)
        └── ooo_cpu.cc          (MODIFY: handle_branch)
```

## Implementation Steps

### Phase 1: Fix LiteXZ Format (REQUIRED FIRST)
1. Modify `litexz_encoder/litexz_encoder.cc` to write size prefix
2. Re-encode a test trace
3. Update `LiteXZReader` in `ooo_cpu.h` to use the format
4. Test in ChampSim

### Phase 2: ChampSim Integration
1. Add `#define REDUCE_LiteXZ` in main.cc when `.liteXZ` detected
2. Modify `handle_branch()` in ooo_cpu.cc to use `lite_xz_reader`
3. Verify instruction counts match

### Phase 3: ZSTD Encoder (SEPARATE)
1. Create `litezstd_encoder/` directory
2. Implement ZSTD encoder/decoder
3. Test compression ratios vs XZ
4. Optionally integrate into ChampSim

## Verification

### Build Test
```bash
cd /home/vadimbir/champsim_VB/litexz_encoder
make clean && make
./litexz_encoder <trace.xz> <trace.liteXZ>
```

### ChampSim Test
```bash
cd /home/vadimbir/champsim_VB/champsim_v10
make clean && make
./bin/champsim <config> -traces <trace.liteXZ>
```

### Equivalency Check
- Instruction count should match between `.xz` and `.liteXZ` runs
- Simulation results should be identical

## Constraints

1. NEVER modify `.bypass` model files without explicit permission
2. NEVER touch files in `archive/` directory
3. Use `qcheck_consistency.sh` for testing, never bare make
4. Do NOT read debug files directly - use grep/head/tail
5. Minimize token usage in Claude Code

## Notes

- The `.liteXZ` format is designed to reduce `lzma_code()` calls by ~128x
- Each decompression yields 128 instructions instead of 1
- The double-buffering allows prefetching while processing
- ZSTD will provide faster decompression but less compression than XZ
