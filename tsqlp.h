#ifndef TSQLP_H
#define TSQLP_H

#include <stdlib.h>

#include "include/tsqlp.h"

struct tsqlp_placeholders placeholders_new();

void placeholders_push(struct tsqlp_placeholders *placeholders, size_t location);

void placeholders_destroy(struct tsqlp_placeholders *placeholders);

struct tsqlp_sql_section sql_section_new();

void sql_section_destroy(struct tsqlp_sql_section *sql_section);

void sql_section_update(const char *chunk, size_t len, struct tsqlp_placeholders placeholders, struct tsqlp_sql_section *sql_section);

#endif //SQL_QUERY_PARSER_TSQLP_H
