#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "$NDK_HOME" || ! -d "$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin" ]]; then
  echo "ERROR: ANDROID_NDK_HOME must point to Android NDK r27c." >&2
  exit 1
fi

TOOLCHAIN="$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
CC="$TOOLCHAIN/aarch64-linux-android24-clang"
CXX="$TOOLCHAIN/aarch64-linux-android24-clang++"
OBJDUMP="$TOOLCHAIN/llvm-objdump"
READELF="$TOOLCHAIN/llvm-readelf"

for tool in "$CC" "$CXX" "$OBJDUMP" "$READELF"; do
  if [[ ! -x "$tool" ]]; then
    echo "ERROR: required NDK tool is missing: $tool" >&2
    exit 1
  fi
done

NETWORK="$ROOT/net-7342fb032855.nnue"
if [[ ! -f "$NETWORK" ]]; then
  echo "ERROR: NNUE file is missing: $NETWORK" >&2
  exit 1
fi
NETWORK_BYTES="$(stat -c '%s' "$NETWORK")"
if [[ "$NETWORK_BYTES" != "1579024" ]]; then
  echo "ERROR: unexpected NNUE size $NETWORK_BYTES (expected 1579024)." >&2
  exit 1
fi
NETWORK_MAGIC="$(od -An -tx1 -N4 "$NETWORK" | tr -d ' \n')"
if [[ "$NETWORK_MAGIC" != "42524b52" ]]; then
  echo "ERROR: invalid RukChess NNUE magic: $NETWORK_MAGIC (expected BRKR / 42524b52)." >&2
  exit 1
fi

mkdir -p "$ROOT/dist" "$ROOT/build"
cd "$ROOT"
mapfile -t SOURCES < <(find . -maxdepth 1 -type f -name '*.cpp' -printf '%f\n' | sort)
if [[ "${#SOURCES[@]}" -eq 0 ]]; then
  echo "ERROR: no C++ source files were found." >&2
  exit 1
fi

OUTPUT="$ROOT/dist/RukChess-Android-ARM64-NEON"
EMBEDDED_OBJECT="$ROOT/build/embedded_nnue.o"
echo "Embedding RukChess NNUE into the executable..."
"$CC" -c "$ROOT/scripts/embedded_nnue.S" -o "$EMBEDDED_OBJECT"
echo "Building RukChess Android ARM64 NEON (API 24, NDK r27c)..."
"$CXX" \
  -std=gnu++17 \
  -O3 \
  -DNDEBUG \
  -march=armv8-a+simd \
  -fno-strict-aliasing \
  -fno-exceptions \
  -fno-rtti \
  -pthread \
  -static-libstdc++ \
  -Wno-unknown-pragmas \
  -Wno-deprecated-declarations \
  "${SOURCES[@]}" \
  "$EMBEDDED_OBJECT" \
  -o "$OUTPUT"

"$READELF" -h "$OUTPUT" > "$ROOT/dist/readelf-header.txt"
grep -q 'Machine:.*AArch64' "$ROOT/dist/readelf-header.txt" || {
  echo "ERROR: output is not an AArch64 ELF executable." >&2
  exit 1
}

# Confirm the compiler emitted real 128-bit Advanced SIMD instructions from
# the explicitly selected NEON NNUE path, not just an ARM64 baseline binary.
"$OBJDUMP" -d --mattr=+neon "$OUTPUT" > "$ROOT/dist/disassembly.txt"
if ! grep -Eiq '[[:space:]](add|sub)[[:space:]]+v[0-9]+\.8h' "$ROOT/dist/disassembly.txt"; then
  echo "ERROR: no expected NEON 8x16-bit add/sub instructions found." >&2
  exit 1
fi

# Verify the complete NNUE byte range is present in the final ELF.
EMBEDDED_SIZE_HEX="$("$READELF" -sW "$OUTPUT" | awk '$8 == "rukchess_embedded_nnue_start" { print $3; exit }')"
if [[ "$EMBEDDED_SIZE_HEX" =~ ^0x[0-9a-fA-F]+$ || "$EMBEDDED_SIZE_HEX" =~ ^[0-9]+$ ]]; then
  EMBEDDED_SIZE=$((EMBEDDED_SIZE_HEX))
else
  echo "ERROR: embedded NNUE symbol was not found in the final ELF." >&2
  exit 1
fi
if [[ "$EMBEDDED_SIZE" != "1579024" ]]; then
  echo "ERROR: embedded NNUE symbol size is $EMBEDDED_SIZE bytes (expected 1579024)." >&2
  exit 1
fi

cat > "$ROOT/dist/BUILD_INFO.txt" <<'INFO'
Engine: RukChess 4.3.0dev
Target: Android ARM64 / AArch64
Minimum API: Android API 24
Compiler: Android NDK r27c (27.2.12479018) Clang
CPU baseline: ARMv8-A Advanced SIMD (NEON)
NNUE: embedded in executable (1,579,024 bytes); 768 x 512 x 2-perspective, int16 accumulator path
Threads: 1 (Android compatibility build uses serial OpenMP stubs)
Dotprod: not enabled as a separate variant; this NNUE's arithmetic is int16 and has no native int8 dot-product path.
Runtime data: none; the default NNUE is embedded. UCI option NnueFile can still load an optional external network.
INFO

(
  cd "$ROOT/dist"
  sha256sum RukChess-Android-ARM64-NEON > SHA256SUMS.txt
)

printf '\nBuild completed.\n'
file "$OUTPUT"
cat "$ROOT/dist/BUILD_INFO.txt"
