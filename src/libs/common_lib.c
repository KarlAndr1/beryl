#include "../interpreter.h"
#include "lib_utils.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

struct eval_record {
	i_val str;
	struct eval_record *next;
};

static struct eval_record *evals;

static i_val eval_callback(const i_val *args, size_t n_args) {
	if(args[0].type != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Cannot eval non-string values");
	}
	
	struct eval_record *new_eval = malloc(sizeof(struct eval_record));
	if(new_eval == NULL)
		return STATIC_ERR("Out of memory");
	
	new_eval->str = args[0];
	new_eval->next = evals;
	evals = new_eval;
	
	beryl_retain(args[0]);
	return beryl_eval(i_val_get_raw_str(&new_eval->str), i_val_get_len(new_eval->str), true, BERYL_ERR_PROP);
}

static i_val concat_callback(const i_val *args, size_t n_args) {
	size_t total_length = 0;
	for(size_t i = 0; i < n_args; i++) {
		if(args[i].type != TYPE_STR) {
			beryl_blame_arg(args[i]);
			return STATIC_ERR("Can only concatenate strings");
		}
		total_length += i_val_get_len(args[i]);
	}
	
	if(total_length > I_SIZE_MAX)
		return STATIC_ERR("Resulting string is too large");
	
	i_val res_str = i_val_managed_str(NULL, total_length);
	if(BERYL_TYPEOF(res_str) == TYPE_NULL)
		return STATIC_ERR("Out of memory");
	
	char *p = (char *) i_val_get_raw_str(&res_str);
	for(size_t i = 0; i < n_args; i++) {
		i_size len = i_val_get_len(args[i]);
		memcpy(p, i_val_get_raw_str(&args[i]), len);
		p += len;
		//assert(p - res_str.str <= total_length);
	}
	
	return res_str;
}

static i_val typeof_callback(const i_val *args, size_t n_args) {
	switch(BERYL_TYPEOF(args[0])) {
		case TYPE_INT:
			return I_STATIC_STR("int");
		case TYPE_FLOAT:
			return I_STATIC_STR("float");
		case TYPE_STR:
			return I_STATIC_STR("string");
		case TYPE_BOOL:
			return I_STATIC_STR("bool");
		case TYPE_NULL:
			return I_STATIC_STR("null");
		case TYPE_ARRAY:
			return I_STATIC_STR("array");
		case TYPE_FN:
		case TYPE_EXT_FN:
			return I_STATIC_STR("function");
		
		default: {
			ti_interface_callback typename_fn = beryl_interface_val(args[0], TI_TYPENAME);
			if(typename_fn == NULL)
				return I_STATIC_STR("unknown");
			const char *typename;
			i_size typename_len;
			typename_fn(&typename, &typename_len);
			return i_val_static_str(typename, typename_len);
		}
	}
}

/*
static i_val apply_left_callback(const i_val *args, size_t n_args) {
	return int_call_fn(args[0], &args[1], 1, INT_ERR_PROP);
}

static i_val apply_right_callback(const i_val *args, size_t n_args) {
	return int_call_fn(args[1], &args[0], 1, INT_ERR_PROP);
}
*/

static i_val apply_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Can only apply array values");
	}
	return beryl_call_fn(args[1], i_val_get_raw_array(args[0]), i_val_get_len(args[0]), BERYL_ERR_PROP);
}

/*
static i_val implies_callback(const i_val *args, size_t n_args) {
	for(size_t i = 0; i < 2; i++) {
		if(BERYL_TYPEOF(args[i]) != TYPE_BOOL) {
			beryl_blame_arg(args[i]);
			return STATIC_ERR("Implies? only accepts boolean values");
		}
	}
	
	return i_val_bool(!i_val_get_bool(args[0]) || i_val_get_bool(args[1]));
}
*/

static i_val max_callback(const i_val *args, size_t n_args) {
	i_val max_v = args[0];
	for(size_t i = 1; i < n_args; i++) {
		i_val arg = args[i];
		int cmp = i_val_cmp(max_v, arg);
		
		if(cmp == 2) {
			beryl_blame_arg(max_v);
			beryl_blame_arg(arg);
			return STATIC_ERR("Incomparable values");
		}
		
		if(cmp == 1)
			max_v = arg;
	}
	
	return beryl_retain(max_v);
}

