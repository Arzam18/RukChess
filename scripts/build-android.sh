#!/usr/bin/env bash
set -Eeuo pipefail

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
  echo "ERROR: invalid RukChess NNUE magic: $NETWORK_MAGIC (expected 42524b52)." >&2
  exit 1
fi

rm -rf "$ROOT/build/obj"
mkdir -p "$ROOT/build/obj" "$ROOT/dist"
cd "$ROOT"
mapfile -t SOURCES < <(find . -maxdepth 1 -type f -name '*.cpp' -printf '%f\n' | sort)
if [[ "${#SOURCES[@]}" -eq 0 ]]; then
  echo "ERROR: no C++ source files were found." >&2
  exit 1
fi

OUTPUT="$ROOT/dist/RukChess-Android-ARM64-NEON"
EMBEDDED_OBJECT="$ROOT/build/embedded_nnue.o"
NNUE_OBJECT="$ROOT/build/obj/NNUE2.o"

printf '\n=== Assemble embedded NNUE object ===\n'
"$CC" -march=armv8-a+simd -c scripts/embedded_nnue.S -o "$EMBEDDED_OBJECT"

CXXFLAGS=(
  -std=gnu++17
  -O3
  -DNDEBUG
  -march=armv8-a+simd
  -fno-strict-aliasing
  -fno-exceptions
  -fno-rtti
  -pthread
  -Wno-unknown-pragmas
  -Wno-deprecated-declarations
  -Wno-writable-strings
  -fdiagnostics-color=never
)

printf '\n=== Building RukChess Android ARM64 NEON (API 24, NDK r27c) ===\n'
for source in "${SOURCES[@]}"; do
  object="$ROOT/build/obj/${source%.cpp}.o"
  printf '\n=== Compile %s ===\n' "$source"
  "$CXX" "${CXXFLAGS[@]}" -c "$source" -o "$object"
done

printf '\n=== Link Android ARM64 executable ===\n'
OBJECTS=()
for source in "${SOURCES[@]}"; do
  OBJECTS+=("$ROOT/build/obj/${source%.cpp}.o")
done
"$CXX" -fuse-ld=lld -pthread -static-libstdc++ -Wl,--no-undefined \
  "${OBJECTS[@]}" "$EMBEDDED_OBJECT" -o "$OUTPUT"

printf '\n=== Inspect ELF header ===\n'
"$READELF" -h "$OUTPUT" > "$ROOT/dist/readelf-header.txt"
if ! grep -Eiq 'Machine:[[:space:]]+AArch64' "$ROOT/dist/readelf-header.txt"; then
  echo "ERROR: output is not an AArch64 ELF executable." >&2
  exit 1
fi

printf '\n=== Verify NEON accumulator instructions in NNUE2.o ===\n'
# NNUE2.cpp uses vaddq_s16/vsubq_s16 under USE_NNUE_NEON. Disassemble the object
# directly; this avoids fragile symbol-name filtering on the final linked ELF.
"$OBJDUMP" -d "$NNUE_OBJECT" > "$ROOT/dist/nnue2-disassembly.txt"
if ! grep -Eiq '[[:space:]]add[[:space:]]+v[0-9]+\.8h' "$ROOT/dist/nnue2-disassembly.txt"; then
  echo "ERROR: NNUE2.o is missing the expected NEON signed-16-bit vector ADD." >&2
  exit 1
fi
if ! grep -Eiq '[[:space:]]sub[[:space:]]+v[0-9]+\.8h' "$ROOT/dist/nnue2-disassembly.txt"; then
  echo "ERROR: NNUE2.o is missing the expected NEON signed-16-bit vector SUB." >&2
  exit 1
fi

echo "PASS: NNUE2.o contains both 8x16-bit NEON ADD and SUB instructions."

printf '\n=== Verify embedded NNUE in final executable ===\n'
EMBEDDED_SIZE="$("$READELF" -sW "$OUTPUT" | awk '$8 == "rukchess_embedded_nnue_start" { print $3; exit }')"
if [[ "$EMBEDDED_SIZE" != "1579024" ]]; then
  echo "ERROR: embedded NNUE symbol size is '${EMBEDDED_SIZE:-missing}' (expected 1579024)." >&2
  exit 1
fi

cat > "$ROOT/dist/BUILD_INFO.txt" <<'INFO'
Engine: RukChess 4.3.0dev (upstream source archive commit 014d9573e5905dad83b9acfdba17fd9294c88147)
Target: Android ARM64 / AArch64
Minimum API: Android API 24
Compiler: Android NDK r27c (27.2.12479018) Clang
CPU baseline: ARMv8-A Advanced SIMD (NEON)
NNUE: embedded in executable (1,579,024 bytes)
NNUE arithmetic: int16 NEON accumulator add/sub and NEON output layer
Threads: one advertised mobile thread; UCI search worker uses pthread
Dotprod: not enabled; the network arithmetic is int16 and has no native int8 dot-product path
Runtime data: no external NNUE required; UCI NnueFile can still load an optional external network
INFO
sha256sum "$OUTPUT" | tee "$ROOT/dist/SHA256SUMS.txt"
printf '\nBuild output: %s\n' "$OUTPUT"
