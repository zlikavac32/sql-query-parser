#ifndef TSQLP_H
#define TSQLP_H

#include <stdlib.h>

#include "lib/tsqlp.h"

struct tsqlp_placeholders placeholders_new();

int placeholders_count(const struct tsqlp_placeholders *placeholders);

size_t placeholders_position_at(const struct tsqlp_placeholders *placeholders, unsigned int index);

void placeholders_push(struct tsqlp_placeholders *placeholders, size_t location);

void placeholders_destroy(struct tsqlp_placeholders *placeholders);

struct tsqlp_sql_section sql_section_new();

void sql_section_destroy(struct tsqlp_sql_section *sql_section);

struct tsqlp_placeholders *sql_section_placeholders(struct tsqlp_sql_section *sql_section);

int sql_section_is_populated(const struct tsqlp_sql_section *sql_section);

size_t sql_section_length(const struct tsqlp_sql_section *sql_section);

const char *sql_section_content(const struct tsqlp_sql_section *sql_section);

void sql_section_update(const char *chunk, size_t len, struct tsqlp_placeholders placeholders, struct tsqlp_sql_section *sql_section);

#endif //SQL_QUERY_PARSER_TSQLP_H