static i_val min_callback(const i_val *args, size_t n_args) {
	i_val min_v = args[0];
	for(size_t i = 1; i < n_args; i++) {
		i_val arg = args[i];
		int cmp = i_val_cmp(min_v, arg);
		
		if(cmp == 2) {
			beryl_blame_arg(min_v);
			beryl_blame_arg(arg);
			return STATIC_ERR("Incomparable values");
		}
		
		if(cmp == -1)
			min_v = arg;
	}
	
	return beryl_retain(min_v);
}

int char_as_digit(char c) {
	if(c >= '0' && c <= '9')
		return c - '0';
	return -1;
}

static i_val parse_int_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Expected string as argument");
	}
	
	i_size len = i_val_get_len(args[0]);
	const char *str = i_val_get_raw_str(&args[0]);
	const char *end = str + len;
	
	while(str != end && char_as_digit(*str) == -1 && *str != '-')
		str++;
	
	bool negative = false;
	if(str != end && *str == '-') {
		negative = true;
		str++;
	}
	
	i_int val = 0;
	for(const char *c = str; c != end; c++) {
		int digit = char_as_digit(*c);
		if(digit == -1)
			break;
			
		if(val != 0 && I_INT_MAX / val < 10)
			goto ERR;
		val *= 10;
		
		if(I_INT_MAX - val < digit)
			goto ERR;
		val += digit;
		
		continue;

		ERR:
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Integer too large");
	}
	
	if(negative)
		val = -val;
	return i_val_int(val);
}

static i_val int_to_str_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_INT) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Expected integer as argument");
	}
	
	i_size mod = 0;
	i_int int_val = i_val_get_int(args[0]);
	bool is_negative = int_val < 0;
	size_t val = is_negative ? -int_val : int_val;
	
	if(int_val == 0)
		return I_STATIC_STR("0");
	
	for(i_int i = val; i != 0; i = i / 10) {
		if(mod == I_SIZE_MAX) {
			beryl_blame_arg(args[0]);
			return STATIC_ERR("Integer too large");
		}
		mod++;
	}
	
	size_t size = mod;
	if(is_negative)
		size++;
	
	i_val str = i_val_managed_str(NULL, size);
	if(BERYL_TYPEOF(str) == TYPE_NULL)
		return STATIC_ERR("Out of memory");
	
	char *s = (char *) i_val_get_raw_str(&str);
	char *end = s + size;
	
	if(is_negative)
		*(s++) = '-';
	
	for(char *c = end - 1; c >= s; c--) {
		*c = '0' + val % 10;
		val = val / 10;
	}
	
	return str;
}

static i_val rest_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_INT) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument must be integer");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_INT) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Second argument must be integer");
	}
	
	i_int i = i_val_get_int(args[0]);
	i_int div = i_val_get_int(args[1]);
	
	if(div == 0)
		return STATIC_ERR("Second argument must not be zero");
	
	if(i == I_INT_MIN && div == -1)
		return STATIC_ERR("Integer overflow");
	
	return i_val_int(i % div);
}

static i_val modulus_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_INT) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument must be integer");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_INT) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Second argument must be integer");
	}
	
	i_int i = i_val_get_int(args[0]);
	i_int div = i_val_get_int(args[1]);
	
	if(div == 0)
		return STATIC_ERR("Second argument must not be zero");
	
	if(i == I_INT_MIN && div == -1)
		return STATIC_ERR("Integer overflow");
	
	i_int res = (i % div + div) % div;
	
	return i_val_int(res);
}

static i_val null_check_callback(const i_val *args, size_t n_args) {
	for(size_t i = 0; i < n_args; i++)
		if(BERYL_TYPEOF(args[i]) == TYPE_NULL)
			return i_val_bool(1);
	
	return i_val_bool(0);
}

static i_val expect_callback(const i_val *args, size_t n_args) {
	for(size_t i = 0; i < n_args; i++)
		if(BERYL_TYPEOF(args[i]) == TYPE_NULL) {
			beryl_blame_arg(args[i]);
			return STATIC_ERR("Value is null");
		}
	return I_NULL;
}

