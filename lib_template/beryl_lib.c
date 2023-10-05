#include "berylscript.h"

static struct i_val test_fn_callback(const struct i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected number");
	}
	return BERYL_NUMBER(beryl_as_num(args[0]) * 2);
}

static bool loaded = false;

static struct i_val lib_val;

#define LENOF(a) (sizeof(a)/sizeof(a[0]))

static void init_lib() {
	#define FN(name, arity, fn) { arity, name, sizeof(name) - 1, fn }
	static struct beryl_external_fn fns[] = { // Function constants
		FN("test-fn", 2, test_fn_callback),
	};
	
	#define CONST(name, val) { name, sizeof(name) - 1, val } // Constants
	static struct { const char *name; size_t name_len; struct i_val val; } consts[] = {
		CONST("test-constant", BERYL_NUMBER(42))
	};
	
	struct i_val table = beryl_new_table(LENOF(fns) + LENOF(consts), true);
	if(BERYL_TYPEOF(table) == TYPE_NULL) {
		lib_val = BERYL_ERR("Out of memory");
		return;
	}
	
	for(size_t i = 0; i < LENOF(fns); i++) {
		beryl_set_var(fns[i].name, fns[i].name_len, BERYL_EXT_FN(&fns[i]), false);
	}
	
	for(size_t i = 0; i < LENOF(consts); i++) {
		beryl_set_var(consts[i].name, consts[i].name_len, consts[i].val, false);
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
