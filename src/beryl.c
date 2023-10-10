#include "berylscript.h"
#include "lexer.h"

#include "utils.h"


typedef struct i_val i_val;

typedef struct i_val_pair i_val_pair;

typedef struct beryl_table beryl_table;

typedef struct beryl_object beryl_object;
typedef struct beryl_object_class beryl_object_class;

typedef struct i_managed_str {
	i_refc ref_c;
	char str[];
} i_managed_str;

typedef struct i_managed_array {
	i_refc ref_c;
	i_size cap;
	i_val items[];
} i_managed_array;

typedef struct stack_trace_entry {
	unsigned char type;
	const char *src, *src_end;
	const char *str;
	size_t len;
} stack_trace_entry;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define ERR_STACK_TRACE_SIZE 16
static size_t n_err_stack_trace_entries = 0;
static stack_trace_entry err_stack_trace[ERR_STACK_TRACE_SIZE];

static bool push_stack_trace(stack_trace_entry entry) {
	if(n_err_stack_trace_entries == ERR_STACK_TRACE_SIZE)
		return false;
		
	err_stack_trace[n_err_stack_trace_entries++] = entry;
	return true;
}

static void blame_token(struct lex_state *lex, struct lex_token tok) {
	push_stack_trace( (stack_trace_entry) { 0, lex->src, lex->end, tok.src, tok.len } );
}

static void blame_range(struct lex_state *lex, const char *start, const char *end) {
	push_stack_trace( (stack_trace_entry) { 0, lex->src, lex->end, start, end - start } );
}

static void blame_name(const char *str, size_t len) {
	push_stack_trace( (stack_trace_entry) { .type = 1, .str = str, .len = len } );
}

#define MAX_BLAMED_ARGS 8
static size_t n_blamed_args = 0;
static i_val blamed_args[MAX_BLAMED_ARGS];

void beryl_blame_arg(i_val val) {
	if(n_blamed_args == MAX_BLAMED_ARGS)
		return;
	
	blamed_args[n_blamed_args++] = beryl_retain(val);
}

static void clear_err() {
	for(size_t i = 0; i < n_blamed_args; i++)
		beryl_release(blamed_args[i]);
	
	n_blamed_args = 0;
	n_err_stack_trace_entries = 0;
}

static void *beryl_errf = NULL;

static void (*print_callback)(void *, const char *str, size_t len) = NULL;
static void (*print_i_val_callback)(void *, i_val val) = NULL;

void beryl_set_io(void (*print)(void *, const char *, size_t), void (*print_i_val)(void *, i_val), void *err_f) {
	beryl_errf = err_f;
	print_callback = print;
	print_i_val_callback = print_i_val;
}

void beryl_print_i_val(void *f, i_val val) {
	if(print_i_val_callback == NULL)
		return;

	print_i_val_callback(f, val);
}

static void print_string(void *f, const char *msg) {
	if(print_callback == NULL)
		return;
	
	size_t len = 0;
	for(const char *c = msg; *c != '\0'; c++)
		len++;
	
	print_callback(f, msg, len);
}

static void print_bytes(void *f, const char *b, size_t len) {
	if(print_callback == NULL)
		return;
	print_callback(f, b, len);
}

void beryl_i_vals_printf(void *f, const char *str, size_t strlen, const i_val *vals, unsigned n) {
	assert(n <= 10);
	const char *str_end = str + strlen;
	const char *prev = NULL;
	for(const char *c = str; c < str_end; c++) {
		if(*c == '%' && c != str_end - 1) {
			if(prev != NULL) {
				print_bytes(f, prev, c - prev);
				prev = NULL;
			}
			c++;
			if(*c < '0' || *c > '9')
				continue;
			unsigned i = *c - '0';
			if(i < n)
				beryl_print_i_val(f, vals[i]);
		} else if(prev == NULL) {
			prev = c;
		}
	}
	
	if(prev != NULL)
		print_bytes(f, prev, str_end - prev);
}

static void log_err(i_val err_val) {
	#ifndef NO_COLOURS
	print_string(beryl_errf, "\x1B[31m");
	#endif
	print_string(beryl_errf, "\n----------------\n");
	for(size_t i = 1; i <= n_err_stack_trace_entries; i++) {
		size_t index = n_err_stack_trace_entries - i;
		stack_trace_entry entry = err_stack_trace[index];
		if(entry.type == 1) {
			print_string(beryl_errf, "In:\n");
			print_bytes(beryl_errf, entry.str, entry.len);
		} else if(entry.type == 0) {
			print_string(beryl_errf, "At:\n");
			const char *line_start;
			size_t n_tabs = 0;
			size_t n_spaces = 0;
			for(line_start = entry.str - 1; line_start >= entry.src; line_start--) {
				if(*line_start == '\n') {
					line_start++;
					break;
				}
				if(*line_start == '\t')
					n_tabs++;
				else
					n_spaces++;
			}
			const char *line_end;
			for(line_end = entry.str + entry.len; line_end < entry.src_end; line_end++) {
				if(*line_end == '\n')
					break;
			}
			if(line_start < entry.src)
				line_start = entry.src;
			if(line_end < line_start)
				line_end = line_start;

			print_bytes(beryl_errf, line_start, line_end - line_start);
			print_string(beryl_errf, "\n");
			while(n_tabs--)
				print_bytes(beryl_errf, "\t", 1);
			while(n_spaces--)
				print_bytes(beryl_errf, " ", 1);
			for(size_t i = 0; i < entry.len; i++)
				print_bytes(beryl_errf, "^", 1);
		}
		print_string(beryl_errf, "\n----------------\n");
	}
	
	if(n_blamed_args > 0) {
		for(size_t i = 0; i < n_blamed_args; i++) {
			beryl_print_i_val(beryl_errf, blamed_args[i]);
			print_string(beryl_errf, " ");
		}
		print_string(beryl_errf, "\n----------------\n");
	}
	
	assert(BERYL_TYPEOF(err_val) == TYPE_ERR);
	const char *err_str = beryl_get_raw_str(&err_val);
	i_size err_str_len = BERYL_LENOF(err_val);
	print_string(beryl_errf, "Error: ");
	beryl_i_vals_printf(beryl_errf, err_str, err_str_len, blamed_args, n_blamed_args);
	print_string(beryl_errf, "\n");
	
	#ifndef NO_COLOURS
	print_string(beryl_errf, "\x1B[0m");
	#endif
}

bool cmp_len_strs(const char *a, size_t alen, const char *b, size_t blen) {
	if(alen != blen)
		return false;
	
	while(alen--) {
		if(*a != *b)
			return false;
		a++, b++;
	}
	
	return true;
}

