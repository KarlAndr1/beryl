#include "../interpreter.h"
#include "lib_utils.h"
#include "../utils.h"

static i_int safe_sum(i_int a, i_int b, int *err) {
	*err = 0;
	if(a >= 0) {
		if(b >= 0) {
			if(I_INT_MAX - a < b) {
				*err = 1;
				return 0;
			}
		} //if a >= 0 && b <= 0 then a + b > I_INT_MIN, because b >= I_INT_MIN
	} else {
		if(b < 0) {
			if(I_INT_MIN - a > b) {
				*err = 1;
				return 0;
			}
		}
	}
	return a + b;
}

static i_int safe_sub(i_int a, i_int b, int *err) {
	if(b < -I_INT_MAX) {
		*err = 1;
		return 0;
	}
	return safe_sum(a, -b, err);
}

static i_int safe_div(i_int a, i_int b, int *err) {
	if(a == I_INT_MIN && b == -1) {
		*err = 1;
		return 0;
	}
	*err = 0;
	return a / b;
}

static i_int safe_mul(i_int a, i_int b, int *err) {
	static_assert(I_INT_MIN <= -I_INT_MAX, "The absolute minimum value should be larger than the maximum value");
	
	if(a == 0 || b == 0) {
		*err = 0;
		return 0;
	}
	
	i_uint a_u;
	if(a <= -I_INT_MAX)
		a_u = a;
	else
		a_u = a < 0 ? -a : a;
	
	i_uint b_u;
	
	if(b <= -I_INT_MAX)
		b_u = b;
	else
		b_u = b < 0 ? -b : b;
	
	bool sign = (a < 0) ^ (b < 0);
	
	i_uint max = I_INT_MAX;
	if(max / a_u < b_u) {
		*err = 1;
		return 0;
	}
	*err = 0;
	i_uint res = a_u * b_u;

	return sign ? -((i_int) res) : (i_int) res;
}

#define OP_CALLBACK(name, op, safe_op, opt, check_v) \
static struct i_val name(const struct i_val *args, size_t n_args) { \
	i_int int_res; \
	i_float float_res; \
	bool is_float = false; \
	if(BERYL_TYPEOF(args[0]) == TYPE_INT) \
		int_res = i_val_get_int(args[0]); \
	else if(BERYL_TYPEOF(args[0]) == TYPE_FLOAT) { \
		is_float = true; \
		float_res = i_val_get_float(args[0]); \
	} else { \
		beryl_blame_arg(args[0]); \
		goto ERR; \
	} \
	opt; \
	for(size_t i = 1; i < n_args; i++) { \
		if(BERYL_TYPEOF(args[i]) == TYPE_INT) { \
			i_int val = i_val_get_int(args[i]); \
			check_v; \
			if(is_float) \
				float_res op##= val; \
			else { \
				int err; \
				int_res = safe_op(int_res, val, &err); \
				if(err) \
					return STATIC_ERR("Integer out of bounds (overflow/underflow)"); \
			} \
		} else if(BERYL_TYPEOF(args[i]) == TYPE_FLOAT) { \
			i_float val = i_val_get_float(args[i]); \
			check_v; \
			if(!is_float) { \
				is_float = true; \
				float_res = int_res; \
			} \
			float_res op##= val; \
		} else { \
			beryl_blame_arg(args[i]);\
			goto ERR; \
		} \
	} \
	if(is_float) \
		return i_val_float(float_res); \
	return i_val_int(int_res); \
	\
	ERR: \
	return STATIC_ERR("Function '" #op "' only accepts integer and float values"); \
}

OP_CALLBACK(add_callback, +, safe_sum, ,)
OP_CALLBACK(sub_callback, -, safe_sub, if(n_args == 1) return is_float ? i_val_float(-float_res) : i_val_int(-int_res);, )
OP_CALLBACK(mul_callback, *, safe_mul, ,)
OP_CALLBACK(div_callback, /, safe_div, , if(val == 0) return STATIC_ERR("Division by zero"))

static struct i_val if_callback(const struct i_val *args, size_t n_args) {
	for(size_t i = 0; i < n_args - 1; i += 2) {
		i_val cond = args[i];
		i_val do_fn = args[i+1];
		
		if(cond.type != TYPE_BOOL) {
			beryl_blame_arg(cond);
			return STATIC_ERR("If condition must be boolean");
		} 
		if(i_val_get_bool(cond))
			return beryl_call_fn(do_fn, NULL, 0, BERYL_ERR_PROP);
	}
	
