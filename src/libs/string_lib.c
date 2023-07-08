#include "../interpreter.h"

#include "lib_utils.h"
#include "../utils.h"

#include <stdlib.h>
#include <string.h>

/*
typedef struct match_result {
	const char *from, *to;
} match_result;

static bool is_alpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

static const char* strmatch(const char *str, const char *str_end, const char *pattern, const char *pattern_end) {
	while(pattern != pattern_end) {
		switch(*pattern) {
			
			case '*':
				if(str == str_end)
					return NULL;
				str++;
				break;
			
			default:
				if(str == str_end || *str != *pattern)
					return NULL;
				str++;
				break;
		}
		pattern++;
	}
	return str;
}

static struct match_result strmatchf(const char *str, size_t str_len, const char *pattern, size_t pattern_len) {
	for(size_t i = 0; i < str_len; i++) {
		const char *res = strmatch(str + i, str + str_len, pattern, pattern + pattern_len);
		if(res != NULL)
			(match_result) { str + i, res };
	}
	return (match_result) { NULL, NULL };
}

static i_val match_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Can only match strings");
	}
	if(BERYL_TYPEOF(args[¡]) != TYPE_STR) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Expected string as second argument");
	}
	
	i_size strlen = i_val_get_len(args[0]);
	const char *str = i_val_get_raw_str(&args[0]);
	
	i_size plen = i_val_get_len(args[1]);
	const char *p = i_val_get_raw_str(&args[1]);
	
	i_size i = 0;
	i_val res = I_NULL;
	for(i = 0; i < strlen; i++) {
		if(plen > strlen - i)
			return res;
		for(i_size j = 0; j < plen; j++) {
			
		}
	}
} */

#include "../lexer.h"

/*
static i_val tokenize_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Can only tokenize strings");
	}
	
	const char *str = i_val_get_raw_str(args[0]);
	i_size len = i_val_get_len(args[0]);
	
	struct lex_state lex;
	lex_state_init(&lex, str, len);
	
	i_size len = 0, cap = 8;
	i_val *buff = calloc(sizeof(i_val) * cap);
	if(buff == 0)
		return STATIC_ERR("Out of memory");
	
	while(!lex_accept(&lex, TOK_EOF)) {
		struct lex_token tok = lex_pop(&lex);
		if(
	}
} */

static i_val tok_to_i_val(struct lex_token tok) {
	switch(tok.type) {
		
		case TOK_ENDLINE:
			return I_STATIC_STR("\n");
		
		case TOK_INT:
			return i_val_int(tok.content.int_literal);
		case TOK_FLOAT:
			return i_val_float(tok.content.float_literal);
		
		case TOK_STRING:
			return i_val_managed_str(tok.content.sym.str, tok.content.sym.len);
		
		default:
			return i_val_managed_str(tok.src, tok.len);
	}
}

static i_val tokenize_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("Can only tokenize strings");
	}
	
	const char *str = i_val_get_raw_str(&args[0]);
	i_size len = i_val_get_len(args[0]);
	
	struct lex_state lex;
	lex_state_init(&lex, str, len);
	
	while(!lex_accept(&lex, TOK_EOF, NULL)) {
		struct lex_token tok = lex_pop(&lex);
		if(tok.type == TOK_ERR)
			return STATIC_ERR("Lexing error");
		i_val tok_val = tok_to_i_val(tok);
		if(BERYL_TYPEOF(tok_val) == TYPE_NULL)
			return STATIC_ERR("Out of memory");
		
		i_val res = beryl_call_fn(args[1], &tok_val, 1, BERYL_ERR_PROP);
		beryl_release(tok_val);
		if(BERYL_TYPEOF(res) == TYPE_ERR)
			return res;
		beryl_release(res);
	}
	
	return I_NULL;
}

static i_val endswith_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument of endswith? must be string");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_STR) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Second argument of endswith? must be string");
	}
	
	i_size al = i_val_get_len(args[0]);
	const char *as = i_val_get_raw_str(&args[0]);
	
	i_size bl = i_val_get_len(args[1]);
	const char *bs = i_val_get_raw_str(&args[1]);
	
	if(bl > al)
		return i_val_bool(0);
	
	const char *a_end = as + al;
	for(const char *c = a_end - bl; c != a_end; c++) {
		if(*c != *bs)
			return i_val_bool(0);
		bs++;
	}
	return i_val_bool(1);
}

static bool str_match(const char *str, const char *str_end, const char *substr, size_t substr_len) {
	if(str_end - str < substr_len)
		return false;
	
	for(; substr_len > 0; substr_len--) {
		assert(str < str_end);
		if(*str != *substr)
			return false;
		str++, substr++;
	}
	return true;
}

static i_val find_callback(const i_val *args, size_t n_args) {
	if(BERYL_TYPEOF(args[0]) != TYPE_STR) {
		beryl_blame_arg(args[0]);
		return STATIC_ERR("First argument of find must be string");
	}
	if(BERYL_TYPEOF(args[1]) != TYPE_STR) {
		beryl_blame_arg(args[1]);
		return STATIC_ERR("Second argument of find must be string");
	}
	
	const char *str = i_val_get_raw_str(&args[0]);
	i_size str_len = i_val_get_len(args[0]);
	
	const char *substr = i_val_get_raw_str(&args[1]);
	i_size substr_len = i_val_get_len(args[1]);
	
	const char *str_end = str + str_len;
	for(const char *s = str; s < str_end + 1 - substr_len; s++) {
		if(str_match(s, str_end, substr, substr_len)) {
			i_size index = s - str;
			if(index > I_INT_MAX)
				return STATIC_ERR("Index outside integer range");
			return i_val_int(index);
		}
	}
	
	return I_NULL;
}

