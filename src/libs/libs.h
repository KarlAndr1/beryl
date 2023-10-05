#ifndef LIBS_H_INCLUDED
#define LIBS_H_INCLUDED

#include "../beryl.h"

bool load_core_lib();
struct i_val i_val_as_string(struct i_val val);
void beryl_core_lib_clear_evals();

bool load_debug_lib();

bool load_io_lib();
 
bool load_unix_lib();

#endif
