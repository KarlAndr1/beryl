#include <stdlib.h>

#include "../utils.h"

#include "../interpreter.h"
#include "lib_utils.h"

i_type type_table;

typedef struct i_val_pair {
	struct i_val key, val;
} i_val_pair;

typedef struct i_table {
	i_size cap, len;
	i_refc ref_c;
	const struct i_val_pair entries[];
} i_table;

static bool is_hashable(i_val val) {
	switch(val.type) {
		case TYPE_INT:
		case TYPE_BOOL:
		case TYPE_STR:
			return true;
		
		default:
			return false;
	}
}

static i_size hash_i_val(i_val val) {
	switch(val.type) {
		case TYPE_BOOL:
		case TYPE_INT:
			if(val.val.int_v >= 0)
				return (i_size) i_val_get_int(val);
			else
				return I_SIZE_MAX - ((i_size) (-i_val_get_int(val)));
		
		case TYPE_STR: {
			i_size hash = 0;
			const char *str = i_val_get_raw_str(&val);
			i_size str_len = i_val_get_len(val);
			for(i_size i = 0; i < str_len; i++) {
				hash *= 7;
				hash += (unsigned char) str[i];
			}
			return hash;
		}
		
		default:
			assert(false);
			return 0;
	}
}

// This function finds either the entry with the given key in the table, if it exists, or 
// an open slot where that key could fit in
// It returns NULL if there's no space left in the table and the key is not already in the table
static i_val_pair *find_in_table(i_val key, i_table *table) {
	assert(is_hashable(key));
	
	if(table->cap == 0)
		return NULL;
	
	i_size hash = hash_i_val(key) % table->cap;
	const i_val_pair *entries = table->entries;
	const i_val_pair *end = entries + table->cap;
	const i_val_pair *expected = entries + hash;
	
	for(const i_val_pair *p = expected; p < end; p++) {
		if(p->key.type == TYPE_NULL)
			return (i_val_pair *) p;

		if(i_val_eq(p->key, key))
			return (i_val_pair *) p;
	}
	
	for(const i_val_pair *p = entries; p < expected; p++) { //If nothing was from the expected location to the end, search all the rest from the beginning
		if(p->key.type == TYPE_NULL)
			return (i_val_pair *) p;
		
		if(i_val_eq(p->key, key))
			return (i_val_pair *) p;
	}
	
	return NULL;
}

static const i_val *index_table(i_val key, i_table *table) {	
	if(!is_hashable(key))
		return NULL;
	
	i_val_pair *entry = find_in_table(key, table);
	if(entry == NULL)
		return NULL;
	
	if(entry->key.type == TYPE_NULL)
		return NULL;
	
	return &entry->val;
}

static bool table_foreach(i_table *table, bool (*f)(const i_val_pair *pair, bool is_last, void *closure), void *closure) {
	i_size size = table->cap;
	i_size length = table->len;
	const i_val_pair *end = table->entries + size;
	
	i_size i = length;
	for(const i_val_pair *p = table->entries; p < end; p++) {
		if(p->key.type != TYPE_NULL) {
			bool should_exit = f(p, i == 1, closure);
			if(should_exit)
				return true;
			i--;
			if(i == 0)
				break;
		}
	}
	return false;
}

// Create an empty table that has at least the capacity needed to fit 'fit_for' entries
// Returns null if out of memory
static i_table *create_empty_table(i_size fit_for) {
	i_size cap = (fit_for * 3) / 2;
	if(cap < fit_for)
		return NULL;
	i_table *table = beryl_get_alloc()(sizeof(i_table) + sizeof(i_val_pair) * cap);
	if(table == NULL)
		return NULL;
	
	i_val_pair *entries = (i_val_pair *) table->entries;
	for(i_size i = 0; i < cap; i++)
		*(entries++) = (i_val_pair) { I_NULL, I_NULL };
		
	table->len = 0;
	table->cap = cap;
	table->ref_c = 1;
	return table;
}

