# RukChess Evaluate signature hotfix

This is a narrowly scoped fix for the upstream-synced RukChess source where:

- `NNUE2.h` declares `Evaluate(BoardItem*, SearchDataItem*)`.
- `Board.cpp` and `Search.cpp` call that two-argument function.
- `NNUE2.cpp` still defined only `Evaluate(BoardItem*)`.

The link error is therefore an API/signature mismatch. It is not caused by the embedded NNUE object.

## Apply

From the repository root, run:

```bash
bash scripts/apply-evaluate-signature-fix.sh
```

Alternatively, apply `NNUE2-Evaluate-signature-fix.patch` with `git apply` from the repository root.

Only `NNUE2.cpp` is changed. The helper script checks the exact old function body and stops without changing the file if the source has drifted. It is safe to run more than once.

## Corrected implementation

```cpp
int Evaluate(BoardItem* Board, SearchDataItem* SearchData)
{
    (void)SearchData;
#ifdef USE_STATISTIC
    if (SearchData != NULL) {
        ++SearchData->EvaluateCount;
    }
#endif // USE_STATISTIC

    return (int)OutputLayer(Board);
}
```

`SearchDataItem` owns `EvaluateCount` in the current upstream structure; `BoardItem` does not. The null check preserves the `Evaluate(Board, NULL)` call used for board display. The NNUE output calculation itself is unchanged.
