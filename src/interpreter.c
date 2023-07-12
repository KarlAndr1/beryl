#include "interpreter.h"

#include "lexer.h"
#include "utils.h"

#define STATIC_STACK_SIZE 128 //How many (local) variables can exist

typedef struct i_val i_val;
typedef struct i_string i_string;
typedef struct i_ext_fn i_ext_fn;
typedef struct i_fn i_fn;

typedef struct i_managed_string i_managed_string;
typedef struct i_managed_array i_managed_array;

typedef struct i_array i_array;

typedef struct ti_interface ti_interface;

struct scope_namespace {
	const char *start, *end;
};

typedef struct stack_entry {
	const char *name;
	i_size name_len;
	i_val val;
	bool is_const;
	struct scope_namespace namespace;
} stack_entry;

i_size i_val_get_len(i_val val) {
	return val.len;
}

i_size i_val_get_cap(i_val val) {
	if(val.type != TYPE_ARRAY || !val.managed)
		return val.len;
	return val.val.managed_array->cap;
}

#include "libs/int_libs.h"

#ifndef MINIMAL_BUILD
#include "io.h"
#endif

void beryl_load_standard_libs() {
	core_lib_load();
	#ifndef MINIMAL_BUILD
	common_lib_load(); math_lib_load(); io_lib_load(); string_lib_load();
	datastructures_lib_load(); modules_lib_load();
	beryl_set_err_handler(io_print_stacktrace);
	#endif
	#ifdef DEBUG
	debug_lib_load();
	#endif
}

i_val beryl_new_tag() {
	static i_int tag_counter = 0;

	if(tag_counter == I_INT_MAX)
		return I_NULL;
	return (i_val) { .type = TYPE_TAG, .managed = false, .val = { .int_v = tag_counter++ } };
}

// Getters

const char *i_val_get_raw_str(const i_val *val) {
	assert(val->type == TYPE_STR || val->type == TYPE_ERR);
	if(val->managed) {
		if(i_val_get_len(*val) <= I_PACKED_STR_MAX_LEN)
			return &val->val.packed_str[0];
		else
			return val->val.managed_str->str;
	}
	return val->val.str;
}

#define GETTER(ret_type, name, req_type, property) ret_type name(i_val val) { assert(val.type == req_type); return val.val.property; }

GETTER(i_int, i_val_get_int, TYPE_INT, int_v)
GETTER(i_int, i_val_get_tag, TYPE_TAG, int_v)
GETTER(i_float, i_val_get_float, TYPE_FLOAT, float_v)
GETTER(i_int, i_val_get_bool, TYPE_BOOL, int_v)

const i_val *i_val_get_raw_array(i_val val) {
	assert(val.type == TYPE_ARRAY);
	if(val.managed)
		return val.val.managed_array->items;
	return val.val.array;
}

void *i_val_get_ptr(i_val val) {
	assert(val.type >= BERYL_N_STATIC_TYPES);
	return val.val.ptr;
}

i_fn i_val_get_fn(i_val val) {
	assert(val.type == TYPE_FN);
	return (i_fn) { val.val.str, val.len };
}

// Constructors

i_val i_val_static_str(const char *str, i_size len) {
	return (i_val) { .type = TYPE_STR, .managed = false, .val = { .str = str }, .len = len };
}

static void mcpy(void *to, const void *from, size_t n) {
	char *to_ptr = to;
	const char *from_ptr = from;
	for(; n > 0; n--) {
		*to_ptr = *from_ptr;
		to_ptr++, from_ptr++;
	}
}

static void (*int_free) (void *) = 0;
static void *(*int_alloc) (size_t) = 0;
static void *(*int_realloc) (void *, size_t) = 0;

void beryl_set_mem(void (*free)(void *), void *(*alloc)(size_t), void *(*realloc)(void *, size_t)) {
	int_free = free;
	int_alloc = alloc;
	int_realloc = realloc;
}

void *(*beryl_get_alloc())(size_t) { return int_alloc; }
void (*beryl_get_free())(void *) { return int_free; }
void *(*beryl_get_realloc())(void *, size_t) { return int_realloc; }

i_val i_val_managed_str(const char *src, i_size len) {
	if(int_alloc == NULL)
		return I_NULL;
	if(len <= I_PACKED_STR_MAX_LEN) {
		i_val res = { .type = TYPE_STR, .managed = true, .len = len };
		if(src != NULL)
			mcpy(res.val.packed_str, src, len);
		return res;
	} else {
		i_managed_string *str = int_alloc(sizeof(i_managed_string) + len * sizeof(char));
		if(str == NULL)
			return I_NULL;
		if(src != NULL)
			mcpy((char*) str->str, src, len);
		str->ref_count = 1;
		return (i_val) { .type = TYPE_STR, .managed = true, .val = { .managed_str = str }, .len = len };
	}
}

