#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include "berylscript.h"
#include <stdio.h>

char *load_file(const char *path, size_t *len);

void print_i_val(FILE *f, struct i_val val);

#endif
