#include "../beryl.h"

#include "../utils.h"

typedef struct i_val i_val;

#define FN(arity, name, fn) { arity, true, name, sizeof(name) - 1, fn }

static i_val refcount_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	return BERYL_NUMBER(beryl_get_refcount(args[0]));
}

static i_val ptrof_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	const void *ptr = NULL;
	switch(BERYL_TYPEOF(args[0])) {
		case TYPE_TABLE:
			ptr = args[0].val.table;
			break;
		case TYPE_ARRAY:
			ptr = beryl_get_raw_array(args[0]);
			break;
	}
	
	return BERYL_NUMBER((unsigned long long) ptr);
}

static i_val container_capacity_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	switch(BERYL_TYPEOF(args[0])) {
		case TYPE_ARRAY:
			return BERYL_NUMBER(beryl_get_array_capacity(args[0]));
		
		default:
			return BERYL_NUMBER(0);
	}
}

bool load_debug_lib() {
	static struct beryl_external_fn fns[] = {
		FN(1, "refcount", refcount_callback),
		FN(1, "ptrof", ptrof_callback),
		FN(1, "capof", container_capacity_callback)
	};

	for(size_t i = 0; i < LENOF(fns); i++) {
		bool ok = beryl_set_var(fns[i].name, fns[i].name_len, BERYL_EXT_FN(&fns[i]), true);
		if(!ok)
			return false;
	}
	
	beryl_set_var("max-refcount", sizeof("max-refcount") - 1, BERYL_NUMBER(I_REFC_MAX), true);
	beryl_set_var("valsize", sizeof("valsize") - 1, BERYL_NUMBER(sizeof(i_val)), true);
	
	return true;
}