i_val i_val_static_array(const i_val *array, i_size len) {
	return (i_val) { .type = TYPE_ARRAY, .managed = false, .val = { .array = array }, .len = len };
}

i_val i_val_managed_array(const i_val *from_array, i_size len, i_size cap) {
	if(int_alloc == NULL)
		return I_NULL;
	i_managed_array *array = int_alloc(sizeof(i_managed_array) + cap * sizeof(i_val));
	if(array == NULL)
		return I_NULL;
	array->ref_count = 1;
	array->cap = cap;
	
	i_val *items = (i_val *) array->items;
	
	if(from_array != NULL) {
		for(i_size i = 0; i < len; i++) {
			items[i] = beryl_retain(from_array[i]);
		}
	}
	
	return (i_val) { .type = TYPE_ARRAY, .managed = true, .val = { .managed_array = array }, .len = len }; 
}

i_val i_val_int(i_int i) {
	return (i_val) { .type = TYPE_INT, .managed = false, .val = { .int_v = i } };
}

i_val i_val_float(i_float f) {
	return (i_val) { .type = TYPE_FLOAT, .managed = false, .val = { .float_v = f } };
}

i_val i_val_bool(i_int i) {
	return (i_val) { .type = TYPE_BOOL, .managed = false, .val = { .int_v = i } };
}

i_val i_val_ext_fn(i_ext_fn *fn) {
	return (i_val) { .type = TYPE_EXT_FN, .managed = false, .val = { .ext_fn = fn } };
}

i_val i_val_fn(const char *src, i_size src_len) {
	return (i_val) { .type = TYPE_FN, .managed = false, .val = { .str = src }, .len = src_len };
}

i_val i_val_err(i_val from_str) {
	assert(from_str.type == TYPE_STR);
	from_str.type = TYPE_ERR;
	return from_str;
}

static i_managed_string *get_managed_str(i_val str) {
	assert(str.type == TYPE_STR || str.type == TYPE_ERR);
	assert(str.managed);
	
	if(i_val_get_len(str) <= I_PACKED_STR_MAX_LEN)
		return NULL;
		
	return str.val.managed_str;
}

static i_managed_array *get_managed_array(i_val array) {
	assert(array.type == TYPE_ARRAY && array.managed);
	return array.val.managed_array;
	//return (i_managed_array *) ( (unsigned char *) array.items - offsetof(i_managed_array, items));
}

struct dynamic_type {
	void (*free)(void *);
	void (*retain)(void *); 
	i_refc (*release)(void *);
	i_refc (*get_refs)(void *);
	i_val (*call)(void *, const i_val *, size_t);
	const struct ti_interface *interfaces;
	size_t n_interfaces;
};

#define MAX_DYN_TYPES 8
static struct dynamic_type loaded_types[MAX_DYN_TYPES];

static i_type n_loaded_types = 0;

struct dynamic_type *get_dynamic_type(i_type type) {
	if(type < BERYL_N_STATIC_TYPES)
		return NULL;
	
	i_type type_id = type - BERYL_N_STATIC_TYPES;
	if(type_id >= n_loaded_types)
		return NULL;
	return &loaded_types[type_id];
}

void beryl_release(i_val val) {
	if(!val.managed)
		return;
	
	switch(val.type) {		
		case TYPE_ERR:
		case TYPE_STR: {
			i_managed_string *str = get_managed_str(val);
			if(str == NULL)
				return;
			assert(str->ref_count != 0);
			if(--str->ref_count == 0)
				int_free(str);
		} break;
		
		case TYPE_ARRAY: {
			i_managed_array *array = get_managed_array(val);
			i_size len = i_val_get_len(val);
			assert(array->ref_count != 0);
			if(--array->ref_count == 0) {
				for(i_size i = 0; i < len; i++) {
					beryl_release(array->items[i]);
				}
				int_free(array);
			}
		} break;
		
		default: {
			struct dynamic_type *dtype = get_dynamic_type(val.type);
			assert(dtype != NULL);
			if(dtype != NULL) {
				if(dtype->release(val.val.ptr) == 0)
					dtype->free(val.val.ptr);
			}
		}
	}
}

