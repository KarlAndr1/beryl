#include "libs.h"

#include "../beryl.h"

#include "../utils.h"

/*@$
	libname corelib
$@*/

typedef struct i_val i_val;


#define FN(arity, name, fn) { arity, true, name, sizeof(name) - 1, fn }
#define MANUAL_RELEASE_FN(arity, name, fn) { arity, false, name, sizeof(name) - 1, fn }

#define MATH_OP(name, start_val, start_index, op) \
static i_val name(const i_val *args, i_size n_args) { \
	for(int i = 0; i < start_index; i++) { \
		if(BERYL_TYPEOF(args[i]) != TYPE_NUMBER) { \
			beryl_blame_arg(args[i]); \
			return BERYL_ERR("Expected number as argument for '" #op "'"); \
		} \
	} \
	i_float res = start_val; \
	for(i_size i = start_index; i < n_args; i++) { \
		if(BERYL_TYPEOF(args[i]) != TYPE_NUMBER) { \
			beryl_blame_arg(args[i]); \
			return BERYL_ERR("Expected number as argument for '" #op "'"); \
		} \
		res = res op beryl_as_num(args[i]); \
	} \
	return BERYL_NUMBER(res); \
}

MATH_OP(add_callback, 0, 0, +)
MATH_OP(mul_callback, 1, 0, *)
MATH_OP(div_callback, beryl_as_num(args[0]), 1, /)

static i_val sub_callback(const i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected number as first argument for '-'");
	}
	
	i_float res = beryl_as_num(args[0]);
	if(n_args == 1)
		return BERYL_NUMBER(-res);
	
	for(i_size i = 1; i < n_args; i++) {
		if(BERYL_TYPEOF(args[i]) != TYPE_NUMBER) {
			beryl_blame_arg(args[i]);
			return BERYL_ERR("Expected number as argument for '-'");
		}
		res -= beryl_as_num(args[i]);
	}
	
	return BERYL_NUMBER(res);
}

#define POP_ARG(to, err_msg) { if(n_args > 0) { n_args--; to = *args; args++; } else { return BERYL_ERR("Unexpected end of arguments; " err_msg); } }
#define EXPECT_TYPE(val, type, err_msg) { if(BERYL_TYPEOF(val) != type) { beryl_blame_arg(val); return BERYL_ERR(err_msg); } }

static i_val else_tag, elseif_tag;

static i_val if_callback(const i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_BOOL) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected boolean as if condition");
	}
	
	if(beryl_as_bool(args[0])) {
		return beryl_call(args[1], NULL, 0, true);
	}
	assert(n_args >= 2);
	n_args -= 2;
	args += 2;
	
	while(n_args > 0) {
		i_val tag;
		POP_ARG(tag, "");
		if(beryl_val_cmp(tag, elseif_tag) == 0) {
			i_val cond;
			POP_ARG(cond, "Expected condition following 'elseif'");
			EXPECT_TYPE(cond, TYPE_BOOL, "Expected boolean condition following 'elseif'");
			i_val then_do;
			POP_ARG(then_do, "Expected argument following 'elseif'");
			if(beryl_as_bool(cond))
				return beryl_call(then_do, NULL, 0, true);
		} else if(beryl_val_cmp(tag, else_tag) == 0) {
			i_val then_do;
			POP_ARG(then_do, "Expected argument following 'else'");
			return beryl_call(then_do, NULL, 0, true);
		} else {
			beryl_blame_arg(tag);
			return BERYL_ERR("Unexpected argument for 'if'. Expected either 'elseif' or 'else'");
		}
	}
	
	if(n_args != 0) {
		i_val arg;
		POP_ARG(arg, "");
		beryl_blame_arg(arg);
		return BERYL_ERR("Expected no more arguments after 'else'");
	}
	
	return BERYL_NULL;
}

static i_val eq_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	return BERYL_BOOL(beryl_val_cmp(args[0], args[1]) == 0);
}

static i_val not_eq_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	return BERYL_BOOL(beryl_val_cmp(args[0], args[1]) != 0);
}

static i_val not_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_BOOL) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected boolean as argument to 'not'");
	}
	return BERYL_BOOL(!beryl_as_bool(args[0]));
}

/*@@
	array
	... items

	Variadic function that accepts any number of arguments.
	Creates a new array containing the given *items* and returns it.
	May return an error if out of memory.
@@*/
static i_val array_callback(const i_val *args, i_size n_args) {
	i_val array = beryl_new_array(n_args, args, n_args, false);
	if(BERYL_TYPEOF(array) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	return array;
}

/*@@
	for-in
	container body
	
	Binary function for linearly iterating through arrays.
	Increments an internal counter, starting at zero, by one.
	For each increment, the container is indexed (called) with the counter as an argument. If the
	container returns null, the function halts, otherwise it calls body with the returned result from the container and
	then continues incrementing.

	For example:
		for-in (array 1 2 3) with i do
			print i
		end
	Will print '1', '2', '3'
@@*/
static i_val for_in_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	i_val index = BERYL_NUMBER(0);
	
	i_val res = BERYL_NULL;
	while(true) {
		i_val index_res = beryl_call(args[0], &index, 1, true);
		if(BERYL_TYPEOF(index_res) == TYPE_ERR)
			return index_res;
		if(BERYL_TYPEOF(index_res) == TYPE_NULL)
			break;
		beryl_release(res);
		
		res = beryl_call(beryl_retain(args[1]), &index_res, 1, false);
		
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			return res;
		index = BERYL_NUMBER(beryl_as_num(index) + 1);
	}
	return res;
}

/*@@
	for
	from to body
	
	Trinary function.
	Increments a counter from *from* until it is greater than or equal to *to*. Increments by one.
	For each increment, the function *body* is called with the current number as an argument.
	If *to* < *from* then the counter is decremented by one instead, until it is less than or equal to *from*.
@@*/
static i_val for_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_NUMBER) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("First argument of 'for' must be number");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_NUMBER) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Second argument of 'for' must be number");
	}
	
	i_float from = beryl_as_num(args[0]);
	i_float until = beryl_as_num(args[1]);
	i_float i = from;
	
	i_val res = BERYL_NULL;
	
	if(from <= until) {
		while(i < until) {
			i_val index = BERYL_NUMBER(i++);
			
			beryl_release(res);
			res = beryl_call(args[2], &index, 1, true);
			beryl_release(index);
			
			if(BERYL_TYPEOF(res) == TYPE_ERR)
				return res;
		}
	} else {
		while(i > until) {
			i_val index = BERYL_NUMBER(i--);
			
			beryl_release(res);
			res = beryl_call(args[2], &index, 1, true);
			beryl_release(index);
			
			if(BERYL_TYPEOF(res) == TYPE_ERR)
				return res;
		}
	}
	
	return res;
}

/*@@
	table
	... args

	Variadic function that takes an even number of arguments.
	Creates a new table consiting of the keys and values given in *args*.
	May return an error on out of memory, or if an uneven number of arguments are provided.
	Also returns an error if any of the keys are duplicates or are invalid as keys.

	Example:
		table "foo" 1 "bar" 2
	Creates the table { ("foo" 1) ("bar" 2) }
@@*/
static i_val table_callback(const i_val *args, i_size n_args) {
	if(n_args % 2 != 0)
		return BERYL_ERR("Table function only accepts an even number of arguments");
	
	i_val table = beryl_new_table(n_args / 2, true);
	if(BERYL_TYPEOF(table) == TYPE_NULL)
		return BERYL_ERR("Out of memory");

	for(i_size i = 0; i < n_args; i += 2) {
		assert(i + 1 < n_args);
		int res = beryl_table_insert(&table, args[i], args[i + 1], false);
		if(res != 0) {
			beryl_blame_arg(args[i]);
			beryl_release(table);
			switch(res) {
				case 3: 
					return BERYL_ERR("Value is not valid key");
				case 2:
					return BERYL_ERR("Duplicate key");
				
				default:
					assert(false);
			}
		}
	}
	
	return table;
}

/*@@
	tag


	Zero-arity function.
	Creates a new tag.

	Example:
		let t1 = new tag
		let t2 = new tag
		
		assert t1 =/= t2
		assert t1 == t1
		assert t2 == t2
	Each new tag created is guaranteed to only be equal to itself, and nothing else.
@@*/
static i_val tag_callback(const i_val *args, i_size n_args) {
	(void) n_args, (void) args;
	i_val tag = beryl_new_tag();
	if(BERYL_TYPEOF(tag) == TYPE_NULL)
		return BERYL_ERR("Out of tags");
	return tag;
}

/*@@
	invoke
	fn
	
	Unary function.
	Calls the given function *fn* with no arguments.
	Synonymous to 'new'.
@@*/
/*@@
	new
	fn
	
	Unary function.
	Calls the given function *fn* with no arguments.
	Synonymous to 'invoke'.
@@*/ 
static i_val invoke_callback(const i_val *args, i_size n_args) { // DOESN'T USE AUTORELEASE
	(void) n_args;
	
	i_val res = beryl_call(args[0], NULL, 0, false);
	return res;
}

/*@@
	assert
	condition ... opt-msg

	Takes a boolean condition or a nullable value, and if it is false or null, returns an error with the given message *opt-msg*.
	If *opt-msg* is not given, the error message defaults to "Assertion failed".
	If the assertion doesn't return an error, the *condtion* value is returned.
@@*/
static i_val assert_callback(const i_val *args, i_size n_args) {
	if(n_args != 1 && n_args != 2)
		return BERYL_ERR("Assert takes either one or two arguments");
	
	if(n_args == 2 && BERYL_TYPEOF(args[1]) != TYPE_STR) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected string message as second argument for 'assert'");
	}
	
	
	bool passed_assertion;
	if(BERYL_TYPEOF(args[0]) == TYPE_BOOL)
		passed_assertion = beryl_as_bool(args[0]);
	else
		passed_assertion = BERYL_TYPEOF(args[0]) != TYPE_NULL;

	if(!passed_assertion) {
		if(n_args == 1)
			return BERYL_ERR("Assertion failed");
		
		return beryl_str_as_err(args[1]);
	}
	
	return beryl_retain(args[0]);
}

#define BOOL_OP(name, display_name, op) \
static i_val name(const i_val *args, i_size n_args) { \
	if(BERYL_TYPEOF(args[0]) != TYPE_BOOL) { \
		beryl_blame_arg(args[0]); \
		return BERYL_ERR("Expected boolean as argument for '" display_name "'"); \
	} \
	bool res = beryl_as_bool(args[0]); \
	for(i_size i = 1; i < n_args; i++) { \
		if(BERYL_TYPEOF(args[i]) != TYPE_BOOL) { \
			beryl_blame_arg(args[i]); \
			return BERYL_ERR("Expected boolean as argument for '" display_name "'"); \
		} \
		res = res op beryl_as_bool(args[i]); \
	} \
	return BERYL_BOOL(res); \
}