// Attempts to insert the given key and value into the table
// Returns 0 if successful, 1 if the key already exists (in which case the old value is kept) 
// and 2 if there's no space left in the table and the key already doesn't exist
static int table_insert(i_table *table, i_val key, i_val val) {
	i_val_pair *kv = find_in_table(key, table);
	if(kv == NULL)
		return 2;
	if(BERYL_TYPEOF(kv->key) != TYPE_NULL)
		return 1;
	
	kv->key = beryl_retain(key);
	kv->val = beryl_retain(val);
	table->len++;
	return 0;
}

static i_val map_array(i_val array, i_val fn) {
	assert(BERYL_TYPEOF(array) == TYPE_ARRAY);
	
	i_val new_array;
	i_size len = i_val_get_len(array);
	if(beryl_get_references(array) == 1) {
		new_array = beryl_retain(array);
		i_val *a = (i_val *) i_val_get_raw_array(new_array);
		for(i_size i = 0; i < len; i++) {
			i_val res = beryl_call_fn(fn, &a[i], 1, BERYL_ERR_PROP);
			if(res.type == TYPE_ERR) {
				beryl_release(new_array);
				return res;
			}
			beryl_release(a[i]);
			a[i] = res;
		}
	} else {
		new_array = i_val_managed_array(NULL, len, len);
		if(BERYL_TYPEOF(new_array) == TYPE_NULL)
			return STATIC_ERR("Out of memory");
		i_val *a = (i_val *) i_val_get_raw_array(new_array);
		const i_val *src = i_val_get_raw_array(array);
		for(i_size i = 0; i < len; i++) {
			i_val res = beryl_call_fn(fn, &src[i], 1, BERYL_ERR_PROP);
			if(BERYL_TYPEOF(res) == TYPE_ERR) {
				if(i > 0) {
					for(i_size j = i - 1; j >= 0; j--) {
						beryl_release(a[j]);
						a[j] = I_NULL;
						if(j == 0)
							break;
					}
				}
				beryl_release(new_array);
				return res;
			}
			a[i] = res;
		}
	}
	
	return new_array;
}

static struct i_val map_callback(const i_val *args, size_t n_args) {	
	i_type type = BERYL_TYPEOF(args[0]);
	
	if(type == TYPE_ARRAY) {
		return map_array(args[0], args[1]);
	} else {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument of map must be array");
	}
}

static i_val filter_array(i_val array, i_val fn) {
	assert(BERYL_TYPEOF(array) == TYPE_ARRAY);
	
	i_size len = i_val_get_len(array);
	i_val new_array;
	
	if(beryl_get_references(array) == 1) {
		new_array = beryl_retain(array);
		i_val *a = (i_val *) i_val_get_raw_array(new_array);
		i_size n = 0;
		for(i_size i = 0; i < len; i++) {
			i_val res = beryl_call_fn(fn, &a[i], 1, BERYL_ERR_PROP);
			i_type res_t = BERYL_TYPEOF(res);
			if(res_t == TYPE_ERR) {
				beryl_release(new_array);
				return res;
			}
			if(res_t != TYPE_BOOL) {
				beryl_release(new_array);
				beryl_blame_arg(res);
				return STATIC_ERR("Filter result must be boolean");
			}
			
			if(i_val_get_bool(res)) {
				i_val tmp = a[n];
				a[n++] = a[i];
				beryl_release(tmp);
			}
		}
		new_array.len = n;
	} else {
		new_array = i_val_managed_array(NULL, 0, len);
		if(BERYL_TYPEOF(new_array) == TYPE_NULL)
			return STATIC_ERR("Out of memory");
		
		const i_val *src = i_val_get_raw_array(array);
		i_val *dst = (i_val *) i_val_get_raw_array(new_array);
		for(i_size i = 0; i < len; i++) {
			i_val res = beryl_call_fn(fn, &src[i], 1, BERYL_ERR_PROP);
			i_type res_t = BERYL_TYPEOF(res);
			if(res_t == TYPE_ERR) {
				beryl_release(new_array);
				return res;
			}
			if(res_t != TYPE_BOOL) {
				beryl_release(new_array);
				beryl_blame_arg(res);
				return STATIC_ERR("Filter result must be boolean");
			}
			
			if(i_val_get_bool(res))
				dst[new_array.len++] = beryl_retain(src[i]);
		}
	}
	
	return new_array;
}