i_val beryl_retain(i_val val) {
	if(!val.managed)
		return val;
	
	switch(val.type) {		
		case TYPE_ERR:
		case TYPE_STR: {
			i_managed_string *str = get_managed_str(val);
			if(str == NULL)
				return val;
			assert(str->ref_count != 0);
			str->ref_count++;
		} break;
		
		case TYPE_ARRAY: {
			i_managed_array *array = get_managed_array(val);
			assert(array->ref_count != 0);
			array->ref_count++;
		} break;
		
		default: {
			struct dynamic_type *dtype = get_dynamic_type(val.type);
			assert(dtype != NULL);
			if(dtype != NULL)
				dtype->retain(val.val.ptr);
		}
	}
	
	return val;
}

i_refc beryl_get_references(i_val val) {
	if(!val.managed)
		return I_REF_COUNT_MAX;
	
	switch(val.type) {
		case TYPE_ERR:
		case TYPE_STR: {
			i_managed_string *str = get_managed_str(val);
			if(str == NULL)
				return I_REF_COUNT_MAX;
			assert(str->ref_count != 0);
			return str->ref_count;
		}
		
		case TYPE_ARRAY: {
			i_managed_array *array = get_managed_array(val);
			assert(array->ref_count != 0);
			return array->ref_count;
		}
		
		default: {
			struct dynamic_type *dtype = get_dynamic_type(val.type);
			if(dtype == NULL) {
				assert(false);
				return I_REF_COUNT_MAX;
			}
			return dtype->get_refs(val.val.ptr);
			//return I_REF_COUNT_MAX;
		}
	}
}

ti_interface_callback beryl_interface_val(i_val val, ti_interface_id interface) {
	struct dynamic_type *dtype = get_dynamic_type(val.type);
	if(dtype == NULL)
		return NULL;
	for(size_t i = 0; i < dtype->n_interfaces; i++) {
		struct ti_interface ti_i = dtype->interfaces[i];
		if(ti_i.id == interface)
			return ti_i.callback;
	}
	
	return NULL;
}

i_type beryl_add_type(
	void (*free)(void *), 
	void (*retain)(void *), 
	i_refc (*release)(void *), 
	i_refc (*get_refs)(void *), 
	i_val (*call)(void *, const i_val *, size_t),
	const ti_interface *interfaces,
	size_t n_interfaces
) {
	if(n_loaded_types == MAX_DYN_TYPES || n_loaded_types == I_MAX_TYPES)
		return TYPE_NULL;
	
	loaded_types[n_loaded_types] = (struct dynamic_type) { free, retain, release, get_refs, call, interfaces, n_interfaces };
	return BERYL_N_STATIC_TYPES + n_loaded_types++;
}

static stack_entry static_stack[STATIC_STACK_SIZE];
static stack_entry *stack_base = static_stack, *stack_top = static_stack, *stack_start = static_stack;
static stack_entry *stack_end = static_stack + STATIC_STACK_SIZE;

static struct scope_namespace current_namespace = { NULL, NULL };

static bool namespace_overlap(struct scope_namespace a, struct scope_namespace b) {
	return
		(a.start >= b.start && a.end <= b.end) //That is to say, if either a is inside b
		||
		(b.start >= a.start && b.end <= a.end) //or if b is inside a
	;
}

static bool push_stack(stack_entry *entry) {
	if(stack_top < stack_end) {
		*stack_top = *entry;
		stack_top++;
		beryl_retain(entry->val);
		return true;
	}
	return false;
}

static stack_entry *enter_scope() {
	stack_entry *old_base = stack_base;
	stack_base = stack_top;
	return old_base;
}

static void leave_scope(stack_entry *prev_base) {
	for(stack_entry *p = stack_top - 1; p >= stack_base; p--) {
		beryl_release(p->val);
	}
	stack_top = stack_base;
	stack_base = prev_base;
}