BOOL_OP(and_callback, "and?", &&)
BOOL_OP(or_callback, "or?", ||)

#define CMP_OP(name, ...) \
static i_val name(const i_val *args, i_size n_args) { \
	i_val prev = args[0]; \
	for(i_size i = 1; i < n_args; i++) { \
		i_val next = args[i]; \
		{ __VA_ARGS__ } \
		prev = next; \
	} \
	\
	return BERYL_TRUE; \
}

CMP_OP(less_callback,
	int cmp_res = beryl_val_cmp(prev, next);
	if(cmp_res == 0 || cmp_res == -1)
		return BERYL_FALSE;
	if(cmp_res == 2) {
		beryl_blame_arg(prev);
		beryl_blame_arg(next);
		return BERYL_ERR("Uncomparable values");
	}
)

CMP_OP(greater_callback,
	int cmp_res = beryl_val_cmp(prev, next);
	if(cmp_res == 0 || cmp_res == 1)
		return BERYL_FALSE;
	if(cmp_res == 2) {
		beryl_blame_arg(prev);
		beryl_blame_arg(next);
		return BERYL_ERR("Uncomparable values");
	}
)

CMP_OP(less_eq_callback,
	int cmp_res = beryl_val_cmp(prev, next);
	if(cmp_res == -1)
		return BERYL_FALSE;
	if(cmp_res == 2) {
		beryl_blame_arg(prev);
		beryl_blame_arg(next);
		return BERYL_ERR("Uncomparable values");
	}
)

CMP_OP(greater_eq_callback,
	int cmp_res = beryl_val_cmp(prev, next);
	if(cmp_res == 1)
		return BERYL_FALSE;
	if(cmp_res == 2) {
		beryl_blame_arg(prev);
		beryl_blame_arg(next);
		return BERYL_ERR("Uncomparable values");
	}
)

/*@@
	foreach-in
	table body
	
	Binary function.
	For each key and value in *table*, calls *body* with the key and value as arguments.
	Can also iterate over arrays, in which case the arguments given to *body* are the index and element for each entry.
@@*/
static i_val foreach_in_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	switch(BERYL_TYPEOF(args[0])) {
		case TYPE_TABLE: {
			struct i_val_pair *iter = NULL;
			i_val res = BERYL_NULL;
			while( (iter = beryl_iter_table(args[0], iter)) ) {
				beryl_release(res);
				i_val iter_args[] = { iter->key, iter->val };
				res = beryl_call(args[1], iter_args, 2, true);
				if(BERYL_TYPEOF(res) == TYPE_ERR)
					return res;
				}
				return res;
			}
		
		case TYPE_ARRAY: {
			i_val res = BERYL_NULL;
			i_size len = BERYL_LENOF(args[0]);
			const i_val *items = beryl_get_raw_array(args[0]);

			for(i_size i = 0; i < len; i++) {
				beryl_release(res);
				i_val iter_args[] = { BERYL_NUMBER(i), items[i] };
				res = beryl_call(args[1], iter_args, 2, true);
				if(BERYL_TYPEOF(res) == TYPE_ERR)
					return res;
			}
			return res;
		}
		
		default:
			beryl_blame_arg(args[0]);
			return BERYL_ERR("Expected array or table as argument for 'foreach-in'");
	}
}

static void union_tables(i_val *into_table, i_val from_table) {
	assert(beryl_get_refcount(*into_table) == 1);

	i_size remaining_cap = into_table->val.table->cap - BERYL_LENOF(*into_table);
	assert(BERYL_LENOF(from_table) <= remaining_cap); (void) remaining_cap;
	
	struct i_val_pair *iter = NULL;
	while( (iter = beryl_iter_table(from_table, iter)) ) {
		beryl_table_insert(into_table, iter->key, iter->val, false);
	}
}

/*@@
	insert
	table key value

	Creates a new table with the given *key* and *value* inserted.
	Returns an error if out of memory or if the given *key* already exists or is not a valid key.
@@*/
static i_val insert_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_TABLE) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected table as first argument for 'insert'");
	}
	
	i_val table = args[0];
	
	i_refc refs = beryl_get_refcount(table);
	bool retain_table = false;
	
	if(refs != 1) {
		i_val new_table = beryl_new_table(BERYL_LENOF(table) + 1, true);
		if(BERYL_TYPEOF(new_table) == TYPE_NULL)
			return BERYL_ERR("Out of memory");
		
		union_tables(&new_table, args[0]);
		table = new_table;
	} else {
		if(beryl_table_should_grow(table, 1)) {
			i_val new_table = beryl_new_table(BERYL_LENOF(table) + 1, true);
			if(BERYL_TYPEOF(new_table) == TYPE_NULL)
				return BERYL_ERR("Out of memory");
			union_tables(&new_table, table);
			table = new_table;
		} else
			retain_table = true;
	}
	
	int err = beryl_table_insert(&table, args[1], args[2], false);
	if(retain_table)
		beryl_retain(table);
	if(err == 0)
		return table;

	beryl_release(table);
	switch(err) {
		case 3:
			beryl_blame_arg(args[1]);
			return BERYL_ERR("Invalid table key");
		case 2:
			beryl_blame_arg(args[1]);
			return BERYL_ERR("Duplicate key");
		case 1:
			assert(false); //Should never happen, as this means that the newly allocated table is out of space
			return BERYL_NULL;
		
		default:
			assert(false);
			return BERYL_NULL;
	}
}

/*@@
	union
	table-a table-b
	
	Binary function.
	Takes two tables as arguments and creates a new table that
	is the union (contains all keys that exist in either *table-a* or *table-b*) of the two tables.
	If a key exists in both tables, then the value in that entry will be that of *table-a*.

	Returns an error if out of memory.

	Example:
		let a = table "foo" 1 "bar" 2
		let b = table "foo" 3 "char" 4
		let u = union a b
	The resulting table 'u' will be
	{ ("foo" 1) ("bar" 2) ("char" 4) }
@@*/
static i_val union_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_TABLE) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected table as first argument for 'union:'");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_TABLE) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected table as second argument for 'union:'");
	}
	
	i_size a_len = BERYL_LENOF(args[0]);
	i_size b_len = BERYL_LENOF(args[1]);
	if(I_SIZE_MAX - a_len < b_len)
		return BERYL_ERR("Out of memory");
	
	i_size total_len = a_len + b_len;
	i_val new_table = beryl_new_table(total_len, true); //TODO: Maybe optimize this to be done via mutation if possible
	if(BERYL_TYPEOF(new_table) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	union_tables(&new_table, args[0]);
	union_tables(&new_table, args[1]);
	return new_table;
}

static void mcpy(void *to_ptr, const void *from_ptr, size_t n) {
	char *to = to_ptr;
	const char *from = from_ptr;
	while(n--)
		*(to++) = *(from++);
}

/*@@
	cat
	a b ... rest

	Binary function taking at least two arguments.
	Turns all of the given arguments into strings, and returns the concatenated result.
	May return an error if out of memory.

	Example:
		let x = 5
		let y = 10
		let s = cat "x is " x ", y is " y
	The string 's' will in this case be "x is 5, y is 10"
@@*/
static i_val concat_callback(const i_val *args, i_size n_args) {
	i_val err;
	bool only_strings = true;
	
	for(i_size i = 0; i < n_args; i++) {
		if(BERYL_TYPEOF(args[i]) != TYPE_STR) {
			only_strings = false;
		}
	}
	
	const i_val *strings = args;
	i_val *str_array = NULL;
	if(!only_strings) {
		str_array = beryl_talloc(n_args * sizeof(i_val));
		for(i_size i = 0; i < n_args; i++)
			str_array[i] = i_val_as_string(args[i]);
		strings = str_array;
	}
	
	i_size total_len = 0;
	for(i_size i = 0; i < n_args; i++) {
		if(BERYL_TYPEOF(strings[i]) == TYPE_ERR) {
			err = beryl_retain(strings[i]);
			goto ERR;
		}
		total_len += BERYL_LENOF(strings[i]);
	}
	
	i_val res_str = beryl_new_string(total_len, NULL);
	if(BERYL_TYPEOF(res_str) == TYPE_NULL) {
		err = BERYL_ERR("Out of memory");
		goto ERR;
	}
	
	char *raw_str = (char *) beryl_get_raw_str(&res_str);
	char *s = raw_str;
	for(i_size i = 0; i < n_args; i++) {
		i_size len = BERYL_LENOF(strings[i]);
		if(len > 0) {
			assert(s < raw_str + total_len);
		}
		mcpy(s, beryl_get_raw_str(&strings[i]), len);
		s += len;
	}
	
	if(str_array != NULL) {
		for(size_t i = 0; i < n_args; i++)
			beryl_release(str_array[i]);
		beryl_tfree(str_array);
	}
	return res_str;
	
	ERR:
	if(str_array != NULL) {
		for(size_t i = 0; i < n_args; i++)
			beryl_release(str_array[i]);
		beryl_tfree(str_array);
	}
	return err;
}

/*@@
	in?
	key table
	
	Returns true if the given *key* exists in the table *t*. Returns false if it does not.
	Returns an error if t is not a table.
@@*/
static i_val in_callback(const i_val *args, i_size n_args) { // DOESN'T USE AUTORELASE
	(void) n_args;
	i_val index_res = beryl_call(args[1], &args[0], 1, false);
	if(BERYL_TYPEOF(index_res) == TYPE_ERR)
		return index_res;
	i_val res = BERYL_BOOL(BERYL_TYPEOF(index_res) != TYPE_NULL);
	beryl_release(index_res);
	return res;
}

/*@@
	sizeof
	value

	Unary function.
	Returns the size of the given *value*;
	If *value* is an array, returns the number of items in that array.
	If *value* is a table, returns the number of entries.
	If *value* is a string, returns the number of bytes in the string.
	Otherwise, returns 1.
@@*/
static i_val sizeof_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	switch(BERYL_TYPEOF(args[0])) {
		case TYPE_STR:
		case TYPE_TABLE:
		case TYPE_ARRAY:
			return BERYL_NUMBER(BERYL_LENOF(args[0]));
		
		default:
			return BERYL_NUMBER(1);
	}
}

