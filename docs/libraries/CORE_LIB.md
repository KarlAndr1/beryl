# Core library

The core library consists of the most of the functions and constants which are essential to using the language.

## Constants

# null
Null constant

# true
Boolean constant

# false
Boolean constant

## Functions

# if
Takes a minimum of two arguments.
The function steps through arguments two at a time, if the first is true then the second is called as a function without any arguments and
the return value of that function is returned as the result of the entire function. Otherwise the function continues to the new two arguments. The last
argument may be just a single function instead of a boolean-function pair, in which case that function is evaluated if no earlier conditions were true.
Returns an error if any condition is not a boolean, or if a function returns an error. Note that a non-boolean condition which is not reached will not
cause an error.
If an error is returned by any called function then the if-function will return that error without iterating through the rest of the conditions or functions.

Example:
	if x == 1 do
		...
	end, elseif x == 2 do
		...
	end, else do
		...
	end

# for
Takes either three or four arguments. 
The first two are the start and end integer values. The optional argument is the step size, if not provided then it
defaults to either 1, or -1 if the end value is less than the start value. The function then iterates from the start value until, but not including, the
end value, by the step size. For each iteration it calls the last argument as a function with the current integer index as an argument. The function returns
the result of calling the function at the final iteration.
Returns an error if the first and second or optional third arguments are not integers, or if the final argument is not callable.
If the given function returns an error then the for-function immediately exits, returning that error, without calling the function for the rest of the
indicies. 

Example:
	# Prints 1, 2, ..., 9, 10
	for 1 11 with i do 
		print i
	end

	# Prints 0, 2, 4
	for 0 6, by 2 i do 
		print i
	end

# loop
Takes a single argument.
Continually calls the given argument as a function with no arguments until it returns a non-null value, at which point the function terminates 
and returns the returned value.
Returns an error and stops looping if either the function returns an error, or is not callable.

# forevery
Takes at least two arguments.
Calls the final given argument as a function with a single argument, for every earlier argument.
Returns an error if the final argument is not callable. 
If the called function returns an error at any point, then the forevery-function immediately returns that error, without
iterating through the rest of the given arguments.

Example:
	# Prints 3, 7, 11, 13
	forevery 3 7 11 13 with x do
		print x
	end

# Mathematical operators (+, *, /)
Takes at least two arguments. Computes either the integer or floating-point result
of all the given arguments in order. x + y + z; x / y / z
Returns an error on non integer or floating-point values.
"/" returns an error if any of the arguments, except the first one, are either the integer or floating-point number zero.
Will also return an error on integer overflow or underflow.

# -
Takes at least a single argument.
Computes the result of x - y - z - ...
If only a single argument is provided the the negation of that integer or floating-point value is returned.
Returns an error on non integer or floating-point values.
Will also return an error on integer overflow or underflow.

# Comparisons (==, /=, <, >, <=, >=)
Takes two arguments.
Returns either true of false depending on if the arguments a and b are equal, not equal, a < b, a > b and so on.
<= and >= represent less than or equal and greater than or equal, respectively.
If the two arguments are not of the same type, or are not comparable the function returns an error.

# ~=
Lenient comparison.
Takes two arguments.
Returns true if the given arguments are equal, false if they are not equal or not of the same type or are not comparable.

# assert
Takes at least a single argument.
Returns an error if any of the given arguments are either false or not booleans.

# try
Takes either one or two arguments.
Calls the first argument as a function with zero arguments. If it returns an error, the second argument is called with the error message as an argument and
the result of that function is returned. If the first function does not return an error then the try-function returns that result without calling the second
argument. If an error is returned by the first argument and no second argument is provided then the try-function returns null.
Returns an error if either the first argument is not callable, or the first argument returns an error and the second argument is not callable, or if
the second argument returns an error.

# error
Takes a single argument.
Returns an error with the given error message.
Returns an error if the argument is not a string.

# sizeof
Takes a single argument.
Returns the size or length of the given value. Depends on the type.

# invoke
Takes a single argument.
Invokes the given argument as a function with zero arguments, and returns the result of that function.
Returns an error if the given function returns an error, or if it is not callable.

# return
Takes either one or two arguments.
If only a single argument is given, then this function causes the current function that return was called from to immediately exit with the 
given argument as the return value. If a second argument is given then the current function is only exited if the second argument is true, otherwise
if false the return function simply returns null. 
Returns an error if the second argument is not a boolean.

