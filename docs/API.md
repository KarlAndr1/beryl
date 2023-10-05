
# How to embed the language
(NOTE: This guide mostly consists of the API guide for Beryl, so some inaccuracies may occur)

Include the "berylscript.h" header and link with the library.

There are mainly two functions for interacting with the interpreter.
```
	beryl_eval(src, src_len, new_scope, err_action)
```
The src is the source code; a char pointer, and src_len is the length of that string.
Note that the source code should remain in memory for as long as the interpreter is used, as the interpreter does
not copy items such as variable names and string constants, it instead keeps pointers to the source code where they are located.
The new_scope is a boolean; if true then any variables declared in the top level scope of the given code are put inside a new scope that is
cleared when the eval function exits. If false then any declared variables are kept in the current scope, and not cleared when the function exits.
New_scope should only be false when calling the eval function externally, not when inside a function called by a script, as that would place the
declared global variables into the local scope.
The err_action enum value decides how the interpreter deals with any errors that reach the top level. If this is BERYL_PROP_ERR then the errors are
kept in the buffer, and not cleared or printed. This should only be done by code that is called by a script and intends to propagate the error back to the
script. BERYL_CATCH_ERR clears all errors, and BERYL_PRINT_ERR prints and then clears errors.

To call functions that are defined in scripts, the beryl_pcall_fn function is used.
```
	beryl_pcall(fn, args, n_args)
```
This function clears any errors/stack trace that may have arisen during the function call.
fn is the function (or array or some other type) to call, args is a pointer to the arguments and n_args the number of arguments.
Note that this function does not retain or release the given arguments (or the function). It instead expects
the caller to keep ownership of these for as long as the function runs. That is to say, for example, if the given arguments all have a reference count of
one, the function will not alter these counts, and the arguments should thus remain in memory as long as no other code decrements them.

If calling a function from a function that was itself called by a beryl script, and intends to propagate any errors that may occur, use the beryl_call function
instead.

Before calling/evaluating any scripts, the standard library/libraries should generally be loaded using the beryl_load_libs() function.
To enable the interpreter to dynamically allocate memory for arrays, non-constant strings etc, use the beryl_set_mem function.
``` 
	beryl_set_mem(alloc, free, realloc).
```
This function takes three function pointers, to some alloc, free and realloc function respectively. If these are not set, any attempts to allocate
memory inside beryl simply fails with an 'out of memory' error.

## Retain and release

The interpreter uses reference counting to automatically manage memory. This is done via the beryl_retain(val) and beryl_release(val) functions.
Every dynamically allocated value keeps increments and decrements for each call to these functions, respectively. When it reaches zero the dynamically
allocated memory of the value is released. Values constructed by the interpreter, or returned by the interpreter are already retained by the interpreter, so
the reciever of these values should only call beryl_release, when they are done with the value. Any actions that make permanent, or long term copies of the values
should call beryl_retain for each value.

## Calling a custom C function from Beryl

To inject a custom function into Beryl, first declare a statically allocated beryl_external_fn struct, like so:
```
struct beryl_external_fn my_fn_struct = {
	1,
	"my_fn",
	sizeof("my_fn") - 1,
	my_fn_callback
};
```
Where the first value is the arity (how many arguments the function expects). If this value is negative then the function is considered variadic, taking a variable
amount of arguments; the arity value then describes the minimum number of arguments expected, -1 meaning no arguments required, -2 meaning at least one argument
and so on.
Then create an i_val out of this function via the BERYL_EXT_FN macro
```
	struct i_val fn = BERYL_EXT_FN(&my_fn_struct);
```
This macro takes a pointer to the struct.

Then use the beryl_set_var function to set a variable to have this function as its value.
```
	beryl_set_var("my_fn", sizeof("my_fn") - 1, fn, false);
```
The final boolean marks whether the variable is a constant or not.
Note that the beryl_set_var should generally only be used by code that was not called from a beryl script, since it injects the variable
into the current scope, even if that scope happens to be the local scope of a function that is currently being called.
In normal cases beryl_set_var is called before any berylscript code is evaluated/called, in which case it injects the variables into the global scope.