/*@@
	replace
	t k v

	Trinary function.
	Returns a new struct that is an exact copy of the struct *t*, except that 
	the entry with the key *k* has it's value replaced with *v*.
	Returns an error if out of memory or if the key does not exist.

	Example:
		let a = table "foo" 1 "bar" 2
		let b = replace a "foo" 3
	The struct b will in this case be { ("foo" 3) ("bar" 2) }
@@*/
static i_val replace_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	switch(BERYL_TYPEOF(args[0])) {
		case TYPE_TABLE: {
			i_val index_res = beryl_pcall(args[0], &args[1], 1, true, false); 
			if(BERYL_TYPEOF(index_res) == TYPE_NULL) {
				beryl_blame_arg(args[1]);
				beryl_release(index_res);
				return BERYL_ERR("Key not in struct");
			}
			beryl_release(index_res);
			
			if(beryl_get_refcount(args[0]) == 1) {
				i_val res = args[0];
				beryl_table_insert(&res, args[1], args[2], true); //Replace it via mutation
				return beryl_retain(res);
			} else {
				i_val new_table = beryl_new_table(BERYL_LENOF(args[0]) + 1, true);
				if(BERYL_TYPEOF(new_table) == TYPE_NULL)	
					return BERYL_ERR("Out of memory");
				beryl_table_insert(&new_table, args[1], args[2], false);
				union_tables(&new_table, args[0]);
				return new_table;
			}
		}
		
		default:
			beryl_blame_arg(args[0]);
			return BERYL_ERR("Can only use 'replace' on structs");
	}
}

static struct eval_item {
	struct eval_item *prev;
	i_val item;
} *eval_list = NULL;

void beryl_core_lib_clear_evals() {
	struct eval_item *next = NULL;
	for(struct eval_item *p = eval_list; p != NULL; p = next) {
		next = p->prev;
		beryl_release(p->item);
		beryl_free(p);
	}
}

static i_val catch_tag;
static i_val print_trace_tag;

static i_val try_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	int mode;
	if(beryl_val_cmp(args[1], catch_tag) == 0) {
		mode = 0;
	} else if(beryl_val_cmp(args[1], else_tag) == 0) {
		mode = 1;
	} else if(beryl_val_cmp(args[1], print_trace_tag) == 0) {
		mode = 2;
	} else {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected 'catch' or 'else'");
	}
	
	i_val res = beryl_pcall(args[0], NULL, 0, true, mode == 2);
	if(BERYL_TYPEOF(res) != TYPE_ERR)
		return res;
	
	if(mode == 1) { // try ... else x, just returns x on an error 
		beryl_release(res);
		return beryl_retain(args[2]);
	} else {
		i_val err_msg = beryl_err_as_str(res);
		beryl_release(res);
		i_val res = beryl_call(beryl_retain(args[2]), &err_msg, 1, false); // try ... catch with x do ... end calls argument 2 with the error msg as an argument
		return res;
	}
}

/*@@
	eval
	string-expr

	Evaluates the string *string-expr* as a berylscript expression. Returns the result of that expression.
	May return an error if out of memory, or if the given expression returns an error.
@@*/
static i_val eval_callback(const i_val *args, i_size n_args) {
	if(n_args != 1 && n_args != 3)
		return BERYL_ERR("`eval` takes either 1 or 3 arguments");
	
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected string as argument for 'eval'");
	}
	
	enum beryl_err_action err_action = BERYL_PROP_ERR;
	if(n_args == 3) {
		if(beryl_val_cmp(args[1], catch_tag) == 0) {
			err_action = BERYL_CATCH_ERR;
		} else if(beryl_val_cmp(args[1], print_trace_tag) == 0) {
			err_action = BERYL_PRINT_ERR;
		} else {
			beryl_blame_arg(args[1]);
			return BERYL_ERR("Expected 'catch'");
		}
	}
	
	struct eval_item *new_entry = beryl_alloc(sizeof(struct eval_item));
	if(new_entry == NULL)
		return BERYL_ERR("Out of memory");
	
	new_entry->prev = eval_list;
	new_entry->item = beryl_retain(args[0]);
	eval_list = new_entry;
	
	i_val res = beryl_eval(beryl_get_raw_str(&eval_list->item), BERYL_LENOF(eval_list->item), err_action);

	if(err_action != BERYL_PROP_ERR && BERYL_TYPEOF(res) == TYPE_ERR) {
		i_val handler_res = beryl_call(beryl_retain(args[2]), &res, 1, false); // the function is given total ownership of *res* in this case
		return handler_res;
	}
	
	return res;
}

/*@@
	filter
		array fn

	Binary function.
	Creates a new array that is a copy of the array *array*, except only containing the elements
	such that *fn* given the element as an argument returns true. 
	May return an error if out of memory or if *fn* does not return a boolean.

	Example:
		let a = array 1 2 3 4
		let b = filter a with x do x > 2 end
	'b' in this case will be the array (3 4)
@@*/
static i_val filter_callback(const i_val *args, i_size n_args) { // DOESN'T USE AUTORELEASE
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		beryl_release_values(args, n_args);
		return BERYL_ERR("First argument of 'filter' must be array");
	}
	
	i_val res;
	const i_val *from_array = beryl_get_raw_array(args[0]);
	
	if(beryl_get_refcount(args[0]) == 1)
		res = beryl_retain(args[0]);
	else {
		i_size len = BERYL_LENOF(args[0]);
		res = beryl_new_array(len, NULL, len, false);
		if(BERYL_TYPEOF(res) == TYPE_NULL) {
			beryl_release_values(args, n_args);
			return BERYL_ERR("Out of memory");
		}
		i_val *a = (i_val *) beryl_get_raw_array(res);
		for(i_size i = 0; i < len; i++)
			a[i] = BERYL_NULL;
	}
	
	i_val *to_array = (i_val *) beryl_get_raw_array(res);
	i_size res_n = 0;

	for(i_size i = 0; i < BERYL_LENOF(args[0]); i++) {
		i_val filter_res = beryl_call(args[1], &from_array[i], 1, true);
		if(BERYL_TYPEOF(filter_res) == TYPE_ERR) {
			beryl_release(res);
			beryl_release_values(args, n_args);
			return filter_res;
		} else if(BERYL_TYPEOF(filter_res) != TYPE_BOOL) {
			beryl_release(res);
			beryl_blame_arg(filter_res);
			beryl_release(filter_res);
			beryl_release_values(args, n_args);
			return BERYL_ERR("Expected filter function to return boolean");
		}
		if(beryl_as_bool(filter_res)) {
			i_val tmp = beryl_retain(from_array[i]);
			beryl_release(*to_array);
			*(to_array++) = tmp; 
			res_n++;
		}
	}
	
	beryl_release_values(args, n_args);
	
	res.len = res_n;
	return res;
}

/*@@
	push
	array val

	Creates a new array that is a copy of the array *array*, with the value *val* appended to the end of the array.
	May return an error if *array* is not an array or if out of memory.
@@*/
static i_val push_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("First argument of push must be array");
	}
	i_size len = BERYL_LENOF(args[0]);
	
	i_val array = BERYL_NULL;
	if(beryl_get_refcount(args[0]) == 1) {
		assert(args[0].managed);
		if(beryl_get_array_capacity(args[0]) > len)
			array =	beryl_retain(args[0]);
		else
			array = beryl_new_array(len, beryl_get_raw_array(args[0]), len + 1, true);
	} else {
		array = beryl_new_array(len, beryl_get_raw_array(args[0]), len + 1, true);
	}
	
	if(BERYL_TYPEOF(array) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	assert(array.managed && BERYL_TYPEOF(array) == TYPE_ARRAY);
	assert(beryl_get_array_capacity(array) > BERYL_LENOF(array)); //I.e that there's room for at least one extra item
	
	i_val *items = (i_val*) beryl_get_raw_array(array);
	items[array.len++] = beryl_retain(args[1]);
	return array;
}

static i_val mod_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(!beryl_is_integer(args[0])) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Can only take modulus of integer numbers");
	}
	if(!beryl_is_integer(args[1])) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Can only take modulus of integer numbers");
	}
	
	long long t = beryl_as_num(args[0]);
	long long n = beryl_as_num(args[1]);
	
	long long res = ((t % n) + n) % n;
	return BERYL_NUMBER(res);
}



static i_val arrayof_array = BERYL_SNULL;

static i_val arrayof_capture_callback(const i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(arrayof_array) == TYPE_NULL)
		return BERYL_NULL;
	
	if(n_args == 0)
		return BERYL_NULL;
	
	if(n_args == 1) {
		bool ok = beryl_array_push(&arrayof_array, args[0]);
		if(!ok)
			return BERYL_ERR("Out of memory");
	} else {
		i_val new_array = beryl_new_array(n_args, args, n_args, false);
		if(BERYL_TYPEOF(new_array) == TYPE_NULL)
			return BERYL_ERR("Out of memory");
		
		bool ok = beryl_array_push(&arrayof_array, new_array);
		beryl_release(new_array);
		if(!ok)
			return BERYL_ERR("Out of memory");
	}
	
	return BERYL_NULL;
}

static struct beryl_external_fn arrayof_capture_fn = FN(-1, "arrayof-capture", arrayof_capture_callback);

/*@@
	arrayof
	iterator-fn ... args

	Variadic function taking at least one argument.
	Creates a new array based off of the iterating function *iterator-fn*.

	Example:
		let a = arrayof for 1 5
	'a' in this case will be the array (1 2 3 4)
	
	This works by the arrayof function passing *args* plus a "capture" function to *iterator-fn*, and every call
	to said "capture" function stores the arguments passed to the capture function in the array.
	If more than one argument is passed to the capture function, the arguments get inserted into the array as a new array containing
	the arguments.
	
	Example:
		let obj = struct :foo 1 :bar 2
		let a = arrayof foreach-in obj
	'a' in this case will be the array ((:foo 1) (:bar 2))
@@*/
static i_val arrayof_callback(const i_val *args, i_size n_args) {
	
	i_val *pass_args = beryl_talloc(sizeof(i_val) * n_args);
	if(pass_args == NULL)
		return BERYL_ERR("Out of memory");
	for(i_size i = 1; i < n_args; i++)
		pass_args[i-1] = args[i];
	pass_args[n_args-1] = BERYL_EXT_FN(&arrayof_capture_fn); //The pass_args array contains [arg1, arg2, ..., capture_callback]
	
	i_val prev_array = arrayof_array;
	arrayof_array = beryl_new_array(0, NULL, 4, false);
	if(BERYL_TYPEOF(arrayof_array) == TYPE_NULL) {
		arrayof_array = prev_array;
		beryl_tfree(pass_args);
		return BERYL_ERR("Out of memory");
	}
	
	i_val res = beryl_call(args[0], pass_args, n_args, true);
	beryl_tfree(pass_args);
	
	if(BERYL_TYPEOF(res) == TYPE_ERR) {
		beryl_release(arrayof_array);
		arrayof_array = prev_array;
		return res;
	}
	
	beryl_release(res);
	res = arrayof_array;
	arrayof_array = prev_array;
	return res;
}

