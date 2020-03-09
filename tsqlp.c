#include "lexer.h"
#include "tsqlp.h"

struct parse_state {
    struct placeholders placeholders;
    int is_tracking_in_progress;
    size_t section_offset;
};

typedef enum {
    MUST_PARSE,
    TRY_PARSE
} parse_strength;

static parse_status_type
parse_stmt(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_modifiers(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_columns(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_first_into(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_tables(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type parse_partition(struct lexer *lexer);

static parse_status_type
parse_where(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_group_by(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_having(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_order_by(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_limit(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_procedure(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_second_into(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_flags(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_table_list(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_joined_table(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_table_factor(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_arithm_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_predicate_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type
parse_simple_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state);

static parse_status_type parse_alias(struct lexer *lexer);

static parse_status_type
parse_join_specification(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state,
                         parse_strength strength
);

void *malloc_panic(size_t size);

void *realloc_panic(void *ptr, size_t size);

typedef enum {
    STILL_TRACKING_PLACEHOLDERS,
    STARTED_TRACKING_PLACEHOLDERS
} parse_state_type;

struct parse_state parse_state_new();

parse_state_type parse_state_start_counting(struct parse_state *parse_state, size_t section_offset);

void parse_state_register_placeholder(struct parse_state *parse_state, size_t location);

struct placeholders parse_state_finish_counting(struct parse_state *parse_state);



struct parse_state parse_state_new() {
    return (struct parse_state) {
        .placeholders =  placeholders_new(),
        .section_offset = 0,
        .is_tracking_in_progress = 0
    };
}

parse_state_type parse_state_start_counting(struct parse_state *parse_state, size_t section_offset) {
    if (parse_state->is_tracking_in_progress) {
        return STILL_TRACKING_PLACEHOLDERS;
    }

    parse_state->is_tracking_in_progress = 1;
    parse_state->placeholders = placeholders_new();
    parse_state->section_offset = section_offset;

    return STARTED_TRACKING_PLACEHOLDERS;
}

void parse_state_register_placeholder(struct parse_state *parse_state, size_t location) {
    placeholders_push(&parse_state->placeholders, location - parse_state->section_offset);
}

struct placeholders parse_state_finish_counting(struct parse_state *parse_state) {
    if (!parse_state->is_tracking_in_progress) {
        return (struct placeholders) {
            .locations = NULL,
            .count = 0
        };
    }

    parse_state->is_tracking_in_progress = 0;

    return parse_state->placeholders;
}

#define RETURN_IF_NOT_OK(expr) \
    do { \
        parse_status_type status = expr; \
        if (status != PARSE_OK) { \
            return status; \
        } \
    } while (0)
#define RETURN_ERROR_IF_TOKEN_NOT(type, lexer) \
    do { \
        if (!token_is_of_type(type, lexer_peek(lexer))) { \
            return PARSE_INVALID_SYNTAX; \
        } \
        lexer_consume(lexer); \
    } while (0)
#define RETURN_SUCCESS_IF_TOKEN_NOT(type, lexer) \
    do { \
        if (!token_is_of_type(type, lexer_peek(lexer))) { \
            return PARSE_OK; \
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
        parse_status_type status = call; \
        \
        if (is_top_level) { \
            struct placeholders placeholders = parse_state_finish_counting(parse_state); \
            \
            if (tokens_consumed < lexer_tokens_consumed(lexer)) { \
                sql_section_update( \
                    lexer_buffer(lexer) + position, \
                    token_position(lexer_peek_previous(lexer)) + token_length(lexer_peek_previous(lexer)) - position, \
                    placeholders, \
                    &parse_result->section \
                ); \
            } \
        } \
        \
        return status; \
    } while (0)

static parse_status_type
parse_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

                return PARSE_OK;
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

                return PARSE_OK;
            }

            return PARSE_INVALID_SYNTAX;
        default:
            return PARSE_OK;
    }
}

static parse_status_type
parse_predicate_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

            return PARSE_OK;
        default:
            return PARSE_OK;
    }
}

static parse_status_type
parse_arithm_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

            return PARSE_OK;
        default:
            return PARSE_OK;
    }

    return parse_expression(lexer, parse_result, parse_state);
}

static parse_status_type
parse_simple_expression(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

            return PARSE_OK;
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

            return PARSE_OK;
        case T_K_DATE:
            // intentional
        case T_K_TIME:
            // intentional
        case T_K_TIMESTAMP:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);

            return PARSE_OK;
        case T_STRING:
            // intentional
            lexer_consume(lexer);

            if (token_is_of_type(T_K_COLLATE, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
            }

            return PARSE_OK;
        case T_PLACEHOLDER: {
            struct token token = lexer_consume(lexer);
            parse_state_register_placeholder(parse_state, token_position(&token));

            return PARSE_OK;
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

            return PARSE_OK;
        case T_K_EXISTS:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
            RETURN_IF_NOT_OK(parse_stmt(lexer, parse_result, parse_state));
            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return PARSE_OK;
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

            return PARSE_OK;
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

            return PARSE_OK;
        case T_K_CASE:
            lexer_consume(lexer);

            if (!token_is_of_type(T_K_WHEN, lexer_peek(lexer))) {
                RETURN_IF_NOT_OK(parse_expression(lexer, parse_result, parse_state));
            }

            if (!token_is_of_type(T_K_WHEN, lexer_peek(lexer))) {
                return PARSE_INVALID_SYNTAX;
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

            return PARSE_OK;
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

            return PARSE_OK;
        default:
            return PARSE_INVALID_SYNTAX;
    }
}

static parse_status_type parse_modifiers_inner(struct lexer *lexer) {
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

    return PARSE_OK;
}

static parse_status_type
parse_modifiers(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(modifiers, lexer, parse_result, parse_state, parse_modifiers_inner(lexer));
}

static parse_status_type
parse_columns_inner(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

    return PARSE_OK;
}

static parse_status_type
parse_columns(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(columns, lexer, parse_result, parse_state, parse_columns_inner(lexer, parse_result, parse_state));
}

static parse_status_type parse_first_into_inner(struct lexer *lexer) {
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

            return PARSE_OK;
        case T_K_DUMPFILE:
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_STRING, lexer);

            return PARSE_OK;
        case T_VARIABLE:
            lexer_consume(lexer);

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_VARIABLE, lexer);
            }

            return PARSE_OK;
        default:
            return PARSE_INVALID_SYNTAX;
    }
}

static parse_status_type
parse_first_into(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(first_into, lexer, parse_result, parse_state, parse_first_into_inner(lexer));
}

static parse_status_type
parse_tables(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_SUCCESS_IF_TOKEN_NOT(T_K_FROM, lexer);

    TRACK_SECTION(tables, lexer, parse_result, parse_state, parse_table_list(lexer, parse_result, parse_state));
}

static parse_status_type
parse_table_list(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    RETURN_IF_NOT_OK(parse_joined_table(lexer, parse_result, parse_state));

    while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_IF_NOT_OK(parse_joined_table(lexer, parse_result, parse_state));
    }

    return PARSE_OK;
}


static parse_status_type
parse_joined_table(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {

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
                return PARSE_OK;
        }
    }
}

static parse_status_type
parse_join_specification(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state,
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

            return PARSE_OK;
        default:
            return (strength == TRY_PARSE) ? PARSE_OK : PARSE_INVALID_SYNTAX;
    }
}

static parse_status_type
parse_table_factor(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {

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
                    return PARSE_INVALID_SYNTAX;
                }

                lexer_consume(lexer);

                while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                    lexer_consume(lexer);

                    if (!token_is_of_type(T_IDENTIFIER, lexer_peek(lexer)) &&
                        !token_is_of_type(T_QUALIFIED_IDENTIFIER, lexer_peek(lexer))) {
                        return PARSE_INVALID_SYNTAX;
                    }

                    lexer_consume(lexer);
                }

                RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

                return PARSE_OK;
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);

            while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
                lexer_consume(lexer);

                RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
            }

            RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

            return PARSE_OK;
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
                    return PARSE_INVALID_SYNTAX;
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

            return PARSE_OK;
        default:

            return PARSE_INVALID_SYNTAX;
    }
}

