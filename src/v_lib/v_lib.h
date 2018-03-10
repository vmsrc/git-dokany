#ifndef _V_LIB_H_
#define _V_LIB_H_

#include "v_types.h"
#include "v_misc.h"

// TODO v_assert, v_check, v_debug ...

#if !defined(_WIN32) && !defined(__unix__) && defined(__unix)
#define __unix__
#endif

void v_lib_init(void);

#endif // _V_LIB_H_