/*@@
	construct-array
	len constructor-fn

	Binary function.
	Creates a new array of the given length *len*.
	For each index, the function *constructor-fn* gets called with the index (starting at 0) as an argument, and the value
	at that index is assigned to be the result of the function.
	Returns an error if out of memory.

	Example:
		let a = construct-array 4 with x do x + 1 end
	'a' in this case will be the array (1 2 3 4)
@@*/
static i_val construct_array_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(!beryl_is_integer(args[0])) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected integer number as first argument of 'construct-array'");
	}
	
	i_float f = beryl_as_num(args[0]);
	if(f > I_SIZE_MAX) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Array would be too large");
	}
	
	i_size len = f;
	
	i_val array = beryl_new_array(0, NULL, len, false);
	if(BERYL_TYPEOF(array) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	i_val *a = (i_val *) beryl_get_raw_array(array);
	for(i_size i = 0; i < len; i++) {
		i_val arg = BERYL_NUMBER(i);
		i_val item = beryl_call(args[1], &arg, 1, true);
		beryl_release(arg);

		if(BERYL_TYPEOF(item) == TYPE_ERR) {
			beryl_release(array);
			return item;
		}
		a[i] = item;
	}
	array.len = len;
	
	return array;
}

/*@@
	loop
	body

	Unary function.
	Calls the given function *body* continuously.
	If the function returns true, the loop continues. If false then the loop exits.
	Returns an error if body returns a non-boolean value.
@@*/
static i_val loop_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	while(true) {
		i_val res = beryl_call(args[0], NULL, 0, true);
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			return res;
		if(BERYL_TYPEOF(res) != TYPE_BOOL) {
			beryl_blame_arg(res);
			beryl_release(res);
			return BERYL_ERR("Expected loop body to return boolean");
		}
		
		if(!beryl_as_bool(res))
			break;
	}
	
	return BERYL_NULL;
}

#define EXPECT_TYPE_I(index, type, name, ordinal_name) \
{ \
	assert(n_args > index); \
	if(BERYL_TYPEOF(args[index]) != type) { \
		beryl_blame_arg(args[index]); \
		return BERYL_ERR("Expected " name " as " ordinal_name " argument, got '%0'"); \
	} \
}


#define EXPECT_TYPE1(t1, name1) \
{ \
	EXPECT_TYPE_I(0, t1, name1, "first"); \
}

#define EXPECT_TYPE2(t1, name1, t2, name2) \
{ \
	EXPECT_TYPE1(t1, name1); \
	EXPECT_TYPE_I(1, t2, name2, "second"); \
}

#define EXPECT_TYPE3(t1, name1, t2, name2, t3, name3) \
{ \
	EXPECT_TYPE2(t1, name1, t2, name2); \
	EXPECT_TYPE_I(2, t3, name3, "third"); \
}

static bool match_str(const char *str, const char *str_end, const char *substr, size_t substr_len) {
	assert(str <= str_end);
	size_t str_len = str_end - str;
	if(str_len < substr_len)
		return false;
	
	while(substr_len--) {
		assert(str < str_end);
		if(*str != *substr)
			return false;
		str++;
		substr++;
	}
	return true;
}

static const char *find_str(const char *str, const char *str_end, const char *substr, size_t substr_len) {
	for(const char *c = str; c < str_end; c++) {
		if(match_str(c, str_end, substr, substr_len))
			return c;
	}
	return NULL;
}

static const char *find_str_right(const char *str, const char *str_end, const char *substr, size_t substr_len) {
	for(const char *c = str_end - 1; c >= str; c--) {
		if(match_str(c, str_end, substr, substr_len))
			return c;
	}
	return NULL;
}

/*@@
	find
	string substring

	Binary function.
	Returns the index at which *substring* is first found in *string*, starting from the left.
	Returns null if *string* does not contain *substring*.
	The index returned is the first byte of *substring*, offset from the beginning of *string*.
@@*/
static i_val find_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE2(
		TYPE_STR, "string",
		TYPE_STR, "string"
	);
	
	i_size str_len = BERYL_LENOF(args[0]);
	const char *str = beryl_get_raw_str(&args[0]);
	const char *str_end = str + str_len;
	const char *at = find_str(str, str_end, beryl_get_raw_str(&args[1]), BERYL_LENOF(args[1]));
	
	if(at == NULL)
		return BERYL_NULL;
	return BERYL_NUMBER(at - str);
}

/*@@
	beginswith?
	string substring
	
	Binary function.
	Returns true if *string* begins with *substring*, false if it does not.
@@*/
static i_val beginswith_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	
	EXPECT_TYPE2(
		TYPE_STR, "string",
		TYPE_STR, "string"
	);

	i_val res = find_callback(args, n_args);
	return BERYL_BOOL(beryl_val_cmp(res, BERYL_NUMBER(0)) == 0);
}

/*@@
	find-right
	string substring
	
	Binary function.
	Like 'find', but begins from the right instead of the left.
	Returns the index at which *substring* can be found inside *string*.
	Returns null if *string* does not contain *substring*.
@@*/
static i_val find_right_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE2(
		TYPE_STR, "string",
		TYPE_STR, "string"
	);
	
	i_size str_len = BERYL_LENOF(args[0]);
	const char *str = beryl_get_raw_str(&args[0]);
	const char *str_end = str + str_len;
	const char *at = find_str_right(str, str_end, beryl_get_raw_str(&args[1]), BERYL_LENOF(args[1]));
	
	if(at == NULL)
		return BERYL_NULL;
	return BERYL_NUMBER(at - str);
}

/*@@
	endswith?
	string substring

	Returns true if *string* ends with *substring*, false if it does not.
@@*/
static i_val endswith_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE2(
		TYPE_STR, "string",
		TYPE_STR, "string"
	);
	
	i_size str_len = BERYL_LENOF(args[0]);
	const char *str = beryl_get_raw_str(&args[0]);
	const char *str_end = str + str_len;
	
	i_size substr_len = BERYL_LENOF(args[1]);
	const char *at = find_str_right(str, str_end, beryl_get_raw_str(&args[1]), substr_len);
	if(at == NULL)
		return BERYL_FALSE;
	
	return BERYL_BOOL(str_end - substr_len == at);
}

/*@@
	substring
	string from to

	Returns a substring extracted from *string*, starting at the integer 
	index *from*, and ending at (but not including) the integer index *to*.
	The indicies represent the number of bytes offset from the beginning of *string*.

	Returns an error if out of memory or if any of the indicies are out of bounds.
@@*/
static i_val substring_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE3(
		TYPE_STR, "string",
		TYPE_NUMBER, "number",
		TYPE_NUMBER, "number"
	);
	
	if(!beryl_is_integer(args[1])) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected starting index as integer, got '%0'");
	}
	
	if(!beryl_is_integer(args[2])) {
		beryl_blame_arg(args[2]);
		return BERYL_ERR("Expected ending index as integer, got '%0'");
	}
	
	i_float fi_f = beryl_as_num(args[1]);
	i_float ti_f = beryl_as_num(args[2]);
	
	if(fi_f > ti_f) {
		beryl_blame_arg(args[1]);
		beryl_blame_arg(args[2]);
		return BERYL_ERR("Start index (%0) for substring is larger than end index (%1)");
	}
	
	if(fi_f < 0) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Substring start index (%0) is out of bounds");
	}
	
	if(ti_f > I_SIZE_MAX) {
		beryl_blame_arg(args[2]);
		return BERYL_ERR("Substring end index (%0) is out of bounds");
	}
	
	i_size from = fi_f;
	i_size to = ti_f;
	
	i_size substr_len = to - from;
	if(to > BERYL_LENOF(args[0])) {
		beryl_blame_arg(args[2]);
		return BERYL_ERR("Substring end index (%0) is out of bounds");
	}
	
	const char *from_str = beryl_get_raw_str(&args[0]);
	assert(from_str + from + substr_len <= from_str + BERYL_LENOF(args[0]));
	
	i_val substr = beryl_new_string(substr_len, from_str + from);
	
	if(BERYL_TYPEOF(substr) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	return substr;
}

/*@@
	default
	maybe-null default-value

	Binary function.
	If *maybe-null* is null, *default-value* is returned, otherwise *maybe-null* is returned.
@@*/
static i_val default_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	if(BERYL_TYPEOF(args[0]) == TYPE_NULL)
		return beryl_retain(args[1]);
	else
		return beryl_retain(args[0]);
}

struct str_buff {
	char *str, *top, *end;
};

static bool str_buff_init(struct str_buff *buff) {
	#define STR_BUFF_START_SIZE 16
	buff->str = beryl_alloc(STR_BUFF_START_SIZE);
	buff->top = buff->str;
	buff->end = buff->str + STR_BUFF_START_SIZE;
	
	return buff->str != NULL;
}

static void str_buff_free(struct str_buff *buff) {
	beryl_free(buff->str);
	buff->str = NULL;
	buff->top = NULL;
	buff->end = NULL;
}

/*
static void mcpy(void *to, const void *from, size_t n) {
	char *to_p = to;
	const char *from_p = from;
	while(n--) {
		*(to_p++) = *(from_p++);
	}
}
*/

static bool str_buff_pushs(struct str_buff *buff, const char *str, size_t len) {
	char *p = buff->top;
	buff->top += len;
	if(buff->top > buff->end) {
		size_t cap = buff->end - buff->str;
		size_t blen = p - buff->str;
		cap = (cap * 3) / 2 + len;
		
		char *new_alloc = beryl_realloc(buff->str, cap);
		if(new_alloc == NULL) {
			buff->top -= len;
			return false;
		}
		buff->str = new_alloc;
		buff->top = buff->str + blen + len;
		buff->end = buff->str + cap;
		p = buff->str + blen;
	}
	
	mcpy(p, str, len);
	return true;
}