static parse_status_type parse_alias(struct lexer *lexer) {
    if (token_is_of_type(T_K_AS, lexer_peek(lexer)) || token_is_of_type(T_IDENTIFIER, lexer_peek(lexer))) {
        struct token token = lexer_consume(lexer);

        if (token_is_of_type(T_K_AS, &token)) {
            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
        }
    }

    return PARSE_OK;
}

static parse_status_type parse_partition(struct lexer *lexer) {
    if (token_is_of_type(T_K_PARTITION, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_OPEN_PAREN, lexer);
        RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);

        while (token_is_of_type(T_COMMA, lexer_peek(lexer))) {
            lexer_consume(lexer);

            RETURN_ERROR_IF_TOKEN_NOT(T_IDENTIFIER, lexer);
        }
        RETURN_ERROR_IF_TOKEN_NOT(T_CLOSE_PAREN, lexer);

        return PARSE_OK;
    }

    return PARSE_OK;
}

static parse_status_type
parse_where(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_WHERE, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(where, lexer, parse_result, parse_state, parse_expression(lexer, parse_result, parse_state));
    }

    return PARSE_OK;
}

static parse_status_type parse_group_by_inner(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

    return PARSE_OK;
}

static parse_status_type
parse_group_by(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_GROUP, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);

        TRACK_SECTION(group_by, lexer, parse_result, parse_state, parse_group_by_inner(lexer, parse_result, parse_state));
    }

    return PARSE_OK;
}

