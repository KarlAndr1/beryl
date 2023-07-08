#include "../interpreter.h"
#include "lib_utils.h"
#include "../utils.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

static i_val get_refcount_callback(const i_val *args, size_t n_args) {
	i_refc refs = beryl_get_references(args[0]);
	if(refs > I_INT_MAX)
		return i_val_int(-1);
	
	return i_val_int(refs);
}

static i_val ptrof_callback(const i_val *args, size_t n_args) {
	const void *ptr;

	switch(args[0].type) {
		case TYPE_STR:
			if(i_val_get_len(args[0]) <= I_PACKED_STR_MAX_LEN)
				ptr = NULL;
			else
				ptr = i_val_get_raw_str(&args[0]);
			break;
		case TYPE_ARRAY:
			ptr = i_val_get_raw_array(args[0]);
			break;
		default:
			if(args[0].type < BERYL_N_STATIC_TYPES)
				return I_NULL;
			ptr = i_val_get_ptr(args[0]);
	}
	
	uintptr_t ptr_int = (uintptr_t) ptr;
	if(ptr_int > I_INT_MAX)
		return i_val_int(-1);
	return i_val_int(ptr_int);
}

static i_val capof_callback(const i_val *args, size_t n_args) {
	i_size cap = i_val_get_cap(args[0]);
	if(cap > I_INT_MAX)
		return i_val_int(-1);
	return i_val_int(cap);
}

LIB_FNS(fns) = {
	FN("refcount", get_refcount_callback, 1),
	FN("ptrof", ptrof_callback, 1),
	FN("capof", capof_callback, 1)
};

#ifdef __unix__
	#define PLATFORM "unix"
#endif
#ifdef __win__
	#define PLATFORM "windows"
#endif

#ifndef PLATFORM
	#define PLATFORM "other"
#endif

#ifdef __i386__
	#define ARCH "x86"
#endif
#ifdef __x86_64__
	#define ARCH "x64"
#endif
#ifdef __arm__
	#define ARCH "arm"
#endif

#ifndef ARCH
	#define ARCH "other"
#endif

static i_val platform_info[2];

void debug_lib_load() {
	
	platform_info[0] = i_val_static_str(PLATFORM, sizeof(PLATFORM) - 1);
	platform_info[1] = i_val_static_str(ARCH, sizeof(ARCH) - 1);
		
	LOAD_FNS(fns);
	CONSTANT("ptrsize", i_val_int(sizeof(void *)));
	//CONSTANT("platform", i_val_static_str(PLATFORM, sizeof(PLATFORM) - 1));
	CONSTANT("platform", i_val_static_array(platform_info, LENOF(platform_info)));
	CONSTANT("bytesize", i_val_int(CHAR_BIT));
	CONSTANT("valsize", i_val_int(sizeof(i_val)));
	
	CONSTANT("int-max", i_val_int(I_INT_MAX));
	CONSTANT("int-min", i_val_int(I_INT_MIN));
}
