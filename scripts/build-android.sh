#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
UCI_DATE="${UCI_DATE:-20261110}"
if [[ ! "$UCI_DATE" =~ ^[0-9]{8}$ ]]; then
  echo "ERROR: UCI_DATE must use YYYYMMDD format (got '$UCI_DATE')." >&2
  exit 2
fi
if [[ -z "$NDK_HOME" || ! -d "$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin" ]]; then
  echo "ERROR: ANDROID_NDK_HOME must point to Android NDK r27c." >&2
  exit 1
fi
TOOLCHAIN="$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$TOOLCHAIN/aarch64-linux-android24-clang"
CXX="$TOOLCHAIN/aarch64-linux-android24-clang++"
OBJDUMP="$TOOLCHAIN/llvm-objdump"
READELF="$TOOLCHAIN/llvm-readelf"
STRIP="$TOOLCHAIN/llvm-strip"
for tool in "$CC" "$CXX" "$OBJDUMP" "$READELF" "$STRIP"; do
  [[ -x "$tool" ]] || { echo "ERROR: required NDK tool is missing: $tool" >&2; exit 1; }
done

NETWORK="$ROOT/net-7342fb032855.nnue"
[[ -f "$NETWORK" ]] || { echo "ERROR: NNUE file is missing: $NETWORK" >&2; exit 1; }
NETWORK_BYTES="$(stat -c '%s' "$NETWORK")"
[[ "$NETWORK_BYTES" == "1579024" ]] || { echo "ERROR: unexpected NNUE size $NETWORK_BYTES (expected 1579024)." >&2; exit 1; }
NETWORK_MAGIC="$(od -An -tx1 -N4 "$NETWORK" | tr -d ' \n')"
[[ "$NETWORK_MAGIC" == "42524b52" ]] || { echo "ERROR: invalid RukChess NNUE magic: $NETWORK_MAGIC." >&2; exit 1; }