static parse_status_type
parse_having(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_HAVING, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(having, lexer, parse_result, parse_state, parse_expression(lexer, parse_result, parse_state));
    }

    return PARSE_OK;
}


static parse_status_type parse_order_by_inner(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

    return PARSE_OK;
}

static parse_status_type
parse_order_by(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_ORDER, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_BY, lexer);

        TRACK_SECTION(order_by, lexer, parse_result, parse_state, parse_order_by_inner(lexer, parse_result, parse_state));
    }

    return PARSE_OK;
}

static parse_status_type parse_limit_inner(struct lexer *lexer, struct parse_state *parse_state) {
    if (!token_is_of_type(T_NUMBER, lexer_peek(lexer)) && !token_is_of_type(T_PLACEHOLDER, lexer_peek(lexer))) {
        return PARSE_INVALID_SYNTAX;
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
            return PARSE_INVALID_SYNTAX;
        }

        if (token_is_of_type(T_PLACEHOLDER, lexer_peek(lexer))) {
            parse_state_register_placeholder(parse_state, token_position(lexer_peek(lexer)));
        }

        lexer_consume(lexer);
    }

    return PARSE_OK;
}

static parse_status_type
parse_limit(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_LIMIT, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(limit, lexer, parse_result, parse_state, parse_limit_inner(lexer, parse_state));
    }

    return PARSE_OK;
}

static parse_status_type
parse_procedure_inner(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

    return PARSE_OK;
}

static parse_status_type
parse_procedure(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    if (token_is_of_type(T_K_PROCEDURE, lexer_peek(lexer))) {
        lexer_consume(lexer);

        TRACK_SECTION(procedure, lexer, parse_result, parse_state,
                      parse_procedure_inner(lexer, parse_result, parse_state));
    }

    return PARSE_OK;
}

static parse_status_type
parse_second_into(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(second_into, lexer, parse_result, parse_state, parse_first_into_inner(lexer));
}

static parse_status_type parse_flags_inner(struct lexer *lexer) {
    if (token_is_of_type(T_K_FOR, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_UPDATE, lexer);
    } else if (token_is_of_type(T_K_LOCK, lexer_peek(lexer))) {
        lexer_consume(lexer);

        RETURN_ERROR_IF_TOKEN_NOT(T_K_IN, lexer);
        RETURN_ERROR_IF_TOKEN_NOT(T_K_SHARE, lexer);
        RETURN_ERROR_IF_TOKEN_NOT(T_K_MODE, lexer);
    }

    return PARSE_OK;
}

static parse_status_type
parse_flags(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
    TRACK_SECTION(flags, lexer, parse_result, parse_state, parse_flags_inner(lexer));
}

static parse_status_type
parse_stmt(struct lexer *lexer, struct parse_result *parse_result, struct parse_state *parse_state) {
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

    return PARSE_OK;
}

const char *parse_status_type_to_string(parse_status_type status_type) {
    switch (status_type) {
        case PARSE_OK:
            return "PARSE_OK";
        case PARSE_INVALID_SYNTAX:
            return "PARSE_INVALID_SYNTAX";
        case PARSE_ERROR_INVALID_ARGUMENT:
            return "PARSE_ERROR_INVALID_ARGUMENT";
        default:
            return "UNKNOWN";
    }
}

struct placeholders placeholders_new() {
    return (struct placeholders) {
        .locations = NULL,
        .count = 0
    };
}

void placeholders_push(struct placeholders *placeholders, size_t location) {
    placeholders->locations = (size_t *) realloc_panic(placeholders->locations,
                                                       (placeholders->count + 1) * sizeof(size_t));
    placeholders->locations[placeholders->count++] = location;
}

int placeholders_count(const struct placeholders *placeholders) {
    return placeholders->count;
}

size_t placeholders_position_at(const struct placeholders *placeholders, unsigned int index) {
    if (index < placeholders->count) {
        return placeholders->locations[index];
    }

    return 0;
}

void placeholders_destroy(struct placeholders *placeholders) {
    if (placeholders->locations != NULL) {
        free(placeholders->locations);
    }
}

