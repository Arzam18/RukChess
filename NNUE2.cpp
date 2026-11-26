// NNUE2.cpp

#include "stdafx.h"

#include "NNUE2.h"

#include "BitBoard.h"
#include "Board.h"
#include "Def.h"
#include "Types.h"
#include "Utils.h"

#define NNUE_FILE_MAGIC             ('B' | 'R' << 8 | 'K' << 16 | 'R' << 24)
#define NNUE_FILE_SIZE              (4 + 8 + (INPUT_DIMENSION * HIDDEN_DIMENSION + HIDDEN_DIMENSION + HIDDEN_DIMENSION * 2 + 1) * 4)

#define NNUE_HASH(Hash, Value)      (((Hash) + 324723947ULL + (Value)) ^ 93485734985ULL)

#define STM                         0
#define XSTM                        1

#if defined(USE_NNUE_AVX2)
#define NUM_REGS                    (HIDDEN_DIMENSION * sizeof(I16) / sizeof(__m256i))
#elif defined(USE_NNUE_NEON)
#define NUM_REGS                    (HIDDEN_DIMENSION / 8)
#endif

#if defined(__ANDROID__)
extern "C" {
extern const unsigned char rukchess_embedded_nnue_start[];
extern const unsigned char rukchess_embedded_nnue_end[];
}
#endif

_declspec(align(64)) I16 InputWeights[INPUT_DIMENSION * HIDDEN_DIMENSION];
_declspec(align(64)) I16 InputBiases[HIDDEN_DIMENSION];
_declspec(align(64)) I16 OutputWeights[HIDDEN_DIMENSION * 2];
I32 OutputBias;

BOOL NnueFileLoaded = FALSE;

I16 LoadInt16(const float Value, const int Precision)
{
    return (I16)roundf(Value * (float)Precision);
}

I32 LoadInt32(const float Value, const int Precision)
{
    return (I32)roundf(Value * (float)Precision);
}

static BOOL LoadNetworkData(const unsigned char* Data, const size_t DataSize, const char* DisplayName)
{
    if (Data == NULL || DataSize != (size_t)NNUE_FILE_SIZE) {
        printf("File '%s' format error (size %zu; expected %d)!\n",
            DisplayName != NULL ? DisplayName : "(null)", DataSize, NNUE_FILE_SIZE);
        return FALSE;
    }

    int FileMagic;
    U64 FileHash;
    U64 NnueHash = 0ULL;
    float Value;
    size_t Offset = 0;

    memcpy(&FileMagic, Data + Offset, sizeof(FileMagic));
    Offset += sizeof(FileMagic);
    if (FileMagic != NNUE_FILE_MAGIC) {
        printf("File '%s' format error (invalid magic)!\n", DisplayName);
        return FALSE;
    }

    memcpy(&FileHash, Data + Offset, sizeof(FileHash));
    Offset += sizeof(FileHash);

#ifdef PRINT_MIN_MAX_VALUES
    float MinValue = FLT_MAX;
    float MaxValue = -FLT_MAX;
#endif // PRINT_MIN_MAX_VALUES

    // Keep the upstream network conversion and hash semantics unchanged.
    for (int Index = 0; Index < INPUT_DIMENSION * HIDDEN_DIMENSION; Index++) {
        memcpy(&Value, Data + Offset, sizeof(Value));
        Offset += sizeof(Value);
#ifdef PRINT_MIN_MAX_VALUES
        MinValue = MIN(MinValue, Value);
        MaxValue = MAX(MaxValue, Value);
#endif // PRINT_MIN_MAX_VALUES
        NnueHash = NNUE_HASH(NnueHash, (U64)Value);
        InputWeights[Index] = LoadInt16(Value, QUANTIZATION_PRECISION_IN);
    }

#ifdef PRINT_MIN_MAX_VALUES
    printf("Input weights: MinValue = %f MaxValue = %f\n", MinValue, MaxValue);
    MinValue = FLT_MAX;
    MaxValue = -FLT_MAX;
#endif // PRINT_MIN_MAX_VALUES

    for (int Index = 0; Index < HIDDEN_DIMENSION; Index++) {
        memcpy(&Value, Data + Offset, sizeof(Value));
        Offset += sizeof(Value);
#ifdef PRINT_MIN_MAX_VALUES
        MinValue = MIN(MinValue, Value);
        MaxValue = MAX(MaxValue, Value);
#endif // PRINT_MIN_MAX_VALUES
        NnueHash = NNUE_HASH(NnueHash, (U64)Value);
        InputBiases[Index] = LoadInt16(Value, QUANTIZATION_PRECISION_IN);
    }

#ifdef PRINT_MIN_MAX_VALUES
    printf("Input biases: MinValue = %f MaxValue = %f\n", MinValue, MaxValue);
    MinValue = FLT_MAX;
    MaxValue = -FLT_MAX;
#endif // PRINT_MIN_MAX_VALUES

    for (int Index = 0; Index < HIDDEN_DIMENSION * 2; Index++) {
        memcpy(&Value, Data + Offset, sizeof(Value));
        Offset += sizeof(Value);
#ifdef PRINT_MIN_MAX_VALUES
        MinValue = MIN(MinValue, Value);
        MaxValue = MAX(MaxValue, Value);
#endif // PRINT_MIN_MAX_VALUES
        NnueHash = NNUE_HASH(NnueHash, (U64)Value);
        OutputWeights[Index] = LoadInt16(Value, QUANTIZATION_PRECISION_OUT);
    }

#ifdef PRINT_MIN_MAX_VALUES
    printf("Output weights: MinValue = %f MaxValue = %f\n", MinValue, MaxValue);
#endif // PRINT_MIN_MAX_VALUES

    memcpy(&Value, Data + Offset, sizeof(Value));
    Offset += sizeof(Value);
    NnueHash = NNUE_HASH(NnueHash, (U64)Value);
    OutputBias = LoadInt32(Value, QUANTIZATION_PRECISION_IN * QUANTIZATION_PRECISION_OUT);

#ifdef PRINT_MIN_MAX_VALUES
    printf("Output bias: Value = %f\n", Value);
#endif // PRINT_MIN_MAX_VALUES

    if (Offset != DataSize || FileHash != NnueHash) {
        printf("File '%s' format/hash error!\n", DisplayName);
        return FALSE;
    }

    NnueFileLoaded = TRUE;
    printf("Load network...DONE (%s; 0x%012llx)\n", DisplayName, FileHash);
    return TRUE;
}

