#ifndef _V_MISC_H_
#define _V_MISC_H_

#if defined(_MSC_VER)
#include "v_misc.msc.h"
#elif defined(__GNUC__)
#include "v_misc.gcc.h"
#else
#error Unsupported platform
#endif

#endif // _V_MISC_H_
