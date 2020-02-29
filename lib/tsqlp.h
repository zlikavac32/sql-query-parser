#ifndef SQL_QUERY_PARSER_TSQLP_H
#define SQL_QUERY_PARSER_TSQLP_H

struct placeholders {
    size_t *locations;
    size_t count;
};

struct sql_section {
    char *chunk;
    size_t len;
    struct placeholders placeholders;
};

typedef enum {
    PARSE_OK = 32000,
    PARSE_ERROR_INVALID_ARGUMENT = 32001,
    PARSE_INVALID_SYNTAX = 32002,
} parse_status_type;

struct parse_result {
    struct sql_section modifiers;
    struct sql_section columns;
    struct sql_section first_into;
    struct sql_section tables;
    struct sql_section where;
    struct sql_section group_by;
    struct sql_section having;
    struct sql_section order_by;
    struct sql_section limit;
    struct sql_section procedure;
    struct sql_section second_into;
    struct sql_section flags;
};

struct parse_result *tsqlp_parse_result_new();

parse_status_type tsqlp_parse(const char *sql, size_t len, struct parse_result *parse_result);

void tsql_parse_result_free(struct parse_result *parse_result);

const char *tsqlp_parse_status_to_message(parse_status_type status_type);

#endif //SQL_QUERY_PARSER_TSQLP_H