bool i_val_eq(i_val a, i_val b) {
	if(a.type != b.type)
		return false;
	switch(a.type) {
		case TYPE_INT: return i_val_get_int(a) == i_val_get_int(b);
		case TYPE_BOOL: return i_val_get_bool(a) == i_val_get_bool(b);
		case TYPE_TAG: return i_val_get_tag(a) == i_val_get_tag(b);
		case TYPE_FLOAT: return i_val_get_float(a) == i_val_get_float(b);

		case TYPE_STR: {
			i_size alen = i_val_get_len(a);
			i_size blen = i_val_get_len(b);
			if(alen != blen)
				return false;
			const char *str_a = i_val_get_raw_str(&a);
			const char *str_b = i_val_get_raw_str(&b);
			for(i_size i = alen; i > 0; i--) {
				if(*str_a != *str_b)
					return false;
				str_a++, str_b++;
			}
			return true;
			//return i_string_cmp(a.val.str_v, b.val.str_v);
		}

		case TYPE_FN:
			return i_val_get_fn(a).str == i_val_get_fn(b).str;
		case TYPE_EXT_FN:
			return a.val.ext_fn->callback == b.val.ext_fn->callback;
		
		case TYPE_ARRAY: {
			i_size alen = i_val_get_len(a);
			if(alen != i_val_get_len(b))
				return false;
			const i_val *a_a = i_val_get_raw_array(a);
			const i_val *b_a = i_val_get_raw_array(b);
			for(i_size i = 0; i < alen; i++)
				if(!i_val_eq(a_a[i], b_a[i]))
					return false;
			return true;
		}
		
		case TYPE_NULL:
			return true;
		
		default:
			return false;
	}
}

//Returns 0 if the values are equal, -1 if a is larger, and 1 if b is larger. 2 If they are not equal ant not comparable
int i_val_cmp(i_val a, i_val b) {
	if(a.type != b.type)
		return 2;
	switch(a.type) {
		case TYPE_INT: {
			i_int ai = i_val_get_int(a);
			i_int bi = i_val_get_int(b);
			if(ai == bi)
				return 0;
			else if(ai > bi)
				return -1;
			else
				return 1;
		}
		case TYPE_FLOAT: {
			i_float af = i_val_get_float(a);
			i_float bf = i_val_get_float(b);
			if(af == bf)
				return 0;
			else if(af > bf)
				return -1;
			else
				return 1;
		}
		//case TYPE_STR:
		//	for(const char *a = 
		
		default:
			return 2;
	}
}

//static struct int_err_ref err;
#define STACK_TRACE_MAX 16
static struct beryl_stack_trace stack_trace[STACK_TRACE_MAX];
static size_t stack_trace_counter = 0;

static void blame_token(struct lex_state *lex, struct lex_token tok) {
	if(stack_trace_counter < STACK_TRACE_MAX) {
		stack_trace[stack_trace_counter] = (struct beryl_stack_trace) { NULL, 0, lex->src, tok.src, lex->end - lex->src, tok.len };
		stack_trace_counter++;
	}
}

static void blame_range(struct lex_state *lex, const char *start, const char *end) {
	if(stack_trace_counter < STACK_TRACE_MAX) {
		stack_trace[stack_trace_counter] = (struct beryl_stack_trace) { NULL, 0, lex->src, start, lex->end - lex->src, end - start };
		stack_trace_counter++;
	}
}

#define MAX_ERR_ARGUMENTS 4
static i_val err_arguments[MAX_ERR_ARGUMENTS];
static size_t err_argument_counter = 0;

void beryl_blame_arg(i_val arg) {
	if(err_argument_counter < MAX_ERR_ARGUMENTS) {
		beryl_retain(arg);
		err_arguments[err_argument_counter++] = arg;
	}
}

static void blame_name(const char *name, size_t name_len) {
	if(stack_trace_counter < STACK_TRACE_MAX)
		stack_trace[stack_trace_counter++] = (struct beryl_stack_trace) { name, name_len, NULL, NULL, 0, 0 };
}

static void (*err_handler)(struct beryl_stack_trace *, size_t, const i_val *, size_t) = NULL;

static void clear_stack_trace() {
	stack_trace_counter = 0;
	
	for(size_t i = 0; i < err_argument_counter; i++) {
		beryl_release(err_arguments[i]);
		err_arguments[i] = I_NULL;
	}
	err_argument_counter = 0;
}

static void handle_stack_trace() {
	if((stack_trace_counter != 0 || err_argument_counter != 0) && err_handler != NULL)
		err_handler(stack_trace, stack_trace_counter, err_arguments, err_argument_counter);
	clear_stack_trace();
}

void beryl_set_err_handler(void (*handler)(struct beryl_stack_trace *, size_t, const i_val *, size_t)) {
	err_handler = handler;
}

static void perform_err_action(enum beryl_err_action action) {
	switch(action) {
		case BERYL_ERR_PROP:
			break;
		case BERYL_ERR_CATCH:
			clear_stack_trace();
			break;
		case BERYL_ERR_PRINT:
			handle_stack_trace();
			clear_stack_trace();
			break;
	}
}