rm -rf "$ROOT/build/obj"
mkdir -p "$ROOT/build/obj" "$ROOT/dist"
cd "$ROOT"
mapfile -t SOURCES < <(find . -maxdepth 1 -type f -name '*.cpp' -printf '%f\n' | sort)
[[ ${#SOURCES[@]} -gt 0 ]] || { echo "ERROR: no C++ source files found." >&2; exit 1; }

OUTPUT="$ROOT/dist/RukChess-Android-ARM64-NEON"
EMBEDDED_OBJECT="$ROOT/build/embedded_nnue.o"
NNUE_OBJECT="$ROOT/build/obj/NNUE2.o"

printf '\n=== Assemble embedded NNUE object ===\n'
"$CC" -march=armv8-a+simd -c scripts/embedded_nnue.S -o "$EMBEDDED_OBJECT"

CXXFLAGS=(
  -std=gnu++17 -O3 -DNDEBUG
  -march=armv8-a+simd -mtune=cortex-a76
  -fPIE -fno-strict-aliasing -fno-exceptions -fno-rtti -pthread
  -Wno-unknown-pragmas -Wno-deprecated-declarations -Wno-writable-strings
  -fdiagnostics-color=never
  "-DRUKCHESS_BUILD_DATE=\"${UCI_DATE}\""
)

printf '\n=== Building RukChess %s Android ARM64 NEON (API 24, NDK r27c) ===\n' "$UCI_DATE"
for source in "${SOURCES[@]}"; do
  object="$ROOT/build/obj/${source%.cpp}.o"
  printf '\n=== Compile %s ===\n' "$source"
  "$CXX" "${CXXFLAGS[@]}" -c "$source" -o "$object"
done

OBJECTS=()
for source in "${SOURCES[@]}"; do OBJECTS+=("$ROOT/build/obj/${source%.cpp}.o"); done
printf '\n=== Link Android ARM64 executable ===\n'
"$CXX" -fuse-ld=lld -pie -pthread -static-libstdc++ \
  -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 \
  -Wl,--no-undefined "${OBJECTS[@]}" "$EMBEDDED_OBJECT" -o "$OUTPUT"

printf '\n=== Verify Android ELF ===\n'
"$READELF" -h "$OUTPUT" > "$ROOT/dist/readelf-header.txt"
grep -Eiq 'Class:[[:space:]]+ELF64' "$ROOT/dist/readelf-header.txt"
grep -Eiq 'Machine:[[:space:]]+AArch64' "$ROOT/dist/readelf-header.txt"
"$READELF" -l "$OUTPUT" > "$ROOT/dist/readelf-program-headers.txt"
"$READELF" -d "$OUTPUT" > "$ROOT/dist/readelf-dynamic.txt"
grep -Eiq 'Type:[[:space:]]+DYN' "$ROOT/dist/readelf-header.txt" || { echo "ERROR: Android executable is not PIE (ELF type DYN)." >&2; exit 1; }
grep -Fq '/system/bin/linker64' "$ROOT/dist/readelf-program-headers.txt" || { echo "ERROR: Android ARM64 interpreter is missing." >&2; exit 1; }
if grep -Fq 'libc++_shared.so' "$ROOT/dist/readelf-dynamic.txt"; then
  echo "ERROR: unexpected libc++_shared.so dependency." >&2; exit 1
fi
python3 - "$ROOT/dist/readelf-program-headers.txt" <<'PYALIGN'
import re
import sys
from pathlib import Path
text = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
loads = []
for line in text.splitlines():
    fields = line.split()
    if fields and fields[0] == "LOAD":
        try:
            loads.append(int(fields[-1], 0))
        except (ValueError, IndexError):
            raise SystemExit(f"ERROR: could not parse PT_LOAD alignment: {line}")
if not loads:
    raise SystemExit("ERROR: no PT_LOAD segments found in Android executable")
if any(alignment < 16384 for alignment in loads):
    raise SystemExit(f"ERROR: PT_LOAD alignment below 16 KiB: {loads}")
print("PASS: all PT_LOAD segments are aligned for 16 KiB Android pages.")
PYALIGN

printf '\n=== Verify baseline ARM64 NEON instructions ===\n'
"$OBJDUMP" -d "$NNUE_OBJECT" > "$ROOT/dist/nnue2-disassembly.txt"
grep -Eiq '[[:space:]]add[[:space:]]+v[0-9]+\.8h' "$ROOT/dist/nnue2-disassembly.txt" || { echo "ERROR: missing NEON int16 ADD." >&2; exit 1; }
grep -Eiq '[[:space:]]sub[[:space:]]+v[0-9]+\.8h' "$ROOT/dist/nnue2-disassembly.txt" || { echo "ERROR: missing NEON int16 SUB." >&2; exit 1; }
grep -Eiq '[[:space:]]smlal2?[[:space:]]+v[0-9]+' "$ROOT/dist/nnue2-disassembly.txt" || { echo "ERROR: missing widening NEON multiply-accumulate." >&2; exit 1; }
echo "PASS: NNUE2.o contains NEON ADD, SUB and widening multiply-accumulate."

printf '\n=== Verify embedded NNUE size ===\n'
SYMBOL_TABLE="$ROOT/dist/final-symbol-table.txt"
"$READELF" -sW "$OUTPUT" > "$SYMBOL_TABLE"
EMBEDDED_SIZE="$(awk '$NF == "rukchess_embedded_nnue_start" { size = $3 } END { if (size != "") print size }' "$SYMBOL_TABLE")"
[[ "$EMBEDDED_SIZE" == "1579024" ]] || { echo "ERROR: embedded NNUE symbol size '$EMBEDDED_SIZE' (expected 1579024)." >&2; exit 1; }
"$STRIP" --strip-unneeded "$OUTPUT"

UPSTREAM_COMMIT="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo archive-snapshot)"
cat > "$ROOT/dist/BUILD_INFO.txt" <<INFO
Engine: RukChess 4.3.0dev
Source revision: ${UPSTREAM_COMMIT} (newer upstream snapshot supplied 2026-11-10)
UCI version: ${UCI_DATE}
Target: Android ARM64 / AArch64
Minimum API: Android API 24
Compiler: Android NDK r27c (27.2.12479018) Clang
CPU baseline: ARMv8-A Advanced SIMD (NEON)
NNUE: embedded in executable (1,579,024 bytes; Net 183 / Epoch 111)
NNUE arithmetic: upstream int16 network with NEON accumulator ADD/SUB and NEON output layer
NNUE network SHA-256: $(sha256sum "$NETWORK" | awk '{print $1}')
Threads: one advertised mobile thread; UCI search worker uses pthread
DotProd: not enabled; this network uses int16 weights/accumulators and no native int8 dot-product kernel
INFO
sha256sum "$OUTPUT" | tee "$ROOT/dist/SHA256SUMS.txt"
printf '\nBuild output: %s\n' "$OUTPUT"
