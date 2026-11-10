# RukChess Android ARM64 NEON port — rebased on newer upstream source

## Source and rebase strategy

The supplied archive contains two trees. The Android implementation is the prior port, while `RukChess-master (New)` is the newer upstream source (five commits ahead, as indicated by the user). The Android work is reapplied to the newer tree. Search, move generation, tuning, UCI flow, and the new upstream NNUE parser/evaluation interface are retained; the old Android tree is used only as a compatibility/NEON reference.

The newer source reports `RukChess 4.3.0dev`. Its source entry point remains `RukChess.cpp` in the supplied archive.

## Android changes

- `stdafx.h`: conditional Windows/POSIX includes; Android helpers for secure-CRT calls, timing, pthread search worker, OpenMP single-thread stubs, alignment, and prefetch.
- `Types.h`: portable fixed-width integer aliases outside Windows.
- `Def.h`: selects AArch64 NEON on ARM64 while keeping Windows AVX2, and defaults Android to the embedded network.
- `BitBoard.cpp`: retains BMI2 intrinsics on Windows and uses compiler popcount/ctz plus portable PDEP/PEXT on non-Windows.
- `Utils.cpp`: uses a monotonic clock on non-Windows; leaves Windows-specific NUMA affinity code behind its existing feature switches.
- `NNUE2.cpp`: preserves the newer upstream float-to-int quantization, network hash conversion, `const BoardItem*` output/evaluation interface, and NNUE output behavior. It adds embedded-network loading and explicit NEON int16 accumulator add/sub plus NEON output accumulation.
- `Gen.cpp`, `Gen.h`, `Heuristic.cpp`, `Heuristic.h`: correct the newer source's incompatible `const int**` / `int**` counter-move-history pointer declarations so the existing mutable pointer array can be passed to read-only move-generation functions and the setter can populate it. Search logic is unchanged.
- `UCI.cpp`: appends the required `uci_date` build identity to the UCI engine name.
- `scripts/embedded_nnue.S`: embeds the original Net 183 / Epoch 111 network in the executable.
- `scripts/build-android.sh`: builds Android API 24 / NDK r27c ARM64 baseline NEON, verifies PIE/Android linker/16 KiB PT_LOAD alignment, checks the actual NEON ADD/SUB/widening multiply-accumulate instructions, verifies the embedded NNUE size, and writes build metadata/checksum.
- `.github/workflows/android-arm64.yml`: manual `workflow_dispatch`, required YYYYMMDD `uci_date`, original network download/size/magic check, NDK r27c build, real-network host UCI smoke test, and engine-only artifact upload.
- `scripts/uci-smoke-test.py`: verifies embedded NNUE load, date-bearing UCI identity, UCI handshake, readiness, depth-4 search, and bestmove.

## SIMD and DotProd decision

The current RukChess network is a float-on-disk network quantized at runtime to signed 16-bit input accumulators/weights and signed 16-bit output weights. The output layer is an int16-by-int16 dot product. ARM NEON is a direct fit and is implemented. ARM SDOT is an int8-by-int8 instruction; the current network does not have a native int8 tensor path. A lossless 16-bit decomposition into SDOT sub-products would add unpacking/correction operations to a small output layer and is not treated as a useful DotProd optimization. Therefore this delivery builds a genuine ARM64 NEON binary and does not claim a DotProd-optimized inference path.

## Local checks completed

- Full newer-source host C++17 builds passed with both GCC and Clang after fixing the const-pointer declaration mismatch in the counter-move-history API.
- Host UCI smoke test passed with a synthetic zero-valued network of the exact expected byte size and a correctly computed file hash. This verifies embedded loading, hash/size checks, startup, UCI handshake, and search plumbing; the synthetic network is not shipped and does not test playing strength.
- AArch64 Clang 17 code-generation probe emitted `add vN.8h`, `sub vN.8h`, `smlal2`, and `addv` for the same NEON intrinsics used in `NNUE2.cpp`.
- `bash -n` and Python syntax checks passed; workflow YAML was parsed locally.

## Pending verification

- Full Android NDK r27c cross-build in GitHub Actions.
- Production Net 183 / Epoch 111 hash/load confirmation in the workflow's real-network host smoke test.
- Android runtime/UCI testing on the Galaxy A16 5G.
- NPS and playing-strength testing. Keep RukChess in Testing until these are confirmed.
