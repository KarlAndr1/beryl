#ifndef INTERPRETER_H_INCLUDED
#define INTERPRETER_H_INCLUDED

// TYPE DEFS

#include <stddef.h>
#include <limits.h>
//typedef ptr_diff_t 
//typedef size_t

typedef _Bool bool;
#define true (1)
#define false (0)

typedef unsigned char i_type;
#define I_MAX_TYPES UCHAR_MAX

//Used for integer arithmetic in the interpreter.
typedef long long i_int;
typedef unsigned long long i_uint;
#define I_INT_MAX LLONG_MAX
#define I_INT_MIN LLONG_MIN

typedef double i_float; 

//Used to store the size of interpreter objects, like strings, arrays etc. Must be less than or equal in size to the size_t type
typedef unsigned int i_size;
#define I_SIZE_MAX UINT_MAX

//Used for storing reference counts in the interpreter, i.e this defines how many concurrent references to an object can exist
typedef unsigned int i_refc;
#define I_REF_COUNT_MAX UINT_MAX

// -----------------

enum {
	TYPE_NULL,
	TYPE_BOOL,
	TYPE_INT,
	TYPE_FLOAT,
	TYPE_STR,
	TYPE_ARRAY,
	//TYPE_TABLE,
	TYPE_FN,
	TYPE_EXT_FN,
	TYPE_ERR,
	TYPE_MARKER_RETURN,
	BERYL_N_STATIC_TYPES
};

#define I_PACKED_STR_MAX_LEN sizeof(const char *)

struct i_string {
	union {
		const char *str;
		char packed_str[I_PACKED_STR_MAX_LEN];
	} contents;
	i_size len;
};

struct i_fn {
	const char *str;
	i_size len;
};

struct i_managed_string {
	i_refc ref_count;
	const char str[];
};

struct i_ext_fn {
	const char *name;
	size_t name_len;
	struct i_val (*callback)(const struct i_val*, size_t);
	int arity;
};



struct i_val {
	union {
		i_int int_v;
		i_float float_v;
		const char *str;
		char packed_str[I_PACKED_STR_MAX_LEN];
		struct i_managed_string *managed_str;
		const struct i_val *array;
		struct i_managed_array *managed_array;
		struct i_ext_fn *ext_fn;
		void *ptr;
	} val;
	i_size len;
	i_type type;
	bool managed;
	unsigned char meta;
};

struct i_managed_array {
	i_size cap;
	i_refc ref_count;
	struct i_val items[];
};

struct beryl_err_ref {
	const char *src;
	const char *at;
	size_t src_len, err_len;
};

void beryl_load_standard_libs();

void beryl_set_mem(void (*free)(void *), void *(*alloc)(size_t), void *(*realloc)(void *, size_t));
void *(*beryl_get_alloc())(size_t);
void (*beryl_get_free())(void *);
void *(*beryl_get_realloc())(void *, size_t);

bool beryl_set_var(const char *name, i_size name_len, struct i_val val, bool as_const);

void beryl_release(struct i_val val);
struct i_val beryl_retain(struct i_val val);
i_refc beryl_get_references(struct i_val val);

enum beryl_err_action {
	BERYL_ERR_PROP,
	BERYL_ERR_CATCH,
	BERYL_ERR_PRINT
};

struct beryl_stack_trace {
	const char *name;
	size_t name_len;
	const char *src, *at;
	size_t src_len, err_len;
};

typedef void (*ti_interface_callback)();

enum {
	TI_PRINT,
	TI_SIZEOF,
	TI_TYPENAME
};

typedef unsigned int ti_interface_id;

struct ti_interface {
	ti_interface_callback callback;
	ti_interface_id id;
};

ti_interface_callback beryl_interface_val(struct i_val val, ti_interface_id interface);

i_type beryl_add_type(void (*free)(void *), void (*retain)(void *), i_refc (*release)(void *), i_refc (*get_refs)(void *), struct i_val (*call)(void *, const struct i_val *, size_t), const struct ti_interface *interfaces, size_t n_interfaces);

void beryl_blame_arg(struct i_val arg);
void beryl_set_err_handler(void (*handler)(struct beryl_stack_trace *, size_t, const struct i_val*, size_t));

void beryl_set_return_val(struct i_val val);

struct i_val beryl_eval(const char *src, size_t src_len, bool new_scope, enum beryl_err_action err_action);
struct i_val beryl_call_fn(struct i_val fn, const struct i_val *args, size_t n_args, enum beryl_err_action err_action);

void beryl_clear();

bool i_val_eq(struct i_val a, struct i_val b);
bool i_string_cmp(struct i_string a, struct i_string b);
int i_val_cmp(struct i_val a, struct i_val b);


// Getters
const char *i_val_get_raw_str(const struct i_val *val);	// Note that the lifetime of the returned string may only be as long as the lifetime of the 
														// provided pointer, even if a copy of the value is retained.
i_int i_val_get_int(struct i_val val);
i_int i_val_get_bool(struct i_val val);
i_float i_val_get_float(struct i_val val);
const struct i_val *i_val_get_raw_array(struct i_val val);
void *i_val_get_ptr(struct i_val val);
i_size i_val_get_len(struct i_val val);
i_size i_val_get_cap(struct i_val val);
// ------------------------------------------

//i_val constructors
struct i_val i_val_static_str(const char *str, i_size len);
struct i_val i_val_managed_str(const char *src, i_size len);
struct i_val i_val_managed_array(const struct i_val *from_array, i_size len, i_size cap);
struct i_val i_val_static_array(const struct i_val *array, i_size len);
struct i_val i_val_int(i_int i);
struct i_val i_val_float(i_float f);
struct i_val i_val_bool(i_int i);
struct i_val i_val_err(struct i_val from_str);
struct i_val i_val_ext_fn(struct i_ext_fn *fn);
struct i_val i_val_fn(const char *src, i_size len);
// ------------------------------------------

#define STATIC_ERR(msg) i_val_err(i_val_static_str(msg, sizeof(msg) - 1))
#define I_STATIC_STR(str) i_val_static_str(str, sizeof(str) - 1)

#define I_NULL (struct i_val) { .type = TYPE_NULL }

#define I_EARLY_RETURN (struct i_val) { .type = TYPE_MARKER_RETURN }

#define I_VAL_GET_TYPE(v) (v).type
#define BERYL_TYPEOF(v) (v).type

#define I_VAL_FN(fn) i_val_fn(fn, sizeof(fn) - 1)


#endif
