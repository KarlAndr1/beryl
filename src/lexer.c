#include "lexer.h"
#include "utils.h"

#ifdef DEBUG
	#include <assert.h>
#else
	#define assert(x)
#endif

typedef struct lex_token lex_token;

#define KEYWORD(str, tok) { str, sizeof(str) - 1, tok }
struct { const char *str; i_size len; unsigned char tok_type; } keywords[] = {
	KEYWORD("let", TOK_LET),
	KEYWORD("function", TOK_FN),
	KEYWORD("with", TOK_FN),
	KEYWORD("do", TOK_DO),
	KEYWORD("end", TOK_END),
	KEYWORD("=", TOK_ASSIGN),
	KEYWORD("global", TOK_GLOBAL),
	KEYWORD("...", TOK_VARARGS)
};

#define ERR_MSG(x) { *str_len = sizeof(x) - 1; return x; }

const char *lex_err_str(lex_token tok, size_t *str_len) {
	assert(tok.type == TOK_ERR);
	switch(tok.content.err_type) {
		case TOK_ERR_NULL:
			ERR_MSG("Lexing error: Null");
		
		case TOK_ERR_MISSING_STR_END:
			ERR_MSG("Lexing error: Missing string terminator");
		
		case TOK_ERR_SIZE_OVERFLOW:
			ERR_MSG( "Lexing error: Symbol too long");
		
		case TOK_ERR_INT_OVERFLOW:
			ERR_MSG("Lexing error: Integer too large");
		
		default:
			ERR_MSG("Lexing error: None");
	}
}
#undef ERR_MSG

static bool is_whitespace(char c) {
	switch(c) {
		case ' ':
		case '\t':
		case '\r':
		case '\n':
			return true;
		
		default:
			return false;
	}
}

static bool is_op_character(char c) {
	switch(c) {
		case '+':
		case '-':
		case '*':
		case '/':
		case '%':
		case '?':
		case '^':
		case '=':
		case '<':
		case '>':
		case '!':
		case ':':
		case '.':
			return true;
		
		default:
			return false;
	}
}

static unsigned char get_special_char(char c) {
	switch(c) {
		case '(':
			return TOK_OPEN_BRACKET;
		case ')':
			return TOK_CLOSE_BRACKET;
		
		default:
			return TOK_NULL;
	}
}

static bool is_valid_symbol_char(char c) {
	return !is_whitespace(c) && get_special_char(c) == TOK_NULL && c != ',';
}

static bool is_digit(char c) {
	return c >= '0' && c <= '9';
}

static bool is_at_end(struct lex_state *state) {
	return state->at == state->end;
}

//Skips whitespace *and* comments
static void skip_whitespace(struct lex_state *state) {
	bool inside_comment = false;
	while(!is_at_end(state) && (inside_comment || is_whitespace(*state->at) || *state->at == '#')) {
		if(*state->at == '#')
			inside_comment = !inside_comment;
		else if(*state->at == '\n')
			inside_comment = false;
		state->at++;
	}
}

static bool eqlstrcmp(const char *a, const char *b, i_size len) {
	for(; len != 0; len--) {
		if(*a != *b)
			return false;
		a++;
		b++;
	}
	return true;
}

static unsigned char match_keyword(const char *sym, i_size len) {
	for(unsigned i = 0; i < LENOF(keywords); i++) {
		if(keywords[i].len == len)
			if(eqlstrcmp(keywords[i].str, sym, len))
				return keywords[i].tok_type;
	}
	
	return TOK_NULL;
}

struct lex_token parse_number(struct lex_state *state) {
	const char *start = state->at;
	unsigned long long int_v = 0; //Unsigned integers have defined overflow semantics
	while(!is_at_end(state) && (is_digit(*state->at) || *state->at == '\'')) {
		if(*state->at == '\'') {
			state->at++;
			continue;
		}
		int_v *= 10;
		int_v += *state->at - '0';
		state->at++;
	}

	if(!is_at_end(state) && *state->at == '.') { //Floats
		state->at++;
		double float_v = 0;
		double pow = 1.0;
		while(!is_at_end(state) && (is_digit(*state->at) || *state->at == '\'')) {
			if(*state->at == '\'') {
				state->at++;
				continue;
			}
			pow *= 0.1;
			float_v += (*state->at - '0') * pow;
			state->at++;
		}
		float_v += int_v;
		return (lex_token) { TOK_FLOAT, start, state->at - start, { .float_literal = float_v } };
	}
		