	if(n_args % 2 == 1) {
		return beryl_call_fn(args[n_args - 1], NULL, 0, BERYL_ERR_PROP);
	}
	
	return I_NULL;
}

static struct i_val for_callback(const struct i_val *args, size_t n_args) {
	if(n_args != 3 && n_args != 4)
		return STATIC_ERR("For loop takes either 3 or 4 arguments");
	
	if(BERYL_TYPEOF(args[0]) != TYPE_INT) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument must be integer");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_INT) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Second argument must be integer");
	}

	i_int from = i_val_get_int(args[0]);
	i_int to = i_val_get_int(args[1]);
	
	i_int by = 1;
	if(n_args == 4) {
		by = i_val_get_int(args[2]);
	} else if(from > to) {
		by = -1;
	}
	
	i_val res = I_NULL;
	for(i_int i = from; i < to; i += by) {
		beryl_release(res);
		i_val arg = i_val_int(i);
		res = beryl_call_fn(args[n_args - 1], &arg, 1, BERYL_ERR_PROP);
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			return res;
	}
	return res;
}

static struct i_val loop_callback(const struct i_val *args, size_t n_args) {
	i_val cont = I_NULL;
	do { //The loop function continually calls the provided function until it returns a non-null value
		beryl_release(cont);
		cont = beryl_call_fn(args[0], NULL, 0, BERYL_ERR_PROP);
		if(BERYL_TYPEOF(cont) == TYPE_ERR)
			return cont;
		if(BERYL_TYPEOF(cont) != TYPE_BOOL) {
			beryl_blame_arg(cont);
			return STATIC_ERR("Loop condition must be boolean");
		
		}
	} while(i_val_get_bool(cont));

	return I_NULL;
}

static struct i_val forevery_callback(const i_val *args, size_t n_args) {
	i_val fn = args[n_args - 1];
	
	i_val res = I_NULL;
	for(size_t i = 0; i < n_args - 1; i++) {
		beryl_release(res);
		res = beryl_call_fn(fn, &args[i], 1, BERYL_ERR_PROP);
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			return res;
	}
	
	return res;
}

#define CMP_OP_CALLBACK(name, cmp, expr) \
static i_val name(const i_val *args, size_t n_args) { \
	struct i_val prev = args[0]; \
	for(size_t i = 1; i < n_args; i++) { \
		i_val next = args[i]; \
		if(prev.type != next.type) { beryl_blame_arg(prev); beryl_blame_arg(args[i]); return STATIC_ERR("Comparing values of different types"); } \
		cmp; \
		if(!(expr)) \
			return i_val_bool(0); \
		prev = next; \
	} \
	return i_val_bool(1); \
}

CMP_OP_CALLBACK(eq_callback, bool are_eq = i_val_eq(prev, next), are_eq)
CMP_OP_CALLBACK(not_eq_callback, bool are_eq = i_val_eq(prev, next), !are_eq)
CMP_OP_CALLBACK(less_callback, int cmp = i_val_cmp(prev, next), cmp == 1)
CMP_OP_CALLBACK(greater_callback, int cmp = i_val_cmp(prev, next), cmp == -1)
CMP_OP_CALLBACK(less_eq_callback, int cmp = i_val_cmp(prev, next), cmp == 1 || cmp == 0)
CMP_OP_CALLBACK(greater_eq_callback, int cmp = i_val_cmp(prev, next), cmp == -1 || cmp == 0)

static i_val lenient_eq_callback(const i_val *args, size_t n_args) {
	struct i_val first = args[0];
	for(size_t i = 1; i < n_args; i++) {
		if(args[i].type != first.type)
			return i_val_bool(0);
		if(!i_val_eq(first, args[i]))
			return i_val_bool(0);
	}
	return i_val_bool(1);
}

static struct i_val assert_callback(const struct i_val *args, size_t n_args) {
	for(size_t i = 0; i < n_args; i++) {
		if(BERYL_TYPEOF(args[i]) != TYPE_BOOL) {
			beryl_blame_arg(args[i]);
			return STATIC_ERR("Assert only accepts boolean values");
		}
		if(!i_val_get_bool(args[i]))
			return STATIC_ERR("Assertion failed");
	}
	return I_NULL;
}

static struct i_val not_callback(const struct i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_BOOL)
		return STATIC_ERR("'not' only accepts boolean values");
	
	return i_val_bool(!i_val_get_bool(args[0]));
}

