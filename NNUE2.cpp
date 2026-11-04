// NNUE2.cpp

#include "stdafx.h"

#include "NNUE2.h"

#include "BitBoard.h"
#include "Board.h"
#include "Def.h"
#include "Types.h"
#include "Utils.h"

#define NNUE_FILE_MAGIC             ('B' | 'R' << 8 | 'K' << 16 | 'R' << 24)
#define NNUE_FILE_SIZE              ((INPUT_DIMENSION * HIDDEN_DIMENSION + HIDDEN_DIMENSION + HIDDEN_DIMENSION * 2 + 1) * 4 + 12)

#define NNUE_HASH(Hash, Value)      (((Hash) + (324723947ULL + (Value))) ^ 93485734985ULL)

#define STM                         0
#define XSTM                        1

#ifdef USE_NNUE_AVX2
#define NUM_REGS                    (HIDDEN_DIMENSION * sizeof(I16) / sizeof(__m256i))
#elif defined(USE_NNUE_NEON)
#define NUM_REGS                    (HIDDEN_DIMENSION / 8)
#endif // USE_NNUE_NEON

#ifdef USE_NNUE_AVX2
#define NUM_REGS                    (HIDDEN_DIMENSION * sizeof(I16) / sizeof(__m256i))
#endif // USE_NNUE_AVX2

_declspec(align(64)) I16 InputWeights[INPUT_DIMENSION * HIDDEN_DIMENSION];
_declspec(align(64)) I16 InputBiases[HIDDEN_DIMENSION];
_declspec(align(64)) I16 OutputWeights[HIDDEN_DIMENSION * 2];
I32 OutputBias;

BOOL NnueFileLoaded = FALSE;

#if defined(__ANDROID__)
extern "C" {
extern const unsigned char rukchess_embedded_nnue_start[];
extern const unsigned char rukchess_embedded_nnue_end[];
}
#endif

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

    // Feature weights
    for (int Index = 0; Index < INPUT_DIMENSION * HIDDEN_DIMENSION; Index++) {
        memcpy(&Value, Data + Offset, sizeof(Value));
        Offset += sizeof(Value);
#ifdef PRINT_MIN_MAX_VALUES
        MinValue = MIN(MinValue, Value);
        MaxValue = MAX(MaxValue, Value);
#endif // PRINT_MIN_MAX_VALUES
        NnueHash = NNUE_HASH(NnueHash, (int)Value);
        InputWeights[Index] = LoadInt16(Value, QUANTIZATION_PRECISION_IN);
    }
#ifdef PRINT_MIN_MAX_VALUES
    printf("Input weights: MinValue = %f MaxValue = %f\n", MinValue, MaxValue);
    MinValue = FLT_MAX;
    MaxValue = -FLT_MAX;
#endif // PRINT_MIN_MAX_VALUES

    // Hidden biases
    for (int Index = 0; Index < HIDDEN_DIMENSION; Index++) {
        memcpy(&Value, Data + Offset, sizeof(Value));
        Offset += sizeof(Value);
#ifdef PRINT_MIN_MAX_VALUES
        MinValue = MIN(MinValue, Value);
        MaxValue = MAX(MaxValue, Value);
#endif // PRINT_MIN_MAX_VALUES
        NnueHash = NNUE_HASH(NnueHash, (int)Value);
        InputBiases[Index] = LoadInt16(Value, QUANTIZATION_PRECISION_IN);
    }
#ifdef PRINT_MIN_MAX_VALUES
    printf("Input biases: MinValue = %f MaxValue = %f\n", MinValue, MaxValue);
    MinValue = FLT_MAX;
    MaxValue = -FLT_MAX;
#endif // PRINT_MIN_MAX_VALUES

    // Output weights
    for (int Index = 0; Index < HIDDEN_DIMENSION * 2; Index++) {
        memcpy(&Value, Data + Offset, sizeof(Value));
        Offset += sizeof(Value);
#ifdef PRINT_MIN_MAX_VALUES
        MinValue = MIN(MinValue, Value);
        MaxValue = MAX(MaxValue, Value);
#endif // PRINT_MIN_MAX_VALUES
        NnueHash = NNUE_HASH(NnueHash, (int)Value);
        OutputWeights[Index] = LoadInt16(Value, QUANTIZATION_PRECISION_OUT);
    }
