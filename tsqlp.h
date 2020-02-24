#ifndef TSQLP_H
#define TSQLP_H

#include <stdlib.h>
#include <stdio.h>

#include "lib/tsqlp.h"

typedef enum {
    SECTION_MODIFIERS = 1,
    SECTION_COLUMNS,
    SECTION_FIRST_INTO,
    SECTION_TABLES,
    SECTION_WHERE,
    SECTION_GROUP_BY,
    SECTION_HAVING,
    SECTION_ORDER_BY,
    SECTION_LIMIT,
    SECTION_PROCEDURE,
    SECTION_SECOND_INTO,
    SECTION_FLAGS,
} sql_section_type;

const char *parse_status_type_to_string(parse_status_type status_type);

struct placeholders placeholders_new();

int placeholders_count(const struct placeholders *placeholders);

size_t placeholders_position_at(const struct placeholders *placeholders, unsigned int index);

void placeholders_push(struct placeholders *placeholders, size_t location);

void placeholders_destroy(struct placeholders *placeholders);

struct parse_result parse_result_new();

void parse_result_serialize(struct parse_result *parse_result, FILE *file);

void parse_result_destroy(struct parse_result *parse_result);

struct sql_section sql_section_new();

void sql_section_destroy(struct sql_section *sql_section);

struct placeholders *sql_section_placeholders(struct sql_section *sql_section);

int sql_section_is_populated(const struct sql_section *sql_section);

size_t sql_section_length(const struct sql_section *sql_section);

const char *sql_section_content(const struct sql_section *sql_section);

void sql_section_update(const char *chunk, size_t len, struct placeholders placeholders, struct sql_section *sql_section);

#endif //SQL_QUERY_PARSER_TSQLP_H