// Types.h

#pragma once

#ifndef TYPES_H
#define TYPES_H

#include "Def.h"

#define BOOL                int

#define FALSE               0
#define TRUE                1

#define INF                 10000

#if defined(_WIN32)
typedef __int8              I8;
typedef __int16             I16;
typedef __int32             I32;
typedef __int64             I64;
typedef unsigned __int8     U8;
typedef unsigned __int16    U16;
typedef unsigned __int64    U64;
#else
#include <stdint.h>
typedef int8_t              I8;
typedef int16_t             I16;
typedef int32_t             I32;
typedef long long           I64;
typedef uint8_t             U8;
typedef uint16_t            U16;
typedef unsigned long long  U64;
#endif

#endif // !TYPES_H