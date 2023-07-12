#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../interpreter.h"
#include "lib_utils.h"

#include "../io.h"

void iolib_print_i_val(FILE *f, struct i_val val) {
	switch(val.type) {
		
		case TYPE_INT:
			fprintf(f, "%lli", (long long) val.val.int_v);
			break;
		case TYPE_FLOAT:
			fprintf(f, "%lf", (double) val.val.float_v);
			break;
		case TYPE_BOOL:
			if(val.val.int_v)
				fputs("True", f);
			else
				fputs("False", f);
			break;
		
		case TYPE_ERR:
			fputs("Error: ", f);
		case TYPE_STR: {
			const char *s = i_val_get_raw_str(&val);
			const char *s_end = s + i_val_get_len(val);	
			for(; s != s_end; s++) {
				putc(*s, f);
			}
		} break;
		
		case TYPE_NULL:
			fputs("Null", f);
			break;
		
		case TYPE_FN:
		case TYPE_EXT_FN:
			fputs("Function", f);
			break;
		
		case TYPE_ARRAY: {
			putc('(', f);
			i_size len = i_val_get_len(val);
			const i_val *array = i_val_get_raw_array(val);
			for(i_size i = 0; i < len; i++) {
				iolib_print_i_val(f, array[i]);
				if(i != len - 1)
					putc(' ', f);
			}
			putc(')', f);
		} break;
		
		case TYPE_TAG: {
			fputs("Tag", f);
		} break;
		
		default: {
			ti_interface_callback print_callback = beryl_interface_val(val, TI_PRINT);
			if(print_callback == NULL)
				fputs("Unkown value", f);
			else
				print_callback(val.val.ptr, f, iolib_print_i_val);
			break;
		}
	}
}

static struct i_val print_callback(const struct i_val *args, size_t n_args) {
	for(size_t n = n_args; n > 0; n--) {
		iolib_print_i_val(stdout, *args);
		args++;
		if(n != 1)
			putchar(' ');
	}
	putchar('\n');
	
	return (struct i_val) { .type = TYPE_NULL };
}

static struct i_val read_callback(const struct i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Expected file path");
	}
	
	const char *path_str = i_val_get_raw_str(&args[0]);
	i_size path_len = i_val_get_len(args[0]);
	
	void (*free)(void *) = beryl_get_free();	
	char *path = beryl_get_alloc()(path_len + 1);
	if(path == NULL)
		goto ERR;
	memcpy(path, path_str, path_len);
	path[path_len] = '\0';
	
	size_t len;
	char *contents = load_file(path, &len);
	free(path);
	if(contents == NULL)
		goto ERR;
	
	if(len > I_SIZE_MAX) {
		free(contents);
		beryl_blame_arg(args[0]);
		return STATIC_ERR("File too large");
	}
	
	i_val str_res = i_val_managed_str(contents, len);
	if(BERYL_TYPEOF(str_res) == TYPE_NULL) {
		free(contents);
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Out of memory when loading file");
	}
	
	free(contents);
	
	return str_res;
	
	ERR:
	beryl_blame_arg(args[0]);
	return STATIC_ERR("Unable to open file");
}

static char read_buffer[512];

static void clear_stdin() {
	int r;
	while( (r = getchar()) != EOF && r != '\n');
}

static struct i_val readline_callback(const struct i_val *args, size_t n_args) {
	char *buff_end = read_buffer + sizeof(read_buffer);
	char *at = read_buffer;
	
	if(n_args == 1)
		iolib_print_i_val(stdout, args[0]);
	else if(n_args != 0)
		return STATIC_ERR("Readline only accepts 0 or 1 argument");
	int r;
	while( (r = getchar()) != EOF ) {
		if(r == '\n')
			break;
		if(at == buff_end) {
			clear_stdin();
			break;
		}
		*(at++) = r;
	}
	
	size_t len = at - read_buffer;
	if(len > I_SIZE_MAX)
		return STATIC_ERR("Input line too large");
	
	i_val str_res = i_val_managed_str(read_buffer, len);
	if(BERYL_TYPEOF(str_res) == TYPE_NULL)
		return STATIC_ERR("Out of memory");
	
	return str_res;
}

static i_val write_callback(const struct i_val *args, size_t n_args) {
	i_val content = args[0];
	i_val path = args[1];
	if(BERYL_TYPEOF(content) != TYPE_STR) {
		beryl_blame_arg(content);
		return STATIC_ERR("First argument of write must be a string");
	}
	if(BERYL_TYPEOF(path) != TYPE_STR) {
		beryl_blame_arg(path);
		return STATIC_ERR("Expected string path as second argument");
	}
	
	i_size path_len = i_val_get_len(path);
	if(path_len + 1 > sizeof(read_buffer)) {
		beryl_blame_arg(path);
		return STATIC_ERR("Path too long");
	}
	memcpy(read_buffer, i_val_get_raw_str(&path), path_len);
	read_buffer[path_len] = '\0';
	
	FILE *f = fopen(read_buffer, "w");
	if(f == NULL) {
		beryl_blame_arg(path);
		return STATIC_ERR("Unable to open file");
	}
	
	i_size content_len = i_val_get_len(content);
	size_t n_written = fwrite(i_val_get_raw_str(&content), sizeof(char), content_len, f);
	
	fclose(f);
	
	if(n_written == 0) {
		beryl_blame_arg(path);
		return STATIC_ERR("Unable to write to file");
	} else if(n_written != content_len) {
		beryl_blame_arg(content);
		beryl_blame_arg(path);
		return STATIC_ERR("Unable to write full string to file");
	}
	
	return I_NULL;
}

LIB_FNS(fns) = {
	FN("print", print_callback, -1),
	FN("read", read_callback, 1),
	FN("readline", readline_callback, -1),
	FN("write", write_callback, 2)
};

#ifdef __windows__
	#define PATH_SEPARATOR "\\"
#else
	#define PATH_SEPARATOR "/"
#endif

void io_lib_load() {
	LOAD_FNS(fns);
	
	CONSTANT("path-separator", I_STATIC_STR(PATH_SEPARATOR));
	
	i_val lib_dir_v;
	const char *lib_dir = getenv("BERYL-PATH");
	if(lib_dir == NULL) {
		lib_dir_v = I_NULL;
	} else {
		lib_dir_v = i_val_managed_str(lib_dir, strlen(lib_dir));
	}
	
	CONSTANT("lib-dir", lib_dir_v);
	beryl_release(lib_dir_v);
}
