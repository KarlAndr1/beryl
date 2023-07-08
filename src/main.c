#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "interpreter.h"

#include "libs/io_lib.h"

#include "io.h"

#define ERR_COL "\x1B[31m"
#define CLEAR_COL "\x1B[0m"
#define THEME_COL "\x1B[35m"

struct line_entry {
	struct line_entry *prev;
	char *line;
};

struct line_entry *history = NULL;

static void str_replace(char *str, char r, char with) {
	for(char *c = str; *c != '\0'; c++) {
		if(*c == r)
			*c = with;
	}
}

static struct i_val run_script(const char *path, bool keep_scope) {
	size_t len;
	char *src = load_file(path, &len);
	if(src == NULL)
		goto ERR_LOAD;
	
	struct line_entry *new_entry = malloc(sizeof(struct line_entry));
	if(new_entry == NULL)
		goto ERR_HISTORY;
	
	new_entry->line = src;
	new_entry->prev = history;
	history = new_entry;
	
	beryl_set_var("script-path", sizeof("script-path") - 1, i_val_managed_str(path, strlen(path)), true);
	return beryl_eval(src, len, !keep_scope, BERYL_ERR_PRINT);
	
	ERR_HISTORY:
	free(src);
	ERR_LOAD:
	fprintf(stderr, "Unable to load script %s\n", path);
	return I_NULL;
}

static void clear_history() {
	struct line_entry *next;
	for(; history != NULL; history = next) {
		next = history->prev;
		free(history->line);
		free(history);
	}
}

static void prompt() {
	const char *prompt_msg = THEME_COL "Beryl Interpreter : 0.1"
	#ifdef DEBUG
		" (debug, " __DATE__ ":" __TIME__ ")"
	#endif
	CLEAR_COL;
	
	puts(prompt_msg);
	puts("Copyright Karl A Larsén 2023");
	char line[512];
	
	while(true) {
		putchar('>');
		char *read_res = fgets(line, sizeof(line), stdin);
		if(read_res == NULL) {
			puts("Unable to read from stdin");
			break;
		}
		if(strcmp(line, "quit\n") == 0)
			break;
		size_t len = strlen(line);
		
		struct i_val res;

		if(line[0] == 'l' && line[1] == 'o' && line[2] == 'a' && line[3] == 'd' && line[4] == ' ') {
			str_replace(line, '\n', '\0');
			const char *path = line + 5;
			while(*path == ' ')
				path++;
			res = run_script(path, true);
		} else {
			struct line_entry *new_entry = malloc(sizeof(struct line_entry));
			if(new_entry == NULL)
				break;
	
			new_entry->prev = history;
			char *src = malloc(len + 1);
			if(src == NULL) {
				free(new_entry);
				break;
			}

			memcpy(src, line, len + 1);
			res = beryl_eval(src, len, false, BERYL_ERR_PRINT);
			
			new_entry->line = src;
			history = new_entry;
		}
		
		if(res.type != TYPE_NULL) {
			if(res.type == TYPE_ERR) {
				fputs(ERR_COL, stderr);
				iolib_print_i_val(stderr, res);
				fputs(CLEAR_COL "\n", stderr);
			} else {
				iolib_print_i_val(stdout, res);
				putchar('\n');
			}
			beryl_release(res);
		}
	}
}

static void print_err(struct i_val err) {
	if(err.type == TYPE_ERR) {
		fputs(ERR_COL, stderr);
		iolib_print_i_val(stderr, err);
		fputs(CLEAR_COL "\n", stderr);
	}
}

static void run_commands(int argc, const char **argv) {
	bool keep_scope = true;
	for(int i = 1; i < argc; i++) {
		if(strcmp(argv[i], "-r") == 0)
			prompt();
		if(strcmp(argv[i], "-s") == 0)
			keep_scope = false;
		else {
			struct i_val err = run_script(argv[i], keep_scope);
			print_err(err);
			if(err.type == TYPE_ERR)
				return;
		}
	}
}

#ifdef MINIMAL_BUILD
#define N_FIXED_BLOCKS 16
static unsigned char fixed_heap[1024 * 8];
static bool free_blocks[N_FIXED_BLOCKS];
#define BLOCK_SIZE (sizeof(fixed_heap)/N_FIXED_BLOCKS)

static void *fixed_alloc(size_t n) {
	if(n > BLOCK_SIZE)
		return NULL;
	for(size_t i = 0; i < N_FIXED_BLOCKS; i++) {
		if(free_blocks[i]) {
			free_blocks[i] = false;
			return fixed_heap + (i * BLOCK_SIZE);
		}
	}
	return NULL;
}

static void fixed_free(void *ptr) {
	size_t diff = (unsigned char *) ptr - fixed_heap;
	size_t i = diff / BLOCK_SIZE;
	assert(i < N_FIXED_BLOCKS);
	assert(!free_blocks[i]);
	free_blocks[i] = true;
}

static void *fixed_realloc(void *ptr, size_t new_size) {
	void *new_ptr = fixed_alloc(new_size);
	if(new_ptr == NULL)
		return NULL;
	memcpy(new_ptr, ptr, BLOCK_SIZE);
	fixed_free(ptr);
	return new_ptr;
}

static void init_fixed_alloc() {
	for(size_t i = 0; i < N_FIXED_BLOCKS; i++)
		free_blocks[i] = true;
}

#endif

int main(int argc, const char **argv) {
	
	#ifndef MINIMAL_BUILD
	beryl_set_mem(free, malloc, realloc); //Provies the interpreter with memory related functions to that it can automatically free() garbage via reference counting
	#else
	init_fixed_alloc();
	beryl_set_mem(fixed_free, fixed_alloc, fixed_realloc);
	#endif
	beryl_load_standard_libs();
	
	if(argc == 1)
		prompt();
	else if(argc == 2) {
		struct i_val res = run_script(argv[1], true);
		print_err(res);
	} else {
		run_commands(argc, argv);
	}
	
	clear_history();
	beryl_clear();
	
	return 0;
}
