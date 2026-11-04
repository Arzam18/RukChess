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
  echo "ERROR: invalid RukChess NNUE magic: $NETWORK_MAGIC (expected BRKR / 42524b52)." >&2
  exit 1
fi

# The Android source must expose the matching two-argument evaluation function.
if ! grep -Eq 'int Evaluate\(BoardItem\* Board, SearchDataItem\* SearchData\)' "$ROOT/NNUE2.cpp"; then
  echo "ERROR: NNUE2.cpp does not define Evaluate(BoardItem*, SearchDataItem*)." >&2
  exit 1
fi

mkdir -p "$ROOT/dist" "$ROOT/build/obj"
cd "$ROOT"
LOG="$ROOT/dist/build.log"
: > "$LOG"

run_logged() {
  local label="$1"
  shift
  local status=0
  echo
  echo "=== $label ==="
  printf 'COMMAND:'
  printf ' %q' "$@"
  printf '\n'
  set +e
  "$@" 2>&1 | tee -a "$LOG"
  status=${PIPESTATUS[0]}
  set -e
  if (( status != 0 )); then
    echo "ERROR: $label failed with exit code $status." >&2
    echo "----- Last 100 lines of $LOG -----" >&2
    tail -n 100 "$LOG" >&2 || true
    echo "---------------------------------" >&2
    exit "$status"
  fi
}

mapfile -t SOURCES < <(find . -maxdepth 1 -type f -name '*.cpp' -printf '%f\n' | sort)
if [[ "${#SOURCES[@]}" -eq 0 ]]; then
  echo "ERROR: no C++ source files were found." >&2
  exit 1
fi

COMMON_FLAGS=(
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
  -fdiagnostics-color=never
)

EMBEDDED_OBJECT="$ROOT/build/embedded_nnue.o"
run_logged "Assemble embedded NNUE object" "$CC" -march=armv8-a+simd -c "$ROOT/scripts/embedded_nnue.S" -o "$EMBEDDED_OBJECT"

echo "Building RukChess Android ARM64 NEON (API 24, NDK r27c)..."
OBJECTS=()
for SOURCE in "${SOURCES[@]}"; do
  OBJECT="$ROOT/build/obj/${SOURCE%.cpp}.o"
  run_logged "Compile $SOURCE" "$CXX" "${COMMON_FLAGS[@]}" -c "$SOURCE" -o "$OBJECT"
  OBJECTS+=("$OBJECT")
done

OUTPUT="$ROOT/dist/RukChess-Android-ARM64-NEON"
run_logged "Link Android ARM64 executable" "$CXX" \
  -fuse-ld=lld \
  -pthread \
  -static-libstdc++ \
  -Wl,--no-undefined \
  "${OBJECTS[@]}" \
  "$EMBEDDED_OBJECT" \
  -o "$OUTPUT"

printf '\n=== Inspect ELF header ===\n'
if "$READELF" -h "$OUTPUT" > "$ROOT/dist/readelf-header.txt" 2>> "$LOG"; then
  :
else
  STATUS=$?
  echo "ERROR: ELF header inspection failed with exit code $STATUS." >&2
  tail -n 100 "$LOG" >&2 || true
  exit "$STATUS"
fi
if ! grep -q 'Machine:.*AArch64' "$ROOT/dist/readelf-header.txt"; then
  echo "ERROR: output is not an AArch64 ELF executable." >&2
  exit 1
fi

# Disassemble only the two NNUE accumulator functions by their exact AArch64 C++ ABI symbol names. A whole-executable dump
# is unnecessary and can trigger tool failures on large binaries containing
# the embedded network. AArch64's mandatory Advanced SIMD instructions are
# recognized by llvm-objdump without an extra --mattr=+neon override.
printf '\n=== Verify NEON in NNUE accumulator functions ===\n'
DISASSEMBLY="$ROOT/dist/neon-disassembly.txt"
DISASSEMBLY_SOURCE="final executable"
OBJ_DUMP_ERROR="$ROOT/dist/objdump-error.txt"

if "$OBJDUMP" -d --disassemble-symbols=_Z13AccumulatorAddP9BoardItemii,_Z13AccumulatorSubP9BoardItemii "$OUTPUT" > "$DISASSEMBLY" 2> "$OBJ_DUMP_ERROR"; then
  echo "NEON disassembly source: final executable"
else
  STATUS=$?
  echo "WARNING: targeted disassembly of final executable returned $STATUS."
  cat "$OBJ_DUMP_ERROR" >&2 || true
  # The object is exactly the compiled translation unit linked above. This
  # fallback isolates objdump from the large final ELF while still checking
  # the instructions generated for the NNUE functions.
  DISASSEMBLY_SOURCE="NNUE2.o fallback (linked into final executable)"
  if "$OBJDUMP" -d --disassemble-symbols=_Z13AccumulatorAddP9BoardItemii,_Z13AccumulatorSubP9BoardItemii \
      "$ROOT/build/obj/NNUE2.o" > "$DISASSEMBLY" 2> "$OBJ_DUMP_ERROR"; then
    echo "NEON disassembly source: NNUE2.o fallback"
  else
    STATUS=$?
    echo "ERROR: NEON disassembly failed for both the final executable and NNUE2.o (exit $STATUS)." >&2
    cat "$OBJ_DUMP_ERROR" >&2 || true
    exit "$STATUS"
  fi
fi

if ! grep -Eiq '[[:space:]](add|sub)[[:space:]]+v[0-9]+\.8h' "$DISASSEMBLY"; then
  echo "ERROR: no expected NEON 8x16-bit add/sub instruction found in AccumulatorAdd/AccumulatorSub." >&2
  echo "Disassembly source: $DISASSEMBLY_SOURCE" >&2
  cat "$DISASSEMBLY" >&2 || true
  exit 1
fi

# Verify that the entire NNUE byte range is embedded in the final ELF.
EMBEDDED_SIZE="$("$READELF" -sW "$OUTPUT" | awk '$8 == "rukchess_embedded_nnue_start" { print $3; exit }')"
if [[ ! "$EMBEDDED_SIZE" =~ ^[0-9]+$ ]]; then
  echo "ERROR: embedded NNUE start symbol was not found or has an invalid size." >&2
  exit 1
fi
if [[ "$EMBEDDED_SIZE" != "1579024" ]]; then
  echo "ERROR: embedded NNUE symbol size is $EMBEDDED_SIZE bytes (expected 1579024)." >&2
  exit 1
fi

cat > "$ROOT/dist/BUILD_INFO.txt" <<INFO
Engine: RukChess 4.3.0dev
Target: Android ARM64 / AArch64
Minimum API: Android API 24
Compiler: Android NDK r27c (27.2.12479018) Clang
CPU baseline: ARMv8-A Advanced SIMD (NEON)
NNUE: embedded in executable (1,579,024 bytes); 768 x 512 x 2-perspective, int16 accumulator path
NEON validation: verified add/sub 8x16-bit instructions in AccumulatorAdd/AccumulatorSub ($DISASSEMBLY_SOURCE)
Threads: 1 (Android compatibility build uses serial OpenMP stubs)
Dotprod: not enabled as a separate variant; this NNUE uses int16 arithmetic and has no native int8 dot-product path.
Runtime data: none; the default NNUE is embedded. UCI option NnueFile can still load an optional external network.
INFO

(
  cd "$ROOT/dist"
  sha256sum RukChess-Android-ARM64-NEON > SHA256SUMS.txt
)

printf '\nBuild completed.\n'
file "$OUTPUT"
cat "$ROOT/dist/BUILD_INFO.txt"
