# The Beryl programming language

Beryl is a small programming language with value semantics that is largely based off of (and shares some code with) legacy Beryl.
Like the legacy Beryl it is an embeddable scripting language that executes directly from source code, no parsing or compiling done ahead of time.
Most of the core language can run without access to libc or any dynamic allocation functions like malloc.

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
	end else do
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

More code examples can be found in ./test_scripts

## Building/Installing

### Unix

Run
```
make
```
to build the project.
Run
```
make install
``` 
to install the built executable and set up the required directories and environment variables locally (in home directory).
To install globally run
```
sudo make install-global
```

Alternatively, without making use of the Makefiles, one can instead first run build.sh
```
./build.sh
```
Which will build the project and all the external/optional libraries. Then run install.sh to install everything and
set up the environment.
```
./install_local.sh
```
or
```
./install_global.sh
```

## Windows

Windows support has not been tested; both the interpreter and the installer/build script may exhibit bugs.

First run
```
make windows
```

Then run install.bat
```
./install.bat
```

## Embedding

The language is written in C99 and is easy to embedd:
```
#include "beryl.h"

#include <string.h>
#include <assert.h>

// -------------------

	const char *prog = 
		"let sum = 0\n"
		"for 1 11 with i do \n"
		"	sum += i \n"
		"end \n"
		"sum";

	struct i_val result = beryl_eval(prog, strlen(prog), BERYL_PRINT_ERR);
	double num = beryl_as_number(result);
	assert(num == 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 9 + 10);

```
It is possible to call custom C functions from within Beryl, as well as to call Beryl functions from C; Indeed the entire
standard library is implemented via this API.
