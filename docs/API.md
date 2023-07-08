
# How to embed the language

Include the "interpreter.h" header and link with the library.

There are mainly two functions for interacting with the interpreter.
	int_eval(src, src_len, new_scope, err_action)
The src is the source code, a char pointer, and the src_len is the length of that string.
Note that the source code should remain in memory for as long as the interpreter is used, as the interpreter does
not copy items such as variable names and string constants, it instead keeps pointers to the source code where they are located.
The new_scope is a boolean; if true then any variables declared in the top level scope of the given code are put inside a new scope that is
cleared when the eval function exits. If false then any declared variables are kept in the current scope, and not cleared when the function exits.
New_scope should only be false when calling the eval function externally, not when inside a function called by a script, as that would place the
declared global variables into the local scope.
The err_action enum value decides how the interpreter deals with any errors that reach the top level. If this is INT_ERR_PROP then the errors are
kept in the buffer, and not cleared or printed. This should only be done by code that is called by a script and intends to propagate the error back to the
script. INT_ERR_CATCH clears all errors, and INT_ERR_PRINT prints and then clears errors.

To call functions that are defined in scripts, the int_call_fn function is used.
	int_call_fn(fn, args, n_args, err_action)
fn is the function (or array or some other type) to call, args is a pointer to the arguments, n_args the number of arguments and err_action
is the same as previously described. Note that this function does not retain or release the given arguments (or the function). It instead expects
the caller to keep ownership of these for as long as the function runs. That is to say, for example, if the given arguments all have a reference count of
one, the function will not alter these counts, and the arguments should thus remain in memory as long as no other code decrements them.

## Retain and release

The interpreter uses reference counting to automatically manage memory. This is done via the int_retain(val) and int_release(val) functions.
Every dynamically allocated value keeps increments and decrements for each call to these functions, respectively. When it reaches zero the dynamically
allocated memory of the value is released. Values constructed by the interpreter, or returned by the interpreter are already retained by the interpreter, so
the reciever of these values should only call int_release, when they are done with the value. Any actions that make permanent, or long term copies of the values
should call int_retain for each value.
To actually free the values, the interpreter calls on the "free" function pointer set via the function int_set_mem. The standard library (outside of core_lib)
expects this to be the free function corresponding to the malloc function defined in stdlib.h

## Reading values

The i_val struct represents the values handled by the language. Generally one should not directly access the members of this struct, but instead
do so via the various functions and macros defined in "interpreter.h". 
The most important is the INT_TYPEOF(val) macro, that gives the type of the given value, an unsigned integer value (generally 1 byte) that represents
the dynamic type of that value. All the calls to the various i_val_get_XXX functions should only be performed after confirming that the type of the
value is correct, as these functions will not check the type of the given value (except in debug builds, where it asserts that it is the correct type).
For example, i_val_get_raw_str(val_p) should only be called if val_p points to a value with the type TYPE_STR or TYPE_ERR, i_val_get_int(val) only if the given
value has a type of TYPE_INT and so on.

Note that i_val_get_raw_str(val_p) is an exception to the other getter functions, as this function takes a pointer to the value instead of the value itself.
This is due to the fact that shorter strings may be kept inside the value struct itself, instead of dynamically on the heap. As such the returned char pointer
should only be used for as long as the given i_val pointer is valid. For example
	char *str;
	i_size len;
	{
		i_val strv = ....
		str = i_val_get_raw_str(&strv);
		len = i_val_get_len(strv);
	}
	print_string(str, len);
Could potentially be problemantic, as the pointer to strv does not remain valid after the block is exited, and as such if the string was kept inside strv it
may be overidden by some other value after the block has exited.

## Error handling

When an error/stack trace is to be printed, the interpreter calls the error handler to handle the actual printing. 
This handler must be set before calling the interpreter in a way such that it may try to print out an error stack trace.
The handler is set via the int_set_err_handler function, that takes a function pointer to a function of the type
	void handler(struct int_stack_trace *, size_t, const i_val *, size_t)
Where the first argument is an array to the stack trace, the second the number of entries in said array, the third are the blamed values/arguments
and finally the number of said blamed values.
The set function gets called when right before int_eval or int_call_fn exit, assuming that the err_action argument is INT_ERR_PRINT.


