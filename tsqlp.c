#include <string.h>

#include "tsqlp.h"

static void *malloc_panic(size_t size);

static void *realloc_panic(void *ptr, size_t size);

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

extern tsqlp_parse_status tsqlp_parse_mysql(const char *sql, size_t len, struct tsqlp_parse_result *parse_result);

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