static struct i_val filter_callback(const i_val *args, size_t n_args) {
	i_type type = BERYL_TYPEOF(args[0]);
	
	if(type == TYPE_ARRAY) {
		return filter_array(args[0], args[1]);
	} else {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument of filter must be array");
	}
}

static void table_free(void *ptr);

static i_val table_construct_callback(const i_val *args, size_t n_args) {
	if(n_args % 2 != 0)
		return STATIC_ERR("Table constructor arguments must be even in number");
	
	if(n_args > I_SIZE_MAX)
		return STATIC_ERR("Too many arguments");
	
	i_size len = n_args / 2;
	//i_size cap = (len * 3)/2;
	
	i_table *table = create_empty_table(len);
	if(table == NULL)
		return STATIC_ERR("Out of memory");
	
	for(i_size i = 0; i < len; i++) {
		int res = table_insert(table, args[i*2], args[i*2 + 1]);
		if(res == 1) {
			table_free(table);
			beryl_blame_arg(args[i * 2]);
			return STATIC_ERR("Duplicate key");
		}
	}
	
	return (i_val) { .type = type_table, .managed = true, .val = { .ptr = table } };
}

static i_val array_construct_callback(const i_val *args, size_t n_args) {
	if(n_args > I_SIZE_MAX)
		return STATIC_ERR("Too many arguments");
	i_val array = i_val_managed_array(args, n_args, n_args);
	if(array.type == TYPE_NULL)
		return STATIC_ERR("Out of memory");
	return array;
}

static i_size i_size_max(i_size a, i_size b) {
	if(a > b)
		return a;
	return b;
}

static i_val array_push_callback(const i_val *args, size_t n_args) {
	i_val array = args[0];
	i_val item = args[1];
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(array);
		return STATIC_ERR("First argument of push must be array");
	}
	
	i_size len = i_val_get_len(array);
	if(len == I_SIZE_MAX) {
		return STATIC_ERR("Array size overflow");
	}
	
	i_val new_array;
	if(beryl_get_references(array) == 1 && i_val_get_cap(array) > len) {
		new_array = beryl_retain(array);
	} else { //Static arrays will always have I_REF_COUNT_MAX number of references
		i_size new_cap = i_size_max(len + 1, len * 3 / 2);
		new_array = i_val_managed_array(i_val_get_raw_array(array), len, new_cap);
		if(BERYL_TYPEOF(new_array) == TYPE_NULL)
			return STATIC_ERR("Out of memory");
	}
	
	i_val *items = (i_val *) i_val_get_raw_array(new_array);
	items[len] = beryl_retain(item);
	new_array.len++;
	
	return new_array;
}

static i_val array_pop_callback(const i_val *args, size_t n_args) {
	i_val array = args[0];
	if(BERYL_TYPEOF(array) != TYPE_ARRAY) {
		beryl_blame_arg(array);
		return STATIC_ERR("Can only pop arrays");
	}
	
	i_size len = i_val_get_len(array);
	if(len == 0) {
		beryl_blame_arg(array);
		return STATIC_ERR("Cannot pop empty array");
	}
	
	i_val new_array;
	if(beryl_get_references(array) == 1) {
		new_array = beryl_retain(array);
		i_val *items = (i_val *) i_val_get_raw_array(array);
		beryl_release(items[len - 1]);
		new_array.len--;
	} else {
		new_array = i_val_managed_array(i_val_get_raw_array(array), len - 1, len - 1);
		if(BERYL_TYPEOF(new_array) == TYPE_NULL)
			return STATIC_ERR("Out of memory");
	}
	
	return new_array;
}

