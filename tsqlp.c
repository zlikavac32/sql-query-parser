#include "lexer.h"
#include "tsqlp.h"

struct parse_state {
    struct tsqlp_placeholders placeholders;
    int is_tracking_in_progress;
    size_t section_offset;
};

typedef enum {
    MUST_PARSE,
    TRY_PARSE
} parse_strength;

static tsqlp_parse_status
parse_stmt(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_modifiers(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_columns(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_first_into(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_tables(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status parse_partition(struct lexer *lexer);

static tsqlp_parse_status
parse_where(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_group_by(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_having(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_order_by(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_limit(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_procedure(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_second_into(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_flags(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_table_list(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_joined_table(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_table_factor(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_arithm_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_predicate_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status
parse_simple_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state);

static tsqlp_parse_status parse_alias(struct lexer *lexer);

static tsqlp_parse_status
parse_join_specification(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state,
                         parse_strength strength
);

static tsqlp_parse_status tsqlp_parse_mysql(const char *sql, size_t len, struct tsqlp_parse_result *parse_result);

static void *malloc_panic(size_t size);

static void *realloc_panic(void *ptr, size_t size);

typedef enum {
    STILL_TRACKING_PLACEHOLDERS,
    STARTED_TRACKING_PLACEHOLDERS
} parse_state_type;

static struct parse_state parse_state_new();

static parse_state_type parse_state_start_counting(struct parse_state *parse_state, size_t section_offset);

static void parse_state_register_placeholder(struct parse_state *parse_state, size_t location);

static struct tsqlp_placeholders parse_state_finish_counting(struct parse_state *parse_state);



static struct parse_state parse_state_new() {
    return (struct parse_state) {
        .placeholders =  tsqlp_placeholders_new(),
        .section_offset = 0,
        .is_tracking_in_progress = 0
    };
}

static parse_state_type parse_state_start_counting(struct parse_state *parse_state, size_t section_offset) {
    if (parse_state->is_tracking_in_progress) {
        return STILL_TRACKING_PLACEHOLDERS;
    }

    parse_state->is_tracking_in_progress = 1;
    parse_state->placeholders = tsqlp_placeholders_new();
    parse_state->section_offset = section_offset;

    return STARTED_TRACKING_PLACEHOLDERS;
}

static void parse_state_register_placeholder(struct parse_state *parse_state, size_t location) {
    tsqlp_placeholders_push(&parse_state->placeholders, location - parse_state->section_offset);
}

static struct tsqlp_placeholders parse_state_finish_counting(struct parse_state *parse_state) {
    if (!parse_state->is_tracking_in_progress) {
        return (struct tsqlp_placeholders) {
            .locations = NULL,
            .count = 0
        };
    }

    parse_state->is_tracking_in_progress = 0;

    return parse_state->placeholders;
}

#define RETURN_IF_NOT_OK(expr) \
    do { \
        tsqlp_parse_status status = expr; \
        if (status != TSQLP_PARSE_OK) { \
            return status; \
        } \
    } while (0)
#define RETURN_ERROR_IF_TOKEN_NOT(type, lexer) \
    do { \
        if (!token_is_of_type(type, lexer_peek(lexer))) { \
            return TSQLP_PARSE_INVALID_SYNTAX; \
        } \
        lexer_consume(lexer); \
    } while (0)
#define RETURN_SUCCESS_IF_TOKEN_NOT(type, lexer) \
    do { \
        if (!token_is_of_type(type, lexer_peek(lexer))) { \
            return TSQLP_PARSE_OK; \
        } \
        lexer_consume(lexer); \
    } while (0)
#define CONSUME_IF_TOKEN(type, lexer) \
    do { \
        if (token_is_of_type(type, lexer_peek(lexer))) { \
            lexer_consume(lexer); \
        } \
    } while (0)

#define TRACK_SECTION(section, lexer, parse_result, parse_state, call) \
    do { \
        size_t position = token_position(lexer_peek(lexer)); \
        int is_top_level = parse_state_start_counting(parse_state, position) == STARTED_TRACKING_PLACEHOLDERS; \
        size_t tokens_consumed = lexer_tokens_consumed(lexer); \
        \
        tsqlp_parse_status status = call; \
        \
        if (is_top_level) { \
            struct tsqlp_placeholders tsqlp_placeholders = parse_state_finish_counting(parse_state); \
            \
            if (tokens_consumed < lexer_tokens_consumed(lexer)) { \
                tsqlp_sql_section_update( \
                    lexer_buffer(lexer) + position, \
                    token_position(lexer_peek_previous(lexer)) + token_length(lexer_peek_previous(lexer)) - position, \
                    tsqlp_placeholders, \
                    &parse_result->section \
                ); \
            } \
        } \
        \
        return status; \
    } while (0)

static tsqlp_parse_status
parse_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_predicate_expression(lexer, parse_result, parse_state));

    switch (token_type(lexer_peek(lexer))) {
        case T_K_XOR:
            // intentional
        case T_AND:
            // intentional
        case T_K_AND:
            // intentional
        case T_K_OR:
            // intentional
        case T_ARROW:
            // intentional
        case T_OR:
            lexer_consume(lexer);

            return parse_expression(lexer, parse_result, parse_state);
        case T_COMPARISON_OPERATOR:
            lexer_consume(lexer);

            if (token_is_of_type(T_K_ALL, lexer_peek(lexer)) || token_is_of_type(T_K_ANY, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
                RETURN_IF_NOT_OK(parse_stmt(lexer, parse_result, parse_state));
                RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

                return TSQLP_PARSE_OK;
            }

            return parse_expression(lexer, parse_result, parse_state);
        case T_K_IS:
            lexer_consume(lexer);

            if (token_is_of_type(T_K_NOT, lexer_peek(lexer))) {
                lexer_consume(lexer);
            }

            const struct token *token = lexer_peek(lexer);

            if (
                token_is_of_type(T_K_UNKNOWN, token)
                || token_is_of_type(T_K_NULL, token)
                || token_is_of_type(T_K_TRUE, token)
                || token_is_of_type(T_K_FALSE, token)
                ) {
                lexer_consume(lexer);

                return TSQLP_PARSE_OK;
            }

            return TSQLP_PARSE_INVALID_SYNTAX;
        default:
            return TSQLP_PARSE_OK;
    }
}

static tsqlp_parse_status
parse_predicate_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_arithm_expression(lexer, parse_result, parse_state));

    if (token_is_of_type(T_K_SOUNDS, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_LIKE, lexer);

        return parse_expression(lexer, parse_result, parse_state);
    }

    if (token_is_of_type(T_K_NOT, lexer_peek(lexer))) {
        lexer_consume(lexer);
    }

    switch (token_type(lexer_peek(lexer))) {
        case T_K_REGEXP:
            lexer_consume(lexer);

            return parse_expression(lexer, parse_result, parse_state);
        case T_K_BETWEEN:
            lexer_consume(lexer);

            RETURN_IF_NOT_OK(parse_predicate_expression(lexer, parse_result, parse_state));
            RETURN_ERROR_IF_TOKEN_NOT(T_K_AND, lexer);

            return parse_expression(lexer, parse_result, parse_state);
        case T_K_LIKE:
            lexer_consume(lexer);

            RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

            RETURN_SUCCESS_IF_TOKEN_NOT(T_K_ESCAPE, lexer);

            return parse_expression(lexer, parse_result, parse_state);
        case T_K_IN:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
            RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        default:
            return TSQLP_PARSE_OK;
    }
}

static tsqlp_parse_status
parse_arithm_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_simple_expression(lexer, parse_result, parse_state));

    switch (token_type(lexer_peek(lexer))) {
        case T_BIT_OR:
            //intentional
        case T_BIT_AND:
            //intentional
        case T_LEFT_SHIFT:
            //intentional
        case T_RIGHT_SHIFT:
            //intentional
        case T_PLUS:
            //intentional
        case T_MINUS:
            //intentional
        case T_MULT:
            //intentional
        case T_DIV:
            //intentional
        case T_K_DIV:
            //intentional
        case T_K_MOD:
            //intentional
        case T_MOD:
            //intentional
        case T_BIT_XOR:
            lexer_consume(lexer);

            break;
        case T_K_COLLATE:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);

            return TSQLP_PARSE_OK;
        default:
            return TSQLP_PARSE_OK;
    }

    return parse_expression(lexer, parse_result, parse_state);
}

static tsqlp_parse_status
parse_simple_expression(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    switch (token_type(lexer_peek(lexer))) {
        case T_K_ROW:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);

            RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        case T_NUMBER:
            // intentional
        case T_BIT_VALUE:
            // intentional
        case T_HEX_VALUE:
            // intentional
        case T_K_TRUE:
            // intentional
        case T_K_FALSE:
            // intentional
        case T_VARIABLE:
            // intentional
        case T_MULT: // SELECT *
            // intentional
        case T_QUALIFIED_IDENTIFIER:
            // intentional
        case T_WILDCARD_IDENTIFIER:
            // intentional
        case T_K_NULL:
            lexer_consume(lexer);

            return TSQLP_PARSE_OK;
        case T_K_DATE:
            // intentional
        case T_K_TIME:
            // intentional
        case T_K_TIMESTAMP:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);

            return TSQLP_PARSE_OK;
        case T_STRING:
            // intentional
            lexer_consume(lexer);

            if (token_is_of_type(T_K_COLLATE, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
            }

            return TSQLP_PARSE_OK;
        case T_PLACEHOLDER: {
            struct token token = lexer_consume(lexer);
            parse_state_register_placeholder(parse_state, token_position(&token));

            return TSQLP_PARSE_OK;
        }
        case T_IDENTIFIER:
            lexer_consume(lexer);

            RETURN_SUCCESS_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);

            if (!token_is_of_type(T_CLOSE_PAREN, lexer_peek(lexer))) {
                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

                while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
                }
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        case T_K_EXISTS:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
            RETURN_IF_NOT_OK(parse_stmt(lexer, parse_result, parse_state));
            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        case T_K_SELECT:
            return parse_stmt(lexer, parse_result, parse_state);
        case T_OPEN_PAREN:
            lexer_consume(lexer);

            RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        case T_PLUS:
            // intentional
        case T_MINUS:
            // intentional
        case T_NOT:
            // intentional
        case T_K_NOT:
            // intentional
        case T_BIT_NOT:
            // intentional
        case T_K_BINARY:
            lexer_consume(lexer);

            return parse_expression(lexer, parse_result, parse_state);
        case T_K_INTERVAL:
            lexer_consume(lexer);

            RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            RETURN_ERROR_IF_TOKEN_NOT(T_INTERVAL_UNIT, lexer);

            return TSQLP_PARSE_OK;
        case T_K_CASE:
            lexer_consume(lexer);

            if (!token_is_of_type(T_K_WHEN, lexer_peek(lexer))) {
                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            }

            if (!token_is_of_type(T_K_WHEN, lexer_peek(lexer))) {
                return TSQLP_PARSE_INVALID_SYNTAX;
            }

            while (token_is_of_type(T_K_WHEN, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
                RETURN_ERROR_IF_TOKEN_NOT(T_K_THEN, lexer);
                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

            }

            if (token_is_of_type(T_K_ELSE, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_K_END, lexer);

            return TSQLP_PARSE_OK;
        case T_K_MATCH:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
            RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            }
            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_K_AGAINST, lexer);
            RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);

            RETURN_IF_NOT_OK(parse_arithm_expression(lexer, parse_result, parse_state));

            if (token_is_of_type(T_K_WITH, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_K_QUERY, lexer);
                RETURN_ERROR_IF_TOKEN_NOT(T_K_EXPANSION, lexer);
            } else if (token_is_of_type(T_K_IN, lexer_peek(lexer))) {
                lexer_consume(lexer);

                if (token_is_of_type(T_K_BOOLEAN, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_ERROR_IF_TOKEN_NOT(T_K_MODE, lexer);
                } else if (token_is_of_type(T_K_NATURAL, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_ERROR_IF_TOKEN_NOT(T_K_LANGUAGE, lexer);
                    RETURN_ERROR_IF_TOKEN_NOT(T_K_MODE, lexer);

                    if (token_is_of_type(T_K_WITH, lexer_peek(lexer))) {
                        lexer_consume(lexer);

                        RETURN_ERROR_IF_TOKEN_NOT(T_K_QUERY, lexer);
                        RETURN_ERROR_IF_TOKEN_NOT(T_K_EXPANSION, lexer);
                    }
                }
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        default:
            return TSQLP_PARSE_INVALID_SYNTAX;
    }
}

static tsqlp_parse_status parse_modifiers_inner(struct lexer *lexer) {
    const struct token *token = lexer_peek(lexer);

    if (token_is_of_type(T_K_ALL, token) || token_is_of_type(T_K_DISTINCT, token) ||
        token_is_of_type(T_K_DISTINCTROW, token)) {
        lexer_consume(lexer);
    }

    CONSUME_IF_TOKEN(T_K_HIGH_PRIORITY, lexer);
    CONSUME_IF_TOKEN(T_K_STRAIGHT_JOIN, lexer);
    CONSUME_IF_TOKEN(T_K_SQL_SMALL_RESULT, lexer);
    CONSUME_IF_TOKEN(T_K_SQL_BIG_RESULT, lexer);
    CONSUME_IF_TOKEN(T_K_SQL_BUFFER_RESULT, lexer);

    token = lexer_peek(lexer);

    if (token_is_of_type(T_K_SQL_CACHE, token) || token_is_of_type(T_K_SQL_NO_CACHE, token) ||
        token_is_of_type(T_K_SQL_CALC_FOUND_ROWS, token)) {
        lexer_consume(lexer);
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_modifiers(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(modifiers, lexer, parse_result, parse_state, parse_modifiers_inner(lexer));
}

static tsqlp_parse_status
parse_columns_inner(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

    RETURN_IF_NOT_OK(parse_alias(lexer));

    while (1) {
        if (!token_is_of_type(T_COMMA, lexer_peek(lexer))) {
            break;
        }

        lexer_consume(lexer);

        RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
        RETURN_IF_NOT_OK(parse_alias(lexer));
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_columns(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(columns, lexer, parse_result, parse_state, parse_columns_inner(lexer, parse_result, parse_state));
}

static tsqlp_parse_status parse_first_into_inner(struct lexer *lexer) {
    RETURN_SUCCESS_IF_TOKEN_NOT(T_K_INTO, lexer);

    switch (token_type(lexer_peek(lexer))) {
        case T_K_OUTFILE:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);

            if (token_is_of_type(T_K_CHARACTER, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_K_SET, lexer);
                RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
            }

            if (token_is_of_type(T_K_FIELDS, lexer_peek(lexer)) || token_is_of_type(T_K_COLUMNS, lexer_peek(lexer))) {
                lexer_consume(lexer);


                if (token_is_of_type(T_K_TERMINATED, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);
                    RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);
                }


                if (token_is_of_type(T_K_ENCLOSED, lexer_peek(lexer)) ||
                    token_is_of_type(T_K_OPTIONALLY, lexer_peek(lexer))) {
                    struct token token = lexer_consume(lexer);

                    if (token_is_of_type(T_K_OPTIONALLY, &token)) {
                        RETURN_ERROR_IF_TOKEN_NOT(T_K_ENCLOSED, lexer);
                    }

                    RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);
                    RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);
                }


                if (token_is_of_type(T_K_ESCAPED, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);
                    RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);
                }
            }

            if (token_is_of_type(T_K_LINES, lexer_peek(lexer))) {
                lexer_consume(lexer);

                if (token_is_of_type(T_K_STARTING, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);
                    RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);
                }

                if (token_is_of_type(T_K_TERMINATED, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);
                    RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);
                }
            }

            return TSQLP_PARSE_OK;
        case T_K_DUMPFILE:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);

            return TSQLP_PARSE_OK;
        case T_VARIABLE:
            lexer_consume(lexer);

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_VARIABLE, lexer);
            }

            return TSQLP_PARSE_OK;
        default:
            return TSQLP_PARSE_INVALID_SYNTAX;
    }
}

static tsqlp_parse_status
parse_first_into(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(first_into, lexer, parse_result, parse_state, parse_first_into_inner(lexer));
}

static tsqlp_parse_status
parse_tables(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_SUCCESS_IF_TOKEN_NOT(T_K_FROM, lexer);

    TRACK_SECTION(tables, lexer, parse_result, parse_state, parse_table_list(lexer, parse_result, parse_state));
}

static tsqlp_parse_status
parse_table_list(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_joined_table(lexer, parse_result, parse_state));

    while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_IF_NOT_OK(parse_joined_table(lexer, parse_result, parse_state));
    }

    return TSQLP_PARSE_OK;
}


static tsqlp_parse_status
parse_joined_table(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {

    RETURN_IF_NOT_OK(parse_table_factor(lexer, parse_result, parse_state));

    while (1) {
        switch (token_type(lexer_peek(lexer))) {
            case T_K_INNER:
                // intentional
            case T_K_CROSS:
                // intentional
            case T_K_STRAIGHT:
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_K_JOIN, lexer);

                RETURN_IF_NOT_OK(parse_table_factor(lexer, parse_result, parse_state));

                RETURN_IF_NOT_OK(parse_join_specification(lexer, parse_result, parse_state, TRY_PARSE));

                break;
            case T_K_JOIN:
                lexer_consume(lexer);

                RETURN_IF_NOT_OK(parse_table_factor(lexer, parse_result, parse_state));

                RETURN_IF_NOT_OK(parse_join_specification(lexer, parse_result, parse_state, TRY_PARSE));

                break;
            case T_K_LEFT:
                // intentional
            case T_K_RIGHT:
                lexer_consume(lexer);

                if (token_is_of_type(T_K_OUTER, lexer_peek(lexer))) {
                    lexer_consume(lexer);
                }

                RETURN_ERROR_IF_TOKEN_NOT(T_K_JOIN, lexer);
                RETURN_IF_NOT_OK(parse_table_factor(lexer, parse_result, parse_state));

                RETURN_IF_NOT_OK(parse_join_specification(lexer, parse_result, parse_state, MUST_PARSE));

                break;
            case T_K_NATURAL:
                lexer_consume(lexer);

                if (token_is_of_type(T_K_INNER, lexer_peek(lexer)) || token_is_of_type(T_K_LEFT, lexer_peek(lexer)) ||
                    token_is_of_type(T_K_RIGHT, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    if (token_is_of_type(T_K_OUTER, lexer_peek(lexer))) {
                        lexer_consume(lexer);
                    }
                }

                RETURN_ERROR_IF_TOKEN_NOT(T_K_JOIN, lexer);

                RETURN_IF_NOT_OK(parse_table_factor(lexer, parse_result, parse_state));

                break;
            default:
                return TSQLP_PARSE_OK;
        }
    }
}

static tsqlp_parse_status
parse_join_specification(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state,
                         parse_strength strength
) {
    switch (token_type(lexer_peek(lexer))) {
        case T_K_ON:
            lexer_consume(lexer);

            return parse_expression(lexer, parse_result, parse_state);
        case T_K_USING:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        default:
            return (strength == TRY_PARSE) ? TSQLP_PARSE_OK : TSQLP_PARSE_INVALID_SYNTAX;
    }
}

static tsqlp_parse_status
parse_table_factor(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {

    switch (token_type(lexer_peek(lexer))) {
        case T_OPEN_PAREN:
            lexer_consume(lexer);

            if (token_is_of_type(T_K_SELECT, lexer_peek(lexer))) {
                RETURN_IF_NOT_OK(parse_stmt(lexer, parse_result, parse_state));
                RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

                RETURN_IF_NOT_OK(parse_alias(lexer));

                RETURN_SUCCESS_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);

                if (!token_is_of_type(T_IDENTIFIER, lexer_peek(lexer)) &&
                    !token_is_of_type(T_QUALIFIED_IDENTIFIER, lexer_peek(lexer))) {
                    return TSQLP_PARSE_INVALID_SYNTAX;
                }

                lexer_consume(lexer);

                while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    if (!token_is_of_type(T_IDENTIFIER, lexer_peek(lexer)) &&
                        !token_is_of_type(T_QUALIFIED_IDENTIFIER, lexer_peek(lexer))) {
                        return TSQLP_PARSE_INVALID_SYNTAX;
                    }

                    lexer_consume(lexer);
                }

                RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

                return TSQLP_PARSE_OK;
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return TSQLP_PARSE_OK;
        case T_PLACEHOLDER: {
            struct token token = lexer_consume(lexer);

            parse_state_register_placeholder(parse_state, token_position(&token));

            return TSQLP_PARSE_OK;
        }
        case T_IDENTIFIER:
            // intentional
        case T_QUALIFIED_IDENTIFIER:
            lexer_consume(lexer);

            if (token_is_of_type(T_K_PARTITION, lexer_peek(lexer))) {
                RETURN_IF_NOT_OK(parse_partition(lexer));
            }

            RETURN_IF_NOT_OK(parse_alias(lexer));

            while (
                token_is_of_type(T_K_USE, lexer_peek(lexer))
                || token_is_of_type(T_K_FORCE, lexer_peek(lexer))
                || token_is_of_type(T_K_IGNORE, lexer_peek(lexer))
                ) {
                lexer_consume(lexer);

                if (!token_is_of_type(T_K_INDEX, lexer_peek(lexer)) && !token_is_of_type(T_K_KEY, lexer_peek(lexer))) {
                    return TSQLP_PARSE_INVALID_SYNTAX;
                }

                lexer_consume(lexer);

                if (token_is_of_type(T_K_FOR, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    if (token_is_of_type(T_K_JOIN, lexer_peek(lexer))) {
                        lexer_consume(lexer);
                    } else if (token_is_of_type(T_K_ORDER, lexer_peek(lexer)) ||
                               token_is_of_type(T_K_GROUP, lexer_peek(lexer))) {
                        lexer_consume(lexer);

                        RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);
                    }
                }

                RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
                RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);

                while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
                }

                RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

                if (
                    token_is_of_type(T_COMMA, lexer_peek(lexer))
                    &&
                    (
                        token_is_of_type(T_K_USE, lexer_peek_next(lexer))
                        || token_is_of_type(T_K_FORCE, lexer_peek(lexer))
                        || token_is_of_type(T_K_IGNORE, lexer_peek(lexer))
                    )) {
                    lexer_consume(lexer);

                    continue;
                }

                break;
            }

            return TSQLP_PARSE_OK;
        default:

            return TSQLP_PARSE_INVALID_SYNTAX;
    }
}

