#!/usr/bin/env bash
set -e

ENCODER_DIR="$(dirname "$0")/lzstd_encoder"
INPUT="$1"
OUTPUT="${2:-${INPUT%.xz}.zstd}"

if [ -z "$INPUT" ]; then
    echo "Usage: $0 <input.xz> [output.zstd]"
    exit 1
fi

echo "Building encoder..."
make -C "$ENCODER_DIR" -j"$(nproc)" 2>&1

echo "Re-encoding: $INPUT -> $OUTPUT"
"$ENCODER_DIR/lzstd_encoder" "$INPUT" "$OUTPUT"
