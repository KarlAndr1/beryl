#include "../interpreter.h"

#include "lib_utils.h"

#include <math.h>

static i_float i_val_as_float(i_val val) {
	if(BERYL_TYPEOF(val) == TYPE_INT)
		return i_val_get_int(val);
	else if(BERYL_TYPEOF(val) == TYPE_FLOAT)
		return i_val_get_float(val);
	return 0;
}

static i_val sqrt_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_INT && BERYL_TYPEOF(args[0]) != TYPE_FLOAT) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Sqrt argument must be either float or integer");
	}
	//f(x) = x^2 - n. Find where f(x) = 0, i.e wheter x^2 = n
	//f'(x) = 2x
	
	//At each step, x = x - f(x)/f'(x)
	
	i_float num = i_val_as_float(args[0]);
	if(num < 0) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Cannot take square root of negative number");
	}
	i_float g = num;
	for(int i = 0; i < 16; i++) {
		g = g - (g*g - num) / (2 * g);
		if(g * g == num)
			break;
	}
	
	return i_val_float(g);
}

static i_int ii_pow(i_int a, i_int b) {
	if(b == 2)
		return a * a;
	if(b == 1)
		return a;
	if(b == 0)
		return 1;
	
	if(b % 2 == 0) {
		i_int r = ii_pow(a, b/2);
		if(r == I_INT_MAX)
			return r;
		if(I_INT_MAX / r < r)
			return I_INT_MAX;
		return r*r;
	} else {
		i_int r = ii_pow(a, b - 1);
		if(r == I_INT_MAX)
			return r;
		if(I_INT_MAX / r < a)
			return I_INT_MAX;
		return r * a;
	}
}

static i_float fi_pow(i_float f, i_int b) {
	if(b == 2)
		return f * f;
	if(b == 1)
		return f;
	if(b == 0)
		return 0;
	
	if(b % 2 == 0) {
		i_float r = fi_pow(f, b / 2);
		return r*r;
	} else {
		i_float r = fi_pow(f, b - 1);
		return r * f;
	}
}

static i_val pow_callback(const i_val *args, size_t n_args) {
	i_type t = BERYL_TYPEOF(args[1]);
	if(t == TYPE_INT) {
		i_int exp = i_val_get_int(args[1]);
		i_type bt = BERYL_TYPEOF(args[0]);
		if(bt == TYPE_INT) {
			if(i_val_get_int(args[0]) == 0 && exp == 0)
				goto ERR_ZERO;
			if(exp < 0)
				return i_val_int(0);
			i_int res = ii_pow(i_val_get_int(args[0]), exp);
			if(res == I_INT_MAX)
				return STATIC_ERR("Integer overflow");
			return i_val_int(res);
		} else if(bt == TYPE_FLOAT) {
			i_float res = fi_pow(i_val_get_float(args[0]), exp);
			if(res == 0 && exp == 0)
				goto ERR_ZERO;
			if(exp < 0)
				return i_val_float(1.0 / res);
			return i_val_float(res);
		} else {
			beryl_blame_arg(args[0]);
			return STATIC_ERR("Base must be either integer or floating point number");
		}
	} else if(t == TYPE_FLOAT) {
		i_float base = i_val_as_float(args[0]);
		i_float exp = i_val_as_float(args[1]);
		if(base == 0 && exp == 0)
			goto ERR_ZERO;
		return i_val_float(pow(base, exp));
	} else {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Exponent must be either integer or floating point number");
	}
	
	ERR_ZERO:
	return STATIC_ERR("Cannot raise 0 to the zeroth power");
}

#define TRIG_FN(f) static i_val f##_callback(const i_val *args, size_t n_args) { \
	i_type t = BERYL_TYPEOF(args[0]); \
	if(t != TYPE_INT && t != TYPE_FLOAT) { \
		beryl_blame_arg(args[0]); \
		return STATIC_ERR("Can only take " #f " of numbers"); \
	} \
	return i_val_float(f(i_val_as_float(args[0]))); \
}

TRIG_FN(sin)
TRIG_FN(cos)
TRIG_FN(tan)

TRIG_FN(asin)
TRIG_FN(acos)
TRIG_FN(atan)

LIB_FNS(fns) = {
	FN("sqrt", sqrt_callback, 1),
	FN("^", pow_callback, 2),
	
	FN("cos", cos_callback, 1),
	FN("sin", sin_callback, 1),
	FN("tan", tan_callback, 1),
	
	FN("acos", acos_callback, 1),
	FN("asin", asin_callback, 1),
	FN("atan", atan_callback, 1)
};

void math_lib_load() {
	LOAD_FNS(fns);
	
	CONSTANT("pi", i_val_float(3.14159265359));
}
