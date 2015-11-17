#ifndef _V_MISC_H_
#define _V_MISC_H_

#if defined(_MSC_VER)
#include "v_misc.msc.h"
#elif defined(__GNUC__)
#include "v_misc.gcc.h"
#else
#error Unsupported platform
#endif

static inline u16 popcnt16(u16 value);
static inline u32 popcnt32(u32 value);
static inline u64 popcnt64(u64 value);

#endif // _V_MISC_H_