/*@@
	str-replace
	str replace-str with-str

	Creates a copy of *str* where every instance of *replace-str* is
	replaced with *with-str*.
	Returns an error if out of memory.
@@*/
static i_val str_replace_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE3(
		TYPE_STR, "string",
		TYPE_STR, "string",
		TYPE_STR, "string"
	);
	
	const char *str = beryl_get_raw_str(&args[0]);
	i_size str_len = BERYL_LENOF(args[0]);
	const char *str_end = str + str_len;
	
	const char *replace = beryl_get_raw_str(&args[1]);
	i_size replace_len = BERYL_LENOF(args[1]);
	
	const char *replace_with = beryl_get_raw_str(&args[2]);
	i_size replace_with_len = BERYL_LENOF(args[2]);
		
	/*
	if(BERYL_LENOF(replace) == BERYL_LENOF(replace_with) && beryl_get_refcount(str) == 1) {
	
	}*/
	struct str_buff buff;
	bool ok = str_buff_init(&buff);
	if(!ok)
		goto MEM_ERR;
	
	const char *start = NULL;
	for(const char *c = str; c < str_end; c++) {
		if(match_str(c, str_end, replace, replace_len)) {
			if(start != NULL) {
				bool ok = str_buff_pushs(&buff, start, c - start);
				if(!ok)
					goto MEM_ERR;	
			}
			bool ok = str_buff_pushs(&buff, replace_with, replace_with_len);
			if(!ok)
				goto MEM_ERR;
			c += replace_len;
			c--;
			start = NULL;
		} else if(start == NULL)
			start = c;
	}
	
	if(start != NULL) {
		bool ok = str_buff_pushs(&buff, start, str_end - start);
		if(!ok)
			goto MEM_ERR;
	}
	
	size_t res_len = buff.top - buff.str;
	if(res_len > I_SIZE_MAX) {
		str_buff_free(&buff);
		return BERYL_ERR("Resulting string would be too large");
	}
	
	i_val res = beryl_new_string(res_len, buff.str);
	if(BERYL_TYPEOF(res) == TYPE_NULL)
		goto MEM_ERR;
	
	str_buff_free(&buff);
	return res;
	
	MEM_ERR:
	str_buff_free(&buff);
	return BERYL_ERR("Out of memory");
}

static bool c_is_digit(char c) {
	return c >= '0' && c <= '9';
}

static int char_as_digit(char c) {
	assert(c_is_digit(c));
	return c - '0';
}

/*@@
	parse-number
	str

	Unary function.
	Parses the string *str* as a number.
	Skips any leading non-numeric characters.
	Also skips any characters found after the number.
	
	Example:
		parse-number "   foo -12.5bar"
	returns the number -12.5
@@*/
static i_val parse_number_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE1(TYPE_STR, "string");
	
	const char *str = beryl_get_raw_str(&args[0]);
	i_size len = BERYL_LENOF(args[0]);
	const char *str_end = str + len;
	
	const char *c = str;
	for(; c != str_end; c++) { //Skip all the leading non-number characters
		if(c_is_digit(*c) || *c == '-')
			break;
	}
	
	i_float res = 0;
	bool negative = false;
	if(c != str_end && *c == '-') {
		negative = true;
		c++;
	}
	
	for(; c != str_end; c++) {
		if(!c_is_digit(*c))
			break;
		res *= 10;
		res += char_as_digit(*c);
	}
	
	if(c != str_end && *c == '.') {
		c++;
		i_float pow = 1;
		for(; c != str_end; c++) {
			if(!c_is_digit(*c))
				break;
			pow *= 0.1;
			res += pow * char_as_digit(*c);
		}
	}
	
	return BERYL_NUMBER(negative ? -res : res);
}

size_t iilog10(size_t i) {
	size_t n = 0;
	while(i /= 10) //TODO: Optimize
		n++;
	return n;
}

size_t iflog10(i_float f, i_float *opt_out) {
	size_t n = 0;
	while((f /= 10.0) > 1) //TODO: Optimize
		n++;
	
	if(opt_out != NULL)
		*opt_out = f * 10;
	
	return n;
}

static char int_as_digit(int i) {
	assert(i >= 0 && i <= 9);
	return '0' + i;
}

struct stringf {
	char *str, *end, *at;
};

static void init_stringf(struct stringf *f, char *str, size_t len) {
	f->str = str;
	f->end = str + len;
	f->at = str;
}

static void write_char(struct stringf *f, char c) {
	assert(f->at < f->end);
	*(f->at++) = c;
}

static void write_char_at(struct stringf *f, size_t offset, char c) {
	char *s = f->at + offset;
	assert(s >= f->str && s < f->end);
	*s = c;
}

static void	move_cursor(struct stringf *f, size_t offset) {
	f->at += offset;
	assert(f->at <= f->end);
}


static i_val num_to_str(i_float num) {
	bool negative = num < 0;
	if(negative)
		num = -num;
	
	if(num / 10 == num) {
		if(num == 0)
			return BERYL_CONST_STR("0");

		if(negative)
			return BERYL_CONST_STR("-infinity");
		else
			return BERYL_CONST_STR("infinity");
	}
	
	if(num > (i_float) LARGE_UINT_TYPE_MAX) {
		i_float	significand;
		size_t exp = iflog10(num, &significand);
		size_t n_sig_digits = 4;
		size_t n_exp_digits = iilog10(exp) + 1;

		size_t n_total = (negative ? 1 : 0) + (n_sig_digits + 2) + 2 + n_exp_digits; // [-]x.(digit * n_sig_digits)e+(digit * n_exp_digits)
		i_val res = beryl_new_string(n_total, NULL);
		if(BERYL_TYPEOF(res) == TYPE_NULL)
			return BERYL_NULL;

		struct stringf s;
		init_stringf(&s, (char *) beryl_get_raw_str(&res), n_total);
		if(negative) {
			write_char(&s, '-');
		}
		
		write_char(&s, int_as_digit(significand));
		write_char(&s, '.');
		for(size_t i = 0; i < n_sig_digits; i++) {
			significand -= (int) significand;
			significand *= 10;
			write_char(&s, int_as_digit(significand));
		}
		
		write_char(&s, 'e');
		write_char(&s, '+');
		for(size_t i = n_exp_digits - 1; true; i--) {
			write_char_at(&s, i, int_as_digit(exp % 10));
			exp /= 10;
			if(i == 0)
				break;
		}
		return res;
	}
	
	large_uint_type int_v = num;
	
	size_t n_idigits = iilog10(int_v) + 1;
	size_t n_ddigits = beryl_is_integer(BERYL_NUMBER(num)) ? 0 : 4;
	
	size_t n_total = (negative ? 1 : 0) + n_idigits + n_ddigits;
	if(n_ddigits != 0)
		n_total += 1; // The dot '.'
	i_val res = beryl_new_string(n_total, NULL);
	if(BERYL_TYPEOF(res) == TYPE_NULL)
		return BERYL_NULL;
	
	struct stringf s;
	init_stringf(&s, (char *) beryl_get_raw_str(&res), n_total);
	if(negative)
		write_char(&s, '-');
	
	large_uint_type i_num = int_v;
	for(size_t i = n_idigits - 1; true; i--) {
		char digit = int_as_digit(i_num % 10);
		i_num /= 10;
		write_char_at(&s, i, digit);
		if(i == 0)
			break;
	}
	move_cursor(&s, n_idigits);
	
	if(n_ddigits != 0)
		write_char(&s, '.');
	
	i_float dec_part = num - int_v;
	for(size_t i = 0; i < n_ddigits; i++) {
		dec_part *= 10;
		char digit = int_as_digit(((large_uint_type) dec_part) % 10);
		write_char_at(&s, i, digit);
	}
	
	return res;
}

i_val i_val_as_string(i_val val) {
	switch(BERYL_TYPEOF(val)) {
		
		case TYPE_NUMBER: {
			i_val res = num_to_str(beryl_as_num(val));
			if(BERYL_TYPEOF(res) == TYPE_NULL)
				return BERYL_ERR("Out of memory");
			return res;
		}
		
		case TYPE_FN:
		case TYPE_EXT_FN:
			return BERYL_CONST_STR("Function");
		case TYPE_ARRAY:
			return BERYL_CONST_STR("Array"); //TODO: Implement array->string conversion
		case TYPE_TAG:
			return BERYL_CONST_STR("Tag");
		case TYPE_NULL:
			return BERYL_CONST_STR("Null");
		case TYPE_STR:
			return beryl_retain(val);
		case TYPE_TABLE:
			return BERYL_CONST_STR("Table");
		default:
			return BERYL_CONST_STR("Unkown");
	}
}

/*@@
	as-string
	val

	Unary function.
	Coerces *val* into a string.
@@*/
static i_val as_string_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	return i_val_as_string(args[0]);
}

/*@@
	round
	number

	Rounds the number to the nearest integer.
	Rounds away from zero. (0.5 is rounded to 1, -0.5 is rounded to -1)
@@*/
static i_val round_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE1(TYPE_NUMBER, "number");
	
	i_float num = beryl_as_num(args[0]);
	
	bool negative = num < 0;
	if(negative)
		num = -num;
	
	if(num > BERYL_NUM_MAX_INT)
		return beryl_retain(args[0]);
	
	large_uint_type i = (large_uint_type) (num + 0.5);
	i_float res = i;
	if(negative)
		res = -res;
	
	return BERYL_NUMBER(res);
}

/*@@
	is-int
	val

	Returns true if *val* is an integer number, false otherwise.
@@*/
static i_val is_int_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	return BERYL_BOOL(beryl_is_integer(args[0]));
}

static i_val pipe_callback(const i_val *args, i_size n_args) { // DOESN'T USE AUTORELEASE
	(void) n_args;

	i_val res = beryl_call(args[1], &args[0], 1, false);
	return res;
}

static i_val error_callback(const i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected string error message as argument for 'error', got '%0'");
	}
	for(i_size i = 1; i < n_args; i++)
		beryl_blame_arg(args[i]);	
	return beryl_str_as_err(args[0]);
}

static i_size bt_parent(i_size i) {
	assert(i != 0);
	return (i - 1) / 2;
}

static i_size bt_left_child(i_size i) {
	return i * 2 + 1;
}

static i_size bt_right_child(i_size i) {
	return i * 2 + 2;
}

static bool is_inside_heap(i_size i, i_size len) {
	return i < len;
}

static bool sort_err = false;

static inline int checked_cmp(i_val a, i_val b) {
	int res = beryl_val_cmp(a, b);
	if(res == 2 && !sort_err) {
		sort_err = true;
		beryl_blame_arg(a);
		beryl_blame_arg(b);
	}
	return res;
}