static bool cmp_len_strs(const char *a, i_size al, const char *b, i_size bl) {
	if(al != bl)
		return false;
	for(; al > 0; al--) {
		if(*a != *b)
			return false;
		a++;
		b++;
	}
	return true;
}

static stack_entry *get_local(const char *name, i_size len) {
	for(stack_entry *var = stack_top - 1; var >= stack_base; var--) {
		if(cmp_len_strs(var->name, var->name_len, name, len))
			return var;
	}
	return NULL;
}

static stack_entry *get_global(const char *name, i_size len) {
	for(stack_entry *var = stack_top - 1; var >= stack_start; var--) {
		if(
			(var->namespace.start == NULL || namespace_overlap(var->namespace, current_namespace)) //Null indicates the top level namespace
			&& 
			cmp_len_strs(var->name, var->name_len, name, len)
		)
			return var;
	}
	return NULL;
}

bool beryl_set_var(const char *name, i_size name_len, i_val val, bool as_const) {
	stack_entry new_var = { name, name_len, val, as_const, current_namespace };
	stack_entry *var = get_global(name, name_len);
	if(var == NULL) {
		return push_stack(&new_var);
	}

	if(var->is_const)
		return false;
	
	beryl_release(var->val);
	beryl_retain(val);
	var->val = val;
	var->is_const = as_const;
	return true;
}

#define EXPR_ARG_STACK_SIZE 128 //Determines how many concurrent "temporaries" can exist on the stack
static i_val arg_stack[EXPR_ARG_STACK_SIZE];
static i_val
	*arg_stack_top = arg_stack, 
	*arg_stack_end = arg_stack + EXPR_ARG_STACK_SIZE
;


static bool push_arg(i_val val) {
	if(arg_stack_top < arg_stack_end) {
		*arg_stack_top = val;
		arg_stack_top++;
		return true;
	} else {
		return false;
	}
}

static i_val *save_arg_state() {
	return arg_stack_top;
}

static void restore_arg_state(i_val *state) {
	for(i_val *p = arg_stack_top - 1; p >= state; p--)
		beryl_release(*p);
	arg_stack_top = state;
}

static i_val parse_eval_expr(struct lex_state *lex, bool eval, bool ignore_endlines, stack_entry *current_assignment);
static i_val parse_eval_all_exprs(struct lex_state *lex, bool eval, unsigned char until_tok);

static unsigned rec_counter = 0;
#define RECURSION_LIMIT 64

static i_val return_val = { .type = TYPE_NULL };

void beryl_set_return_val(struct i_val val) {
	if(return_val.type == TYPE_NULL) {
		beryl_retain(val);
		return_val = val;
	}
}

static bool val_should_propagate(i_val val) {
	return val.type == TYPE_ERR || val.type == TYPE_MARKER_RETURN;
}

static i_val call_internal_fn(i_fn fn, const i_val *args, size_t n_args) {
	
	if(rec_counter >= RECURSION_LIMIT)
		return STATIC_ERR("Recursion limit exceeded");
	
	stack_entry *prev_scope = enter_scope();
	
	struct scope_namespace new_namespace = { fn.str, fn.str + fn.len };
	
	struct lex_state lex;
	lex_state_init(&lex, fn.str, fn.len);

	lex_accept(&lex, TOK_FN, NULL);

	size_t i = 0;
	while(!lex_accept(&lex, TOK_DO, NULL)) {
		struct lex_token arg = lex_pop(&lex);
		
		if(arg.type == TOK_VARARGS) {
			lex_pop(&lex);
			size_t n_varargs = n_args - i;
			i_val varargs;
			if(n_varargs > I_SIZE_MAX || BERYL_TYPEOF(varargs = i_val_managed_array(args + i, n_varargs, n_varargs)) == TYPE_NULL) {
				leave_scope(prev_scope);
				return STATIC_ERR("Out of memory");
			}
			stack_entry varargs_var = { "varargs", sizeof("varargs") - 1, varargs, false, new_namespace };
			push_stack(&varargs_var);
			beryl_release(varargs);
			i = n_args;
			break;
		}
		
		if(arg.type != TOK_SYM && arg.type != TOK_OP) {
			leave_scope(prev_scope);
			blame_token(&lex, arg);
			return STATIC_ERR("Not an argument"); //This shouldn't ever happen, as the code that constructs the function 
			// should check that the argument list only contains valid arguments regardless 
		}
		if(i == n_args) {
			leave_scope(prev_scope);
			return STATIC_ERR("Not enough arguments provided when calling function");
		}

		if(get_local(arg.content.sym.str, arg.content.sym.len) != NULL) {
			leave_scope(prev_scope);
			blame_token(&lex, arg);
			return STATIC_ERR("Duplicate argument name");
		}
		stack_entry arg_var = { UNPACK_S(arg.content.sym), args[i], false, new_namespace };
		push_stack(&arg_var);
		
		i++;
	}
	
	if(i != n_args) {
		leave_scope(prev_scope);
		return STATIC_ERR("Too many arguments when calling function");
	}
	
	rec_counter++;
	
	struct scope_namespace prev_namespace = current_namespace;
	current_namespace = new_namespace;
	
	i_val res = parse_eval_all_exprs(&lex, true, TOK_EOF);
	
	current_namespace = prev_namespace;
	
	rec_counter--;
	
	leave_scope(prev_scope);
	
	return res;
}