void (*free_callback)(void *ptr) = NULL;
void *(*alloc_callback)(size_t n) = NULL;
void *(*realloc_callback)(void *ptr, size_t) = NULL;

void beryl_set_mem(void *(*alloc)(size_t), void (*free)(void *), void *(*realloc)(void *, size_t)) {
	free_callback = free;
	alloc_callback = alloc;
	realloc_callback = realloc;
}

void beryl_free(void *ptr) {
	if(free_callback == NULL)
		return;
	free_callback(ptr);
}

void *beryl_alloc(size_t n) {
	if(alloc_callback == NULL)
		return NULL;
	return alloc_callback(n);
}

#define TMP_ALLOC_BUFFER_SIZE 512
static unsigned char tmp_alloc_buffer[TMP_ALLOC_BUFFER_SIZE];

static bool tmp_buffer_free = true;

//#define MIN(x, y) ((x) < (y)? (x) : (y))

void *beryl_realloc(void *ptr, size_t n) {
	if(ptr == tmp_alloc_buffer) {
		assert(!tmp_buffer_free);
		
		if(n <= TMP_ALLOC_BUFFER_SIZE)
			return ptr;
			
		unsigned char *new_alloc = beryl_alloc(n);
		if(new_alloc == NULL)
			return NULL;
			
		unsigned char *p = ptr;
		size_t cpy_len = MIN(TMP_ALLOC_BUFFER_SIZE, n);
		unsigned char *t = new_alloc;
		while(cpy_len--)
			*(t++) = *(p++);
		tmp_buffer_free = true;
		return new_alloc;
	}
	if(realloc_callback == NULL)
		return NULL;
	return realloc_callback(ptr, n);
}

void *beryl_talloc(size_t n) {
	if(n < sizeof(tmp_alloc_buffer) && tmp_buffer_free) {
		tmp_buffer_free = false;
		return tmp_alloc_buffer;
	}
	return beryl_alloc(n);
}

void beryl_tfree(void *ptr) {
	if(ptr == tmp_alloc_buffer) {
		assert(!tmp_buffer_free); //Check for double free
		tmp_buffer_free = true;
	} else {
		beryl_free(ptr);
	}
}

static i_refc *get_reference_counter(i_val of_val) {
	switch(of_val.type) {
		case TYPE_TABLE:
			if(!of_val.managed)
				return NULL;
			return &of_val.val.table->ref_c;
		
		case TYPE_ERR:
		case TYPE_STR:
			if(!of_val.managed || of_val.len <= BERYL_INLINE_STR_MAX_LEN)
				return NULL;
			return &of_val.val.managed_str->ref_c;
		
		case TYPE_ARRAY:
			if(!of_val.managed)
				return NULL;
			return &of_val.val.managed_array->ref_c;
		
		case TYPE_OBJECT:
			return &of_val.val.object->ref_c;
		
		default:
			return NULL;
	}
}

i_val beryl_retain(i_val val) {
	i_refc *ref_counter = get_reference_counter(val);
	if(ref_counter == NULL)
		return val;
	
	if(*ref_counter != I_REFC_MAX)
		(*ref_counter)++;
	return val;
}

void beryl_retain_values(const i_val *items, size_t n) {
	while(n--)
		beryl_retain(*(items++));
}

void beryl_release(i_val val) {
	i_refc *ref_counter = get_reference_counter(val);
	if(ref_counter == NULL)
		return;
	if(*ref_counter == I_REFC_MAX) 	// If it has somehow reached the max value, then it should never decrease again. 
		return;						// Better to leak memory than to potentially use it after freeing it.
	
	assert(*ref_counter != 0);
	
	if(--(*ref_counter) == 0) {
		switch(val.type) {
			
			case TYPE_ERR:
			case TYPE_STR:
				beryl_free(val.val.managed_str);
				break;
			case TYPE_TABLE: {
				i_val_pair *iter = NULL;
				while( (iter = beryl_iter_table(val, iter)) ) {
					beryl_release(iter->key);
					beryl_release(iter->val);
				}
				beryl_free(val.val.table);
			} break;
			case TYPE_ARRAY: {
				const i_val *items = beryl_get_raw_array(val);
				for(i_size i = 0; i < BERYL_LENOF(val); i++)
					beryl_release(items[i]);
				beryl_free(val.val.managed_array);
			} break;
			
			case TYPE_OBJECT: {
				beryl_object *obj = val.val.object;
				if(obj->obj_class->free != NULL)
					obj->obj_class->free(obj);
				beryl_free(obj);
			} break;
			
			default:
				assert(false);
		}
	}
}

void beryl_release_values(const i_val *items, size_t n) {
	while(n--)
		beryl_release(*(items++));
}

i_refc beryl_get_refcount(i_val val) {
	i_refc *counter = get_reference_counter(val);
	if(counter == NULL)
		return I_REFC_MAX;
	return *counter;
}

const char *beryl_get_raw_str(const i_val *str) {
	assert(str->type == TYPE_STR || str->type == TYPE_ERR);
	if(!str->managed)
		return str->val.str;
	
	if(str->len <= BERYL_INLINE_STR_MAX_LEN)
		return &str->val.inline_str[0];
	
	return &str->val.managed_str->str[0];
}

i_float beryl_as_num(struct i_val val) {
	assert(BERYL_TYPEOF(val) == TYPE_NUMBER);
	return val.val.num_v;
}

bool beryl_as_bool(struct i_val val) {
	assert(BERYL_TYPEOF(val) == TYPE_BOOL);
	return val.val.bool_v;
}

beryl_tag beryl_as_tag(i_val val) {
	assert(BERYL_TYPEOF(val) == TYPE_TAG);
	return val.val.tag;
}

i_val beryl_new_tag() {
	static beryl_tag tag_counter = 0;
	if(tag_counter == BERYL_MAX_TAGS)
		return BERYL_NULL;
	
	return (i_val) { .type = TYPE_TAG, .len = 0, .managed = false, .val.tag = tag_counter++ };
}

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

#define STATIC_STACK_SIZE 256 //How many (local) variables can exist

static stack_entry static_stack[STATIC_STACK_SIZE];
static stack_entry *stack_base = static_stack, *stack_top = static_stack, *stack_start = static_stack;
static stack_entry *stack_end = static_stack + STATIC_STACK_SIZE;

static struct scope_namespace current_namespace = { NULL, NULL };