#ifdef PRINT_MIN_MAX_VALUES
    printf("Output weights: MinValue = %f MaxValue = %f\n", MinValue, MaxValue);
    MinValue = FLT_MAX;
    MaxValue = -FLT_MAX;
#endif // PRINT_MIN_MAX_VALUES

    // Output bias
    memcpy(&Value, Data + Offset, sizeof(Value));
    Offset += sizeof(Value);
    NnueHash = NNUE_HASH(NnueHash, (int)Value);
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
        __m256i* AccumulatorTile = (__m256i*)Board->Accumulator.Accumulator[Perspective];
        const __m256i* Weights = (const __m256i*)&InputWeights[WeightIndex * HIDDEN_DIMENSION];
        for (int Reg = 0; Reg < NUM_REGS; Reg++) {
            AccumulatorTile[Reg] = _mm256_add_epi16(AccumulatorTile[Reg], Weights[Reg]);
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
#endif // NNUE accumulator add
    }
}

void AccumulatorSub(BoardItem* Board, int Square, int PieceWithColor)
{
    for (int Perspective = 0; Perspective < 2; Perspective++) { // STM/XSTM
        int WeightIndex = CalculateWeightIndex(Perspective, Square, PieceWithColor);

#ifdef USE_NNUE_AVX2
        __m256i* AccumulatorTile = (__m256i*)Board->Accumulator.Accumulator[Perspective];
        const __m256i* Weights = (const __m256i*)&InputWeights[WeightIndex * HIDDEN_DIMENSION];
        for (int Reg = 0; Reg < NUM_REGS; Reg++) {
            AccumulatorTile[Reg] = _mm256_sub_epi16(AccumulatorTile[Reg], Weights[Reg]);
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
#endif // NNUE accumulator sub
    }
}

void InitAccumulator(BoardItem* Board)
{
    U64 Pieces;

    int Square;
    int PieceWithColor;

    memcpy(Board->Accumulator.Accumulator[STM], InputBiases, sizeof(Board->Accumulator.Accumulator[STM]));
    memcpy(Board->Accumulator.Accumulator[XSTM], InputBiases, sizeof(Board->Accumulator.Accumulator[XSTM]));

    Pieces = (Board->BB_WhitePieces | Board->BB_BlackPieces);

    while (Pieces) {
        Square = LSB(Pieces);
        PieceWithColor = Board->Pieces[Square];

        AccumulatorAdd(Board, Square, PieceWithColor);

        Pieces &= Pieces - 1;
    }
}

I32 OutputLayer(BoardItem* Board)
{
    I32 Result = OutputBias;

#ifdef USE_NNUE_AVX2
    const __m256i ConstZero = _mm256_setzero_si256();

#ifdef USE_CRELU
    const __m256i ConstMax = _mm256_set1_epi16(QUANTIZATION_PRECISION_IN);
#endif // USE_CRELU

    __m256i Sum0 = ConstZero;
    __m256i Sum1 = ConstZero;

    __m256i* AccumulatorTile0 = (__m256i*)Board->Accumulator.Accumulator[Board->CurrentColor];
    __m256i* AccumulatorTile1 = (__m256i*)Board->Accumulator.Accumulator[CHANGE_COLOR(Board->CurrentColor)];

    __m256i* Weights0 = (__m256i*)&OutputWeights;
    __m256i* Weights1 = (__m256i*)&OutputWeights[HIDDEN_DIMENSION];

    for (int Reg = 0; Reg < NUM_REGS; Reg++) {
#ifdef USE_CRELU
        const __m256i Acc0 = _mm256_max_epi16(ConstZero, _mm256_min_epi16(ConstMax, AccumulatorTile0[Reg])); // CReLU
        const __m256i Acc1 = _mm256_max_epi16(ConstZero, _mm256_min_epi16(ConstMax, AccumulatorTile1[Reg])); // CReLU
#else
        const __m256i Acc0 = _mm256_max_epi16(ConstZero, AccumulatorTile0[Reg]); // ReLU
        const __m256i Acc1 = _mm256_max_epi16(ConstZero, AccumulatorTile1[Reg]); // ReLU
#endif // USE_CRELU

        Sum0 = _mm256_add_epi32(Sum0, _mm256_madd_epi16(Acc0, Weights0[Reg]));
        Sum1 = _mm256_add_epi32(Sum1, _mm256_madd_epi16(Acc1, Weights1[Reg]));
    }

    const __m256i R8 = _mm256_add_epi32(Sum0, Sum1);
    const __m128i R4 = _mm_add_epi32(_mm256_castsi256_si128(R8), _mm256_extractf128_si256(R8, 1));
    const __m128i R2 = _mm_add_epi32(R4, _mm_srli_si128(R4, 8));
    const __m128i R1 = _mm_add_epi32(R2, _mm_srli_si128(R2, 4));

    Result += _mm_cvtsi128_si32(R1);
#elif defined(USE_NNUE_NEON)
    const int16x8_t ConstZero = vdupq_n_s16(0);
#ifdef USE_CRELU
    const int16x8_t ConstMax = vdupq_n_s16(QUANTIZATION_PRECISION_IN);
#endif // USE_CRELU

    int32x4_t Sum0 = vdupq_n_s32(0);
    int32x4_t Sum1 = vdupq_n_s32(0);
    const int16x8_t* AccumulatorTile0 = (const int16x8_t*)Board->Accumulator.Accumulator[Board->CurrentColor];
    const int16x8_t* AccumulatorTile1 = (const int16x8_t*)Board->Accumulator.Accumulator[CHANGE_COLOR(Board->CurrentColor)];
    const int16x8_t* Weights0 = (const int16x8_t*)&OutputWeights[0];
    const int16x8_t* Weights1 = (const int16x8_t*)&OutputWeights[HIDDEN_DIMENSION];

    for (int Reg = 0; Reg < NUM_REGS; Reg++) {
#ifdef USE_CRELU
        const int16x8_t Acc0 = vminq_s16(ConstMax, vmaxq_s16(ConstZero, AccumulatorTile0[Reg]));
        const int16x8_t Acc1 = vminq_s16(ConstMax, vmaxq_s16(ConstZero, AccumulatorTile1[Reg]));
#else
        const int16x8_t Acc0 = vmaxq_s16(ConstZero, AccumulatorTile0[Reg]);
        const int16x8_t Acc1 = vmaxq_s16(ConstZero, AccumulatorTile1[Reg]);
#endif // USE_CRELU
        Sum0 = vmlal_s16(Sum0, vget_low_s16(Acc0), vget_low_s16(Weights0[Reg]));
        Sum0 = vmlal_s16(Sum0, vget_high_s16(Acc0), vget_high_s16(Weights0[Reg]));
        Sum1 = vmlal_s16(Sum1, vget_low_s16(Acc1), vget_low_s16(Weights1[Reg]));
        Sum1 = vmlal_s16(Sum1, vget_high_s16(Acc1), vget_high_s16(Weights1[Reg]));
    }
    Result += (I32)vaddvq_s32(vaddq_s32(Sum0, Sum1));
#else
    for (int Index = 0; Index < HIDDEN_DIMENSION; Index++) {
#ifdef USE_CRELU
        const I16 Acc0 = MAX(0, MIN(QUANTIZATION_PRECISION_IN, Board->Accumulator.Accumulator[Board->CurrentColor][Index])); // CReLU
        const I16 Acc1 = MAX(0, MIN(QUANTIZATION_PRECISION_IN, Board->Accumulator.Accumulator[CHANGE_COLOR(Board->CurrentColor)][Index])); // CReLU
#else
        const I16 Acc0 = MAX(0, Board->Accumulator.Accumulator[Board->CurrentColor][Index]); // ReLU
        const I16 Acc1 = MAX(0, Board->Accumulator.Accumulator[CHANGE_COLOR(Board->CurrentColor)][Index]); // ReLU
#endif // USE_CRELU

        Result += Acc0 * OutputWeights[Index];
        Result += Acc1 * OutputWeights[HIDDEN_DIMENSION + Index];
    }
#endif // USE_NNUE_AVX2

    return (Result * SCALE) / (QUANTIZATION_PRECISION_IN * QUANTIZATION_PRECISION_OUT);
}

int Evaluate(BoardItem* Board, SearchDataItem* SearchData)
{
#ifdef USE_STATISTIC
    if (SearchData) {
        SearchData->EvaluateCount++;
    }
#endif // USE_STATISTIC

    return (int)OutputLayer(Board);
}