static void make_heap(i_val *array, i_size at, i_size len) {
	START:
	assert(at < len);
	
	i_val top = array[at];
	i_size left_i = bt_left_child(at);
	i_size right_i = bt_right_child(at);
	
	if(!is_inside_heap(left_i, len)) { //If the index has no children
		assert(!is_inside_heap(right_i, len));
		return;
	}
	
	i_val left = array[left_i];
	
	if(!is_inside_heap(right_i, len)) { //If it only has one child
		if(checked_cmp(left, top) == -1) { //If the left node is larger
			array[at] = left;
			array[left_i] = top;
			
			at = left_i;
			goto START; //Essentially tail recursion
		} else
			return; //If it only has one child, and that child is less or equal then nothing needs to be done
	}
	
	//If it has two children
	assert(is_inside_heap(right_i, len));
	i_val right = array[right_i];
	
	if(checked_cmp(left, top) == -1) {
		if(checked_cmp(left, right) == -1) {
			array[at] = left;
			array[left_i] = top;
			
			at = left_i;
			goto START;
		} else {
			array[at] = right;
			array[right_i] = top;
			
			at = right_i;
			goto START;
		}
	}
	
	if(checked_cmp(right, top) == -1) {
		if(checked_cmp(right, left) == -1) {
			array[at] = right;
			array[right_i] = top;
			
			at = right_i;
			goto START;
		} else {
			array[at] = left;
			array[left_i] = top;
			
			at = left_i;
			goto START;
		}
	}
	
	return;
}

static i_val sort_array(i_val *array, i_size len) { //Returns BERYL_NULL on sucess, an error otherwise
	for(i_size i = bt_parent(len - 1); true; i--) {
		make_heap(array, i, len); //Make heap makes a heap at the index i, assuming that all children of i are already heaps
		if(i == 0)
			break;
	}
	
	for(i_size heap_len = len; heap_len > 1; heap_len--) {
		i_val max = array[0];
		i_size leaf_i = heap_len - 1;
		
		array[0] = array[leaf_i]; //Swap the leaf and the top
		array[leaf_i] = max;
		
		//Fix the heap now that we've swapped the leaf and the top, but don't include the former leaf (i.e reduce the len by 1)
		make_heap(array, 0, heap_len - 1);
	}
	
	if(sort_err) { // If an comparison error occured somewhere when sorting
		sort_err = false;
		return BERYL_ERR("Cannot compare values '%0' and '%1'");
	}
	
	return BERYL_NULL;
}

/*@@
	sort
	array

	Returns a sorted copy of *array*.
	May return an error if any of the values inside array are not comparable, or if out of memory. 
@@*/
static i_val sort_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Can only sort arrays");
	}
	
	i_val array_to_sort;
	i_size len = BERYL_LENOF(args[0]);
	if(beryl_get_refcount(args[0]) == 1) {
		array_to_sort = beryl_retain(args[0]);
	} else {
		array_to_sort = beryl_new_array(len, beryl_get_raw_array(args[0]), len, false); //Creates a copy of the array
	}
	if(BERYL_TYPEOF(array_to_sort) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	i_val err = sort_array((i_val *) beryl_get_raw_array(array_to_sort), len);
	if(BERYL_TYPEOF(err) == TYPE_ERR) {
		beryl_release(array_to_sort);
		return err;
	}
	
	return array_to_sort;
}

static large_uint_type random_from(large_uint_type from) {
	static large_uint_type tape[] = { 59243892, 2014914089654949231, 120301499583420, 23, 3230239239, 120302102103, 904924490212, 10412  };
	#define TAPE_LEN (sizeof(tape) / sizeof(tape[0]))
	for(size_t i = 0; i < TAPE_LEN; i++) {
		tape[i] *= tape[i] + (from+1);
	}
	
	size_t tape_head = 0;
	large_uint_type res = from;
	
	for(large_uint_type i = 1; i != 0; i = i << 1) {
		char bit = from & 1;
		from = from >> 1;
		
		if(bit)
			tape_head += res + 1;
		else
			tape_head -= res + 1;
		
		res *= res;
		res += tape[tape_head % TAPE_LEN];
	}
	
	return res;
}

/*@@
	random


	Zero-arity function. Returns a pseudo-random number in the range 0-1 (inclusive).
	This function should not be used for cryptographic purposes.
@@*/
static i_val random_callback(const i_val *args, i_size n_args) {
	(void) n_args, (void) args;

	#ifndef __EMSCRIPTEN__
	static large_uint_type seed = (large_uint_type) &random_callback;
	#else
	static large_uint_type seed = 0;
	#endif
	
	seed = random_from(seed);
	return BERYL_NUMBER((i_float) (seed) / (i_float) LARGE_UINT_TYPE_MAX);
}

static i_val slice_array(i_val array, i_size from, i_size to) {
	assert(BERYL_TYPEOF(array) == TYPE_ARRAY);
	assert(from <= to);	
	assert(to <= BERYL_LENOF(array));
	
	const i_val *items = beryl_get_raw_array(array);
	
	if(from == 0 && beryl_get_refcount(array) == 1) {
		for(i_size i = BERYL_LENOF(array) - 1; i >= to; i--) {
			beryl_release(items[i]);
			if(i == 0)
				break;
		}
		array.len = to;
		return beryl_retain(array);
	}
	
	i_size len = to - from;
	assert(from + len <= BERYL_LENOF(array));
	i_val res = beryl_new_array(len, items + from, len, false);
	
	if(BERYL_TYPEOF(res) == TYPE_ERR)
		return BERYL_ERR("Out of memory");
	return res;
}

/*@@
	slice
	array from to

	Trinary function.
	Creates a copy of a slice of *array*, starting at the integer 
	index *from*, and ending at (but not including) the integer index *to*.
	Returns an error if out of memory or if either indicies are out of range.
@@*/
static i_val slice_callback(const i_val *args, i_size n_args) {
	EXPECT_TYPE3(
		TYPE_ARRAY, "array",
		TYPE_NUMBER, "number",
		TYPE_NUMBER, "number"
	); 
	(void) n_args;
	
	if(!beryl_is_integer(args[1])) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Can only use integers as array indicies");
	}
	if(!beryl_is_integer(args[2])) {
		beryl_blame_arg(args[2]);
		return BERYL_ERR("Can only use integers as array indicies");
	}
	
	i_float from_n = beryl_as_num(args[1]);
	i_float to_n = beryl_as_num(args[2]);
	
	if(to_n > I_SIZE_MAX) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Index out of range");
	}
	if(from_n > to_n) {
		beryl_blame_arg(args[1]);
		beryl_blame_arg(args[2]);
		return BERYL_ERR("'From' index cannot be larger than 'to' index");
	}
	
	i_size from = from_n;
	i_size to = to_n;
	
	if(from_n < 0) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("'From' index out of range");
	}
	if(to > BERYL_LENOF(args[0])) {
		beryl_blame_arg(args[2]);
		return BERYL_ERR("'To' index out of range");
	}
	
	return slice_array(args[0], from, to);
}

/*@@
	pop
	array

	Unary function.
	Creates a copy of *array* with the top-most element removed.
	Returns an error if out of memory or if *array* is empty.
@@*/
static i_val pop_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE1(TYPE_ARRAY, "array"); (void) n_args;
	
	if(BERYL_LENOF(args[0]) == 0) {
		return BERYL_ERR("Cannot pop empty array");
	}
	
	return slice_array(args[0], 0, BERYL_LENOF(args[0]) - 1);
}

/*@@
	peek
	array

	Returns the topmost element of *array*.
	Returns an error if *array* is empty.
@@*/
static i_val peek_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE1(TYPE_ARRAY, "array"); (void) n_args;
	
	i_size len = BERYL_LENOF(args[0]);
	if(len == 0)
		return BERYL_ERR("Cannot peek empty array");
	
	return beryl_retain(beryl_get_raw_array(args[0])[len - 1]);
}

/*@@
	apply
	fn args-array

	Calls the given function *fn* with the arguments contained inside the array *args-array*.
	Returns the result of calling *fn*.
@@*/
static i_val apply_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	if(BERYL_TYPEOF(args[1]) != TYPE_ARRAY) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Can only apply arrays to functions");
	}
	
	return beryl_call(args[0], beryl_get_raw_array(args[1]), BERYL_LENOF(args[1]), true);
}

/*@@
	join
	array-a array-b

	Returns a new array that is the result of joining *array-b* to
	the end of *array-a*.
	Returns an error if out of memory.
@@*/
static i_val join_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE2(
		TYPE_ARRAY, "array",
		TYPE_ARRAY, "array"
	);
	
	i_size alen = BERYL_LENOF(args[0]);
	i_size blen = BERYL_LENOF(args[1]);
	
	if(blen > I_SIZE_MAX - alen)
		return BERYL_ERR("Out of memory");
	
	i_size res_len = alen + blen;
	i_val res_array = beryl_new_array(res_len, NULL, res_len, false);
	if(BERYL_TYPEOF(res_array) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	const i_val *a_a = beryl_get_raw_array(args[0]);
	const i_val *b_a = beryl_get_raw_array(args[1]);
	
	i_val *res_a = (i_val *) beryl_get_raw_array(res_array);
	
	while(alen--) {
		*(res_a++) = beryl_retain(*(a_a++));
	}
	while(blen--) {
		*(res_a++) = beryl_retain(*(b_a++));
	}
	
	return res_array;
}

/*@@
	map
	array fn

	Returns a copy of *array*, where every element has been replaced by
	the result of calling *fn* with said element.
	May return an error if out of memory.

	Example:
		let a = array 1 2 3
		let b = map a with x do x * 2 end
	'b' in this case will be the array (2 4 6)
@@*/
static i_val map_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Can only 'map' arrays");
	}
	i_size len = BERYL_LENOF(args[0]);
	
	i_val map_array;
	if(beryl_get_refcount(args[0]) == 1) {
		map_array = beryl_retain(args[0]);
	} else {
		map_array = beryl_new_array(len, NULL, len, 0);
		if(BERYL_TYPEOF(map_array) == TYPE_NULL)
			return BERYL_ERR("Out of memory");
		i_val *a = (i_val *) beryl_get_raw_array(map_array);
		for(i_size i = 0; i < len; i++) {
			a[i] = BERYL_NULL;
		}
	}
	
	assert(BERYL_TYPEOF(map_array) == TYPE_ARRAY);
	assert(BERYL_LENOF(map_array) >= len);
	
	i_val *dst = (i_val *) beryl_get_raw_array(map_array);
	const i_val *src = beryl_get_raw_array(args[0]);
	for(i_size i = 0; i < len; i++) {
		i_val arg = beryl_retain(src[i]);
		beryl_release(dst[i]);
		
		i_val res = beryl_call(beryl_retain(args[1]), &arg, 1, false);
		if(BERYL_TYPEOF(res) == TYPE_ERR) {
			beryl_release(map_array);
			return res;
		}
		dst[i] = res;
	}
	
	return map_array;
}

