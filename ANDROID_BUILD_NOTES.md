# RukChess Android ARM64 NEON Port — rebased on supplied upstream source

## Source baseline

Rebuilt from the user's uploaded `RukChess-master.zip`. The archive's root commit marker is:

`014d9573e5905dad83b9acfdba17fd9294c88147`

This package keeps that uploaded source as the baseline. It does not overwrite it with the older Android-port snapshot.

## Android changes carried onto this source revision

- `stdafx.h`: added POSIX/Android compatibility wrappers while preserving the Windows include path; one-thread OpenMP stubs, Android pthread search worker, secure-CRT compatibility helpers, monotonic performance-counter stubs, and Advanced SIMD header.
- `Types.h`: added portable fixed-width integer types for non-Windows builds, retaining the original Windows typedefs.
- `Def.h`: selects explicit `USE_NNUE_NEON` on AArch64 and keeps `USE_NNUE_AVX2` for the Windows build; defaults Android network loading to `embedded` without changing the desktop network filename.
- `BitBoard.cpp`: replaces Windows-only popcount/scan/PDEP/PEXT intrinsics with compiler builtins and portable bit-deposit/extract routines on non-Windows. Windows continues using the original BMI2 intrinsics.
- `Utils.cpp`: uses `CLOCK_MONOTONIC` on non-Windows, preserving the Windows clock implementation.
- `NNUE2.cpp`: adds embedded NNUE loading with the original file magic, full size, and hash checks; adds explicit int16x8 NEON accumulator add/sub and a NEON output layer; preserves the uploaded source's two-argument `Evaluate(BoardItem*, SearchDataItem*)` implementation.
- `scripts/embedded_nnue.S`: embeds the downloaded network in the executable.
- `scripts/build-android.sh`: builds API 24 / NDK r27c, verifies AArch64 ELF, verifies both NEON 8x16-bit ADD and SUB directly in `build/obj/NNUE2.o`, and checks the embedded network symbol size.
- `.github/workflows/android-arm64.yml`: manual-only `workflow_dispatch` build, network validation, NDK install, host UCI smoke test, and single self-contained artifact upload.
- `scripts/uci-smoke-test.py`: checks embedded NNUE loading, UCI handshake, `isready`, a depth-4 search and `bestmove`.

## What was verified here

- `bash -n scripts/build-android.sh` passed.
- `python3 -m py_compile scripts/uci-smoke-test.py` passed.
- Workflow YAML parsed successfully.
- Both ZIP archives passed integrity checks.
- AArch64 Clang 17 cross-compiled a small NEON intrinsic test; its disassembly contained `add vN.8h`, `sub vN.8h`, `smlal`, and `addv`.
- A host build of the full uploaded C++ source plus the embedded-loader changes passed a UCI smoke test through `uci`, `isready`, `go depth 4`, and `bestmove`. For this local test only, a synthetic zero-valued NNUE file with valid size, magic, and matching hash was used. It is not bundled. This validates startup/embedding/UCI flow, not engine strength or the production network.

## Still required

A full Android NDK r27c build and a real-network test are not available in this local environment. Run the included GitHub Actions workflow, retrieve the artifact, then test on the Galaxy A16 in Termux with `uci`, `isready`, and a short search. Do not mark RukChess successful until both pass.

## Upload steps

1. Extract the modified-files-only ZIP at the repository root, preserving paths.
2. Commit all replacements/additions to the branch used by the workflow.
3. Run **Build RukChess for Android ARM64** from Actions.
4. Confirm the artifact is uploaded, then run the Android UCI test.