void LoadNetwork(const char* NnueFileName)
{
    printf("\nLoad network...\n");
    NnueFileLoaded = FALSE;

#if defined(__ANDROID__)
    const BOOL UseEmbedded = (NnueFileName == NULL || NnueFileName[0] == '\0' ||
        strcmp(NnueFileName, "embedded") == 0);
    if (UseEmbedded) {
        const size_t EmbeddedSize = (size_t)((uintptr_t)rukchess_embedded_nnue_end -
            (uintptr_t)rukchess_embedded_nnue_start);
        LoadNetworkData(rukchess_embedded_nnue_start, EmbeddedSize, "embedded");
        return;
    }
#endif

    if (NnueFileName == NULL || NnueFileName[0] == '\0') {
        NnueFileName = DEFAULT_NNUE_FILE_NAME;
    }

    FILE* File = NULL;
    if (fopen_s(&File, NnueFileName, "rb") != 0 || File == NULL) {
        printf("File '%s' open error!\n", NnueFileName);
        return;
    }
    if (fseek(File, 0, SEEK_END) != 0) {
        printf("File '%s' read error!\n", NnueFileName);
        fclose(File);
        return;
    }
    const long FileSize = ftell(File);
    if (FileSize < 0 || (size_t)FileSize != (size_t)NNUE_FILE_SIZE || fseek(File, 0, SEEK_SET) != 0) {
        printf("File '%s' format error!\n", NnueFileName);
        fclose(File);
        return;
    }

    unsigned char* Buffer = (unsigned char*)malloc((size_t)NNUE_FILE_SIZE);
    if (Buffer == NULL) {
        printf("File '%s' load error (out of memory)!\n", NnueFileName);
        fclose(File);
        return;
    }
    const size_t BytesRead = fread(Buffer, 1, (size_t)NNUE_FILE_SIZE, File);
    fclose(File);
    if (BytesRead != (size_t)NNUE_FILE_SIZE) {
        printf("File '%s' read error!\n", NnueFileName);
        free(Buffer);
        return;
    }
    LoadNetworkData(Buffer, BytesRead, NnueFileName);
    free(Buffer);
}

BOOL IsNetworkLoaded(void)
{
    return NnueFileLoaded;
}