static tsqlp_parse_status parse_alias(struct lexer *lexer) {
    if (token_is_of_type(T_K_AS, lexer_peek(lexer)) || token_is_of_type(T_IDENTIFIER, lexer_peek(lexer))) {
        struct token token = lexer_consume(lexer);

        if (token_is_of_type(T_K_AS, &token)) {
            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
        }
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status parse_partition(struct lexer *lexer) {
    if (token_is_of_type(T_K_PARTITION, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
        RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);

        while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
        }
        RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

        return TSQLP_PARSE_OK;
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_where(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_WHERE, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(where, lexer, parse_result, parse_state, parse_expression(lexer, parse_result, parse_state));
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status parse_group_by_inner(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

    if (token_is_of_type(T_K_ASC, lexer_peek(lexer)) || token_is_of_type(T_K_DESC, lexer_peek(lexer))) {
        lexer_consume(lexer);
    }

    while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

        if (token_is_of_type(T_K_ASC, lexer_peek(lexer)) || token_is_of_type(T_K_DESC, lexer_peek(lexer))) {
            lexer_consume(lexer);
        }
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_group_by(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_GROUP, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);

        TRACK_SECTION(group_by, lexer, parse_result, parse_state, parse_group_by_inner(lexer, parse_result, parse_state));
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_having(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_HAVING, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(having, lexer, parse_result, parse_state, parse_expression(lexer, parse_result, parse_state));
    }

    return TSQLP_PARSE_OK;
}


static tsqlp_parse_status parse_order_by_inner(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

    if (token_is_of_type(T_K_ASC, lexer_peek(lexer)) || token_is_of_type(T_K_DESC, lexer_peek(lexer))) {
        lexer_consume(lexer);
    }

    while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

        if (token_is_of_type(T_K_ASC, lexer_peek(lexer)) || token_is_of_type(T_K_DESC, lexer_peek(lexer))) {
            lexer_consume(lexer);
        }
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_order_by(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_ORDER, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);

        TRACK_SECTION(order_by, lexer, parse_result, parse_state, parse_order_by_inner(lexer, parse_result, parse_state));
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status parse_limit_inner(struct lexer *lexer, struct parse_state *parse_state) {
    if (!token_is_of_type(T_NUMBER, lexer_peek(lexer)) && !token_is_of_type(T_PLACEHOLDER, lexer_peek(lexer))) {
        return TSQLP_PARSE_INVALID_SYNTAX;
    }

    if (token_is_of_type(T_PLACEHOLDER, lexer_peek(lexer))) {
        parse_state_register_placeholder(parse_state, token_position(lexer_peek(lexer)));
    }

    lexer_consume(lexer);

    if (token_is_of_type(T_NUMBER, lexer_peek(lexer)) || token_is_of_type(T_PLACEHOLDER, lexer_peek(lexer))) {
        lexer_consume(lexer);
    } else if (token_is_of_type(T_K_OFFSET, lexer_peek(lexer)) || token_is_of_type(T_COMMA, lexer_peek(lexer))) {
        lexer_consume(lexer);

        if (!token_is_of_type(T_NUMBER, lexer_peek(lexer)) && !token_is_of_type(T_PLACEHOLDER, lexer_peek(lexer))) {
            return TSQLP_PARSE_INVALID_SYNTAX;
        }

        if (token_is_of_type(T_PLACEHOLDER, lexer_peek(lexer))) {
            parse_state_register_placeholder(parse_state, token_position(lexer_peek(lexer)));
        }

        lexer_consume(lexer);
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_limit(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_LIMIT, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(limit, lexer, parse_result, parse_state, parse_limit_inner(lexer, parse_state));
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_procedure_inner(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
    RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);

    if (!token_is_of_type(T_CLOSE_PAREN, lexer_peek(lexer))) {
        RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));

        while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
            lexer_consume(lexer);

            RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
        }
    }

    RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_procedure(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_PROCEDURE, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(procedure, lexer, parse_result, parse_state,
                      parse_procedure_inner(lexer, parse_result, parse_state));
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_second_into(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(second_into, lexer, parse_result, parse_state, parse_first_into_inner(lexer));
}

static tsqlp_parse_status parse_flags_inner(struct lexer *lexer) {
    if (token_is_of_type(T_K_FOR, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_UPDATE, lexer);
    } else if (token_is_of_type(T_K_LOCK, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_IN, lexer);
        RETURN_ERROR_IF_TOKEN_NOT(T_K_SHARE, lexer);
        RETURN_ERROR_IF_TOKEN_NOT(T_K_MODE, lexer);
    }

    return TSQLP_PARSE_OK;
}

static tsqlp_parse_status
parse_flags(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(flags, lexer, parse_result, parse_state, parse_flags_inner(lexer));
}

static tsqlp_parse_status
parse_stmt(struct lexer *lexer, struct tsqlp_parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_ERROR_IF_TOKEN_NOT(T_K_SELECT, lexer);

    RETURN_IF_NOT_OK(parse_modifiers(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_columns(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_first_into(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_tables(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_where(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_group_by(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_having(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_order_by(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_limit(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_procedure(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_second_into(lexer, parse_result, parse_state));
    RETURN_IF_NOT_OK(parse_flags(lexer, parse_result, parse_state));

    return TSQLP_PARSE_OK;
}

struct tsqlp_placeholders tsqlp_placeholders_new() {
    return (struct tsqlp_placeholders) {
        .locations = NULL,
        .count = 0
    };
}

void tsqlp_placeholders_push(struct tsqlp_placeholders *placeholders, size_t location) {
    placeholders->locations = (size_t *) realloc_panic(placeholders->locations,
                                                       (placeholders->count + 1) * sizeof(size_t));
    placeholders->locations[placeholders->count++] = location;
}

int tsqlp_placeholders_count(const struct tsqlp_placeholders *placeholders) {
    return placeholders->count;
}

size_t tsqlp_placeholders_position_at(const struct tsqlp_placeholders *placeholders, unsigned int index) {
    if (index < placeholders->count) {
        return placeholders->locations[index];
    }

    return 0;
}

void tsqlp_placeholders_destroy(struct tsqlp_placeholders *placeholders) {
    if (placeholders->locations != NULL) {
        free(placeholders->locations);
    }
}

struct tsqlp_sql_section tsqlp_sql_section_new() {
    return (struct tsqlp_sql_section) {
        .chunk = NULL,
        .len = 0,
        .placeholders = {
            .locations = NULL,
            .count = 0
        }
    };
}

size_t tsqlp_sql_section_length(const struct tsqlp_sql_section *sql_section) {
    return sql_section->len;
}

const char *tsqlp_sql_section_content(const struct tsqlp_sql_section *sql_section) {
    return sql_section->chunk;
}

int tsqlp_sql_section_is_populated(const struct tsqlp_sql_section *sql_section) {
    return sql_section->len > 0;
}

struct tsqlp_placeholders *tsqlp_sql_section_placeholders(struct tsqlp_sql_section *sql_section) {
    return &sql_section->placeholders;
}

void
tsqlp_sql_section_update(const char *chunk, size_t len, struct tsqlp_placeholders placeholders, struct tsqlp_sql_section *sql_section) {

    // @todo: remove +1 and null character when tests don't print using %s
    char *buff = (char *) malloc_panic(sizeof(char) * (len + 1));
    buff[len] = '\0';

    memcpy(buff, chunk, len);

    *sql_section = (struct tsqlp_sql_section) {
        .chunk = buff,
        .len = len,
        .placeholders = placeholders
    };

}

void tsqlp_sql_section_destroy(struct tsqlp_sql_section *sql_section) {
    if (sql_section->chunk == NULL) {
        return;
    }

    free(sql_section->chunk);

    tsqlp_placeholders_destroy(&sql_section->placeholders);
}

void tsqlp_parse_result_serialize(struct tsqlp_parse_result *parse_result, FILE *file) {

#define PRINT_SECTION(section) \
    do { \
        if (tsqlp_sql_section_is_populated(&parse_result->section)) { \
            struct tsqlp_placeholders *tsqlp_placeholders = tsqlp_sql_section_placeholders(&parse_result->section); \
            size_t count = tsqlp_placeholders_count(tsqlp_placeholders); \
            fprintf(file, "%s %ld ", #section, count); \
            \
            for (int i = 0; i < count; i++) { \
                fprintf(file, "%ld ", tsqlp_placeholders_position_at(tsqlp_placeholders, i)); \
            } \
            \
            fprintf(file, "%ld ", tsqlp_sql_section_length(&parse_result->section)); \
            fwrite(tsqlp_sql_section_content(&parse_result->section), tsqlp_sql_section_length(&parse_result->section), 1, file); \
            \
            fprintf(file, "\n"); \
        } \
    } while (0)

    PRINT_SECTION(modifiers);
    PRINT_SECTION(columns);
    PRINT_SECTION(first_into);
    PRINT_SECTION(tables);
    PRINT_SECTION(where);
    PRINT_SECTION(group_by);
    PRINT_SECTION(having);
    PRINT_SECTION(order_by);
    PRINT_SECTION(procedure);
    PRINT_SECTION(second_into);
    PRINT_SECTION(flags);
}

#define SHUTDOWN() exit(2)

void *malloc_panic(size_t size) {
    void *ptr = malloc(size);

    if (ptr == NULL) {
        SHUTDOWN();
    }

    return ptr;
}

void *realloc_panic(void *ptr, size_t size) {
    ptr = realloc(ptr, size);

    if (ptr == NULL) {
        SHUTDOWN();
    }

    return ptr;
}

static tsqlp_parse_status tsqlp_parse_mysql(const char *sql, size_t len, struct tsqlp_parse_result *parse_result) {
    struct lexer lexer = lexer_new(sql, len);
    struct parse_state parse_state = parse_state_new();

    tsqlp_parse_status status = parse_stmt(&lexer, parse_result, &parse_state);

    if (status == TSQLP_PARSE_OK && lexer_has(&lexer)) {
        status = TSQLP_PARSE_INVALID_SYNTAX;
    }

    lexer_destroy(&lexer);

    return status;
}

tsqlp_parse_status tsqlp_parse(const char *sql, size_t len, tsqlp_platform platform, struct tsqlp_parse_result *parse_result) {
    if (sql == NULL) {
        return TSQLP_PARSE_STRING_IS_NULL;
    }

    switch (platform) {
        case TSQLP_PLATFORM_MYSQL:
            return tsqlp_parse_mysql(sql, len, parse_result);
        default:
            return TSQLP_PARSE_UNKNOWN_PLATFORM;
    }
}

struct tsqlp_parse_result *tsqlp_parse_result_new() {
    struct tsqlp_parse_result *parse_result = (struct tsqlp_parse_result *) malloc(sizeof(struct tsqlp_parse_result));

    if (parse_result == NULL) {
        return NULL;
    }

    parse_result->modifiers = tsqlp_sql_section_new();
    parse_result->columns = tsqlp_sql_section_new();
    parse_result->first_into = tsqlp_sql_section_new();
    parse_result->tables = tsqlp_sql_section_new();
    parse_result->where = tsqlp_sql_section_new();
    parse_result->group_by = tsqlp_sql_section_new();
    parse_result->having = tsqlp_sql_section_new();
    parse_result->order_by = tsqlp_sql_section_new();
    parse_result->limit = tsqlp_sql_section_new();
    parse_result->procedure = tsqlp_sql_section_new();
    parse_result->second_into = tsqlp_sql_section_new();
    parse_result->flags = tsqlp_sql_section_new();

    return parse_result;
}

unsigned int tsqlp_api_version() {
    return API_VERSION;
}

void tsqlp_parse_result_free(struct tsqlp_parse_result *parse_result) {
    tsqlp_sql_section_destroy(&parse_result->modifiers);
    tsqlp_sql_section_destroy(&parse_result->columns);
    tsqlp_sql_section_destroy(&parse_result->first_into);
    tsqlp_sql_section_destroy(&parse_result->tables);
    tsqlp_sql_section_destroy(&parse_result->where);
    tsqlp_sql_section_destroy(&parse_result->group_by);
    tsqlp_sql_section_destroy(&parse_result->having);
    tsqlp_sql_section_destroy(&parse_result->order_by);
    tsqlp_sql_section_destroy(&parse_result->limit);
    tsqlp_sql_section_destroy(&parse_result->procedure);
    tsqlp_sql_section_destroy(&parse_result->second_into);
    tsqlp_sql_section_destroy(&parse_result->flags);

    free(parse_result);
}

const char *tsqlp_parse_status_to_message(tsqlp_parse_status parse_status) {
    switch (parse_status) {
        case TSQLP_PARSE_OK:
            return "PARSE_OK";
        case TSQLP_PARSE_INVALID_SYNTAX:
            return "PARSE_INVALID_SYNTAX";
        case TSQLP_PARSE_STRING_IS_NULL:
            return "PARSE_ERROR_INVALID_ARGUMENT";
        default:
            return "UNKNOWN";
    }
}