static i_val array_join_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument of join must be array");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_ARRAY) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Second argument of join must be array");
	}
	i_size len1 = i_val_get_len(args[0]);
	i_size len2 = i_val_get_len(args[1]);
	
	if(I_SIZE_MAX - len1 < len2)
		return STATIC_ERR("Resulting array would be too large");
	
	i_val new_array = i_val_managed_array(NULL, len1 + len2, len1 + len2);
	if(BERYL_TYPEOF(new_array) == TYPE_NULL)
		return STATIC_ERR("Out of memory");
	
	i_val *items = (i_val *) i_val_get_raw_array(new_array);
	
	const i_val *a1 = i_val_get_raw_array(args[0]);
	const i_val *a2 = i_val_get_raw_array(args[1]);
	for(i_size i = 0; i < len1; i++) {
		items[i] = beryl_retain(a1[i]);
	}
	for(i_size i = 0; i < len2; i++) {
		items[len1 + i] = beryl_retain(a2[i]);
	}
	
	return new_array;
}

static i_val array_peek_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Can only peek arrays");
	}
	
	i_size len = i_val_get_len(args[0]);
	if(len == 0)
		return STATIC_ERR("Cannot peek empty array");
	
	return beryl_retain(i_val_get_raw_array(args[0])[len - 1]);
}

static bool table_foreach_callback(const i_val_pair *pair, bool is_last, void *closure) {
	struct {
		i_val fn, res;
	} *env = closure;
	
	i_val args[2] = { pair->key, pair->val };
	
	i_val res = beryl_call_fn(env->fn, args, 2, BERYL_ERR_PROP);
	beryl_release(env->res);
	env->res = res;
	
	if(res.type == TYPE_ERR)
		return true;
	return false;
}

static i_val foreach_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY && BERYL_TYPEOF(args[0]) != type_table) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Can only iterate over arrays and tables");
	}
	
	i_val fn = args[1];
	
	if(BERYL_TYPEOF(args[0]) == TYPE_ARRAY) {
		i_size len = i_val_get_len(args[0]);
		const i_val *items = i_val_get_raw_array(args[0]);
		
		i_val res;
		for(i_size i = 0; i < len; i++) {
			res = beryl_call_fn(fn, &items[i], 1, BERYL_ERR_PROP);
			if(BERYL_TYPEOF(res) == TYPE_ERR)
				break;
		}
		return res;
	} else {
		i_table *table = i_val_get_ptr(args[0]);
		struct {
			i_val fn, res;
		} env = { fn, I_NULL };
		table_foreach(table, table_foreach_callback, &env);
		return env.res;
	}
}

static i_val arrayof_construct_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_INT) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument must be integer");
	}
	
	i_int n = i_val_get_int(args[0]);
	if(n < 0) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Array size must be zero or greater");
	}
	
	i_val array = i_val_managed_array(NULL, n, n);
	if(BERYL_TYPEOF(array) == TYPE_NULL)
		return STATIC_ERR("Out of memory");
	
	i_val *a = (i_val *) i_val_get_raw_array(array);
	
	for(i_int i = 0; i < n; i++) {
		i_val arg = i_val_int(i);
		i_val res = beryl_call_fn(args[1], &arg, 1, BERYL_ERR_PROP);
		if(BERYL_TYPEOF(res) == TYPE_ERR) {
			for(i_int j = i - 1; j >= 0; j--) {
				beryl_release(a[j]);
			}
			return res;
		}
		a[i] = res;
	}
	
	return array;
}

static i_val in_callback(const i_val *args, size_t n_args) {
	i_type t = BERYL_TYPEOF(args[1]);
	if(t == TYPE_ARRAY) {
		i_size len = i_val_get_len(args[1]);
		const i_val *a = i_val_get_raw_array(args[1]);
		for(i_size i = 0; i < len; i++)
			if(i_val_eq(args[0], a[i]))
				return i_val_bool(1);
		return i_val_bool(0);
	} else if(type_table) {
		i_table *table = i_val_get_ptr(args[1]);
		const void *p = index_table(args[0], table);
		return i_val_bool(p != NULL);
	} else {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Second argument must be either array or table");
	}	
}

static bool insert_into_table_from_table_callback(const i_val_pair *kv, bool is_last, void *closure) {
	i_table *into_table = closure;
	int res = table_insert(into_table, kv->key, kv->val);
	assert(res == 0 || res == 1);
	return false;
}

