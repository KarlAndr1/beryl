#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include <stddef.h>
#include "interpreter.h"

char *load_file(const char *path, size_t *len);

void io_print_stacktrace(struct beryl_stack_trace *trace, size_t n, const struct i_val *args, size_t n_args);

#endif