i_val beryl_call_fn(i_val fn, const i_val *args, size_t n_args, enum beryl_err_action err_action) {
	
	i_val res;
	
	switch(fn.type) {
		
		case TYPE_EXT_FN: {
			unsigned min_args;
			i_ext_fn *ext_fn = fn.val.ext_fn;
			if(ext_fn->arity >= 0) {
				min_args = ext_fn->arity;
				if(n_args > min_args) {
					res = STATIC_ERR("Too many arguments");
					break;
				}
			}
			else
				min_args = -ext_fn->arity - 1;
			
			if(n_args < min_args) {
				return STATIC_ERR("Not enough arguments");
				break;
			}
			
			res = ext_fn->callback(args, n_args);
			if(res.type == TYPE_ERR)
				blame_name(ext_fn->name, ext_fn->name_len);
		} break;
		
		case TYPE_FN:
			res = call_internal_fn(i_val_get_fn(fn), args, n_args);
			break;
		
		case TYPE_ARRAY: {
			if(n_args != 1) {
				res = STATIC_ERR("Can only index array with a single index");
				break;
			}
			
			if(args[0].type != TYPE_INT) {
				res = STATIC_ERR("Can only index array with integer values");
				break;
			}
			
			i_int index = i_val_get_int(args[0]);
			if(index < 0 || index >= i_val_get_len(fn)) {
				res = STATIC_ERR("Index out of range");
				break;
			}
			
			i_val val = i_val_get_raw_array(fn)[index];
			beryl_retain(val);
			res = val;
		} break;
		
		default: {
			struct dynamic_type *dtype = get_dynamic_type(fn.type);
			if(dtype == NULL) {
				beryl_blame_arg(fn);
				res = STATIC_ERR("Cannot evaluate as function");
				break;
			}
			
			res = dtype->call(fn.val.ptr, args, n_args);
		} break;
	}
	
	if(res.type == TYPE_ERR)
		perform_err_action(err_action);
	
	return res;
}

static i_val parse_eval_subexpr(struct lex_state *lex, bool eval);

static const char *parse_do_block(struct lex_state *lex, i_val *err) {
	struct lex_token end_tok;
	*err = parse_eval_all_exprs(lex, false, TOK_END);
	if(err->type != TYPE_ERR) {
		end_tok = lex_pop(lex);
		assert(end_tok.type == TOK_END);
	}
	
	return end_tok.src;
}

static i_val make_internal_fn(struct lex_state *lex, const char *start, const char *end) {
	if(end - start > I_SIZE_MAX) {
		blame_range(lex, start, end);
		return STATIC_ERR("Function body too large");
	}
	i_val fn = { .type = TYPE_FN, .managed = false, .val = { .str = start }, .len = end - start };
	return fn;
}

static i_val get_var(const char *name, i_size name_len) {
	stack_entry *var = get_global(name, name_len);
	if(var == NULL) {
		return STATIC_ERR("Unkown variable");
	}
	return beryl_retain(var->val);
}

static bool should_end_expr(struct lex_state *lex);