static i_val union_callback(const i_val *args, size_t n_args) {
	EXPECT_ARG_T2(
		args, n_args,
		type_table, "table",
		type_table, "table"
	);
	
	i_table *t_a = i_val_get_ptr(args[0]);
	i_table *t_b = i_val_get_ptr(args[1]);
	
	i_size s_a = t_a->len;
	i_size s_b = t_b->len;
	if(s_b > I_SIZE_MAX - s_a)
		return STATIC_ERR("Resulting table would be too large");
	i_size max_entries = s_a + s_b;
	
	i_table *res_table = create_empty_table(max_entries);
	if(res_table == NULL)
		return STATIC_ERR("Out of memory");
	table_foreach(t_a, insert_into_table_from_table_callback, res_table);
	table_foreach(t_b, insert_into_table_from_table_callback, res_table);
	
	return (i_val) { .type = type_table, .managed = true, .val = { .ptr = res_table } };
}

LIB_FNS(fns) = {
	FN("array", array_construct_callback, -1),
	FN("table", table_construct_callback, -1),
	
	FN(";", array_construct_callback, -1),
	FN("@", table_construct_callback, -1),
	
	FN("push:", array_push_callback, 2),
	FN("pop", array_pop_callback, 1),
	FN("join:", array_join_callback, 2),
	FN("peek", array_peek_callback, 1),
	FN("foreach-in", foreach_callback, 2),
	FN("map", map_callback, 2),
	FN("filter", filter_callback, 2),
	FN("arrayof", arrayof_construct_callback, 2),
	
	FN("in?", in_callback, 2),
	
	FN("union:", union_callback, 2)
};

static i_val table_call(void *ptr, const i_val *args, size_t n_args) {
	if(n_args != 1)
		return STATIC_ERR("Table can only be indexed with 1 key");
	
	i_table *table = ptr;
	
	const i_val *res = index_table(args[0], table);
	if(res == NULL) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Index out of bounds");
	}
	
	return beryl_retain(*res);
}

static i_refc table_release(void *ptr) {
	i_table *table = ptr;
	assert(table->ref_c != 0);
	return --table->ref_c;
}

static void table_retain(void *ptr) {
	i_table *table = ptr;
	table->ref_c++;
}

static i_refc table_get_refs(void *ptr) {
	return ((i_table *) ptr)->ref_c;
}

static bool table_free_callback(const i_val_pair *pair, bool is_last, void *closure) {
	beryl_release(pair->key);
	beryl_release(pair->val);
	return false;
}

static void table_free(void *ptr) {
	i_table *table = ptr;
	table_foreach(table, table_free_callback, NULL);
	beryl_get_free()(table);
}

#include <stdio.h>

static bool table_print_callback(const i_val_pair *entry, bool is_last, void *closure) {
	struct {
		FILE *f;
		void (*print_callback)(FILE *f, i_val val);
	} *env = closure;
	
	putc('(', env->f);
	
	env->print_callback(env->f, entry->key);
	putc(' ', env->f);
	env->print_callback(env->f, entry->val);
	
	if(is_last)
		putc(')', env->f);
	else
		fputs(") ", env->f);
	return false;
}

static void table_print(void *ptr, FILE *f, void (*print_callback)(FILE *f, i_val val)) {
	i_table *table = ptr;
	putc('{', f);
	
	struct {
		FILE *f;
		void (*print_callback)(FILE *f, i_val val);
	} env = { f, print_callback };
	table_foreach(table, table_print_callback, &env);
	
	putc('}', f);
}

static void table_sizeof(void *ptr, i_size *out_size) {
	i_table *table = ptr;
	*out_size = table->len;
}

static void table_typename(const char **typename, i_size *typename_size) {
	*typename = "table";
	*typename_size = sizeof("table") - 1;
}

struct ti_interface table_interfaces[] = {
	{ table_print, TI_PRINT },
	{ table_sizeof, TI_SIZEOF },
	{ table_typename, TI_TYPENAME }	
};

int datastructures_lib_load() {
	type_table = beryl_add_type(table_free, table_retain, table_release, table_get_refs, table_call, table_interfaces, LENOF(table_interfaces));
	if(type_table == TYPE_NULL)
		return -1;
	LOAD_FNS(fns);
	
	return 0;
}