#define BOOL_OP_CALLBACK(name, displayname, op) \
static struct i_val name(const struct i_val *args, size_t n_args) { \
	if(BERYL_TYPEOF(args[0]) != TYPE_BOOL || BERYL_TYPEOF(args[1]) != TYPE_BOOL) \
		return STATIC_ERR("'" displayname "' only accepts boolean values"); \
	return i_val_bool(i_val_get_bool(args[0]) op i_val_get_bool(args[1])); \
}

BOOL_OP_CALLBACK(and_callback, "and", &&)
BOOL_OP_CALLBACK(or_callback, "or", ||)

static i_val try_callback(const i_val *args, size_t n_args) {
	if(n_args != 2 && n_args != 1)
		return STATIC_ERR("Try function takes either 1 or 2 arguments");

	i_val res = beryl_call_fn(args[0], NULL, 0, BERYL_ERR_CATCH);
	if(res.type != TYPE_ERR)
		return res;
	
	i_val err_msg = res;
	err_msg.type = TYPE_STR;
	
	if(n_args == 2) {
		i_val catch_res = beryl_call_fn(args[1], &err_msg, 1, BERYL_ERR_PROP);
		beryl_release(err_msg);
		return catch_res;
	} else {
		beryl_release(err_msg);
		return I_NULL;
	}
}

static i_val error_callback(const i_val *args, size_t n_args) {
	if(args[0].type != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Error argument must be string");
	}
	
	i_val err = args[0];
	err.type = TYPE_ERR;
	beryl_retain(err);
	return err;
}

static i_val sizeof_callback(const i_val *args, size_t n_args) {
	i_size size;
	switch(args[0].type) {
		case TYPE_ARRAY:
		case TYPE_STR:
			//Not the "true" length for strings, but the length in bytes. TODO: In some other library, implement UTF-8 compliant size/length function for strings
			size = i_val_get_len(args[0]);
			break;
		
		default: {
			ti_interface_callback sizeof_callback = beryl_interface_val(args[0], TI_SIZEOF);
			if(sizeof_callback == NULL)
				return i_val_int(1);

			sizeof_callback(args[0].val.ptr, &size);
		} break;
	}
	
	if(size > I_INT_MAX)
		return STATIC_ERR("Size too large");
	
	return i_val_int(size);
}

static i_val invoke_callback(const i_val *args, size_t n_args) {
	return beryl_call_fn(args[0], NULL, 0, BERYL_ERR_PROP);
}


static i_val return_callback(const i_val *args, size_t n_args) {
	switch(n_args) {
		case 0:
			beryl_set_return_val(I_NULL);
			return I_EARLY_RETURN;
		
		case 1:
			beryl_set_return_val(args[0]);
			return I_EARLY_RETURN;
		
		case 2:
			if(BERYL_TYPEOF(args[1]) != TYPE_BOOL)
				return STATIC_ERR("Return condition must be boolean type");

			if(i_val_get_bool(args[1])) {
				beryl_set_return_val(args[0]);
				return I_EARLY_RETURN;
			} else
				return I_NULL;
		
		default:
			return STATIC_ERR("Return accepts either 1, 2 or 3 arguments");
	}
}

LIB_FNS(fns) = {
	FN("if", if_callback, -3),
	FN("for", for_callback, -4),
	FN("loop", loop_callback, 1),
	FN("forevery", forevery_callback, -3),
	
	FN("+", add_callback, -3),
	FN("-", sub_callback, -2),
	FN("*", mul_callback, -3),
	FN("/", div_callback, -3),

	FN("==", eq_callback, -3),
	FN("/=", not_eq_callback, -3),
	FN("not", not_callback, 1),
	FN("or?", or_callback, 2),
	FN("and?", and_callback, 2),
	FN("<", less_callback, -3),
	FN(">", greater_callback, -3),
	FN("<=", less_eq_callback, -3),
	FN(">=", greater_eq_callback, -3),
	
	FN("~=", lenient_eq_callback, -3),
	
	FN("assert", assert_callback, -1),
	
	FN("try", try_callback, -2),
	FN("error", error_callback, 1),
	
	FN("sizeof", sizeof_callback, 1),
	
	FN("invoke", invoke_callback, 1),
	FN("return", return_callback, -1)
};

void core_lib_load() {
	LOAD_FNS(fns);
	CONSTANT("true", i_val_bool(1));
	CONSTANT("false", i_val_bool(0));
	CONSTANT("null", I_NULL);
	CONSTANT("empty-array", i_val_static_array(NULL, 0));
}
