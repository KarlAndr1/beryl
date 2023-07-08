#ifndef LIB_UTILS_H_INCLUDED
#define LIB_UTILS_H_INCLUDED

typedef struct i_val i_val;
//typedef struct i_table i_table;
typedef struct i_string i_string;
//typedef struct i_val_pair i_val_pair;
typedef struct i_array i_array;

/*
struct library_callback {
	const char *name; i_size len;
	
}
*/

#define retain(x) beryl_retain(x)
#define release(x) beryl_release(x)
#define call_fn(fn, args, n_args, action) beryl_call_fn(fn, args, n_args, BERYL_ERR_##action)
#define eval(src, src_len, action) beryl_call_fn(src, src_len, BERYL_ERR_##action)

#define FN(name, fn, arity) { name, sizeof(name) - 1, fn, arity }
#define LIB_FNS(name) static struct i_ext_fn name[]

#define LOAD_FN(fn) beryl_set_var((fn)->name, (fn)->name_len, i_val_ext_fn(fn), true)

#define LOAD_FNS(array) for(size_t i = 0; i < sizeof(array)/sizeof(array[0]); i++) { \
	LOAD_FN(&array[i]); \
}

#define LOAD(name, fn, arity) beryl_set_var(name, sizeof(name) - 1, i_val_ext_fn(fn, arity), true)

#define CONSTANT(name, val) beryl_set_var(name, sizeof(name) - 1, val, true)

#define DEF_VAR(name, src) \
{ \
	struct i_val res = beryl_eval(#src, sizeof(#src) - 1, true, BERYL_ERR_CATCH); \
	beryl_set_var(name, sizeof(name) - 1, res, false); \
	beryl_release(res); \
} \

#define DEF_FN(name, args, ...) beryl_set_var(name, sizeof(name) - 1, i_val_fn("function " #args " do " #__VA_ARGS__, sizeof("function " #args " do " #__VA_ARGS__) - 1), true)

#define EXPECT_ARG_T1(args, n_args, t1, tname1) { \
	assert(n_args >= 1); \
	if(BERYL_TYPEOF(args[0]) != t1) { \
		beryl_blame_arg(args[0]); \
		return STATIC_ERR("Expected first argument to be of type " tname1); \
	} \
}

#define EXPECT_ARG_T2(args, n_args, t1, tname1, t2, tname2) { \
	assert(n_args >= 2); \
	if(BERYL_TYPEOF(args[0]) != t1) { \
		beryl_blame_arg(args[0]); \
		return STATIC_ERR("Expected first argument to be of type " tname1); \
	} \
	if(BERYL_TYPEOF(args[1]) != t2) { \
		beryl_blame_arg(args[1]); \
		return STATIC_ERR("Expected second argument to be of type " tname2); \
	} \
}

#define EXPECT_ARG_T3(args, n_args, t1, tname1, t2, tname2, t3, tname3) { \
	assert(n_args >= 3); \
	if(BERYL_TYPEOF(args[0]) != t1) { \
		beryl_blame_arg(args[0]); \
		return STATIC_ERR("Expected first argument to be of type " tname1); \
	} \
	if(BERYL_TYPEOF(args[1]) != t2) { \
		beryl_blame_arg(args[1]); \
		return STATIC_ERR("Expected second argument to be of type " tname2); \
	} \
	if(BERYL_TYPEOF(args[2]) != t3) { \
		beryl_blame_arg(args[2]); \
		return STATIC_ERR("Expected third argument to be of type " tname3); \
	} \
}



#endif
