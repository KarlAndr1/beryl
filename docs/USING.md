# Interpreter and language manual

If the interpreter is started without any arguments, it instead starts up the read-eval-print loop (REPL)
and will then evaluate any expressions given to it via the command prompt.

## Basic syntax and semantics

Each script or function is made up of a series of expressions, evaluated in order.
An expression is made up of one or more subexpressions. If an expression consists of multiple subexpressions it is
evaluated as function call to the first subexpression, using the rest as arguments. If the expression only contains a single subexpression
it is instead just evaluated as that subexpression by itself.

A subexpression is made up of either a single term, or a series of terms separated by operators.

A term may be any of the following:
	-A full expression enclosed in brackets ( )
	-A do ... end block
	-An assignment or declaration; "a = b" or "let c = :expression:"
	-A literal of some kind; 1, 0.5, "a string"
	-A variable or constant
	-A function declaration; function x y do ... end

Expressions end when they encounter either a newline, or some other token that marks the end of an expression; ), end
For example, a valid expression would be
	print 1 2 3
This expression contains 4 subexpressions, each made up of only a single term. As this expression contains more than one
subexpression, it is evaluated as a function call to the first subexpression; in this case print, given the arguments 1, 2 and 3

A more complex expression might be
	print 1 2 + 3
This contains 3 subexpressions; print, 1 and (2 + 3)

The right hand side of an assignment or declaration is parsed as a complete expression, thus:
	let x = f x y
Is evaluated as assigning the result of the function f, given the arguments x and y, to the variable x.

A function may be declared with either the 'function' or 'with' keywords, followed by the name of the function arguments and then a 
'do' followed by a series of expressions ended with 'end'.
For example:
	function x do
		x * x
	end
creates a function that takes a single argument, x, and then inside the do block evaluates x * x. Do blocks return their last evaluated expression, in 
this case x * x, and thus this entire function would evaluate as x * x
It could be applied as such:
	function x do
		x * x
	end 3
Which would evaluate to 3 * 3, 9
One could also assign the function to a variable for more conventient usage
	let square = function x do
		x * x
	end
	
	square 3

If a function takes no arguments, the 'function' or 'with' keyword can be omitted
	do
		print "Hello world!"
	end
Declares a valid function that takes zero arguments.

Functions that take no arguments can be called using the 'invoke' function.
	invoke fn

Any variable that ends with a operator character (+, -, *, /, ? = etc) can be used as a binary operator, thus the function
	let pow^ = function x y do
		let result = 1
		for 0 y with i do
			result = result * x
		end
		result
	end
can be used as a binary operator
	10 pow^ 3
it may still be used as a regular function however
	pow^ 10 3

Note the 'for' in the declaration of the pow^ function. It is just a regular function that takes three arguments, in this case 0, y and a function. The for
function then iterates from 0 until y, calling the given function with the current index at every step.

'if' is another function that is included with the interpreter, it calls the given function if the given boolean value is true; multiple conditions and functions
may be given, in which case only the first function with a condition that is true is called, or the last one, the one without a condition, if
no conditions are true; provided one exists. Each additional condition should be prefixed with 'elseif', and the final optional block with 'else'
```
	if x == 2 do
		...
	end elseif x == 3 do
		...
	end else do
		...
	end
```
'elseif' and 'else' are simply constants defined by the same library that defines the 'if' function constant.

## Scope

When a variable is declared, the name and value is pushed onto the global variable stack. When a variable is retrieved, the first one (starting from the most
recently declared variables) is returned. The same logic applies to when a variable is assigned to. 
A single function may only have a single variable for a given name, duplicates are not allowed.
Thus
	let x = 10
	let y = function do
		print x
	end
	invoke y
will print "10", even if the variable x was declared outside of the function y. This is commonly referred to as **dynamic scoping**.
However, when searching through the stack, the interpreter will ignore any variables that were declared inside a function that is not either a parent
or child function of the current function. Therefore
	let caller = function f do
		let x = 10
		invoke f
	end
	invoke do
		let x = 20
		let callback = do
			print x
		end
		caller callback
	end
Will print 20, not 10. This is intended to reduce the number of issues that could occur due to unrelated functions choosing the same names for variables.

## Errors

Errors may either be returned by a function, or caused by syntax errors. If an expression evaluates as an error, or a syntax error is encountered, the
interpreter will immediately exit the current function, returning said error. As the function returns an error to the caller, the same will then happen
in that function and so on until the error reaches the top level scope, where it will then be printed out to the user. To handle errors, use the
try function; this function takes three arguments and immedietaly calls the first one, with no arguments. 
If said function returns an error, then that error gets caught and handled. How it is handled depends on what the second argument it.
If the second argument is the constant 'catch', then the third argument gets called with the error message (as a string) as an argument.
If the second argument is the constant 'else', then the third argument is returned by the entire try expression if an error occured; the third argument
acts as a default value that is returned in case of an error.
This may look like so:
	let res = try do
		let x = invoke some-function
		print "Ok"
		x
	end catch with err do
		print "Error occured:" err
		default-value
	end
If some-function were to return an error, that entire do-function/block would immediately exit and the error would then be caught by the try function.
The error message would then be passed on to the second function which would then log the error and return some default-value, which would then be the final
value of the entire expression, that gets assigned to the variable 'res'. If no error occurs then the first function would continue after the invocation of
some-function, printing out "Ok" and then returning x; the result of some-function would then be assigned to 'res'.



