#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FILE="$ROOT/NNUE2.cpp"

if [[ ! -f "$FILE" ]]; then
  echo "ERROR: NNUE2.cpp was not found at repository root: $ROOT" >&2
  exit 1
fi

python3 - "$FILE" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
source = path.read_text(encoding="utf-8")
old = '''int Evaluate(BoardItem* Board)
{
#ifdef USE_STATISTIC
    ++Board->EvaluateCount;
#endif // USE_STATISTIC

    return (int)OutputLayer(Board);
}'''
new = '''int Evaluate(BoardItem* Board, SearchDataItem* SearchData)
{
    (void)SearchData;
#ifdef USE_STATISTIC
    if (SearchData != NULL) {
        ++SearchData->EvaluateCount;
    }
#endif // USE_STATISTIC

    return (int)OutputLayer(Board);
}'''

if new in source:
    print("Evaluate signature fix already present; no changes made.")
elif old in source:
    path.write_text(source.replace(old, new, 1), encoding="utf-8")
    print("Updated NNUE2.cpp: Evaluate now matches the two-argument declaration.")
else:
    raise SystemExit(
        "ERROR: expected old Evaluate() function was not found. "
        "No file was changed; inspect the current NNUE2.cpp before applying a manual fix."
    )
PY
