#include "beryl.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "io.h"

static void generic_print_callback(void *f, const char *str, size_t len) {
	fwrite(str, sizeof(char), len, f); 
}

static void print_i_val_io_callback(void *f, struct i_val val) {
	print_i_val(f, val);
}

static bool load_beryl_argv(const char **args, int argc) {
	assert(argc >= 0);
	if((size_t) argc > I_SIZE_MAX)
		return false;
	struct i_val array = beryl_new_array(argc, NULL, argc, false);
	if(BERYL_TYPEOF(array) == TYPE_NULL)
		return false;
	
	struct i_val *items = (struct i_val *) beryl_get_raw_array(array);
	for(int i = 0; i < argc; i++) {
		struct i_val str = beryl_new_string(strlen(args[i]), args[i]);
		if(BERYL_TYPEOF(str) == TYPE_NULL) {
			for(int j = i - 1; j >= 0; j--)
				beryl_release(items[j]);
			return false;
		}
		items[i] = str;
	}
	
	bool ok = beryl_set_var("argv", 4, array, true);
	beryl_release(array);
	
	return ok;
}

#include "libs/libs.h"

int main(int argc, const char **argv) {
	
	beryl_set_mem(malloc, free, realloc);
	beryl_set_io(generic_print_callback, print_i_val_io_callback, stderr);
	
	
	bool ok = beryl_load_included_libs();
	if(!ok) {
		fputs("Could not load libraries!\n", stderr);
		exit(-1);
	}
	
	assert(argc >= 1);
	
	int ret_code = 0;
	
	const char *init_script_path = getenv("BERYL_SCRIPT_INIT");
	
	if(init_script_path != NULL) {
		if(!load_beryl_argv(argv + 1, argc - 1)) {
			fputs("Could not load argv!\n", stderr);
			return -1;
		}
		size_t len;
		char *init_script = load_file(init_script_path, &len);
		if(init_script == NULL) {
			fprintf(stderr, "Could not read init script at '%s'\n", init_script_path);
			return -1;
		}
		
		struct i_val res = beryl_eval(init_script, len, BERYL_PRINT_ERR);
		
		if(beryl_is_integer(res))
			ret_code = beryl_as_num(res);
		else if(BERYL_TYPEOF(res) == TYPE_ERR)
			ret_code = -2;
			
		beryl_release(res);
		free(init_script);
	} else if (argc >= 2) { //Fallback in case the init script doesn't exist: Just run the first argument as a script
		fputs("Warning, no init.beryl file was found; running in fallback mode", stderr);
		if(!load_beryl_argv(argv + 2, argc - 2)) {
			fputs("Could not load argv!\n", stderr);
			return -1;
		}
		size_t len;
		char *run_script = load_file(argv[1], &len);
		if(run_script == NULL) {
			fprintf(stderr, "Unable to read script at '%s'\n", argv[1]);
		}
		
		struct i_val res = beryl_eval(run_script, len, BERYL_PRINT_ERR);
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			ret_code = 1;
			
		beryl_release(res);
		free(run_script);
	}
	
	beryl_clear();
	beryl_core_lib_clear_evals();
	
	return ret_code;
}