	if(int_v > I_INT_MAX) {
		return (lex_token) { TOK_ERR, start, state->at - start, { .err_type = TOK_ERR_INT_OVERFLOW } };
	}
	return (lex_token) { TOK_INT, start, state->at - start, { .int_literal = int_v } };
}

struct lex_token next_token(struct lex_state *state) {
	START:

	if(is_at_end(state))
		return (struct lex_token) { .type = TOK_EOF, .src = state->end, .len = 0 };

	if(*state->at == '\n') {
		const char *start = state->at;
		state->at++;
		skip_whitespace(state); //Skips all following whitespace, including newlines and comments, so that multiple blank lines still count as just one token
		return (lex_token) { .type = TOK_ENDLINE, .src = start, .len = 1 };
	}
	
	if(is_whitespace(*state->at)) {
		while(!is_at_end(state) && is_whitespace(*state->at) && *state->at != '\n')
			state->at++;
		//skip_whitespace(state);
		goto START;
	}
	
	if(*state->at == ',') {
		state->at++;
		skip_whitespace(state);
		while(!is_at_end(state) && is_valid_symbol_char(*state->at)) {
			state->at++;
		}
		goto START;
	}
	
	if(*state->at == '#') {
		state->at++;
		while(!is_at_end(state)) {
			if(*state->at == '\n')
				break;
			if(*state->at == '#') {
				state->at++;
				break;
			}
			state->at++;
		}
		goto START;
	}
	
	unsigned char special_char = get_special_char(*state->at);
	if(special_char != TOK_NULL) {
		state->at++;
		return (lex_token) { special_char, state->at - 1, 1 };
	}
	
	if(*state->at == '"') {
		const char *start = state->at;
		do {
			state->at++;
			if(is_at_end(state))
				return (lex_token) { .type = TOK_ERR, .src = start, .len = state->at - start, .content = { .err_type = TOK_ERR_MISSING_STR_END } };
		} while(*state->at != '"');
		
		state->at++;
		
		size_t str_len = state->at - start;
		if(str_len > I_SIZE_MAX)
			return (struct lex_token) { TOK_ERR, start, 1, { .err_type = TOK_ERR_SIZE_OVERFLOW } };
		
		return (lex_token) { TOK_STRING, start, str_len, { .sym = { start + 1, str_len - 2 } } };
	}
	
	if(*state->at == '-') {
		state->at++;
		if(is_at_end(state) || !is_digit(*state->at))
			state->at--;
		else {
			lex_token tok = parse_number(state);
			if(tok.type == TOK_INT)
				tok.content.int_literal = -tok.content.int_literal;
			else if(tok.type == TOK_FLOAT)
				tok.content.float_literal = -tok.content.float_literal;
			return tok;
		}
	}
	
	if(is_digit(*state->at)) {
		return parse_number(state);
	}
	
	//Otherwise its a symbol of some sort, or a keyword
	const char *sym_start = state->at;
	while(!is_at_end(state) && is_valid_symbol_char(*state->at)) {
		state->at++;
	}
	
	size_t sym_len = state->at - sym_start;
	if(sym_len > I_SIZE_MAX) {
		return (struct lex_token) { TOK_ERR, sym_start, 1, { .err_type = TOK_ERR_SIZE_OVERFLOW } };
	}
	
	lex_token sym_tok = { TOK_SYM, sym_start, sym_len, { .sym = { sym_start, sym_len } } };
	if(sym_len > 0) {
		if(is_op_character(sym_start[0]) || is_op_character(sym_start[sym_len - 1]))
			sym_tok.type = TOK_OP;
	}
	
	unsigned char keyword = match_keyword(sym_start, sym_len);
	if(keyword != TOK_NULL)
		sym_tok.type = keyword;
	
	return sym_tok;
}

struct lex_token lex_peek(struct lex_state *state) {
	return state->buffer;
}

struct lex_token lex_pop(struct lex_state *state) {
	struct lex_token tok = state->buffer;
	
	state->buffer = next_token(state);
	
	return tok;
}

bool lex_accept(struct lex_state *state, unsigned char type, struct lex_token *opt_tok) {
	struct lex_token tok = lex_peek(state);
	if(tok.type == type) {
		lex_pop(state);
		if(opt_tok != NULL)
			*opt_tok = tok;
		return true;
	}
	return false;
}

void lex_state_init(struct lex_state *state, const char *src, size_t src_len) {
	state->src = src;
	state->end = src + src_len;
	state->at = src;
	
	state->buffer = next_token(state);
}