static i_val substring_callback(const i_val *args, size_t n_args) {
	EXPECT_ARG_T3(
		args, n_args,
		TYPE_STR, "string",
		TYPE_INT, "int",
		TYPE_INT, "int"
	);
	
	i_int i1 = i_val_get_int(args[1]);
	i_int i2 = i_val_get_int(args[2]);
	i_size len = i_val_get_len(args[0]);
	
	if(i2 < i1) {
		beryl_blame_arg(args[1]);
		beryl_blame_arg(args[2]);
		return STATIC_ERR("Second index is before first");
	}
	
	if(i1 < 0 || i1 > len) {
		beryl_blame_arg(args[0]);
		beryl_blame_arg(args[1]);
		return STATIC_ERR("First index is out of range");
	}
	if(i2 > len) {
		beryl_blame_arg(args[0]);
		beryl_blame_arg(args[2]);
		return STATIC_ERR("Second index is out of range");
	}
	
	i_int newlen = i2 - i1;
	if(newlen > I_SIZE_MAX)
		return STATIC_ERR("Substring size too large");
	
	const char *src_str = i_val_get_raw_str(&args[0]);
	i_val substr = i_val_managed_str(src_str + i1, newlen);
	if(BERYL_TYPEOF(substr) == TYPE_NULL)
		return STATIC_ERR("Out of memory");
	return substr;
}

struct str_buff {
	char *str, *end, *top;
};

static bool str_buff_grow(struct str_buff *buff, size_t min) {
	size_t len = (buff->top - buff->str);
	size_t cap = (buff->end - buff->str);
	size_t new_cap = ((cap * 3)/2) + min;
	char *new_buff = beryl_get_realloc()(buff->str, new_cap);
	if(new_buff == NULL)
		return false;
	buff->str = new_buff;
	buff->top = buff->str + len;
	buff->end = buff->str + new_cap;
	
	return true;
}

static bool str_buff_init(struct str_buff *buff, size_t n) {
	*buff = (struct str_buff) { NULL, NULL, NULL };
	return str_buff_grow(buff, n);
}

static bool str_buff_pushc(struct str_buff *buff, char c) {
	if(buff->top == buff->end) {
		if(!str_buff_grow(buff, 1))
			return false;
	}
	
	*(buff->top++) = c;
	
	return true;
}

static bool str_buff_pushs(struct str_buff *buff, const char *str, size_t str_len) {
	if(buff->end - buff->top < str_len) {
		if(!str_buff_grow(buff, str_len))
			return false;
	}
	
	memcpy(buff->top, str, str_len);
	buff->top += str_len;
	
	return true;
}

static i_val replace_callback(const i_val *args, size_t n_args) {
	EXPECT_ARG_T3(
		args, n_args,
		TYPE_STR, "string",
		TYPE_STR, "string",
		TYPE_STR, "string"
	);
	
	const char *src = i_val_get_raw_str(&args[0]);
	i_size src_len = i_val_get_len(args[0]);
	
	const char *replace = i_val_get_raw_str(&args[1]);
	i_size replace_len = i_val_get_len(args[1]);
	
	if(replace_len == 0)
		return beryl_retain(args[0]);
	
	const char *replace_with = i_val_get_raw_str(&args[2]);
	i_size replace_with_len = i_val_get_len(args[2]);
	
	if(replace_len == replace_with_len && beryl_get_references(args[0]) == 1) {
		const char *src_end = src + src_len;
		for(char *s = (char *) src; s <= src_end - replace_len; s++) {
			if(str_match(s, src_end, replace, replace_len)) {
				memcpy(s, replace_with, replace_len);
				s += replace_len - 1;
			}
		}
		return beryl_retain(args[0]);
	} else {
		struct str_buff buff;
		if(!str_buff_init(&buff, src_len))
			return STATIC_ERR("Out of memory");
		
		const char *src_end = src + src_len;
		for(const char *s = src; s < src_end; s++) {
			if(str_match(s, src_end, replace, replace_len)) {
				if(!str_buff_pushs(&buff, replace_with, replace_with_len))
					goto MEM_ERR;
				s += replace_len - 1;
			} else {
				if(!str_buff_pushc(&buff, *s))
					goto MEM_ERR;
			}
		}
		void (*free)(void *) = beryl_get_free();
		
		i_val res_str = i_val_managed_str(buff.str, buff.top - buff.str);
		if(BERYL_TYPEOF(res_str) == TYPE_NULL)
			goto MEM_ERR;
		
		free(buff.str);
		return res_str;
		
		MEM_ERR:
		free(buff.str);
		return STATIC_ERR("Out of memory");
	}
}

LIB_FNS(fns) = {
	//FN("match", match_callback, 3)
	FN("lex", tokenize_callback, 2),
	FN("endswith?", endswith_callback, 2),
	FN("find", find_callback, 2),
	FN("substring", substring_callback, 3),
	FN("replace", replace_callback, 3)
};

void string_lib_load() {
	LOAD_FNS(fns);
	
	CONSTANT("newline", I_STATIC_STR("\n"));
	CONSTANT("tab", I_STATIC_STR("\t"));
	//CONSTANT(
	
	DEF_FN("contains?", str substr, 
		not (null? (find str substr))
	);
	
	DEF_FN("beginswith?", str substr,
		(find str substr) ~= 0
	);
	
	//DEF_FN("endswith?", str substr,
	//	(find str substr) ~= ((sizeof str) - (sizeof substr))
	//);
}