/*@@
	forevery
	... items body

	Variadic function taking at least two arguments.
	Calls the function *body* for every item given in the variadic arguments before *body*.
	
	Example:
		forevery 1 2 3 print
	will print '1', '2', '3'.
@@*/
static i_val forevery_callback(const i_val *args, i_size n_args) { // DOESN'T USE AUTORELEASE
	i_val res = BERYL_NULL;
	i_val fn = args[n_args - 1];
	
	assert(n_args > 0);
	for(i_size i = 0; i < n_args - 1; i++) {
		beryl_release(res);
		res = beryl_call(beryl_retain(fn), &args[i], 1, false);
		
		if(BERYL_TYPEOF(res) == TYPE_ERR) {
			for(i_size j = i + 1; j < n_args - 1; j++)
				beryl_release(args[i]);
			beryl_release(fn);
			return res;
		}
	}
	beryl_release(fn);
	return res;
}

/*@@
	strip
	str

	Returns a copy of the string *str*, with all leading and trailing whitespace removed.
	If the string contains only whitespace, an empty string is returned.
	Returns an error if out of memory.
@@*/
static i_val strip_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE1(
		TYPE_STR, "string"
	);
	
	const char *str = beryl_get_raw_str(&args[0]);
	i_size len = BERYL_LENOF(args[0]);
	const char *str_end = str + len;
	
	const char *start;
	for(start = str; start < str_end; start++) {
		if(*start != '\t' && *start != ' ' && *start != '\r' && *start != '\n')
			break;
	}
	
	const char *end;
	for(end = str_end - 1; end >= start; end--) {
		if(*end != '\t' && *end != ' ' && *end != '\r' && *end != '\n')
			break;
	}
	
	i_val new_str = beryl_new_string((end - start) + 1, start);
	if(BERYL_TYPEOF(new_str) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	return new_str;
}

/*@@
	split
	str split-at

	Returns an array of all substrings located between instances of the string *split-at* in the string *str*.
	This also includes the substring found before the first instance of *split-at* as well as the last instance
	found after the last instance of *split-at*.
	Returns an error on out of memory.
	
	Example:
		split "1,2,,3," ","
	Returns the array ("1" "2" "" "3" "")
@@*/
static i_val split_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE2(
		TYPE_STR, "string",
		TYPE_STR, "string"
	);
	
	const char *str = beryl_get_raw_str(&args[0]);
	i_size len = BERYL_LENOF(args[0]);
	const char *str_end = str + len;
	
	const char *split_at = beryl_get_raw_str(&args[1]);
	i_size split_at_len = BERYL_LENOF(args[1]);
	
	if(split_at_len == 0) {
		return BERYL_ERR("String splitter cannot be an empty string");
	}
	
	i_size tmp_buff_cap = 4, tmp_buff_len = 0; //TODO: Implement some generic "beryl_array_builder" type that handles this more conveniently
	i_val *tmp_buff = beryl_alloc(tmp_buff_cap * sizeof(i_val));
	if(tmp_buff == NULL)
		return BERYL_ERR("Out of memory");
	
	const char *prev = NULL;
	for(const char *c = str; c < str_end;) {
		if(match_str(c, str_end, split_at, split_at_len)) {
			i_val new_str;
			if(prev == NULL)
				new_str = BERYL_CONST_STR("");
			else
				new_str = beryl_new_string(c - prev, prev);
			
			if(BERYL_TYPEOF(new_str) == TYPE_NULL)
				goto MEM_ERR;
			if(tmp_buff_len == tmp_buff_cap) {
				tmp_buff_cap *= 2;
				if(tmp_buff_cap <= tmp_buff_len) {
					beryl_release(new_str);
					goto MEM_ERR;
				}
				
				i_val *tmp = beryl_realloc(tmp_buff, tmp_buff_cap * sizeof(i_val));
				if(tmp == NULL) {
					beryl_release(new_str);
					goto MEM_ERR;
				}
				
				tmp_buff = tmp;
			}
			tmp_buff[tmp_buff_len++] = new_str;
			
			prev = NULL;
			c += split_at_len;
		} else {
			if(prev == NULL)
				prev = c;
			c++;
		}
	}
	
	i_val new_str;
	if(prev == NULL)
		new_str = BERYL_CONST_STR("");
	else
		new_str = beryl_new_string(str_end - prev, prev);
	
	if(BERYL_TYPEOF(new_str) == TYPE_NULL)
		goto MEM_ERR;
	
	if(tmp_buff_len == tmp_buff_cap) {
		tmp_buff_cap += 1;
		if(tmp_buff_cap <= tmp_buff_len) {
			beryl_release(new_str);
			goto MEM_ERR;
		}
			
		i_val *tmp = beryl_realloc(tmp_buff, tmp_buff_cap * sizeof(i_val));
		if(tmp == NULL) {
			beryl_release(new_str);
			goto MEM_ERR;
		}

		tmp_buff = tmp;
	}
	tmp_buff[tmp_buff_len++] = new_str;
	
	i_val res_array = beryl_new_array(tmp_buff_len, tmp_buff, tmp_buff_len, false);
	
	for(i_size i = 0; i < tmp_buff_len; i++)
		beryl_release(tmp_buff[i]); //The array retains  all the strings when created.
	beryl_free(tmp_buff);

	if(BERYL_TYPEOF(res_array) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	return res_array;
	
	MEM_ERR:
	for(i_size i = 0; i < tmp_buff_len; i++)
		beryl_release(tmp_buff[i]);
	beryl_free(tmp_buff);
	return BERYL_ERR("Out of memory");
}

static i_val identity_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	return beryl_retain(args[0]);
}

/*@@
	typeof
	value

	Unary function.
	Returns the type of the given *value*, as a string.
	Returns one of the following:
		number
		struct
		array
		null
		string
		function
		bool
		tag
		object
@@*/
static i_val typeof_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	switch(BERYL_TYPEOF(args[0])) {
	
		case TYPE_NUMBER:
			return BERYL_CONST_STR("number");
		
		case TYPE_TABLE:
			return BERYL_CONST_STR("struct");
		
		case TYPE_ARRAY:
			return BERYL_CONST_STR("array");
		
		case TYPE_NULL:
			return BERYL_CONST_STR("null");
		
		case TYPE_STR:
			return BERYL_CONST_STR("string");
		
		case TYPE_FN:
		case TYPE_EXT_FN:
			return BERYL_CONST_STR("function");
		
		case TYPE_BOOL:
			return BERYL_CONST_STR("bool");
		
		case TYPE_TAG:
			return BERYL_CONST_STR("tag");
		
		case TYPE_OBJECT:
			return BERYL_CONST_STR("object");
		
		default:
			assert(false);
			return BERYL_CONST_STR("unkown");
	}
}

/*@@
	join-with
	array string

	Returns a string that is the result of joining every element of *array* together with *string*.
	The given *array* must contain strings only.
	
	Example:
		let s = join-with (array "1" "2" "3") "-"
		assert s == "1-2-3"
@@*/
static i_val join_with_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE2(
		TYPE_ARRAY, "array",
		TYPE_STR, "string"
	);
	
	i_size items_total_len = 0;
	const i_val *items = beryl_get_raw_array(args[0]);
	i_size n_items = BERYL_LENOF(args[0]);
	
	if(n_items == 0)
		return BERYL_CONST_STR("");
	
	for(i_size i = 0; i < n_items; i++) {
		if(BERYL_TYPEOF(items[i]) != TYPE_STR) {
			beryl_blame_arg(items[i]);
			return BERYL_ERR("Array passed to 'join-with' must contain strings only");
		}
		i_size len = BERYL_LENOF(items[i]);
		if(len > I_SIZE_MAX - items_total_len)
			return BERYL_ERR("Resulting string would be too large");
		items_total_len += len;
	}
	
	i_val join_str = args[1];
	
	assert(n_items >= 1);
	i_size n_joiners = n_items - 1;
	if(BERYL_LENOF(join_str) != 0 && n_joiners > I_SIZE_MAX / BERYL_LENOF(join_str))
		return BERYL_ERR("Resulting string would be too large");
	i_size total_joiners_len = n_joiners * BERYL_LENOF(join_str);
	
	if(total_joiners_len > I_SIZE_MAX - items_total_len)
		return BERYL_ERR("Resulting string would be too large");
	
	i_size total_len = items_total_len + total_joiners_len;
	
	i_val new_str = beryl_new_string(total_len, NULL);
	if(BERYL_TYPEOF(new_str) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	char *str = (char *) beryl_get_raw_str(&new_str);
	const char *str_end = str + total_len;
	
	for(i_size i = 0; i < n_items; i++) {
		i_val item = items[i];
		i_size len = BERYL_LENOF(item);
		mcpy(str, beryl_get_raw_str(&item), len);
		str += len;
		assert(str <= str_end);
		
		if(i != n_items - 1) {
			mcpy(str, beryl_get_raw_str(&join_str), BERYL_LENOF(join_str));
			str += BERYL_LENOF(join_str);
			assert(str <= str_end);
		}
	}
	
	return new_str;
}

/*@@
	repeat
	string n

	Returns a new string that consists of *string* repeated *n* times.
	May return an error on out of memory.
@@*/
static i_val repeat_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE2(
		TYPE_STR, "string",
		TYPE_NUMBER, "number"
	);
	
	if(!beryl_is_integer(args[1])) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Can only repeat strings an integer number of times");
	}
	i_float num = beryl_as_num(args[1]);
	if(num < 0 || num > I_SIZE_MAX) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Invalid number of repetitions");
	}
	
	i_size len = BERYL_LENOF(args[0]);
	i_size times = num;
	if(len == 0 || times == 0)
		return BERYL_CONST_STR("");
	
	if(times > I_SIZE_MAX / len)
		return BERYL_ERR("Resulting string would be too large");
	
	i_size total_len = times * len;
	i_val str_res = beryl_new_string(total_len, NULL);
	if(BERYL_TYPEOF(str_res) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	const char *src_str = beryl_get_raw_str(&args[0]);
	
	char *str = (char *) beryl_get_raw_str(&str_res);
	const char *str_end = str + total_len;
	for(i_size i = 0; i < times; i++) {
		mcpy(str, src_str, len);
		str += len;
		assert(str <= str_end);
	}
	
	return str_res;
}

/*@@
	max
	... values
	Variadic function taking at least one argument.

	Returns the largest value among *values*.
	Returns an error if some values are incomparable.
@@*/
static i_val max_callback(const i_val *args, i_size n_args) {
	i_val max = args[0];
	for(i_size i = 1; i < n_args; i++) {
		int cmp = beryl_val_cmp(args[i], max);
		if(cmp == 2) {
			beryl_blame_arg(max); beryl_blame_arg(args[i]);
			return BERYL_ERR("Uncomparable values");
		}
		if(cmp == -1)
			max = args[i];
	}
	
	return beryl_retain(max);
}

