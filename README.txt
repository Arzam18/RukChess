RUKCHESS ANDROID ARM64 — EVALUATE LINKER FIX + EXIT-CODE DIAGNOSTICS

This ZIP contains exactly two repository files, with paths relative to the
RukChess repository root:

  NNUE2.cpp
  scripts/build-android.sh

UPLOAD
1. Extract this ZIP.
2. In the RukChess repository, replace the root-level NNUE2.cpp with the
   NNUE2.cpp from this ZIP.
3. Replace scripts/build-android.sh with the script from this ZIP.
4. Keep the existing .github/workflows/android-arm64.yml,
   scripts/embedded_nnue.S, Android NNUE download step, and all other source
   files unchanged.
5. Commit both replacements to the branch used by the Android ARM64 workflow.
6. Run “Build RukChess for Android ARM64” again.

FIXES
- Defines Evaluate(BoardItem*, SearchDataItem*) to match NNUE2.h and the
  Search.cpp callers.
- Under USE_STATISTIC, safely increments SearchData->EvaluateCount only when
  SearchData is non-null.
- Preserves the Android NEON NNUE implementation and the embedded-network
  symbols/loader already present in the repository.
- Changes the build to compile each C++ source into its own object, then link.
  The build log identifies the exact file or link stage that fails and prints
  the failing command's exit code, including unusual statuses such as 74.
- Keeps the existing API 24, NDK r27c, ARMv8-A NEON, and embedded NNUE checks.

IMPORTANT
The uploaded log showed warnings from Tests.cpp followed by exit code 74, but
no compiler/linker diagnostic identifying the failing stage. The new script
will isolate that stage instead of treating the warnings as the cause.
The NNUE network remains downloaded by the existing workflow and embedded in
the final executable; no separate NNUE file is required at runtime.
