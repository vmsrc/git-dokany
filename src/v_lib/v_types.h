#ifndef _V_TYPES_H_
#define _V_TYPES_H_

// don't include stdint or limits - they include too much random crap, at least on windows
#include <stdbool.h>

typedef unsigned char u8;
typedef signed char s8;

typedef unsigned short u16;
typedef signed short s16;

typedef unsigned u32;
typedef int s32;

typedef unsigned long long u64;
typedef signed long long s64;

#endif // _V_TYPES_H_
