# The Beryl Programming Language

Beryl is a small, interpreted, embeddable scripting language with value semantics and first class functions.
The main feature of Beryl is that the core interpreter can run without any dynamic memory allocation*, and it does not need
to parse or compile scripts beforehand. It can also be built without any external dependencies, excluding some 
typedefines and constants needed from stddef.h and limits.h; however these could be provived from a custom header if needed.

*User defined variadic functions do need access to malloc or some other memeory allocator to be able to be called. If one is not provided
they will instead throw an "out of memory" error when being called.

The interpreter and standard library can make use of any memory allocator that shares the same interface as malloc, realloc and free.

One important thing to note is that the interpreter expects that all code given to it via eval or call_fn remains as is in memory for as long as
the interpreter is used, including sucessive calls to eval or call_fn. This is because the interpreter only maintains references to things like function
source code, string literals and variable names, it does not make independent copies of them.
One exception to this is if beryl_clear is called and all interpreter-related values are discarded, then previously executed source code may be freed or overwritten.

## Examples

Hello world:
```
print "Hello world!"
```

fib.beryl:
```
let fib = function n do
	if (n == 0) or? (n == 1) do
		n
	end, else do
		(fib n - 1) + (fib n - 2)
	end
end
```
Note that 'if' is a function, just like 'print' or 'fib'.

loop.beryl:
```
for 1 11 with i do
	print i
end
```
This prints the numbers 1, 2, ..., 10. for is also a function defined in the standard library.

More examples can be found in ./examples and ./test_scripts

## How to build/use

Run the build.py python script
	python3 build.py

Select either option one for a standalone executable or option three for an embeddable library + headers

To use the library, include the lib.ar file when compiling, and #include the interpreter.h header

If the automated build fails, compiling main.c, lexer.c, interpreter.c and all source files in the libs directory should give a working
standalone build with a REPL.
```
cc src/main.c src/lexer.c src/interpreter.c src/libs/core_lib.c src/libs/common_lib.c src/libs/io_lib.c src/libs/datastructures_lib.c src/libs/debug_lib.c src/libs/modules_lib.c
```

See the documentation (docs/USING.md) for language usage, syntax and semantics.
docs/libraries contains documentation on the functions and constants defined by libraries (src/libs).
