#ifndef TSQLP_H
#define TSQLP_H

#include <stdlib.h>

#include "include/tsqlp.h"

struct tsqlp_placeholders tsqlp_placeholders_new();

void tsqlp_placeholders_push(struct tsqlp_placeholders *placeholders, size_t location);

void tsqlp_placeholders_destroy(struct tsqlp_placeholders *placeholders);

struct tsqlp_sql_section tsqlp_sql_section_new();

void tsqlp_sql_section_destroy(struct tsqlp_sql_section *sql_section);

void tsqlp_sql_section_update(const char *chunk, size_t len, struct tsqlp_placeholders placeholders, struct tsqlp_sql_section *sql_section);

#endif //SQL_QUERY_PARSER_TSQLP_H