static i_val parse_eval_term(struct lex_state *lex, bool eval) {
	struct lex_token tok = lex_pop(lex);
	switch(tok.type) {
		
		case TOK_SYM:
		case TOK_OP:
			if(lex_accept(lex, TOK_ASSIGN, NULL)) { //Assignment expression
				stack_entry *var = get_global(UNPACK_S(tok.content.sym));
				if(eval) {
					if(var == NULL) {
						blame_token(lex, tok);
						return STATIC_ERR("Undeclared variable");
					}
					if(var->is_const) {
						blame_token(lex, tok);
						return STATIC_ERR("Attempting to override constant");
					}
				}
				
				i_val val = parse_eval_expr(lex, eval, false, var);
				
				if(val_should_propagate(val))
					return val;
				if(!eval)
					return I_NULL;
				
				beryl_release(var->val);
				var->val = beryl_retain(val);
				return val;
			} else {
				if(!eval)
					return I_NULL;

				i_val val = get_var(UNPACK_S(tok.content.sym));
				if(val.type == TYPE_ERR)
					blame_token(lex, tok);
				return val;
			}
		
		case TOK_STRING:
			return i_val_static_str(tok.content.sym.str, tok.content.sym.len);
		case TOK_INT:
			return i_val_int(tok.content.int_literal);
		case TOK_FLOAT:
			return i_val_float(tok.content.float_literal);
		
		case TOK_OPEN_BRACKET: {
			i_val res = parse_eval_expr(lex, eval, true, NULL);
			if(val_should_propagate(res))
				return res;
			
			struct lex_token close_bracket = lex_pop(lex);
			if(close_bracket.type != TOK_CLOSE_BRACKET) {
				blame_token(lex, close_bracket);
				beryl_release(res);
				return STATIC_ERR("Expected closing bracket");
			}
			
			return res;
		}
		
		case TOK_LET: {
			struct scope_namespace var_namespace = current_namespace;
			if(lex_accept(lex, TOK_GLOBAL, NULL))
				var_namespace = (struct scope_namespace) { NULL, NULL };
			
			struct lex_token name = lex_pop(lex);
			if(name.type != TOK_SYM && name.type != TOK_OP) {
				blame_token(lex, name);
				return STATIC_ERR("Expected variable indentifier");
			}
			
			struct lex_token assign_sym = lex_pop(lex);
			if(assign_sym.type != TOK_ASSIGN) {
				blame_token(lex, assign_sym);
				return STATIC_ERR("Expected '='");
			}
			
			struct i_val res = parse_eval_expr(lex, eval, false, NULL);
			if(val_should_propagate(res))
				return res;
			
			if(!eval)
				return I_NULL;
			
			if(get_local(UNPACK_S(name.content.sym)) != NULL) {
				beryl_release(res);
				blame_token(lex, name);
				return STATIC_ERR("Duplicate variable declaration");
			}
			
			push_stack(&((stack_entry) { UNPACK_S(name.content.sym), res, false, var_namespace }));
			return res;
		}

		case TOK_DO: {
			const char *fn_start = tok.src;
			struct i_val err;
			const char *fn_end = parse_do_block(lex, &err);
			if(err.type == TYPE_ERR)
				return err;

			return make_internal_fn(lex, fn_start, fn_end);
		} break;
		
		case TOK_FN: {
			const char *fn_start = tok.src;
			while(!lex_accept(lex, TOK_DO, NULL)) {
				struct lex_token arg = lex_pop(lex);
				if(arg.type == TOK_VARARGS) {
					arg = lex_pop(lex);
					if(arg.type != TOK_DO) {
						blame_token(lex, arg);
						return STATIC_ERR("Expected 'do' following '...' (variadic declaration must be last argumen)");
					}
					break;
				}
				if(arg.type != TOK_SYM && arg.type != TOK_OP) {
					blame_token(lex, arg);
					return STATIC_ERR("Expected function argument name or 'do'");
				}
			}
			
			i_val err;
			const char *fn_end = parse_do_block(lex, &err);
			if(err.type == TYPE_ERR)
				return err;
			
			return make_internal_fn(lex, fn_start, fn_end);
		}

		case TOK_ERR: {
			blame_token(lex, tok);
			size_t err_str_len;
			const char *err_str = lex_err_str(tok, &err_str_len);
			return i_val_err(i_val_static_str(err_str, err_str_len));
		}
		
		case TOK_EOF:
			blame_token(lex, tok);
			return STATIC_ERR("Unexpected end of file");
		
		case TOK_ENDLINE:
			blame_token(lex, tok);
			return STATIC_ERR("Unexpected end of line");
		
		default:
			blame_token(lex, tok);
			return STATIC_ERR("Unexpected token");
	}
}

