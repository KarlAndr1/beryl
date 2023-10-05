#include "../beryl.h"

#include "../utils.h"

#include <stdio.h>
#include <string.h>

#include "../io.h"

typedef struct i_val i_val;

#define FN(arity, name, fn) { arity, true, name, sizeof(name) - 1, fn }

#ifdef __windows__
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif

static char *as_path(const char *str, size_t len) {
	char *res = beryl_talloc(len + 1);
	if(res == NULL)
		return NULL;
	
	memcpy(res, str, len);
	res[len] = '\0';
	return res;
}

static void free_path(char *path) {
	beryl_tfree(path);
}

/*@$
	libname iolib
$@*/

/*@@
	read
	path

	Unary function.
	Returns the contents of the file found at *path* as a string of text.
	Returns an error if out of memory or if the file cannot be read or does not exist.
@@*/
static i_val read_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected string path as argument for 'read'");
	}
	
	i_size path_len = BERYL_LENOF(args[0]);
	char *c_path = as_path(beryl_get_raw_str(&args[0]), path_len);
	if(c_path == NULL)
		return BERYL_ERR("Out of memory");
	
	size_t file_len;
	char *file_contents = load_file(c_path, &file_len);
	free_path(c_path);
	if(file_contents == NULL) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Unable to read from file");
	}
	
	if(file_len > I_SIZE_MAX) {
		beryl_free(file_contents);
		return BERYL_ERR("File too large");
	}
	
	i_val str_res = beryl_new_string(file_len, file_contents);
	beryl_free(file_contents);
	
	if(BERYL_TYPEOF(str_res) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	return str_res;
}

/*@@
	print
	first ... rest

	Variadic function taking at least one argument.
	Prints all the given arguments to stdout, separated by spaces.
	A newline is printed after all values have been printed.
@@*/
static struct i_val print_vals_callback(const struct i_val *args, i_size n_args) {
	for(i_size i = 0; i < n_args; i++) {
		beryl_print_i_val(stdout, args[i]);
		if(i != n_args - 1)
			putchar(' ');
	}
	putchar('\n');
	
	return BERYL_NULL;
}

static i_val write_i_val_to_file(i_val path, i_val content, const char *mode) {
	assert(BERYL_TYPEOF(path) == TYPE_STR);
	assert(BERYL_TYPEOF(content) == TYPE_STR);
	
	char *c_path = as_path(beryl_get_raw_str(&path), BERYL_LENOF(path));
	if(c_path == NULL)
		return BERYL_ERR("Out of memory");
	
	FILE *f = fopen(c_path, mode);
	free_path(c_path);
	if(f == NULL) {
		beryl_blame_arg(path);
		return BERYL_ERR("Unable to open file");
	}
	
	size_t res = fwrite(beryl_get_raw_str(&content), sizeof(char), BERYL_LENOF(content), f);
	fclose(f);
	
	if(res != BERYL_LENOF(content)) {
		beryl_blame_arg(path);
		return BERYL_ERR("Error when writing to file");
	}
	
	return BERYL_NULL;
}

/*@@
	write
	path content

	Binary function.
	Write the string *content* to the file found at *path*. If the file does not
	exist, it is created. If it does exist, it is overwritten.
	Returns an error if out of memory or if the file at *path* cannot be written to.
@@*/
static i_val write_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected file path as first argument for 'write'");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_STR) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected string as second argument for 'write'");
	}
	
	return write_i_val_to_file(args[0], args[1], "w");
}

/*@@
	print-exactly
	first ... rest

	Varadic function taking at least one argument.
	Prints the given arguments to stdout, without any
	added spaces or newlines.
@@*/
static i_val print_exactly_callback(const i_val *args, i_size n_args) {
	for(i_size i = 0; i < n_args; i++) {
		beryl_print_i_val(stdout, args[i]);
	}
	return BERYL_NULL;
}

static i_val read_all_tag = BERYL_NULL;