#define GLOBALS_LOOKUP_TABLE_SIZE 256
static stack_entry globals_lookup[GLOBALS_LOOKUP_TABLE_SIZE];
//static stack_entry *globals_lookup_end = globals_lookup + GLOBALS_LOOKUP_TABLE_SIZE;

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

bool beryl_bind_name(const char *name, i_size name_len, i_val val, bool is_const) {
	stack_entry entry = { name, name_len, val, is_const, { NULL, NULL } };
	return push_stack(&entry);
}

static stack_entry *enter_scope() {
	stack_entry *old_base = stack_base;
	stack_base = stack_top;
	return old_base;
}

void *beryl_new_scope() {
	return enter_scope();
}

static void leave_scope(stack_entry *prev_base) {
	for(stack_entry *p = stack_top - 1; p >= stack_base; p--) {
		beryl_release(p->val);
	}
	stack_top = stack_base;
	stack_base = prev_base;
}

void beryl_restore_scope(void *prev) {
	leave_scope(prev);
}

static stack_entry *get_local(const char *name, i_size len) {
	for(stack_entry *var = stack_top - 1; var >= stack_base; var--) {
		if(cmp_len_strs(var->name, var->name_len, name, len))
			return var;
	}
	return NULL;
}

static stack_entry *index_globals(const char *name, i_size len) {
	size_t hash = 0;
	for(const char *c = name; c < name + len; c++) {
		hash *= 7;
		hash += *c;
	}
	size_t index = hash % GLOBALS_LOOKUP_TABLE_SIZE;
	
	for(size_t i = index; i < GLOBALS_LOOKUP_TABLE_SIZE; i++) {
		stack_entry *entry = &globals_lookup[i];
		if(entry->name == NULL)
			return entry;
		
		if(cmp_len_strs(entry->name, entry->name_len, name, len))
			return entry;
	}
	
	for(size_t i = 0; i < index; i++) {
		stack_entry *entry = &globals_lookup[i];
		if(entry->name == NULL)
			return entry;
		
		if(cmp_len_strs(entry->name, entry->name_len, name, len))
			return entry;
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
	
	//If nothing was found on the stack, look in the globals lookup table
	//return index_globals(name, len);
	stack_entry *global = index_globals(name, len);
	if(global == NULL || global->name == NULL)
		return NULL;
	return global;
}

bool beryl_set_var(const char *name, i_size name_len, i_val val, bool as_const) {
	stack_entry new_var = { name, name_len, val, as_const, current_namespace };
	
	stack_entry *entry = index_globals(name, name_len);
	if(entry == NULL) //Out of space
		return false;

	if(entry->name == NULL) { //If the var doesnt exist in the table
		*entry = new_var;
		beryl_retain(entry->val);
		return true;
	}
	
	if(entry->is_const)
		return false;
	
	beryl_release(entry->val);
	entry->val = beryl_retain(val);
	entry->is_const = as_const;
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

static void restore_arg_state(i_val *state, bool release) {
	if(release)
		for(i_val *p = arg_stack_top - 1; p >= state; p--)
			beryl_release(*p);

	arg_stack_top = state;
}

bool beryl_is_integer(i_val val) {
	if(BERYL_TYPEOF(val) != TYPE_NUMBER)
		return false;
	i_float f = beryl_as_num(val);
	if(f >= BERYL_NUM_MAX_INT || f <= -BERYL_NUM_MAX_INT)
		return true;
	return (i_float) (long long) f - f == 0.0;
}

int beryl_val_cmp(i_val a, i_val b) { // -1 if a is larger, 0 if they are equal, 1 if b is larger, and 2 if they are not comparable and/or not equal
	if(BERYL_TYPEOF(a) != BERYL_TYPEOF(b))
		return 2;
	
	switch(BERYL_TYPEOF(a)) {
		case TYPE_NULL:
			return 0;
		
		case TYPE_BOOL: {
			if(beryl_as_bool(a) == beryl_as_bool(b))
				return 0;
			return 2;
		}
		
		case TYPE_TAG: {
			if(beryl_as_tag(a) == beryl_as_tag(b))
				return 0;
			else
				return 2;
		}
		
		case TYPE_NUMBER: {
			i_float n_a = beryl_as_num(a);
			i_float n_b = beryl_as_num(b);
			if(n_a == n_b)
				return 0;
			if(n_a > n_b)
				return -1;
			return 1;
		}
		
		case TYPE_STR: {
			const char *s_a = beryl_get_raw_str(&a);
			const char *s_b = beryl_get_raw_str(&b);
			i_size l_a = BERYL_LENOF(a), l_b = BERYL_LENOF(b);
			
			i_size minl = MIN(l_a, l_b);
			for(i_size i = 0; i < minl; i++) {
				if(s_a[i] != s_b[i]) {
					if(s_a[i] > s_b[i])
						return -1;
					return 1;
				}
			}
			
			if(l_a == l_b)
				return 0;
			if(l_a > l_b)
				return -1;
			return 1;
		}
		
		case TYPE_ARRAY: {
			const i_val *a_a = beryl_get_raw_array(a);
			const i_val *a_b = beryl_get_raw_array(b);
			i_size l_a = BERYL_LENOF(a), l_b = BERYL_LENOF(b);
			
			i_size minl = MIN(l_a, l_b);
			for(i_size i = 0; i < minl; i++) {
				int cmp = beryl_val_cmp(a_a[i], a_b[i]);
				if(cmp == -1 || cmp == 1)
					return cmp;
				if(cmp == 2) //If any of the items of the arrays are incomparable, then the entire arrays are incomparable
					return 2;
			}
			
			if(l_a == l_b)
				return 0;
			if(l_a > l_b)
				return -1;
			return 1;
		}
		
		default:
			return 2;
	}
}

i_val beryl_new_string(i_size len, const char *from) {
	i_val res = { .type = TYPE_STR, .managed = true, .len = len };
	char *str;
	if(len <= BERYL_INLINE_STR_MAX_LEN)
		str = &res.val.inline_str[0];
	else {
		i_managed_str *mstr = beryl_alloc(sizeof(i_managed_str) + sizeof(char) * len);
		if(mstr == NULL)
			return BERYL_NULL;
			
		mstr->ref_c = 1;
		str = &mstr->str[0];
		
		res.val.managed_str = mstr;
	}
	
	if(from != NULL) {
		while(len--) {
			*(str++) = *(from++);
		}
	}
	
	return res;
}

i_val beryl_new_object(beryl_object_class *obj_class) {
	assert(obj_class->obj_size >= sizeof(beryl_object));
	struct beryl_object *obj = beryl_alloc(obj_class->obj_size);
	if(obj == NULL)
		return BERYL_NULL;
	
	obj->ref_c = 1;
	obj->obj_class = obj_class;
	
	i_val val = { .val = { .object = obj }, .len = 0, .managed = true, .type = TYPE_OBJECT };
	return val;
}

beryl_object_class *beryl_object_class_type(i_val val) {
	if(BERYL_TYPEOF(val) != TYPE_OBJECT)
		return NULL;
	return beryl_as_object(val)->obj_class;
}

beryl_object *beryl_as_object(i_val val) {
	assert(BERYL_TYPEOF(val) == TYPE_OBJECT);
	return val.val.object;
}

i_val beryl_str_as_err(i_val str) {
	assert(BERYL_TYPEOF(str) == TYPE_STR);
	i_val err = beryl_retain(str);
	err.type = TYPE_ERR;
	return err;
}

i_val beryl_err_as_str(i_val err) {
	assert(BERYL_TYPEOF(err) == TYPE_ERR);
	i_val str = beryl_retain(err);
	str.type = TYPE_STR;
	return str;
}

static bool is_hashable(i_val key) {
	switch(BERYL_TYPEOF(key)) {
		
		case TYPE_STR:
		case TYPE_TAG:
		case TYPE_BOOL:
			return true;
		
		case TYPE_NUMBER:
			return beryl_is_integer(key);
		
		default:
			return false;
	}
}

static size_t hash_val(i_val key) {
	assert(is_hashable(key));
	
	switch(BERYL_TYPEOF(key)) {
		case TYPE_NUMBER:
			return beryl_as_num(key);
		case TYPE_BOOL:
			return beryl_as_bool(key);
			
		case TYPE_STR: {
			size_t hash = 0;
			i_size len = key.len;
			const char *str = beryl_get_raw_str(&key);
			while(len--) {
				hash *= 7;
				hash += *str;
				str++;
			}
			return hash;
		}
		
		default:
			assert(false);
			return 0;
	}
}

static i_val_pair *search_table(beryl_table *table, i_val key, i_size from, i_size until) {
	assert(until <= table->cap);
	
	for(i_size i = from; i < until; i++) {
		if(BERYL_TYPEOF(table->entries[i].key) == TYPE_NULL)
			return &table->entries[i];
		if(beryl_val_cmp(table->entries[i].key, key) == 0)
			return &table->entries[i];
	}
	
	return NULL;
}

static i_val_pair *table_get(beryl_table *table, i_val key) {
	if(!is_hashable(key))
		return NULL;
	if(table->cap == 0)
		return NULL;
	
	size_t hash = hash_val(key);
	i_size index = hash % table->cap;
	
	i_val_pair *entry = search_table(table, key, index, table->cap);
	if(entry != NULL)
		return entry;
	
	return search_table(table, key, 0, index);
}

static i_val index_table(i_val table, i_val key) {
	assert(BERYL_TYPEOF(table) == TYPE_TABLE);
	
	i_val_pair *entry = table_get(table.val.table, key);
	if(entry == NULL || BERYL_TYPEOF(entry->key) == TYPE_NULL)
		return BERYL_NULL;
	
	return beryl_retain(entry->val);
}

int beryl_table_insert(i_val *table_v, i_val key, i_val val, bool replace) {
	assert(BERYL_TYPEOF(*table_v) == TYPE_TABLE);
	beryl_table *table = table_v->val.table;
	assert(table->ref_c == 1);
	assert(table_v->managed); //Static tables cannot be modified
	
	if(!is_hashable(key))
		return 3;
	
	if(table_v->len == table->cap)
		return 1;
	assert(table->cap > table_v->len);

	i_val_pair *entry = table_get(table, key);
	assert(entry != NULL);
	if(BERYL_TYPEOF(entry->key) != TYPE_NULL) {
		if(!replace)
			return 2; //Key already exists and replace is false
		
		beryl_release(entry->val);
		beryl_release(entry->key);
	} else
		table_v->len++;

	entry->key = beryl_retain(key);
	entry->val = beryl_retain(val);
	
	return 0;	
}

i_val beryl_new_table(i_size cap, bool padding) {
	if(padding) {
		i_size padded_size = (cap * 3) / 2;
		if(padded_size < cap)
			return BERYL_NULL;
		cap = padded_size;
	}
	
	beryl_table *table = beryl_alloc(sizeof(beryl_table) + sizeof(i_val_pair) * cap);
	if(table == NULL)
		return BERYL_NULL;
	
	table->cap = cap;
	table->ref_c = 1;
	
	for(i_size i = 0; i < cap; i++)
		table->entries[i] = (i_val_pair) { BERYL_NULL, BERYL_NULL };
	
	return (i_val) { .type = TYPE_TABLE, .val.table = table, .managed = true, .len = 0 };
}

i_val beryl_static_table(i_size cap, unsigned char *bytes, size_t bytes_size) {
	assert(sizeof(beryl_table) + sizeof(i_val_pair) * cap <= bytes_size); (void) bytes_size;
	
	beryl_table *table = (beryl_table *) bytes;
	table->cap = cap;
	table->ref_c = 1;
	for(i_size i = 0; i < cap; i++) {
		table->entries[i] = (i_val_pair) { BERYL_NULL, BERYL_NULL };
	}
	
	return (i_val) { .type = TYPE_TABLE, .managed = false, .val.table = table, .len = 0 };
}

bool beryl_table_should_grow(struct i_val table, i_size extra) {
	assert(BERYL_TYPEOF(table) == TYPE_TABLE);
	i_size expected_capacity = (table.len + extra) * 4 / 3;
	return table.val.table->cap < expected_capacity || expected_capacity < table.len;
}

i_val beryl_new_array(i_size len, const i_val *items, i_size fit_for, bool padded) {
	assert(fit_for >= len);
	i_size cap = padded ? (fit_for * 3 / 2) + 1 : fit_for;
	if(padded && cap <= fit_for) // If padded is true, then cap must be at least fit_for + 1
		return BERYL_NULL;

	i_managed_array *array = beryl_alloc(sizeof(i_managed_array) + sizeof(i_val) * cap);
	if(array == NULL)
		return BERYL_NULL;
	array->cap = cap;
	array->ref_c = 1;
	
	if(items != NULL) {
		for(i_size i = 0; i < len; i++) {
			array->items[i] = beryl_retain(items[i]);
		}
	} else
		for(i_size i = 0; i < len; i++)
			array->items[i] = BERYL_NULL;
	
	return (i_val) { .type = TYPE_ARRAY, .len = len, .managed = true, .val.managed_array = array };
}

i_size beryl_get_array_capacity(i_val val) {
	assert(BERYL_TYPEOF(val) == TYPE_ARRAY);
	if(val.managed)
		return val.val.managed_array->cap;
	else
		return BERYL_LENOF(val);
}

const i_val *beryl_get_raw_array(i_val array) {
	assert(BERYL_TYPEOF(array) == TYPE_ARRAY);
	if(array.managed)
		return &array.val.managed_array->items[0];
	return array.val.static_array;
}

bool beryl_array_push(i_val *array, i_val val) {
	assert(BERYL_TYPEOF(*array) == TYPE_ARRAY);
	assert(beryl_get_refcount(*array) == 1);
	assert(array->managed);
	
	i_managed_array *ma = array->val.managed_array;
	if(ma->cap == array->len) {
		i_size new_cap = ma->cap * 3 / 2 + 1;
		if(new_cap <= ma->cap)
			return false;
		
		i_managed_array *new_array = beryl_alloc(sizeof(i_managed_array) + sizeof(i_val) * new_cap);
		if(new_array == NULL)
			return false;
		new_array->ref_c = 1;
		
		for(i_size i = 0; i < array->len; i++)
			new_array->items[i] = ma->items[i];
		new_array->cap = new_cap;
		
		array->val.managed_array = new_array;
		beryl_free(ma);
		ma = new_array;
	}
	
	assert(ma->cap > array->len);
	ma->items[array->len++] = beryl_retain(val);
	return true;
}

i_val_pair *beryl_iter_table(i_val table_v, i_val_pair *iter) {
	assert(BERYL_TYPEOF(table_v) == TYPE_TABLE);
	
	beryl_table *table = table_v.val.table;
	i_val_pair *table_end = table->entries + table->cap;
	
	assert((iter >= table->entries && iter < table_end) || iter == NULL);
	if(table_v.len == 0)
		return NULL;
	
	if(iter == NULL)
		iter = table->entries - 1;
	
	for(i_val_pair *i = iter + 1; i < table_end; i++) {
		if(BERYL_TYPEOF(i->key) != TYPE_NULL)
			return i;
	}
	return NULL;
}

static i_val parse_eval_expr(struct lex_state *lex, bool eval, bool ignore_newlines);
static i_val parse_eval_all_exprs(struct lex_state *lex, bool eval, unsigned char until_tok, struct lex_token *end_tok);
static i_val parse_eval_args(struct lex_state *lex, bool eval, bool ignore_newlines, i_size *n_args);

static i_val parse_do_block(struct lex_state *lex, struct lex_token intial_token) {
	struct lex_token end_tok;
	i_val res = parse_eval_all_exprs(lex, false, TOK_END, &end_tok);
	if(BERYL_TYPEOF(res) == TYPE_ERR)
		return res;

	size_t len = end_tok.src - intial_token.src;
	if(len > I_SIZE_MAX) {
		blame_range(lex, intial_token.src, end_tok.src + end_tok.len);
		return BERYL_ERR("Function body too large");
	}
		
	i_val fn = { .type = TYPE_FN, .val.fn = intial_token.src, .len = len };
	return fn;
}

static i_val parse_eval_fn_assign(struct lex_state *lex, bool eval, struct lex_token var_tok,  struct lex_token fn_assign) {
	i_val *args_begin = save_arg_state();
	stack_entry *assign_to_var;
	i_val fn = BERYL_NULL;
	
	if(eval) {
		assign_to_var = get_global(var_tok.content.sym.str, var_tok.content.sym.len);
		if(assign_to_var == NULL) {
			blame_token(lex, var_tok);
			return BERYL_ERR("Undeclared variable");
		}
					
		stack_entry *fn_var = get_global(fn_assign.content.sym.str, fn_assign.content.sym.len);
		if(fn_var == NULL) {
			blame_token(lex, fn_assign);
			return BERYL_ERR("Unkown function");
		}
		fn = beryl_retain(fn_var->val);
					
		if(!push_arg(assign_to_var->val)) {
			blame_token(lex, var_tok);
			return BERYL_ERR("Argument stack overflow");
		}
		assign_to_var->val = BERYL_NULL;
	}
	
	i_val err;
	
	i_size n_args;
	err = parse_eval_args(lex, eval, false, &n_args);
	if(BERYL_TYPEOF(err) == TYPE_ERR) {
		blame_token(lex, var_tok);
		goto ERR;
	}
		
	if(!eval)
		return BERYL_NULL;

				
	i_val res = beryl_call(fn, args_begin, n_args + 1, false);
	restore_arg_state(args_begin, false);
	
	if(BERYL_TYPEOF(res) == TYPE_ERR)
		blame_token(lex, fn_assign);
	else
		assign_to_var->val = beryl_retain(res);
	return res;
	
	ERR:
	beryl_release(fn);
	restore_arg_state(args_begin, true);
	return err;
}

static i_val parse_eval_term(struct lex_state *lex, bool eval) {
	struct lex_token tok = lex_pop(lex);
	switch(tok.type) {
	
		case TOK_NUMBER:
			return BERYL_NUMBER(tok.content.number);
		case TOK_STRING:
			return BERYL_STATIC_STR(tok.content.sym.str, tok.content.sym.len);
		
		case TOK_OPEN_BRACKET: {
			if(lex_accept(lex, TOK_CLOSE_BRACKET, NULL))
				return BERYL_NULL;
			
			i_val res = parse_eval_expr(lex, eval, true);
			if(BERYL_TYPEOF(res) == TYPE_ERR)
				return res;
			
			struct lex_token end_bracket = lex_pop(lex);
			if(end_bracket.type != TOK_CLOSE_BRACKET) {
				blame_token(lex, end_bracket);
				return BERYL_ERR("Expected ')'");
			}
			return res;
		}
		
		case TOK_SYM:
		case TOK_OP: {
			struct lex_token fn_assign;
			if(lex_accept(lex, TOK_ASSIGN, NULL)) {
				i_val assign_val = parse_eval_expr(lex, eval, false);
				if(BERYL_TYPEOF(assign_val) == TYPE_ERR)
					return assign_val;
				if(!eval)
					return BERYL_NULL;
				
				stack_entry *var = get_global(tok.content.sym.str, tok.content.sym.len);
				if(var == NULL) {
					blame_token(lex, tok);
					beryl_release(assign_val);
					return BERYL_ERR("Undeclared variable");
				}
				if(var->is_const) {
					blame_token(lex, tok);
					beryl_release(assign_val);
					return BERYL_ERR("Attempting to reassign constant variable");
				}
				beryl_release(var->val);
				var->val = beryl_retain(assign_val);
				return assign_val;
			} else if(lex_accept(lex, TOK_FN_ASSIGN, &fn_assign)) { // I.e for cases like x += y z
				return parse_eval_fn_assign(lex, eval, tok, fn_assign);
			}
			
			// If it's just a variable by itself, no = or +=. Then do the following:
			
			if(!eval)
				return BERYL_NULL;

			stack_entry *var = get_global(tok.content.sym.str, tok.content.sym.len);
			if(var == NULL) {
				blame_token(lex, tok);
				return BERYL_ERR("Undeclared variable");
			}
			return beryl_retain(var->val);
		}
		
		case TOK_LET: {
			bool global = lex_accept(lex, TOK_GLOBAL, NULL);
			
			struct lex_token var_sym = lex_pop(lex);
			if(var_sym.type != TOK_SYM && var_sym.type != TOK_OP) {
				blame_token(lex, var_sym);
				return BERYL_ERR("Expected variable name");
			}
			struct lex_token assign_tok = lex_pop(lex);
			if(assign_tok.type != TOK_ASSIGN) {
				blame_token(lex, assign_tok);
				return BERYL_ERR("Expected '='");
			}
			
			i_val assign_val = parse_eval_expr(lex, eval, false);
			if(BERYL_TYPEOF(assign_val) == TYPE_ERR)
				return assign_val;
			
			if(!eval)
				return BERYL_NULL;
			
			const char *var_name = var_sym.content.sym.str;
			i_size var_name_len = var_sym.content.sym.len;
			if(get_local(var_name, var_name_len) != NULL) {
				beryl_release(assign_val);
				blame_token(lex, var_sym);
				return BERYL_ERR("Redeclaration of variable");
			}
			
			if(current_namespace.start != NULL) {
				struct scope_namespace namespace = global? (struct scope_namespace) { NULL, NULL } : current_namespace;
				stack_entry new_var = { var_name, var_name_len, assign_val, false, namespace };
				bool ok = push_stack(&new_var);
				if(!ok) {
					beryl_release(assign_val);
					blame_token(lex, var_sym);
					return BERYL_ERR("Out of variable space");
				}
			} else {
				stack_entry *var = index_globals(var_name, var_name_len);
				if(var->name != NULL) {
					beryl_release(assign_val);
					blame_token(lex, var_sym);
					return BERYL_ERR("Redeclaration of global variable"); //get_local doesn't check the globals lookup table
				}
				var->name = var_name;
				var->name_len = var_name_len;
				var->val = beryl_retain(assign_val);
				var->is_const = false;
			}
			
			return assign_val;
		}
		
		case TOK_FN: {
			while(!lex_accept(lex, TOK_DO, NULL)) {
				struct lex_token arg = lex_pop(lex);
				if(arg.type == TOK_VARARGS) {
					struct lex_token varargs_ident = lex_pop(lex);
					if(varargs_ident.type != TOK_SYM && varargs_ident.type != TOK_OP) {
						blame_token(lex, varargs_ident);
						return BERYL_ERR("Expected variadic argument name");
					}
					struct lex_token do_tok = lex_pop(lex);
					if(do_tok.type != TOK_DO) {
						blame_token(lex, do_tok);
						return BERYL_ERR("Expected 'do' following final argument (variadic argument must be final argument)");
					}
					break;
				} else if(arg.type != TOK_SYM && arg.type != TOK_OP) {
					blame_token(lex, arg);
					return BERYL_ERR("Expected argument name");
				}
			}
			return parse_do_block(lex, tok);
		}
		
		case TOK_DO:
			return parse_do_block(lex, tok);
		
		case TOK_ENDLINE:
			blame_token(lex, tok);
			return BERYL_ERR("Unexpected end of line");
		
		case TOK_ERR: {
			blame_token(lex, tok);
			size_t len;
			const char *msg = lex_err_str(tok, &len);
			return (i_val) { .type = TYPE_ERR, .managed = false, .len = len, .val.str = msg };
		}
		
		default:
			blame_token(lex, tok);
			return BERYL_ERR("Unexpected token");
	}
}

static i_val parse_eval_subexpr(struct lex_state *lex, bool eval) {
	i_val term = parse_eval_term(lex, eval);
	
	if(BERYL_TYPEOF(term) == TYPE_ERR)
		return term;
	
	struct lex_token op;
	while( lex_accept(lex, TOK_OP, &op) ) {
		i_val next_term = parse_eval_term(lex, eval);
		if(BERYL_TYPEOF(next_term) == TYPE_ERR) {
			beryl_release(term);
			return next_term;
		}
		if(!eval)
			continue;
		
		stack_entry *op_var = get_global(op.content.sym.str, op.content.sym.len);
		if(op_var == NULL) {
			blame_token(lex, op);
			beryl_release(term);
			return BERYL_ERR("Unkown variable");
		}
		i_val op_fn = beryl_retain(op_var->val);
		
		i_val args[2] = { term, next_term };
		i_val res = beryl_call(op_fn, args, 2, false);
		term = res;
		
		if(BERYL_TYPEOF(res) == TYPE_ERR) {
			blame_token(lex, op);
			return res;
		}
	}
	
	return term;
}

static bool continue_expr(struct lex_state *lex) {
	switch(lex_peek(lex).type) {
		case TOK_CLOSE_BRACKET:
		case TOK_EOF:
		case TOK_END:
			return false;
		
		default:
			return true;
	}
}

static i_val parse_eval_args(struct lex_state *lex, bool eval, bool ignore_newlines, i_size *out_n_args) {
	i_size n_args = 0;
	while(true) {
		if(ignore_newlines)
			lex_accept(lex, TOK_ENDLINE, NULL);
		else if(lex_peek(lex).type == TOK_ENDLINE)
			break;
		
		if(!continue_expr(lex))
			break;
		
		i_val res = parse_eval_subexpr(lex, eval);
		if(BERYL_TYPEOF(res) == TYPE_ERR) {
			return res;
		}
		
		if(!eval)
			continue;
		
		bool ok = push_arg(res);
		if(!ok) {
			beryl_release(res);
			return BERYL_ERR("Argument stack overflow");
		}
		n_args++;
	}
	
	*out_n_args = n_args;
	return BERYL_NULL;
}

static i_val parse_eval_expr(struct lex_state *lex, bool eval, bool ignore_newlines) {
	i_val err;
	
	struct lex_token fn_tok = lex_peek(lex);
	
	static unsigned expr_recursion_counter = 0;
	expr_recursion_counter++;
	
	i_val *args_begin = save_arg_state();
	
	#define MAX_EXPR_RECURSION 128
	if(expr_recursion_counter > MAX_EXPR_RECURSION) {
		blame_token(lex, fn_tok);
		err = BERYL_ERR("Expression recursion limit reached");
		goto ERR;
	}
	
	i_val fn = parse_eval_subexpr(lex, eval); 
	if(BERYL_TYPEOF(fn) == TYPE_ERR) {
		err = fn;
		goto ERR;
	}
	
	i_size n_args;
	i_val arg_res = parse_eval_args(lex, eval, ignore_newlines, &n_args);
	if(BERYL_TYPEOF(arg_res) == TYPE_ERR) {
		//blame_token(lex, fn_tok);
		err = arg_res;
		goto ERR;
	}
	
	expr_recursion_counter--;

	if(!eval)
		return BERYL_NULL;
	
	if(n_args == 0) //If there are no arguments, return the 'function'. I.e (1) means '1' and not, 'call 1'
		return fn;
	
	i_val res = beryl_call(fn, args_begin, n_args, false);
	if(BERYL_TYPEOF(res) == TYPE_ERR)
		blame_token(lex, fn_tok);
	
	restore_arg_state(args_begin, false);
	return res; 
	
	ERR:
	expr_recursion_counter--;
	restore_arg_state(args_begin, true);
	return err;
}

i_val parse_eval_all_exprs(struct lex_state *lex, bool eval, unsigned char until_tok, struct lex_token *end_tok) {
	i_val res = BERYL_NULL;
	lex_accept(lex, TOK_ENDLINE, NULL);
	while(!lex_accept(lex, until_tok, end_tok)) {
		beryl_release(res);
		res = parse_eval_expr(lex, eval, false);
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			break;
		lex_accept(lex, TOK_ENDLINE, NULL);
	}
	
	return res;
}

i_size get_fn_arity(i_val fn, bool *variadic) {
	assert(BERYL_TYPEOF(fn) == TYPE_FN);
	
	struct lex_state lex;
	lex_state_init(&lex, fn.val.fn, fn.len);
	lex_accept(&lex, TOK_FN, NULL);
	
	*variadic = false;
	i_size n_args = 0;
	
	while(!lex_accept(&lex, TOK_DO, NULL)) {
		if(lex_accept(&lex, TOK_VARARGS, NULL)) {
			*variadic = true;
			
			struct lex_token varargs_name = lex_pop(&lex);
			assert(varargs_name.type == TOK_SYM || varargs_name.type == TOK_OP);
			assert(lex_accept(&lex, TOK_DO, NULL));
			
			break;
		}
		
		struct lex_token arg_name = lex_pop(&lex);
		assert(arg_name.type == TOK_SYM || arg_name.type == TOK_OP);
		
		n_args++;
	}
	
	return n_args;
}

i_val call_internal_fn(i_val fn, const i_val *args, i_size n_args) {
	assert(BERYL_TYPEOF(fn) == TYPE_FN);
	i_val err;
	
	bool is_variadic;
	i_size arity = get_fn_arity(fn, &is_variadic);
	if(is_variadic) {
		if(n_args < arity)
			return BERYL_ERR("Not enough arguments provided");
	} else if(n_args != arity) {
		return BERYL_ERR("Wrong number of arguments");
	}
	
	struct scope_namespace prev_namespace = current_namespace;
	current_namespace = (struct scope_namespace) { fn.val.fn, fn.val.fn + fn.len };
	stack_entry *prev_scope = enter_scope();
	
	struct lex_state lex;
	lex_state_init(&lex, fn.val.fn, fn.len);
	lex_accept(&lex, TOK_FN, NULL);
	
	for(i_size i = 0; i < arity; i++) { //Push every fixed argument to the stack
		struct lex_token arg_tok = lex_pop(&lex);
		assert(arg_tok.type == TOK_SYM || arg_tok.type == TOK_OP);
		
		const char *arg_name = arg_tok.content.sym.str;
		i_size arg_name_len = arg_tok.content.sym.len;
		
		if(get_local(arg_name, arg_name_len) != NULL) {
			blame_token(&lex, arg_tok);
			err = BERYL_ERR("Redeclaration of variable");
			goto ERR;
		}
		
		stack_entry arg_var = { arg_name, arg_name_len, args[i], false, current_namespace };
		bool ok = push_stack(&arg_var);
		beryl_release(args[i]);
		if(!ok) {
			blame_token(&lex, arg_tok);
			err = BERYL_ERR("Out of variable space");
			goto ERR;
		}
	}
	
	if(is_variadic) { //If the function is variadic, push an array containing the rest of the arguments to the stack
		struct lex_token variadic_tok = lex_pop(&lex);
		struct lex_token varargs_name = lex_pop(&lex);
		assert(variadic_tok.type == TOK_VARARGS);
		assert(varargs_name.type == TOK_SYM || varargs_name.type == TOK_OP);
		
		if(get_local(varargs_name.content.sym.str, varargs_name.content.sym.len) != NULL) { //Check that the name is available
			blame_token(&lex, varargs_name);
			err = BERYL_ERR("Redeclaration of variable");
			goto ERR;
		}
		
		assert(n_args >= arity);
		i_size n_varargs = n_args - arity;
		
		assert(args + arity + n_varargs == args + n_args);
		i_val varargs_array = beryl_new_array(n_varargs, args + arity, n_varargs, false); //Construct the argument array
		beryl_release_values(args + arity, n_varargs);
		
		if(BERYL_TYPEOF(varargs_array) == TYPE_NULL) {
			err = BERYL_ERR("Out of memory; cannot construct variadic arguments array");
			goto ERR;
		}
		
		stack_entry arg_var = { varargs_name.content.sym.str, varargs_name.content.sym.len, varargs_array, false, current_namespace }; //Push the var to the stack
		bool ok = push_stack(&arg_var);
		beryl_release(varargs_array);
		if(!ok) {
			blame_token(&lex, varargs_name);
			err = BERYL_ERR("Out of variable space");
			goto ERR;
		}
	}
	
	bool ok = lex_accept(&lex, TOK_DO, NULL);
	assert(ok);
	
	i_val res = parse_eval_all_exprs(&lex, true, TOK_EOF, NULL);
	current_namespace = prev_namespace;
	leave_scope(prev_scope);
	return res;

	ERR:
	current_namespace = prev_namespace;
	leave_scope(prev_scope);
	return err;
}


// beryl_call takes ownership of fn and *args, and will release them all once done. If borrow is true it instead makes "copies" of fn and args
i_val beryl_call(i_val fn, const i_val *args, size_t n_args, bool borrow) {
	i_val err;
	
	if(borrow) {
		beryl_retain(fn);
		beryl_retain_values(args, n_args);
	}	
	
	if(n_args > I_SIZE_MAX) {
		err = BERYL_ERR("Too many arguments");
		goto ERR;
	}
	
	switch(fn.type) {
		
		case TYPE_EXT_FN: {
			struct beryl_external_fn *ext_fn = fn.val.ext_fn;
			if(ext_fn->arity < 0) {
				if(n_args < (size_t) -(ext_fn->arity + 1)) {
					err = BERYL_ERR("Not enough arguments");
					goto ERR;
				}
			} else if(n_args != (size_t) ext_fn->arity) {
				err = BERYL_ERR("Wrong number of arguments");
				goto ERR;
			}
			
			i_val res = ext_fn->fn(args, n_args);
			if(ext_fn->auto_release)
				beryl_release_values(args, n_args);
			beryl_release(fn);
			
			if(BERYL_TYPEOF(res) == TYPE_ERR)
				blame_name(ext_fn->name, ext_fn->name_len);
			return res;
		} break;
		
		case TYPE_FN: {
			i_val res = call_internal_fn(fn, args, n_args);
			beryl_release(fn);
			return res;
		}
		
		case TYPE_TABLE: {
			if(n_args == 0) {
				err = BERYL_ERR("Cannot index table without key");
				goto ERR;
			} if(n_args == 1) {
				i_val res = index_table(fn, args[0]);
				beryl_release(args[0]);
				beryl_release(fn);
				return res;
			}
			
			i_val member = index_table(fn, args[0]);
			beryl_release(args[0]);
			
			stack_entry *prev_scope = enter_scope();
			
			stack_entry self_var = { "self", sizeof("self") - 1, fn, false, { NULL, NULL } };
			bool ok = push_stack(&self_var);
			beryl_release(fn);
			if(!ok) {
				leave_scope(prev_scope);
				beryl_release(member);
				beryl_release_values(args + 1, n_args - 1);
				return BERYL_ERR("Out of variable space");
			}
			
			i_val res = beryl_call(member, args + 1, n_args - 1, false);
			
			leave_scope(prev_scope);
			
			return res;
		}
		
		case TYPE_ARRAY: {
			if(n_args != 1) {
				err = BERYL_ERR("Can only index array with an index");
				goto ERR;
			}
			
			if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
				beryl_blame_arg(args[0]);
				err = BERYL_ERR("Can only index array with a number");
				goto ERR;
			}
			i_float f = beryl_as_num(args[0]);
			if(!beryl_is_integer(args[0])) {
				beryl_blame_arg(args[0]);
				err = BERYL_ERR("Can only index array with an integer number");
				goto ERR;
			}
			i_size index = f;
			if(f > I_SIZE_MAX || index >= BERYL_LENOF(fn)) {
				beryl_release(fn);
				beryl_release(args[0]);
				return BERYL_NULL;
			}
			
			i_val res = beryl_retain(beryl_get_raw_array(fn)[index]);
			beryl_release(fn);
			beryl_release(args[0]);
			return res;
		}

		case TYPE_NULL: {
			// return BERYL_ERR("Attemting to call null");
			beryl_release_values(args, n_args);
			return BERYL_NULL;
		}
		
		case TYPE_OBJECT: {
			beryl_object *obj = beryl_as_object(fn);
			if(obj->obj_class->call == NULL) {
				beryl_blame_arg(fn);
				err = BERYL_ERR("Attempting to call non-callable object");
				goto ERR;
			}
			i_val res = obj->obj_class->call(obj, args, n_args);
			beryl_release(fn);
			beryl_release_values(args, n_args);
			
			return res;
		}
		
		default:
			beryl_blame_arg(fn);
			err = BERYL_ERR("Attempting to call non-function value");
			goto ERR;
	}
	
	ERR:
	beryl_release(fn);
	beryl_release_values(args, n_args);
	return err;
}

i_val beryl_pcall(i_val fn, const i_val *args, size_t n_args, bool borrow, bool print_trace) {
	i_val res = beryl_call(fn, args, n_args, borrow);
	if(print_trace && BERYL_TYPEOF(res) == TYPE_ERR) {
		log_err(res);
	}
	clear_err();
	return res;
}


i_val beryl_eval(const char *src, size_t src_len, enum beryl_err_action err) {
	struct lex_state lex;
	lex_state_init(&lex, src, src_len);
	
	stack_entry *prev_scope = enter_scope();
	struct scope_namespace prev_namespace = current_namespace;
	current_namespace = (struct scope_namespace) { NULL, NULL }; // Global namespace
	
	i_val res = parse_eval_all_exprs(&lex, true, TOK_EOF, NULL);
	
	leave_scope(prev_scope);
	current_namespace = prev_namespace;
	
	if(BERYL_TYPEOF(res) == TYPE_ERR) {
		if(err == BERYL_PRINT_ERR) {
			log_err(res);
			clear_err();
		} else if(err == BERYL_CATCH_ERR)
			clear_err();
	}
	
	return res;
}

void beryl_clear() {
	assert(stack_base == stack_start);
	for(stack_entry *p = stack_top - 1; p >= stack_start; p--) {
		beryl_release(p->val);
	}
	for(size_t i = 0; i < GLOBALS_LOOKUP_TABLE_SIZE; i++) {
		stack_entry *entry = &globals_lookup[i];
		entry->name = NULL;
		beryl_release(entry->val);
		entry->is_const = false;
	}
	
	stack_top = stack_base;
}

#include "libs/libs.h"

bool beryl_load_included_libs() {
	#ifndef MINIMAL_BUILD
	load_debug_lib();
	load_unix_lib();
	load_io_lib();
	#endif
	return load_core_lib();
}

#ifndef BERYL_MINOR_VERSION
#define BERYL_MINOR_VERSION "9"
#endif

#ifndef BERYL_MAJOR_VERSION
#define BERYL_MAJOR_VERSION "0"
#endif

#ifndef BERYL_SUBMAJOR_VERSION
#define BERYL_SUBMAJOR_VERSION "0"
#endif

const char *beryl_minor_version() {
	return BERYL_MINOR_VERSION;
}

const char *beryl_submajor_version() {
	return BERYL_SUBMAJOR_VERSION;
}

const char *beryl_major_version() {
	return BERYL_MAJOR_VERSION;
}

const char *beryl_version() {
	return BERYL_MAJOR_VERSION ":" BERYL_SUBMAJOR_VERSION ":" BERYL_MINOR_VERSION;
}
