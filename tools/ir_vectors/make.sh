#!/bin/bash
# Regenerates tests/ir_vectors.h from the Flipper Zero firmware's infrared
# encoders (downloaded, built on this computer with stand-ins for its
# headers, and not kept). Run after adding a protocol to irproto.c.
#   tools/ir_vectors/make.sh [git ref, default dev]
set -e
cd "$(dirname "$0")/../.."
REF=${1:-dev}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
git clone -q --depth 1 --branch "$REF" --filter=blob:none --sparse \
    https://github.com/flipperdevices/flipperzero-firmware.git "$TMP/fw"
git -C "$TMP/fw" sparse-checkout set lib/infrared/encoder_decoder
mkdir -p "$TMP/shim/core"
cat > "$TMP/shim/core/check.h" <<'H'
#pragma once
#include <stdio.h>
#include <stdlib.h>
#define furi_assert(x) do { if (!(x)) { fprintf(stderr, "assert %s:%d\n", __FILE__, __LINE__); abort(); } } while (0)
#define furi_check(x) furi_assert(x)
#define furi_crash(...) abort()
H
printf '#pragma once\n#include "core_defines.h"\n#include "check.h"\n' > "$TMP/shim/core/common_defines.h"
cat > "$TMP/shim/core/core_defines.h" <<'H'
#pragma once
#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define UNUSED(x) (void)(x)
H
E="$TMP/fw/lib/infrared/encoder_decoder"
cc -std=gnu11 -w -I"$TMP/shim" -I"$E" tools/ir_vectors/make.c $(find "$E" -name '*.c') -o "$TMP/make"
"$TMP/make" > tests/ir_vectors.h
echo "tests/ir_vectors.h written from flipperzero-firmware $REF ($(git -C "$TMP/fw" rev-parse --short HEAD))"