/*@@
	min
	... values
	Variadic function taking at least one argument.

	Returns the smallest value among *values*.
	Returns an error if some values are incomparable.
@@*/
static i_val min_callback(const i_val *args, i_size n_args) {
	i_val min = args[0];
	for(i_size i = 1; i < n_args; i++) {
		int cmp = beryl_val_cmp(args[i], min);
		if(cmp == 2) {
			beryl_blame_arg(min); beryl_blame_arg(args[i]);
			return BERYL_ERR("Uncomparable values");
		}
		if(cmp == 1)
			min = args[i];
	}
	
	return beryl_retain(min);
}

/*@@
	find-in
	array item

	Returns the lowest index at which an element equal to *item* can be found in *array*.
	Returns null if *array* does not contain any element equal to *item*.
	
	Example:
		let i = find-in (array 1 2 3) 2
		assert i == 1
@@*/
static i_val find_in_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected array as first argument for 'find-in'");
	}
	
	const i_val *a = beryl_get_raw_array(args[0]);
	i_size len = BERYL_LENOF(args[0]);
	
	for(i_size i = 0; i < len; i++) {
		if(beryl_val_cmp(a[i], args[1]) == 0)
			return BERYL_NUMBER(i);
	}
	return BERYL_NULL;
}

static i_val floor_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE1(
		TYPE_NUMBER, "number"
	);
	
	if(beryl_is_integer(args[0]))
		return beryl_retain(args[0]);
	
	return BERYL_NUMBER((large_uint_type) beryl_as_num(args[0]));
}

static i_val ceil_callback(const i_val *args, i_size n_args) {
	(void) n_args;

	EXPECT_TYPE1(
		TYPE_NUMBER, "number"
	);
	
	if(beryl_is_integer(args[0]))
		return beryl_retain(args[0]);
	
	i_float res = (large_uint_type) beryl_as_num(args[0]);
	res += 1;
	
	return BERYL_NUMBER(res);
}

static i_val first_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected array as first argument");
	}
	
	const i_val *a = beryl_get_raw_array(args[0]);
	for(i_size i = 0; i < BERYL_LENOF(args[0]); i++) {
		i_val res = beryl_call(args[1], &a[i], 1, true);
		if(BERYL_TYPEOF(res) != TYPE_BOOL) {
			beryl_blame_arg(res);
			beryl_release(res);
			return BERYL_ERR("Expected boolean as result from callback function, got '%0'");
		}
		if(beryl_as_bool(res))
			return BERYL_NUMBER(i);
	}
	
	return BERYL_NULL;
}

static i_val exists_callback(const i_val *args, i_size n_args) {
	i_val res = first_callback(args, n_args);
	if(BERYL_TYPEOF(res) == TYPE_ERR)
		return res;
	
	return BERYL_BOOL(BERYL_TYPEOF(res) != TYPE_NULL);
}

static i_val all_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_ARRAY) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected array as first argument");
	}
	
	const i_val *a = beryl_get_raw_array(args[0]);
	for(i_size i = 0; i < BERYL_LENOF(args[0]); i++) {
		i_val res = beryl_call(args[1], &a[i], 1, true);
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			return res;
		if(BERYL_TYPEOF(res) != TYPE_BOOL) {
			beryl_blame_arg(res);
			beryl_release(res);
			return BERYL_ERR("Expected boolean as result from callback function, got '%0'");
		}
		if(!beryl_as_bool(res))
			return BERYL_FALSE;
	}
	return BERYL_TRUE;
}

static i_val is_valid_identifier_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	
	EXPECT_TYPE1(
		TYPE_STR, "string"
	);
	
	const char *str = beryl_get_raw_str(&args[0]);
	i_size len = BERYL_LENOF(args[0]);
	
	while(len--) {
		char c = *str;
		
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9')) {
		
		} else
			return BERYL_FALSE;
		
		str++;
	}
	
	return BERYL_TRUE;
}

static size_t istrlen(const char *str) {
	size_t l = 0;
	while(*str != '\0') {
		str++;
		l++;
	}
	return l;
}

bool load_core_lib() {
	static struct beryl_external_fn fns[] = {
		FN(-3, "+", add_callback),
		FN(-2, "-", sub_callback),
		FN(-3, "*", mul_callback),
		FN(-3, "/", div_callback),
		
		FN(-3, "if", if_callback),
		
		FN(2, "==", eq_callback),
		FN(2, "=/=", not_eq_callback),
		FN(1, "not", not_callback),
		
		FN(-1, "array", array_callback),
		
		FN(2, "for-in", for_in_callback),
		FN(3, "for", for_callback),
		FN(2, "foreach-in", foreach_in_callback),
		
		FN(-1, "table", table_callback),
		FN(-1, "struct", table_callback), // 'struct' is a more appropriate name for the datatype than 'table' 
		
		FN(0, "tag", tag_callback),
		
		MANUAL_RELEASE_FN(1, "invoke", invoke_callback),
		FN(1, "new", invoke_callback),
		
		FN(-2, "assert", assert_callback),
		
		FN(-3, "and?", and_callback),
		FN(-3, "or?", or_callback),
		
		FN(2, "<", less_callback),
		FN(2, ">", greater_callback), 
		FN(2, "=<=", less_eq_callback),
		FN(2, "=>=", greater_eq_callback),
		
		FN(3, "insert", insert_callback),

		FN(2, "union", union_callback),
		
		FN(-3, "cat", concat_callback),
		
		MANUAL_RELEASE_FN(2, "in?", in_callback),
		
		FN(1, "sizeof", sizeof_callback),
		
		FN(3, "replace", replace_callback),
		
		FN(-2, "eval", eval_callback),
		
		MANUAL_RELEASE_FN(2, "filter", filter_callback),
		
		FN(2, "push", push_callback),
		
		FN(2, "mod", mod_callback),
		
		FN(-2, "arrayof", arrayof_callback),
		FN(2, "construct-array", construct_array_callback),
		
		FN(1, "loop", loop_callback),
		
		FN(2, "find", find_callback),
		FN(2, "beginswith?", beginswith_callback),
		
		FN(2, "find-right", find_right_callback),
		FN(2, "endswith?", endswith_callback),
		
		FN(3, "substring", substring_callback),
		
		FN(2, "default", default_callback),
		
		FN(3, "str-replace", str_replace_callback),
		
		FN(1, "parse-number", parse_number_callback),
		FN(1, "as-string", as_string_callback),
		
		FN(1, "round", round_callback),
		FN(1, "is-int", is_int_callback),
		
		MANUAL_RELEASE_FN(2, "->", pipe_callback),
		
		FN(3, "try", try_callback),
		FN(-2, "error", error_callback),
		
		FN(1, "sort", sort_callback),
		
		FN(0, "random", random_callback),
		
		FN(3, "slice", slice_callback),
		FN(1, "pop", pop_callback),
		FN(1, "peek", peek_callback),
		
		FN(2, "apply", apply_callback),
		
		FN(2, "join", join_callback),
		
		FN(2, "map", map_callback),
		
		MANUAL_RELEASE_FN(-3, "forevery", forevery_callback),
		
		FN(1, "strip", strip_callback),
		
		FN(2, "split", split_callback),
		
		FN(1, "identity", identity_callback),
		
		FN(1, "typeof", typeof_callback),
		
		FN(2, "join-with", join_with_callback),
		
		FN(2, "repeat", repeat_callback),
		
		FN(-2, "max", max_callback),
		FN(-2, "min", min_callback),
		
		FN(2, "find-in", find_in_callback),
		
		FN(1, "floor", floor_callback),
		FN(1, "ceil", ceil_callback),
		
		FN(2, "first", first_callback),
		FN(2, "exists", exists_callback),
		FN(2, "all", all_callback),
		
		FN(1, "is-valid-identifier", is_valid_identifier_callback)
	};
	
	else_tag = beryl_new_tag();
	elseif_tag = beryl_new_tag();
	catch_tag = beryl_new_tag();
	print_trace_tag = beryl_new_tag();
	
	#define LSTR(str) str, sizeof(str) - 1
	beryl_set_var("else", sizeof("else") - 1, else_tag, true);
	beryl_set_var("elseif", sizeof("elseif") - 1, elseif_tag, true);
	beryl_set_var("catch", sizeof("catch") - 1, catch_tag, true);
	beryl_set_var("catch-log", sizeof("catch-log") - 1, print_trace_tag, true);
	
	for(size_t i = 0; i < LENOF(fns); i++) {
		bool ok = beryl_set_var(fns[i].name, fns[i].name_len, BERYL_EXT_FN(&fns[i]), true);
		if(!ok)
			return false;
	}
	
	beryl_set_var("false", sizeof("false") - 1, BERYL_FALSE, true);
	beryl_set_var("true", sizeof("true") - 1, BERYL_TRUE, true);
	beryl_set_var("null", sizeof("null") - 1, BERYL_NULL, true);
	beryl_set_var("max-int", sizeof("max-int") - 1, BERYL_NUMBER(BERYL_NUM_MAX_INT), true);
	
	beryl_set_var("quote", sizeof("quote") - 1, BERYL_CONST_STR("\""), true);
	beryl_set_var("newline", sizeof("newline") - 1, BERYL_CONST_STR("\n"), true);
	beryl_set_var("tab", sizeof("tab") - 1, BERYL_CONST_STR("\t"), true);
	beryl_set_var("carriage-return", sizeof("carriage-return") - 1, BERYL_CONST_STR("\r"), true);
	
	const char *ver = beryl_version();
	const char *major_ver = beryl_major_version();
	beryl_set_var("beryl-version", sizeof("beryl-version") - 1, BERYL_STATIC_STR(ver, istrlen(ver)), true);
	beryl_set_var("beryl-major-version", sizeof("beryl-major-version") - 1, BERYL_STATIC_STR(major_ver, istrlen(major_ver)), true);
	
	#define DEF_FN(name, args, ...) beryl_set_var(name, sizeof(name) - 1, BERYL_LITERAL_FN(args, __VA_ARGS__), true)
	
	DEF_FN("pairs", t,
		arrayof foreach-in t
	);
	
	DEF_FN("implies?", x y,
		(not x) or? y
	);
	
	DEF_FN("require-version", ver,
		if beryl-major-version =/= (as-string ver) do
			error "Wrong Beryl version (expected version: %0, current version: %1)" ver beryl-major-version
		end
	);
	
	DEF_FN("empty?", container,
		(sizeof container) == 0
	);
	
	return true;
}
