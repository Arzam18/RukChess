# Android ARM64 Engine Queue Checkpoint — 2026-11-05

| Order | Engine | Status | Notes |
|---:|---|---|---|
| 1 | Askaig | Successful | Android ARM64 NEON build and UCI runtime confirmed by user. |
| 2 | Chessnix | Successful | Android ARM64 NEON build and UCI runtime confirmed by user. |
| 3 | NARC Genesys | Successful | Android runtime confirmed; approximately 1M NPS reported. Uses NNUE base evaluation with correction-history adjustment; no separate Dotprod variant. |
| 4 | Pea | Successful | Build and Android runtime confirmed by user. |
| 5 | RukChess | Active | Rebased Android port prepared from uploaded source commit `014d9573e5905dad83b9acfdba17fd9294c88147`. Port includes explicit AArch64 NEON NNUE path and portable bitboard operations. Host full-source UCI smoke test passes with a synthetic valid-format network; NEON intrinsic cross-compile/pattern check passes. Awaiting real-network Android NDK GitHub Actions build and Galaxy A16 UCI runtime. |
| 6 | Solo | Next | Start only after RukChess passes Android UCI runtime. |
| 7 | Kociolek | Queued |  |
| 8 | Peacekeeper | Queued | Keep original and Android-added archive variants for comparison. |
| 9 | Quirky | Queued | Keep original and Android-port archive variants for comparison. |

## RukChess verification details

- Uploaded upstream archive is the new base; older Android-port source is not copied wholesale over it.
- Uploaded source already has the matching `Evaluate(BoardItem*, SearchDataItem*)` declaration and implementation; retained unchanged.
- AArch64 selects `USE_NNUE_NEON`, Windows keeps `USE_NNUE_AVX2`.
- Non-Windows bitboard builtins/portable PDEP/PEXT added for the uploaded source's x86-only intrinsics.
- NEON verifier now disassembles `NNUE2.o` with plain `llvm-objdump -d` and checks both `add vN.8h` and `sub vN.8h`; no mangled-symbol filter or final-ELF selective disassembly.
- Host UCI test passed using a synthetic zero-valued network, so the production net is not tested yet.
- Do not mark successful before user confirms the real GitHub Actions artifact and Android UCI runtime.