struct sql_section sql_section_new() {
    return (struct sql_section) {
        .chunk = NULL,
        .len = 0,
        .placeholders = {
            .locations = NULL,
            .count = 0
        }
    };
}

size_t sql_section_length(const struct sql_section *sql_section) {
    return sql_section->len;
}

const char *sql_section_content(const struct sql_section *sql_section) {
    return sql_section->chunk;
}

int sql_section_is_populated(const struct sql_section *sql_section) {
    return sql_section->len > 0;
}

struct placeholders *sql_section_placeholders(struct sql_section *sql_section) {
    return &sql_section->placeholders;
}

void
sql_section_update(const char *chunk, size_t len, struct placeholders placeholders, struct sql_section *sql_section) {

    // @todo: remove +1 and null character when tests don't print using %s
    char *buff = (char *) malloc_panic(sizeof(char) * (len + 1));
    buff[len] = '\0';

    memcpy(buff, chunk, len);

    *sql_section = (struct sql_section) {
        .chunk = buff,
        .len = len,
        .placeholders = placeholders
    };

}

void sql_section_destroy(struct sql_section *sql_section) {
    if (sql_section->chunk == NULL) {
        return;
    }

    free(sql_section->chunk);

    placeholders_destroy(&sql_section->placeholders);
}

struct parse_result parse_result_new() {
    struct parse_result parse_result;

    parse_result.modifiers = sql_section_new();
    parse_result.columns = sql_section_new();
    parse_result.first_into = sql_section_new();
    parse_result.tables = sql_section_new();
    parse_result.where = sql_section_new();
    parse_result.group_by = sql_section_new();
    parse_result.having = sql_section_new();
    parse_result.order_by = sql_section_new();
    parse_result.limit = sql_section_new();
    parse_result.procedure = sql_section_new();
    parse_result.second_into = sql_section_new();
    parse_result.flags = sql_section_new();

    return parse_result;
}


void parse_result_destroy(struct parse_result *parse_result) {
    sql_section_destroy(&parse_result->modifiers);
    sql_section_destroy(&parse_result->columns);
    sql_section_destroy(&parse_result->first_into);
    sql_section_destroy(&parse_result->tables);
    sql_section_destroy(&parse_result->where);
    sql_section_destroy(&parse_result->group_by);
    sql_section_destroy(&parse_result->having);
    sql_section_destroy(&parse_result->order_by);
    sql_section_destroy(&parse_result->limit);
    sql_section_destroy(&parse_result->procedure);
    sql_section_destroy(&parse_result->second_into);
    sql_section_destroy(&parse_result->flags);
}

void parse_result_serialize(struct parse_result *parse_result, FILE *file) {

#define PRINT_SECTION(section) \
    do { \
        if (sql_section_is_populated(&parse_result->section)) { \
            struct placeholders *placeholders = sql_section_placeholders(&parse_result->section); \
            size_t count = placeholders_count(placeholders); \
            fprintf(file, "%s %ld ", #section, count); \
            \
            for (int i = 0; i < count; i++) { \
                fprintf(file, "%ld ", placeholders_position_at(placeholders, i)); \
            } \
            \
            fprintf(file, "%ld ", sql_section_length(&parse_result->section)); \
            fwrite(sql_section_content(&parse_result->section), sql_section_length(&parse_result->section), 1, file); \
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

parse_status_type tsqlp_parse(const char *sql, size_t len, struct parse_result *parse_result) {
    if (sql == NULL) {
        return PARSE_ERROR_INVALID_ARGUMENT;
    }

    struct lexer lexer = lexer_new(sql, len);
    struct parse_state parse_state = parse_state_new();

    parse_status_type status = parse_stmt(&lexer, parse_result, &parse_state);

    if (status == PARSE_OK && lexer_has(&lexer)) {
        status = PARSE_INVALID_SYNTAX;
    }

    lexer_destroy(&lexer);

    return status;
}

struct parse_result *tsqlp_parse_result_new() {
    struct parse_result *parse_result = (struct parse_result *) malloc(sizeof(struct parse_result));

    if (parse_result == NULL) {
        return NULL;
    }

    struct parse_result initialized_parse_result = parse_result_new();

    memcpy(parse_result, &initialized_parse_result, sizeof(struct parse_result));

    return parse_result;
}

unsigned int tsqlp_api_version() {
    return API_VERSION;
}

void tsql_parse_result_free(struct parse_result *parse_result) {
    parse_result_destroy(parse_result);
    free(parse_result);
}

extern const char *tsqlp_parse_status_to_message(parse_status_type status_type) {
    return parse_status_type_to_string(status_type);
}
