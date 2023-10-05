#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#define LENOF(x) (sizeof(x)/sizeof(x[0]))
#define UNPACK_S(strv) (strv).str, (strv).len

#ifdef DEBUG
	#include <assert.h>
	#ifndef static_assert
		#define static_assert(x, y) _Static_assert(x, y)
	#endif
#else
	#define assert(x) (void) 0
	#define static_assert(x, y)
#endif

#endif
