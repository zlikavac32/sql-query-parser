#include "lexer.h"

int token_is_of_type(sql_token_type type, const struct token *token) {
    return token->type == type;
}

struct token token_new(sql_token_type type, const char *value, size_t len, size_t position) {
    return (struct token) {
        .type = type,
        .value = value,
        .len = len,
        .position = position
    };
}

size_t token_position(const struct token *token) {
    return token->position;
}

size_t token_length(const struct token *token) {
    return token->len;
}

sql_token_type token_type(const struct token *token) {
    return token->type;
}


static void lexer_ensure_have_current(struct lexer *lexer);

static void lexer_ensure_have_next(struct lexer *lexer);


extern void mysql_lexer_use_buffer(const char *buff, size_t len);

#define READ_NEXT_TOKEN(token) \
    do { \
        do { \
            token = lexer_lex(); \
        } while (token_is_of_type(T_WHITE_SPACE, &token)); \
         \
        if (token_is_of_type(T_UNKNOWN, &token)) { \
            lexer->is_done = 1; \
        } \
         \
        if (token_is_of_type(T_EOF, &token)) { \
            lexer->is_done = 1; \
        } \
    } while (0)

static void lexer_ensure_have_current(struct lexer *lexer) {
    if (lexer->has_current || lexer->is_done) {
        return;
    }

    struct token token;

    READ_NEXT_TOKEN(token);

    lexer->current = token;
    lexer->has_current = 1;
}

static void lexer_ensure_have_next(struct lexer *lexer) {
    if (lexer->has_next || lexer->is_done) {
        return;
    }

    lexer_ensure_have_current(lexer);

    struct token token;

    READ_NEXT_TOKEN(token);

    lexer->next = token;
    lexer->has_next = 1;
}

struct lexer lexer_new(const char *buff, size_t len) {
    mysql_lexer_use_buffer(buff, len);

    struct lexer lexer = (struct lexer) {
        .current = token_new(T_UNKNOWN, NULL, 0, 0),
        .has_current = 0,
        .previous = token_new(T_UNKNOWN, NULL, 0, 0),
        .has_previous = 0,
        .next = token_new(T_UNKNOWN, NULL, 0, 0),
        .has_next = 0,
        .is_done = 0,
        .context = {
            .buff = buff,
            .len = len
        },
        .tokens_consumed = 0
    };

    return lexer;
}

const char *lexer_buffer(const struct lexer *lexer) {
    return lexer->context.buff;
}

extern void mysql_lexer_clear_buffer();

void lexer_destroy(struct lexer *lexer) {
    mysql_lexer_clear_buffer();
}

size_t lexer_tokens_consumed(const struct lexer *lexer) {
    return lexer->tokens_consumed;
}

int lexer_has(struct lexer *lexer) {
    lexer_ensure_have_current(lexer);

    return lexer_peek(lexer)->type != T_EOF;
}

const struct token *lexer_peek(struct lexer *lexer) {
    lexer_ensure_have_current(lexer);

    return &lexer->current;
}

const struct token *lexer_peek_next(struct lexer *lexer) {
    lexer_ensure_have_next(lexer);

    return &lexer->next;
}

int lexer_has_previous(const struct lexer *lexer) {
    return lexer->has_previous;
}

int lexer_has_next(struct lexer *lexer) {
    return lexer->has_next;
}

const struct token *lexer_peek_previous(const struct lexer *lexer) {
    return &lexer->previous;
}

struct token lexer_consume(struct lexer *lexer) {
    lexer_ensure_have_current(lexer);

    struct token token = lexer->current;
    lexer->has_current = 0;
    lexer->tokens_consumed++;

    lexer->previous = token;
    lexer->has_previous = 1;

    if (lexer->has_next) {
        lexer->current = lexer->next;
        lexer->has_next = 0;
        lexer->has_current = 1;
    }

    return token;
}
