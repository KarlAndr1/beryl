#ifndef LEXER_H_INCLUDED
#define LEXER_H_INCLUDED

#include "interpreter.h"

enum {
	TOK_NULL,
	TOK_LET,
	TOK_GLOBAL,
	TOK_VARARGS,
	TOK_DO,
	TOK_END,
	TOK_FN,
	TOK_SYM,
	TOK_STRING,
	TOK_OP,
	TOK_ASSIGN,
	TOK_ENDLINE,
	TOK_INT,
	TOK_FLOAT,
	TOK_OPEN_BRACKET,
	TOK_CLOSE_BRACKET,
	TOK_ERR,
	TOK_EOF
};

enum {
	TOK_ERR_OK,
	TOK_ERR_NULL,
	TOK_ERR_MISSING_STR_END,
	TOK_ERR_SIZE_OVERFLOW,
	TOK_ERR_INT_OVERFLOW
};

struct lex_token {
	unsigned char type;
	const char *src;
	i_size len;
	union {
		struct {
			const char *str;
			i_size len;
		} sym;
		i_int int_literal;
		i_float float_literal;
		int err_type;
	} content;
};

struct lex_state {
	const char *src, *at, *end;
	struct lex_token buffer;
};

// Currently just a simple assignment; This is implemented like this so that in the future, if needed, copying the lex state could be done via a function
// or more advanced macro. Code that simply uses the lexer via this interface shouldn't care how the state is copied.
#define LEX_STATE_COPY(to, from) (*to = *((struct lex_state*) (from)))

const char *lex_err_str(struct lex_token tok, size_t *str_len);

void lex_state_init(struct lex_state *state, const char *src, size_t src_len);

struct lex_token lex_peek(struct lex_state *state);
struct lex_token lex_pop(struct lex_state *state);

bool lex_accept(struct lex_state *state, unsigned char type, struct lex_token *opt_tok);

#endif
