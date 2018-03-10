#ifndef _V_MISC_GCC_H_
#define _V_MISC_GCC_H_

#include "v_types.h"

#define barrier() __sync_synchronize()

#define inline __attribute__((always_inline))

#define restrict __restrict

#endif // _V_MISC_GCC_H_
