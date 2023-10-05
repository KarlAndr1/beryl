#include "berylscript.h"

#include <math.h>

#define PI 3.14159265359

static struct i_val pow_callback(const struct i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected number as first argument for 'pow'");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_NUMBER) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected number as second argument for 'pow'");
	}
	
	i_float res = pow(beryl_as_num(args[0]), beryl_as_num(args[1]));
	return BERYL_NUMBER(res);
}

#define UNARY_OP(name, fn) \
static struct i_val name##_callback(const struct i_val *args, i_size n_args) { \
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) { \
		beryl_blame_arg(args[0]); \
		return BERYL_ERR("Expected number as first argument for '" #name "'"); \
	} \
	\
	i_float num = beryl_as_num(args[0]); \
	i_float res = fn(num); \
	if(isnan(res)) { \
		beryl_blame_arg(args[0]);\
		return BERYL_ERR("Invalid argument for '" #name "'");\
	} \
	return BERYL_NUMBER(res); \
}
UNARY_OP(sqrt, sqrt)

UNARY_OP(sin, sin)
UNARY_OP(cos, cos)
UNARY_OP(tan, tan)

UNARY_OP(asin, asin)
UNARY_OP(acos, acos)
UNARY_OP(atan, atan)

static struct i_val deg_to_rad_callback(const struct i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected number as first argument for 'deg->rad'");
	}

	i_float num = beryl_as_num(args[0]);
	return BERYL_NUMBER(num / 180 * PI);
}

static struct i_val rad_to_deg_callback(const struct i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected number as first argument for 'rad->deg'");
	}

	i_float num = beryl_as_num(args[0]);
	return BERYL_NUMBER(num / PI * 180);
}

static struct i_val dot_callback(const struct i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected array as first argument for 'dot', got %0");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_ARRAY) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected array as first argument for 'dot', got %0");
	}
	
	i_size len = BERYL_LENOF(args[0]);
	if(BERYL_LENOF(args[1]) != len) {
		beryl_blame_arg(args[0]);
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Can only take the dot product of arrays of equal length (got %0 and %1)");
	}
	
	/*
	struct i_val res;
	if(beryl_get_refcount(args[0]) == 1)
		res = beryl_retain(args[0]);
	else if(beryl_get_refcount(args[1]) == 1)
		res = beryl_retain(args[1]);
	else {
		res = beryl_new_array(len, NULL, len, false);
		if(BERYL_TYPEOF(res) == TYPE_NULL)
			return BERYL_ERR("Out of memory");
	}
	
	i_val *to = (i_val *) beryl_get_raw_array(res); */
	i_float res = 0;
	
	const struct i_val *a_a = beryl_get_raw_array(args[0]);
	const struct i_val *a_b = beryl_get_raw_array(args[1]);
	for(i_size i = 0; i < len; i++) {
		if(BERYL_TYPEOF(a_a[i]) != TYPE_NUMBER || BERYL_TYPEOF(a_b[i]) != TYPE_NUMBER) {
			beryl_blame_arg(args[0]);
			beryl_blame_arg(args[1]);
			return BERYL_ERR("Can only take dot product of arrays consisting of only numbers, got %0 and %1");
		}
		res += beryl_as_num(a_a[i]) * beryl_as_num(a_b[i]);
	}
	
	return BERYL_NUMBER(res);
}

static bool loaded = false;

static struct i_val lib_val;

#define LENOF(a) (sizeof(a)/sizeof(a[0]))

static void init_lib() {
	#define FN(name, arity, fn) { arity, true, name, sizeof(name) - 1, fn }
	#define MANUAL_RELEASE_FN(arity, name, fn) { arity, false, name, sizeof(name) - 1, fn }
	static struct beryl_external_fn fns[] = {
		FN("pow", 2, pow_callback),
		FN("sqrt", 1, sqrt_callback),
		
		FN("sin", 1, sin_callback),
		FN("cos", 1, cos_callback),
		FN("tan", 1, tan_callback),
		FN("asin", 1, asin_callback),
		FN("acos", 1, acos_callback),
		FN("atan", 1, atan_callback),
		
		FN("rad->deg", 1, rad_to_deg_callback),
		FN("deg->rad", 1, deg_to_rad_callback),
		
		FN("dot", 2, dot_callback)
	};
	
	#define CONST(name, val) { name, sizeof(name) - 1, val }
	static struct { const char *name; size_t name_len; struct i_val val; } consts[] = {
		CONST("pi", BERYL_NUMBER(PI))
	};
	
	struct i_val table = beryl_new_table(LENOF(fns) + LENOF(consts), true);
	if(BERYL_TYPEOF(table) == TYPE_NULL) {
		lib_val = BERYL_ERR("Out of memory");
		return;
	}
	
	for(size_t i = 0; i < LENOF(fns); i++) {
		beryl_table_insert(&table, BERYL_STATIC_STR(fns[i].name, fns[i].name_len), BERYL_EXT_FN(&fns[i]), false);
	}
	for(size_t i = 0; i < LENOF(consts); i++) {
		beryl_table_insert(&table, BERYL_STATIC_STR(consts[i].name, consts[i].name_len), consts[i].val, false);
	}
	
	
	lib_val = table;
}

struct i_val beryl_lib_load() {
	if(!loaded) {
		init_lib();
		loaded = true;
	}
	return beryl_retain(lib_val);
}
