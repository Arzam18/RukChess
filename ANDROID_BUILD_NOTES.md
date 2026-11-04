# Android ARM64 build notes

The Android ARM64 NEON build embeds the RukChess NNUE network directly in the executable. The GitHub Actions workflow downloads and validates Net 183 / Epoch 111 (`net-7342fb032855.nnue`) during the build, then links its bytes into a read-only ELF section. The published artifact contains one self-contained engine executable; no sidecar `.nnue` file is required at runtime.

## Build

Run the GitHub Actions workflow **Build RukChess for Android ARM64** with `workflow_dispatch`. It uses Android NDK r27c, API 24, and produces one AArch64 executable using the ARMv8-A NEON baseline. The build checks that the embedded NNUE symbol is exactly 1,579,024 bytes and verifies the executable is AArch64 with emitted NEON instructions.

## Threading and Dotprod

The Android port deliberately exposes one thread. The original OpenMP regions are retained but compiled as serial code because the mobile test profile uses one engine thread and this standalone build does not bundle an OpenMP runtime.

There is no separate Dotprod build. The NNUE network stores and accumulates signed 16-bit values, and its exact output layer uses 16-bit multiply-accumulate arithmetic. ARM Dotprod's SDOT/UDOT instructions operate on 8-bit lanes; using them here would require an additional lossy or multi-pass split-byte kernel. The Android build therefore keeps the existing numerical behavior and uses NEON rather than claiming an unverified Dotprod implementation.

## Runtime

Copy only `RukChess-Android-ARM64-NEON` to Termux or the engine directory used by your chess GUI. No network file is needed. The default UCI option is `NnueFile=embedded`. The existing `NnueFile` option can still point to a valid external RukChess network when an override is deliberately required.

```sh
chmod +x RukChess-Android-ARM64-NEON
./RukChess-Android-ARM64-NEON
```

Select UCI by sending `uci` as the first input line. The engine reports `Threads` max 1 on this build.