int CalculateWeightIndex(const int Perspective, const int Square, const int PieceWithColor)
{
    int PieceIndex;
    int WeightIndex;

    if (Perspective == STM) {
        PieceIndex = PIECE_TYPE(PieceWithColor) + 6 * PIECE_COLOR(PieceWithColor);
        WeightIndex = (PieceIndex << 6) + Square;
    }
    else { // XSTM
        PieceIndex = PIECE_TYPE(PieceWithColor) + 6 * CHANGE_COLOR(PIECE_COLOR(PieceWithColor));
        WeightIndex = (PieceIndex << 6) + (Square ^ 56);
    }

    return WeightIndex;
}

void AccumulatorAdd(BoardItem* Board, int Square, int PieceWithColor)
{
    for (int Perspective = 0; Perspective < 2; Perspective++) { // STM/XSTM
        int WeightIndex = CalculateWeightIndex(Perspective, Square, PieceWithColor);

#ifdef USE_NNUE_AVX2
        __m256i* Accumulator = (__m256i*)Board->Accumulator.Accumulator[Perspective];
        __m256i* Weights = (__m256i*)&InputWeights[WeightIndex * HIDDEN_DIMENSION];

        for (int Reg = 0; Reg < NUM_REGS; Reg++) {
            Accumulator[Reg] = _mm256_add_epi16(Accumulator[Reg], Weights[Reg]);
        }
#elif defined(USE_NNUE_NEON)
        int16x8_t* AccumulatorTile = (int16x8_t*)Board->Accumulator.Accumulator[Perspective];
        const int16x8_t* Weights = (const int16x8_t*)&InputWeights[WeightIndex * HIDDEN_DIMENSION];
        for (int Reg = 0; Reg < NUM_REGS; Reg++) {
            AccumulatorTile[Reg] = vaddq_s16(AccumulatorTile[Reg], Weights[Reg]);
        }
#else
        for (int Index = 0; Index < HIDDEN_DIMENSION; Index++) {
            Board->Accumulator.Accumulator[Perspective][Index] += InputWeights[WeightIndex * HIDDEN_DIMENSION + Index];
        }
#endif // USE_NNUE_AVX2
    }
}

void AccumulatorSub(BoardItem* Board, int Square, int PieceWithColor)
{
    for (int Perspective = 0; Perspective < 2; Perspective++) { // STM/XSTM
        int WeightIndex = CalculateWeightIndex(Perspective, Square, PieceWithColor);

#ifdef USE_NNUE_AVX2
        __m256i* Accumulator = (__m256i*)Board->Accumulator.Accumulator[Perspective];
        __m256i* Weights = (__m256i*)&InputWeights[WeightIndex * HIDDEN_DIMENSION];

        for (int Reg = 0; Reg < NUM_REGS; Reg++) {
            Accumulator[Reg] = _mm256_sub_epi16(Accumulator[Reg], Weights[Reg]);
        }
#elif defined(USE_NNUE_NEON)
        int16x8_t* AccumulatorTile = (int16x8_t*)Board->Accumulator.Accumulator[Perspective];
        const int16x8_t* Weights = (const int16x8_t*)&InputWeights[WeightIndex * HIDDEN_DIMENSION];
        for (int Reg = 0; Reg < NUM_REGS; Reg++) {
            AccumulatorTile[Reg] = vsubq_s16(AccumulatorTile[Reg], Weights[Reg]);
        }
#else
        for (int Index = 0; Index < HIDDEN_DIMENSION; Index++) {
            Board->Accumulator.Accumulator[Perspective][Index] -= InputWeights[WeightIndex * HIDDEN_DIMENSION + Index];
        }
#endif // USE_NNUE_AVX2
    }
}

void InitAccumulator(BoardItem* Board)
{
    memcpy(Board->Accumulator.Accumulator[STM], InputBiases, sizeof(Board->Accumulator.Accumulator[STM]));
    memcpy(Board->Accumulator.Accumulator[XSTM], InputBiases, sizeof(Board->Accumulator.Accumulator[XSTM]));

    U64 Pieces = (Board->BB_WhitePieces | Board->BB_BlackPieces);

    while (Pieces) {
        int Square = LSB(Pieces);

        int PieceWithColor = Board->Pieces[Square];

        AccumulatorAdd(Board, Square, PieceWithColor);

        Pieces &= Pieces - 1ULL;
    }

    Board->Accumulator.AccumulatorActual = TRUE;
}