/*@@
	input
	... opt-msg

	Varadic function.
	Reads a line of text from stdin.
	*opt-msg* are printed (with spaces between them) before reading, if any are given.
@@*/
static i_val input_callback(const i_val *args, i_size n_args) {
	bool stop_at_newline = true;
	if(n_args >= 1 && beryl_val_cmp(args[0], read_all_tag) == 0) {
		args++;
		n_args--;
		stop_at_newline = false;
	}
	
	//print_vals_callback(args, n_args);
	for(i_size i = 0; i < n_args; i++) {
		print_i_val(stdout, args[i]);
		if(i != n_args - 1)
			putchar(' ');
	}
	
	i_size line_buff_size = 256;
	char *line_buff = beryl_talloc(line_buff_size);
	i_size n_read = 0;
	
	int i;
	while( (i = getchar()) != EOF ) {
		char c = i;
		if(c == '\n' && stop_at_newline)
			break;
		if(n_read == line_buff_size) {
			line_buff_size = line_buff_size * 3 / 2 + 1;
			if(line_buff_size <= n_read) {
				beryl_tfree(line_buff);
				return BERYL_ERR("Out of memory");
			}
			void *newp = beryl_realloc(line_buff, line_buff_size);
			if(newp == NULL) {
				beryl_tfree(line_buff);
				return BERYL_ERR("Out of memory");
			}
			line_buff = newp;
		}
		line_buff[n_read++] = c;
	}
	
	i_val res_str = beryl_new_string(n_read, line_buff);
	beryl_tfree(line_buff);
	
	if(BERYL_TYPEOF(res_str) == TYPE_NULL)
		return BERYL_ERR("Out of memory");
	
	return res_str;
}

/*@@
	append
	path content

	Binary function.
	Appends the given string *content* to the end of the file found at *path*.
	If the file does not exist, it is created first.
	Returns an error if out of memory or if the file cannot be written to.
@@*/
static i_val append_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected file path as first argument for 'append'");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_STR) {
		beryl_blame_arg(args[1]);
		return BERYL_ERR("Expected string as second argument for 'append'");
	}
	
	return write_i_val_to_file(args[0], args[1], "a");	
}

/*@@
	file-exists
	path

	Unary function.
	Returns true if there exists a file at the given *path*, false otherwise.
	May return an out of memory error.
@@*/
static i_val file_exists_callback(const i_val *args, i_size n_args) {
	(void) n_args;
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected file path (string) as argument for 'file-exists?'");
	}
	
	char *path = as_path(beryl_get_raw_str(&args[0]), BERYL_LENOF(args[0]));
	if(path == NULL)
		return BERYL_ERR("Out of memory");
	
	FILE *f = fopen(path, "r"); //Kind of hacky, but it works
	free_path(path);
	
	if(f == NULL)
		return BERYL_BOOL(0);
	fclose(f);
	return BERYL_BOOL(1);
}

/*@@
	printf
	fstr ... args

	Prints the given string *fstr* with the arguments *args* replacing any %N with argument N.
	Example:
		printf "Hello %0, my name is %1" other-name my-name
	Note that the function accepts at most 10 arguments after fstr, as each argument has to be addressed using a single digit, 0-9.
@@*/
static i_val printf_callback(const i_val *args, i_size n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return BERYL_ERR("Expected string as first argument for 'printf', got '%0'");
	}
	
	i_size n_format_vals = n_args - 1;
	if(n_format_vals > 10) {
		beryl_blame_arg(BERYL_NUMBER(n_format_vals));
		return BERYL_ERR("printf accepts at most 10 values to interpolate (Got %0)");
	}
	
	const char *str = beryl_get_raw_str(&args[0]);
	i_size strlen = BERYL_LENOF(args[0]);
	
	beryl_i_vals_printf(stdout, str, strlen, args + 1, n_args - 1);
	
	return BERYL_NULL;
}

/*
static i_val hold_callback(const i_val *args, i_size n_args) {
	if(n_args == 1 || n_args == 2) {
		if(BERYL_TYPEOF(args[0]) != TYPE_BOOL) {
			beryl_blame_arg(args[0]);
			return BERYL_ERR("First argument for 'hold' must be boolean, got %0");
		}
		if(!beryl_as_bool(args[0]))
			return BERYL_NULL;
		
		if(n_args == 2)
			beryl_i_vals_printf(stdout, "Hold: %0", 8, &args[1], 1);
	} else if(n_args != 0) {
		return BERYL_ERR("'hold' takes either 0, 1 or 2 arguments");
	}	
} */

bool load_io_lib() {
	static struct beryl_external_fn fns[] = {
		FN(1, "read", read_callback),
		FN(-2, "print", print_vals_callback),
		FN(2, "write", write_callback),
		FN(-2, "print-exactly", print_exactly_callback),
		FN(-1, "input", input_callback),
		FN(2, "append", append_callback),
		FN(1, "file-exists?", file_exists_callback),
		FN(-2, "printf", printf_callback),
		//FN(-1, "hold", hold_callback)
	};

	for(size_t i = 0; i < LENOF(fns); i++) {
		bool ok = beryl_set_var(fns[i].name, fns[i].name_len, BERYL_EXT_FN(&fns[i]), true);
		if(!ok)
			return false;
	}
	
	beryl_set_var("path-separator", sizeof("path-separator") - 1, BERYL_CONST_STR(PATH_SEPARATOR), true);
	
	read_all_tag = beryl_new_tag();
	beryl_set_var("read-all", sizeof("read-all") - 1, read_all_tag, true);
	
	return true;
}