static i_val parse_eval_subexpr(struct lex_state *lex, bool eval) {
	i_val term = parse_eval_term(lex, eval);
	if(val_should_propagate(term))
		return term;
	
	struct lex_token op;
	while(lex_accept(lex, TOK_OP, &op)) {
		stack_entry *fn_entry = get_global(UNPACK_S(op.content.sym));
		if(fn_entry == NULL) {
			beryl_release(term);
			blame_token(lex, op);
			return STATIC_ERR("Unkown function");
		}
		
		i_val second_arg = parse_eval_term(lex, eval);
		if(val_should_propagate(second_arg)) {
			beryl_release(term);
			return second_arg;
		}
		
		i_val res;
		if(eval) {
			i_val args[2] = { term, second_arg };
			res = beryl_call_fn(fn_entry->val, args, 2, BERYL_ERR_PROP);
			if(res.type == TYPE_ERR)
				blame_token(lex, op);
		} else
			res = I_NULL;
		
		beryl_release(term);
		beryl_release(second_arg);
		term = res; //So for the next iteration of the loop (if there is one) the result of the first binop evaluation will be used as 
		// the first argument for the next one; i.e x + y + z = (x + y) + z; 
		
		if(res.type == TYPE_ERR)
			return res;
	}
	
	return term;
}

static bool should_end_expr(struct lex_state *lex) {
	struct lex_token tok = lex_peek(lex);
	switch(tok.type) {
		case TOK_CLOSE_BRACKET:
		case TOK_EOF:
		case TOK_END:
			return true;
		
		default:
			return false;
	}
}

static i_val parse_eval_expr(struct lex_state *lex, bool eval, bool ignore_endlines, stack_entry *current_assignment) {

	struct lex_token expr_start = lex_peek(lex);
	
	i_val fn = parse_eval_subexpr(lex, eval);
	if(val_should_propagate(fn))
		return fn;

	i_val *args_begin = save_arg_state();
	
	while(true) {
		if(ignore_endlines)
			lex_accept(lex, TOK_ENDLINE, NULL);
		else if(lex_peek(lex).type == TOK_ENDLINE)
			break;
	
		if(should_end_expr(lex))
			break;
		
		i_val arg = parse_eval_subexpr(lex, eval);
		if(val_should_propagate(arg)) {
			beryl_release(fn);
			restore_arg_state(args_begin);
			return arg;
		}
		
		if(!eval)
			continue; //Dont push the value onto the stack if we're just parsing the code

		bool ok = push_arg(arg);
		if(!ok) {
			beryl_release(fn);
			restore_arg_state(args_begin);
			blame_token(lex, expr_start);
			return STATIC_ERR("Argument stack overflow");
		}
		
	}
	
	if(!eval)
		return I_NULL;
	
	if(args_begin == arg_stack_top) //If no arguments were evaluated
		return fn;
	
	if(current_assignment != NULL) {
		beryl_release(current_assignment->val);
		current_assignment->val = I_NULL;
	}
	i_val res = beryl_call_fn(fn, args_begin, arg_stack_top - args_begin, BERYL_ERR_PROP);
	beryl_release(fn);
	restore_arg_state(args_begin);
	
	if(res.type == TYPE_ERR)
		blame_token(lex, expr_start);
	
	return res;
}

struct i_val parse_eval_all_exprs(struct lex_state *lex, bool eval, unsigned char until_tok) {
	i_val res = I_NULL;
	lex_accept(lex, TOK_ENDLINE, false);
	while(lex_peek(lex).type != until_tok) {
		beryl_release(res);
		res = parse_eval_expr(lex, eval, false, NULL);
		if(val_should_propagate(res))
			break;
		lex_accept(lex, TOK_ENDLINE, false);
	}
	if(res.type == TYPE_MARKER_RETURN) {
		res = return_val;
		return_val = I_NULL;
	}
	
	return res;
}

struct i_val beryl_eval(const char *src, size_t src_len, bool new_scope, enum beryl_err_action err_action) {
	struct lex_state lex;
	lex_state_init(&lex, src, src_len);
	
	stack_entry *prev_scope;
	if(new_scope)
		prev_scope = enter_scope();
	
	i_val res = parse_eval_all_exprs(&lex, true, TOK_EOF);	
	perform_err_action(err_action);
	
	if(new_scope)
		leave_scope(prev_scope);
	
	return res;
}

void beryl_clear() {
	assert(arg_stack_top == arg_stack); //That is to say, no expression is *currently* in the middle of being evaluated
	clear_stack_trace();
	
	stack_base = stack_start;
	for(stack_entry *p = stack_top - 1; p >= stack_base; p--)
		beryl_release(p->val);
	stack_top = stack_base;
	
	n_loaded_types = 0;
}