BOOL UpdateAccumulator(BoardItem* Board)
{
    int UpdateMoves = 0;
    int ActualMoveIndex = -1;

    for (int MoveIndex = Board->HalfMoveNumber - 1; MoveIndex >= 0; MoveIndex--) {
        UpdateMoves++;

        HistoryItem* Info = &Board->MoveTable[MoveIndex];

        if (Info->Accumulator.AccumulatorActual) {
            ActualMoveIndex = MoveIndex;

            break;
        }

        if (UpdateMoves >= MAX_UPDATE_MOVES) {
            ActualMoveIndex = -1;

            break;
        }
    }

    if (ActualMoveIndex == -1) {
        return FALSE;
    }

    int Color = Board->CurrentColor;

    for (int MoveIndex = Board->HalfMoveNumber - 1; MoveIndex >= ActualMoveIndex; MoveIndex--) {
        Color ^= 1;

        HistoryItem* Info = &Board->MoveTable[MoveIndex];

        if (!(Info->Type & MOVE_NULL)) {
            if (Color == WHITE) {
                if (Info->Type & MOVE_CASTLE_KING) {
                    AccumulatorSub(Board, SQ_H1, PIECE_CREATE(ROOK, WHITE));
                    AccumulatorAdd(Board, SQ_F1, PIECE_CREATE(ROOK, WHITE));
                }

                if (Info->Type & MOVE_CASTLE_QUEEN) {
                    AccumulatorSub(Board, SQ_A1, PIECE_CREATE(ROOK, WHITE));
                    AccumulatorAdd(Board, SQ_D1, PIECE_CREATE(ROOK, WHITE));
                }

                if (Info->Type & MOVE_PAWN_EN_PASSANT) {
                    AccumulatorSub(Board, Info->CapturePawnSquare, PIECE_CREATE(PAWN, BLACK));
                }
                else if (Info->Type & MOVE_CAPTURE) {
                    AccumulatorSub(Board, Info->To, PIECE_CREATE(Info->PieceTypeTo, BLACK));
                }

                if (Info->Type & MOVE_PAWN_PROMOTE) {
                    AccumulatorAdd(Board, Info->To, PIECE_CREATE(Info->PromotePieceType, WHITE));
                }
                else {
                    AccumulatorAdd(Board, Info->To, PIECE_CREATE(Info->PieceTypeFrom, WHITE));
                }

                AccumulatorSub(Board, Info->From, PIECE_CREATE(Info->PieceTypeFrom, WHITE));
            }
            else { // BLACK
                if (Info->Type & MOVE_CASTLE_KING) {
                    AccumulatorSub(Board, SQ_H8, PIECE_CREATE(ROOK, BLACK));
                    AccumulatorAdd(Board, SQ_F8, PIECE_CREATE(ROOK, BLACK));
                }

                if (Info->Type & MOVE_CASTLE_QUEEN) {
                    AccumulatorSub(Board, SQ_A8, PIECE_CREATE(ROOK, BLACK));
                    AccumulatorAdd(Board, SQ_D8, PIECE_CREATE(ROOK, BLACK));
                }

                if (Info->Type & MOVE_PAWN_EN_PASSANT) {
                    AccumulatorSub(Board, Info->CapturePawnSquare, PIECE_CREATE(PAWN, WHITE));
                }
                else if (Info->Type & MOVE_CAPTURE) {
                    AccumulatorSub(Board, Info->To, PIECE_CREATE(Info->PieceTypeTo, WHITE));
                }

                if (Info->Type & MOVE_PAWN_PROMOTE) {
                    AccumulatorAdd(Board, Info->To, PIECE_CREATE(Info->PromotePieceType, BLACK));
                }
                else {
                    AccumulatorAdd(Board, Info->To, PIECE_CREATE(Info->PieceTypeFrom, BLACK));
                }

                AccumulatorSub(Board, Info->From, PIECE_CREATE(Info->PieceTypeFrom, BLACK));
            }
        } // if
    } // for

#ifdef DEBUG_NNUE_UPDATE
    AccumulatorItem Accumulator = Board->Accumulator;

    InitAccumulator(Board);

    for (int Perspective = 0; Perspective < 2; Perspective++) { // STM/XSTM
        for (int Index = 0; Index < HIDDEN_DIMENSION; Index++) {
            if (Board->Accumulator.Accumulator[Perspective][Index] != Accumulator.Accumulator[Perspective][Index]) {
                printf("-- Accumulator error (update)!\n");

                break;
            }
        }
    }
#endif // DEBUG_NNUE_UPDATE

    Board->Accumulator.AccumulatorActual = TRUE;

    return TRUE;
}

