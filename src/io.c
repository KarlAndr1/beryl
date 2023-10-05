#include "io.h"

#include "berylscript.h"

#include <string.h>

char *load_file(const char *path, size_t *len) {
	FILE *f = fopen(path, "r");
	if(f == NULL)
		return NULL;
	
	size_t buff_cap = 128, buff_size = 0;
	char *buff = beryl_alloc(buff_cap);
	if(buff == NULL)
		goto ERR;

	size_t n_read;
	while( (n_read = fread(buff + buff_size, sizeof(char), buff_cap - buff_size, f)) != 0 ) {
		buff_size += n_read;
		if(buff_size == buff_cap) {
			buff_cap *= 2;
			void *tmp = beryl_realloc(buff, buff_cap);
			if(tmp == NULL)
				goto ERR;
			buff = tmp;
		}
	}
	fclose(f);
	
	*len = buff_size;
	return buff;
	
	ERR:
	beryl_free(buff);
	fclose(f);
	return NULL;
}


void print_i_val(FILE *f, struct i_val val) {
	switch(val.type) {
	
		case TYPE_NULL:
			fputs("Null", f);
			break;
			
		case TYPE_NUMBER: {
			i_float num = beryl_as_num(val);
			if(beryl_is_integer(val) && num <= LLONG_MAX && num >= LLONG_MIN)
				fprintf(f, "%lli", (long long int) num);
			else
				fprintf(f, "%f", num);
		} break;
		
		case TYPE_BOOL:
			fputs(beryl_as_bool(val) ? "True" : "False", f);
			break;

		case TYPE_ERR:
			fputs("Error: ", f);
			/* FALLTHRU */
		case TYPE_STR: {
			const char *str = beryl_get_raw_str(&val);
			i_size len = BERYL_LENOF(val);
			fwrite(str, sizeof(char), len, f);
		} break;
		
		case TYPE_TABLE: {
			putc('{', f);
			
			bool first = true;
			for(struct i_val_pair *i = beryl_iter_table(val, NULL); i != NULL; i = beryl_iter_table(val, i)) {
				if(!first)
					putc(' ', f);
				putc('(', f);
				print_i_val(f, i->key);
				putc(' ', f);
				print_i_val(f, i->val);
				putc(')', f);
				first = false;
			}
			
			putc('}', f);
		} break;
		
		case TYPE_ARRAY: {
			putc('(', f);
			const struct i_val *items = beryl_get_raw_array(val);
			for(i_size i = 0; i < BERYL_LENOF(val); i++) {
				print_i_val(f, items[i]);
				if(i != BERYL_LENOF(val) - 1)
					putc(' ', f);
			}
			putc(')', f);
		} break;
		
		case TYPE_FN:
		case TYPE_EXT_FN:
			fputs("Function", f);
			break;
		
		case TYPE_TAG:
			fprintf(f, "Tag (%llu)", (unsigned long long) beryl_as_tag(val));
			break;
		
		case TYPE_OBJECT: {
			struct beryl_object_class *c = beryl_object_class_type(val);
			fputs("Object (", f);
			fwrite(c->name, sizeof(char), c->name_len, f);
			putc(')', f);
		} break;
		
		default:
			fputs("Unkown", f);
	}
}
