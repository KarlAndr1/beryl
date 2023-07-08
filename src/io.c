#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "libs/io_lib.h"

char *load_file(const char *path, size_t *len) {
	FILE *f = fopen(path, "r");
	if(f == NULL)
		return NULL;
	
	size_t buff_cap = 128, buff_size = 0;
	char *buff = malloc(buff_cap);
	if(buff == NULL)
		goto ERR;

	size_t n_read;
	while( (n_read = fread(buff + buff_size, sizeof(char), buff_cap - buff_size, f)) != 0 ) {
		buff_size += n_read;
		if(buff_size == buff_cap) {
			buff_cap *= 2;
			void *tmp = realloc(buff, buff_cap);
			if(tmp == NULL)
				goto ERR;
			buff = tmp;
		}
	}
	
	*len = buff_size;
	return buff;
	
	ERR:
	free(buff);
	fclose(f);
	return NULL;
}

#include "interpreter.h"

#define ERR_COL "\x1B[31m"
#define CLEAR_COL "\x1B[0m"

static void print_ref(FILE *f, const char *src, size_t len, const char *err_at, size_t err_len) {
	assert(len != 0);
	const char *src_end = src + len;
	const char *line_start = src;
	unsigned line_n = 1;
	
	bool found = false;
	for(const char *c = src; c < src_end; c++) {
		if(c == err_at) {
			found = true;
			break;
		}
		if(*c == '\n') {
			line_start = c + 1;
			line_n++;
		}
	}
	
	if(err_at == src_end)
		found = true;
	
	if(!found) {
		fputs("Unable to find in source\n", f);
		return;
	}
	
	fprintf(f, "On line %u\n", line_n);
	for(const char *c = line_start; c < src_end; c++) {
		if(*c == '\n')
			break;
		putc(*c, f);
	}
	putc('\n', f);
	
	if(err_len == 0)
		err_len = 1;
	
	assert(err_at <= src_end);
	assert(line_start <= src_end);
	assert(line_start <= err_at);
	
	for(const char *c = line_start; c != err_at; c++) {
		if(*c == '\t')
			putc('\t', f);
		else
			putc(' ', f);
	}
	
	for(size_t i = err_len; i > 0; i--) {
		putc('^', f);
	}
	putc('\n', f);
	//fputs(CLEAR_COL "\n", f);
}

static void print_len_str(FILE *f, const char *str, size_t len) {
	fwrite(str, sizeof(char), len, f);
}

void io_print_stacktrace(struct beryl_stack_trace *trace, size_t n, const struct i_val *args, size_t n_args) {
	fputs(ERR_COL "Stack trace:\n", stderr);
	fputs("----------------\n", stderr);
	for(size_t i = 1; i <= n; i++) {
		struct beryl_stack_trace entry = trace[n-i];
		if(entry.name != NULL) {
			fputs("In: ", stderr);
			print_len_str(stderr, entry.name, entry.name_len);
			putc('\n', stderr);
		}
		else if(entry.src != NULL) {
			//fputs("At: ", stderr);
			print_ref(stderr, entry.src, entry.src_len, entry.at, entry.err_len);
		}
		fputs("----------------\n", stderr);
	}
	
	if(n_args != 0) {
		fputs("Value(s): ", stderr);
		for(size_t i = 0; i < n_args; i++) {
			if(args[i].type == TYPE_STR) {
				putc('"', stderr);
				iolib_print_i_val(stderr, args[i]);
				putc('"', stderr);
			}
			else
				iolib_print_i_val(stderr, args[i]);
			if(i != n_args - 1)
				fputs(", ", stderr);
		}
		putc('\n', stderr);
	}
	fputs(CLEAR_COL, stderr);
}
