#ifndef _V_MISC_WIN32_H_
#define _V_MISC_WIN32_H_

#include "v_types.h"

#define barrier() MemoryBarrier()

#define inline __forceinline

#define restrict __restrict

unsigned short __popcnt16(unsigned short value);
unsigned int __popcnt(unsigned int value);
unsigned __int64 __popcnt64(unsigned __int64 value);

static inline u16 popcnt16(u16 value) {
	return __popcnt16(value);
}

static inline u32 popcnt32(u32 value) {
	return __popcnt(value);
}

static inline u64 popcnt64(u64 value) {
	return __popcnt64(value);
}

#endif // _V_MISC_WIN32_H_
