#ifndef SQL_QUERY_PARSER_LEXER_H
#define SQL_QUERY_PARSER_LEXER_H

#include <string.h>

typedef enum {
    T_NUMBER,
    T_WHITE_SPACE,
    T_EOF,
    T_UNKNOWN,
    T_PLACEHOLDER,
    T_COMMA,
    T_OPEN_PAREN,
    T_CLOSE_PAREN,
    T_PLUS,
    T_MINUS,
    T_BIT_NOT,
    T_NOT,
    T_BIT_VALUE,
    T_HEX_VALUE,
    T_STRING,
    T_IDENTIFIER,
    T_QUALIFIED_IDENTIFIER,
    T_WILDCARD_IDENTIFIER,
    T_MULT,
    T_INTERVAL_UNIT,
    T_VARIABLE,
    T_BIT_OR,
    T_BIT_AND,
    T_LEFT_SHIFT,
    T_RIGHT_SHIFT,
    T_DIV,
    T_MOD,
    T_BIT_XOR,
    T_OR,
    T_AND,
    T_ARROW,
    T_COMPARISON_OPERATOR,

    T_K_SELECT,
    T_K_ALL,
    T_K_DISTINCT,
    T_K_DISTINCTROW,
    T_K_HIGH_PRIORITY,
    T_K_STRAIGHT_JOIN,
    T_K_SQL_SMALL_RESULT,
    T_K_SQL_BIG_RESULT,
    T_K_SQL_BUFFER_RESULT,
    T_K_SQL_CACHE,
    T_K_SQL_NO_CACHE,
    T_K_SQL_CALC_FOUND_ROWS,
    T_K_BINARY,
    T_K_EXISTS,
    T_K_NULL,
    T_K_TRUE,
    T_K_FALSE,
    T_K_COLLATE,
    T_K_DATE,
    T_K_TIME,
    T_K_TIMESTAMP,
    T_K_INTERVAL,
    T_K_CASE,
    T_K_WHEN,
    T_K_THEN,
    T_K_ELSE,
    T_K_END,
    T_K_MATCH,
    T_K_AGAINST,
    T_K_IN,
    T_K_NATURAL,
    T_K_LANGUAGE,
    T_K_MODE,
    T_K_WITH,
    T_K_QUERY,
    T_K_EXPANSION,
    T_K_BOOLEAN,
    T_K_ROW,
    T_K_MOD,
    T_K_DIV,
    T_K_SOUNDS,
    T_K_LIKE,
    T_K_NOT,
    T_K_REGEXP,
    T_K_BETWEEN,
    T_K_AND,
    T_K_ESCAPE,
    T_K_IS,
    T_K_UNKNOWN,
    T_K_XOR,
    T_K_OR,
    T_K_ANY,
    T_K_AS,
    T_K_INTO,
    T_K_DUMPFILE,
    T_K_OUTFILE,
    T_K_CHARACTER,
    T_K_SET,
    T_K_COLUMNS,
    T_K_FIELDS,
    T_K_TERMINATED,
    T_K_BY,
    T_K_OPTIONALLY,
    T_K_ENCLOSED,
    T_K_ESCAPED,
    T_K_LINES,
    T_K_STARTING,
    T_K_FROM,
    T_K_PARTITION,
    T_K_USE,
    T_K_INDEX,
    T_K_KEY,
    T_K_FOR,
    T_K_JOIN,
    T_K_ORDER,
    T_K_GROUP,
    T_K_FORCE,
    T_K_IGNORE,
    T_K_INNER,
    T_K_LEFT,
    T_K_RIGHT,
    T_K_OUTER,
    T_K_ON,
    T_K_USING,
    T_K_STRAIGHT,
    T_K_CROSS,
    T_K_WHERE,
    T_K_HAVING,
    T_K_ASC,
    T_K_DESC,
    T_K_LIMIT,
    T_K_OFFSET,
    T_K_PROCEDURE,
    T_K_UPDATE,
    T_K_LOCK,
    T_K_SHARE,
} sql_token_type;

struct token {
    sql_token_type type;
    const char *value;
    size_t len;
    size_t position;
};

struct lexer {
    struct token current;
    int has_current;
    struct token previous;
    int has_previous;
    struct token next;
    int has_next;
    int is_done;
    struct {
        const char *buff;
        size_t len;
    } context;
    size_t tokens_consumed;
};

struct lexer lexer_new(const char *buff, size_t len);

void lexer_destroy(struct lexer *lexer);

int lexer_has(struct lexer *lexer);

int lexer_has_next(struct lexer *lexer);

int lexer_has_previous(const struct lexer *lexer);

const struct token *lexer_peek_previous(const struct lexer *lexer);

const char *lexer_buffer(const struct lexer *lexer);

const struct token *lexer_peek(struct lexer *lexer);

const struct token *lexer_peek_next(struct lexer *lexer);

struct token lexer_consume(struct lexer *lexer);

size_t lexer_tokens_consumed(const struct lexer *lexer);

struct token token_new(sql_token_type type, const char *value, size_t len, size_t position);

size_t token_position(const struct token *token);

size_t token_length(const struct token *token);

sql_token_type token_type(const struct token *token);

int token_is_of_type(sql_token_type type, const struct token *token);

struct token lexer_lex();

#endif //SQL_QUERY_PARSER_LEXER_H
