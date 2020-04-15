#ifndef SQL_QUERY_PARSER_TSQLP_H
#define SQL_QUERY_PARSER_TSQLP_H

#define API_VERSION 1

#include <stdio.h>

typedef enum {
    TSQLP_PARSE_OK = 32000,
    TSQLP_PARSE_ERROR_INVALID_ARGUMENT = 32001,
    TSQLP_PARSE_INVALID_SYNTAX = 32002,
} tsqlp_parse_status;

struct tsqlp_placeholders {
    size_t *locations;
    size_t count;
};

struct tsqlp_sql_section {
    char *chunk;
    size_t len;
    struct tsqlp_placeholders placeholders;
};

struct tsqlp_parse_result {
    struct tsqlp_sql_section modifiers;
    struct tsqlp_sql_section columns;
    struct tsqlp_sql_section first_into;
    struct tsqlp_sql_section tables;
    struct tsqlp_sql_section where;
    struct tsqlp_sql_section group_by;
    struct tsqlp_sql_section having;
    struct tsqlp_sql_section order_by;
    struct tsqlp_sql_section limit;
    struct tsqlp_sql_section procedure;
    struct tsqlp_sql_section second_into;
    struct tsqlp_sql_section flags;
};

struct tsqlp_parse_result *tsqlp_parse_result_new();

tsqlp_parse_status tsqlp_parse(const char *sql, size_t len, struct tsqlp_parse_result *parse_result);

void tsqlp_parse_result_free(struct tsqlp_parse_result *parse_result);

const char *tsqlp_parse_status_to_message(tsqlp_parse_status parse_status);

void tsqlp_parse_result_serialize(struct tsqlp_parse_result *parse_result, FILE *file);

unsigned int tsqlp_api_version();

#endif //SQL_QUERY_PARSER_TSQLP_H