I32 OutputLayer(const BoardItem* Board)
{
    I32 Result = OutputBias;

#ifdef USE_NNUE_AVX2
    __m256i Zero = _mm256_setzero_si256();

    __m256i Sum0 = Zero;
    __m256i Sum1 = Zero;

    __m256i* Accumulator0 = (__m256i*)Board->Accumulator.Accumulator[Board->CurrentColor];
    __m256i* Accumulator1 = (__m256i*)Board->Accumulator.Accumulator[CHANGE_COLOR(Board->CurrentColor)];

    __m256i* Weights0 = (__m256i*)&OutputWeights;
    __m256i* Weights1 = (__m256i*)&OutputWeights[HIDDEN_DIMENSION];

    for (int Reg = 0; Reg < NUM_REGS; Reg++) {
        __m256i Acc0 = _mm256_max_epi16(Zero, Accumulator0[Reg]); // ReLU
        __m256i Acc1 = _mm256_max_epi16(Zero, Accumulator1[Reg]); // ReLU

        Sum0 = _mm256_add_epi32(Sum0, _mm256_madd_epi16(Acc0, Weights0[Reg]));
        Sum1 = _mm256_add_epi32(Sum1, _mm256_madd_epi16(Acc1, Weights1[Reg]));
    }

    __m256i R8 = _mm256_add_epi32(Sum0, Sum1);
    __m128i R4 = _mm_add_epi32(_mm256_castsi256_si128(R8), _mm256_extractf128_si256(R8, 1));
    __m128i R2 = _mm_add_epi32(R4, _mm_srli_si128(R4, 8));
    __m128i R1 = _mm_add_epi32(R2, _mm_srli_si128(R2, 4));

    Result += _mm_cvtsi128_si32(R1);
#elif defined(USE_NNUE_NEON)
    const int16x8_t ConstZero = vdupq_n_s16(0);
    int32x4_t Sum0 = vdupq_n_s32(0);
    int32x4_t Sum1 = vdupq_n_s32(0);
    const int16x8_t* Accumulator0 = (const int16x8_t*)Board->Accumulator.Accumulator[Board->CurrentColor];
    const int16x8_t* Accumulator1 = (const int16x8_t*)Board->Accumulator.Accumulator[CHANGE_COLOR(Board->CurrentColor)];
    const int16x8_t* Weights0 = (const int16x8_t*)&OutputWeights[0];
    const int16x8_t* Weights1 = (const int16x8_t*)&OutputWeights[HIDDEN_DIMENSION];
    for (int Reg = 0; Reg < NUM_REGS; Reg++) {
        const int16x8_t Acc0 = vmaxq_s16(ConstZero, Accumulator0[Reg]);
        const int16x8_t Acc1 = vmaxq_s16(ConstZero, Accumulator1[Reg]);
        Sum0 = vmlal_s16(Sum0, vget_low_s16(Acc0), vget_low_s16(Weights0[Reg]));
        Sum0 = vmlal_s16(Sum0, vget_high_s16(Acc0), vget_high_s16(Weights0[Reg]));
        Sum1 = vmlal_s16(Sum1, vget_low_s16(Acc1), vget_low_s16(Weights1[Reg]));
        Sum1 = vmlal_s16(Sum1, vget_high_s16(Acc1), vget_high_s16(Weights1[Reg]));
    }
    Result += (I32)vaddvq_s32(vaddq_s32(Sum0, Sum1));
#else
    for (int Index = 0; Index < HIDDEN_DIMENSION; Index++) {
        I16 Acc0 = MAX(0, Board->Accumulator.Accumulator[Board->CurrentColor][Index]); // ReLU
        I16 Acc1 = MAX(0, Board->Accumulator.Accumulator[CHANGE_COLOR(Board->CurrentColor)][Index]); // ReLU

        Result += Acc0 * OutputWeights[Index];
        Result += Acc1 * OutputWeights[HIDDEN_DIMENSION + Index];
    }
#endif // USE_NNUE_AVX2

    return Result / (QUANTIZATION_PRECISION_IN * QUANTIZATION_PRECISION_OUT);
}

int Evaluate(BoardItem* Board, SearchDataItem* SearchData)
{
#ifdef USE_STATISTIC
    if (SearchData != NULL) {
        SearchData->EvaluateCount++;
    }
#endif // USE_STATISTIC

    if (!Board->Accumulator.AccumulatorActual) {
        if (!UpdateAccumulator(Board)) {
            InitAccumulator(Board);
        }
    }

    return (int)OutputLayer(Board);
}