static i_val round_callback(const i_val *args, size_t n_args) {
	i_type t = BERYL_TYPEOF(args[0]);
	if(t == TYPE_INT)
		return args[0];
	if(t == TYPE_FLOAT) {
		i_float res = i_val_get_float(args[0]);
		if(res < 0)
			res = res - 0.5;
		else
			res = res + 0.5;
		return i_val_int(res);
	}
	beryl_blame_arg(args[0]);
	return STATIC_ERR("Can only round numbers");
}

static i_val inline_if_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[1]) != TYPE_BOOL) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Expected boolean as condition for if?");
	}
	
	if(i_val_get_bool(args[1]))
		return beryl_retain(args[0]);
	else
		return I_NULL;
}

static i_val inline_else_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) == TYPE_NULL)
		return beryl_retain(args[1]);
	else
		return beryl_retain(args[0]);
}

static i_val integer_of_callback(const i_val *args, size_t n_args) {
	i_type t = BERYL_TYPEOF(args[0]);
	if(t == TYPE_INT)
		return beryl_retain(args[0]);
	if(t == TYPE_FLOAT)
		return i_val_int(i_val_get_float(args[0]));
	
	beryl_blame_arg(args[0]);
	return STATIC_ERR("Can only take integer of numbers");
}

static i_val abs_callback(const i_val *args, size_t n_args) {
	i_type t = BERYL_TYPEOF(args[0]);
	if(t == TYPE_INT) {
		i_int res = i_val_get_int(args[0]);
		if(res < 0) {
			if(res < -I_INT_MAX)
				return STATIC_ERR("Integer overflow");
			res = -res;
		}
		return i_val_int(res);
	} else if(t == TYPE_FLOAT) {
		i_float res = i_val_get_float(args[0]);
		if(res < 0)
			res = -res;
		return i_val_float(res);
	} else {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Expected either integer or floating point number");
	}
}

#define IMPLIES(x, y) (!(x) | (y))

static i_val switch_callback(const i_val *args, size_t n_args) {
	i_val switch_val = args[0];
	
	for(size_t i = 1; i < n_args - 1; i += 2) {
		assert(IMPLIES(n_args % 2 == 0, i + 1 < n_args - 1)); // If there's an even number of arguments, then the final argument should never be reached in this loop
		if(i_val_eq(switch_val, args[i]))
			return beryl_call_fn(args[i + 1], NULL, 0, BERYL_ERR_PROP);
	}
	
	if(n_args % 2 == 0)
		return beryl_call_fn(args[n_args-1], NULL, 0, BERYL_ERR_PROP);
	else
		return I_NULL;
}

LIB_FNS(fns) = {
	FN("eval", eval_callback, 1),
	FN("cat+", concat_callback, -1),
	FN("typeof", typeof_callback, -1),
	//FN("<-", apply_left_callback, 2),
	//FN("->", apply_right_callback, 2),
	FN("apply", apply_callback, 2),
	//FN("implies?", implies_callback, 2),
	FN("max", max_callback, -2),
	FN("min", min_callback, -2),
	FN("parse-int", parse_int_callback, 1),
	FN("int->str", int_to_str_callback, 1),
	FN("rest:", rest_callback, 2),
	FN("mod:", modulus_callback, 2),
	FN("null?", null_check_callback, -2),
	FN("expect", expect_callback, -2),
	
	FN("round", round_callback, 1),
	
	FN("if?", inline_if_callback, 2),
	FN("else?", inline_else_callback, 2),
	
	FN("intof", integer_of_callback, 1),
	FN("abs", abs_callback, 1),
	
	FN("switch", switch_callback, -2)
};

void common_lib_load() {
	LOAD_FNS(fns);

	DEF_FN("load", path,
		eval (read path)
	);
	
	DEF_FN("inc", x, x + 1);
	DEF_FN("dec", x, x - 1);
	DEF_FN(":", fn arg, fn arg);
	DEF_FN("->", arg fn, fn arg);
	
	DEF_FN("implies?", x y, (not x) or? y);
	
	DEF_FN("float->str", x,
		let integer-part = intof x \n
		let float-part = abs (x - integer-part) \n
		cat+ (int->str integer-part) "." (int->str (round float-part * 10))
	);
	
	DEF_FN("empty?", x,
		(sizeof x) ~= 0
	);
}
