#include <criterion/criterion.h>
#include <string.h>
#include <stdarg.h>

#include "tsqlp.h"

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

Test(tsqlp_parse, error_is_returned_when_sql_is_null) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(tsqlp_parse(NULL, 0, TSQLP_PLATFORM_MYSQL, parse_result), TSQLP_PARSE_ERROR_INVALID_ARGUMENT);

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, error_is_returned_when_platform_is_unknown) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(tsqlp_parse("", 0, 3264, parse_result), TSQLP_PARSE_UNKNOWN_PLATFORM);

    tsqlp_parse_result_free(parse_result);
}

void
parse_result_init_section(sql_section_type section_name, struct tsqlp_sql_section section, struct tsqlp_parse_result *parse_result) {

#define MAKE_SECTION(name, section_property) \
    do { \
        if (section_name == name) { \
            parse_result->section_property = section; \
            \
            return ; \
        } \
    } while (0)

    MAKE_SECTION(SECTION_MODIFIERS, modifiers);
    MAKE_SECTION(SECTION_COLUMNS, columns);
    MAKE_SECTION(SECTION_FIRST_INTO, first_into);
    MAKE_SECTION(SECTION_TABLES, tables);
    MAKE_SECTION(SECTION_WHERE, where);
    MAKE_SECTION(SECTION_GROUP_BY, group_by);
    MAKE_SECTION(SECTION_HAVING, having);
    MAKE_SECTION(SECTION_ORDER_BY, order_by);
    MAKE_SECTION(SECTION_LIMIT, limit);
    MAKE_SECTION(SECTION_PROCEDURE, procedure);
    MAKE_SECTION(SECTION_SECOND_INTO, second_into);
    MAKE_SECTION(SECTION_FLAGS, flags);
}

struct tsqlp_parse_result *make_parse_result(sql_section_type section_name, struct tsqlp_sql_section section, ...) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    parse_result_init_section(section_name, section, parse_result);

    va_list arg_pointer;
    va_start(arg_pointer, section);
    while (1) {
        section_name = va_arg(arg_pointer, sql_section_type);

        if (section_name == 0) {
            break;
        }

        parse_result_init_section(section_name, va_arg(arg_pointer, struct tsqlp_sql_section), parse_result);
    }
    va_end(arg_pointer);

    return parse_result;
}

static struct tsqlp_sql_section sql_section_new_from_string(char *chunk, size_t placeholder_count, ...) {
    struct tsqlp_sql_section section = tsqlp_sql_section_new();

    struct tsqlp_placeholders placeholders = tsqlp_placeholders_new();

    va_list arg_pointer;
    va_start(arg_pointer, placeholder_count);
    for (int i = 0; i < placeholder_count; i++) {
        size_t location = va_arg(arg_pointer, size_t);

        tsqlp_placeholders_push(&placeholders, location);
    }
    va_end(arg_pointer);

    tsqlp_sql_section_update(chunk, strlen(chunk), placeholders, &section);

    return section;
}

void assert_parse_result_eq(struct tsqlp_parse_result *got, struct tsqlp_parse_result *expected) {
#define ASSERT_SECTION(section) \
    do { \
        cr_assert_eq(got->section.placeholders.count, expected->section.placeholders.count); \
        for (int i = 0; i < expected->section.placeholders.count; i++) { \
            cr_assert_eq(got->section.placeholders.locations[i], expected->section.placeholders.locations[i], "Expected %ld, got %ld on index %d", expected->section.placeholders.locations[i], got->section.placeholders.locations[i], i); \
        } \
        cr_assert_eq(got->section.len, expected->section.len, "Expected %ld, got %ld", expected->section.len, got->section.len); \
        cr_assert_eq(memcmp(got->section.chunk, expected->section.chunk, expected->section.len), 0, "Expected \"%s\", got \"%s\"", expected->section.chunk, got->section.chunk); \
    } while (0)

    ASSERT_SECTION(modifiers);
    ASSERT_SECTION(columns);
    ASSERT_SECTION(first_into);
    ASSERT_SECTION(tables);
    ASSERT_SECTION(where);
    ASSERT_SECTION(group_by);
    ASSERT_SECTION(having);
    ASSERT_SECTION(order_by);
    ASSERT_SECTION(limit);
    ASSERT_SECTION(procedure);
    ASSERT_SECTION(second_into);
    ASSERT_SECTION(flags);

    tsqlp_parse_result_free(expected);
}

#define PARSE_SQL_STR(sql, parse_result) tsqlp_parse(sql, strlen(sql), TSQLP_PLATFORM_MYSQL, parse_result)

Test(tsqlp_parse, invalid_syntax) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT ", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1, ", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT ??", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT \"\\\"", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT '\\'", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT d.d.d.d", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT INTERVAL 3", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT CASE 1 THEN", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT MATCH(f) AGAINST", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 +", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 ORDER BY ", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 GROUP BY ", parse_result), TSQLP_PARSE_INVALID_SYNTAX);

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, select_a_number) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, case_insensitive) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("seLEcT 1", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, select_a_placeholder) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT ?", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("?", 1, 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}


Test(tsqlp_parse, select_a_comma_delimited_columns) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1, ?, 22, ?", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1, ?, 22, ?", 2, 3, 10),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, select_subquery) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT ?, (SELECT ?, (SELECT ?, 1))", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("?, (SELECT ?, (SELECT ?, 1))", 3, 0, 11, 22),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, query_modifiers) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT ALL 1", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_MODIFIERS, sql_section_new_from_string("ALL", 0),
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT DISTINCT SQL_BUFFER_RESULT 1", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_MODIFIERS, sql_section_new_from_string("DISTINCT SQL_BUFFER_RESULT", 0),
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT DISTINCTROW HIGH_PRIORITY 1", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_MODIFIERS, sql_section_new_from_string("DISTINCTROW HIGH_PRIORITY", 0),
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
    PARSE_SQL_STR(
            "SELECT HIGH_PRIORITY STRAIGHT_JOIN SQL_SMALL_RESULT SQL_BIG_RESULT SQL_CACHE 1", parse_result
        ),
    TSQLP_PARSE_OK
    );

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_MODIFIERS,sql_section_new_from_string("HIGH_PRIORITY STRAIGHT_JOIN SQL_SMALL_RESULT SQL_BIG_RESULT SQL_CACHE", 0),
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, unary_operators) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
    PARSE_SQL_STR(
            "SELECT +1, -1, ~1, !1, BINARY 2, BINARY -2, NOT 2", parse_result
        ),
    TSQLP_PARSE_OK
    );

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("+1, -1, ~1, !1, BINARY 2, BINARY -2, NOT 2", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, expression_in_parenthesis) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1, (1), ((1)), (((+1))), (((?)))", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1, (1), ((1)), (((+1))), (((?)))", 1, 28),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}


Test(tsqlp_parse, exists) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT EXISTS (SELECT ?)", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("EXISTS (SELECT ?)", 1, 15),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, literals) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
    PARSE_SQL_STR(
            "SELECT 1, NULL, TRUE, FALSE, b'0', b'1', b'101001', 0xa, 0xA, x'1b'",
            parse_result
        ),
    TSQLP_PARSE_OK
    );

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS,
            sql_section_new_from_string("1, NULL, TRUE, FALSE, b'0', b'1', b'101001', 0xa, 0xA, x'1b'", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
    PARSE_SQL_STR(
            "SELECT 0.0, .0, 0., 00.00, 1e12, .1e-12, 1.1e+12, 2.2e1",
            parse_result
        ),
    TSQLP_PARSE_OK
    );

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("0.0, .0, 0., 00.00, 1e12, .1e-12, 1.1e+12, 2.2e1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR(
        "SELECT '', 'foo', '\\'', 'f \\' ', \"\", \"foo\", \"\\\"\", \"f \\\" \", 'a''b', 'a'  'b', \"a\"\"b\", \"a\"  \"b\"",
        parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("'', 'foo', '\\'', 'f \\' ', \"\", \"foo\", \"\\\"\", \"f \\\" \", 'a''b', 'a'  'b', \"a\"\"b\", \"a\"  \"b\"",0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    // @todo: check if these multiple charset literals are valid
    cr_assert_eq(PARSE_SQL_STR("SELECT utf8'f' utf8'g' 'c', utf8\"f\" utf8\"c\"", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("utf8'f' utf8'g' 'c', utf8\"f\" utf8\"c\"", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT '' COLLATE demo, '' COLLATE bar", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("'' COLLATE demo, '' COLLATE bar", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT DATE 'd', TIME 'time', TIMESTAMP 'timestamp'", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("DATE 'd', TIME 'time', TIMESTAMP 'timestamp'", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, colum_name) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT d, `d`, `d.d`, `*`, *, d.d, d.d.d, d.*, d.d.*, _d, $d", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("d, `d`, `d.d`, `*`, *, d.d, d.d.d, d.*, d.d.*, _d, $d", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}


Test(tsqlp_parse, interval_expression) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
        PARSE_SQL_STR("SELECT INTERVAL 3 YEAR, INTERVAL -3 YEAR_MONTH, INTERVAL (SELECT 1) DAY", parse_result),
        TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS,
            sql_section_new_from_string("INTERVAL 3 YEAR, INTERVAL -3 YEAR_MONTH, INTERVAL (SELECT 1) DAY", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, case_expression) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR(
        "SELECT CASE 1 WHEN 1 THEN 2 END, CASE WHEN 1 THEN 2 END, CASE 1 WHEN 1 THEN 2 WHEN 3 THEN 4 ELSE 5 END, CASE WHEN 1 THEN 2 WHEN 3 THEN 4 ELSE 5 END",
        parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("CASE 1 WHEN 1 THEN 2 END, CASE WHEN 1 THEN 2 END, CASE 1 WHEN 1 THEN 2 WHEN 3 THEN 4 ELSE 5 END, CASE WHEN 1 THEN 2 WHEN 3 THEN 4 ELSE 5 END",0),
                NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, match_against) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    const char *sql = "SELECT "
                      "MATCH(f) AGAINST ('c'), "
                      "MATCH(f, b) AGAINST ('c' WITH QUERY EXPANSION), "
                      "MATCH(f) AGAINST ('c' IN BOOLEAN MODE), "
                      "MATCH(f) AGAINST ('c' IN NATURAL LANGUAGE MODE WITH QUERY EXPANSION), "
                      "MATCH(f) AGAINST ('c' IN NATURAL LANGUAGE MODE)";

    cr_assert_eq(PARSE_SQL_STR(sql, parse_result), TSQLP_PARSE_OK);

    char *expected = "MATCH(f) AGAINST ('c'), "
                     "MATCH(f, b) AGAINST ('c' WITH QUERY EXPANSION), "
                     "MATCH(f) AGAINST ('c' IN BOOLEAN MODE), "
                     "MATCH(f) AGAINST ('c' IN NATURAL LANGUAGE MODE WITH QUERY EXPANSION), "
                     "MATCH(f) AGAINST ('c' IN NATURAL LANGUAGE MODE)";

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string(expected, 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, procedure_call) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT F(), f(), f(1), f(1, NULL, 'str'), f((SELECT 1))", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("F(), f(), f(1), f(1, NULL, 'str'), f((SELECT 1))", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, row_statement) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT ROW (1), ROW ((SELECT 1), 2)", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("ROW (1), ROW ((SELECT 1), 2)", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, expression_list) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT (1), (1, 2, (SELECT 1))", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("(1), (1, 2, (SELECT 1))", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, arithmetic_and_bitwise_expression) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR(
        "SELECT 1 | 2, 1 & 2, 1 << 2, 1 >> 2, (1) + 2, 1 - 2, 1 * 2, 1 / 2, 1 DIV 2, 1 MOD 2, 1 % 2, 1 ^ 2, 2 COLLATE demo",
        parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1 | 2, 1 & 2, 1 << 2, 1 >> 2, (1) + 2, 1 - 2, 1 * 2, 1 / 2, 1 DIV 2, 1 MOD 2, 1 % 2, 1 ^ 2, 2 COLLATE demo",0),
                NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, predicate_expression) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    const char *sql = "SELECT "
                      "'bar' SOUNDS LIKE 'foo', "
                      "c REGEXP `b`, "
                      "c NOT REGEXP `b`, "
                      "5 BETWEEN 1 AND (2), "
                      "c NOT BETWEEN (SELECT 1) AND (2), "
                      "c LIKE f(), "
                      "c NOT LIKE (1 + 1), "
                      "c LIKE 'str' ESCAPE bar, "
                      "c IN (1), "
                      "c NOT IN (1, 2, 3), "
                      "c IN (SELECT 1)";

    cr_assert_eq(PARSE_SQL_STR(sql, parse_result), TSQLP_PARSE_OK);

    char *expected = "'bar' SOUNDS LIKE 'foo', "
                     "c REGEXP `b`, "
                     "c NOT REGEXP `b`, "
                     "5 BETWEEN 1 AND (2), "
                     "c NOT BETWEEN (SELECT 1) AND (2), "
                     "c LIKE f(), "
                     "c NOT LIKE (1 + 1), "
                     "c LIKE 'str' ESCAPE bar, "
                     "c IN (1), "
                     "c NOT IN (1, 2, 3), "
                     "c IN (SELECT 1)";

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string(expected, 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}


Test(tsqlp_parse, expression) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    const char *sql = "SELECT "
                      "NOT 2, "
                      "! 2, "
                      "a IS TRUE, "
                      "b IS FALSE, "
                      "c IS UNKNOWN, "
                      "d IS NULL, "
                      "NULL IS NOT NULL, "
                      "1 OR 2, "
                      "2 || (3), "
                      "(1 + 2) XOR 4, "
                      "3 AND 4, "
                      "'f' && 'd', "
                      "'f' <=> 'd', "
                      "g < ALL (SELECT 1), "
                      "g > ANY (SELECT 2), "
                      "1 < 2, "
                      "1 <= 2, "
                      "2 > 3, "
                      "2 >= 3, "
                      "3 <=> 5, "
                      "5 = 9, "
                      "6 != 0";

    cr_assert_eq(PARSE_SQL_STR(sql, parse_result), TSQLP_PARSE_OK);

    char *expected = "NOT 2, "
                     "! 2, "
                     "a IS TRUE, "
                     "b IS FALSE, "
                     "c IS UNKNOWN, "
                     "d IS NULL, "
                     "NULL IS NOT NULL, "
                     "1 OR 2, "
                     "2 || (3), "
                     "(1 + 2) XOR 4, "
                     "3 AND 4, "
                     "'f' && 'd', "
                     "'f' <=> 'd', "
                     "g < ALL (SELECT 1), "
                     "g > ANY (SELECT 2), "
                     "1 < 2, "
                     "1 <= 2, "
                     "2 > 3, "
                     "2 >= 3, "
                     "3 <=> 5, "
                     "5 = 9, "
                     "6 != 0";

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string(expected, 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, column_alias) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 demo, 1 AS demo, SUM(1 + 2) `bar`", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1 demo, 1 AS demo, SUM(1 + 2) `bar`", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, first_into) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 INTO DUMPFILE 'bar'", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FIRST_INTO, sql_section_new_from_string("INTO DUMPFILE 'bar'", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 INTO @var", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FIRST_INTO, sql_section_new_from_string("INTO @var", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 INTO @var, @other_var, @also_var", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FIRST_INTO, sql_section_new_from_string("INTO @var, @other_var, @also_var", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 INTO OUTFILE 'bar'", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FIRST_INTO, sql_section_new_from_string("INTO OUTFILE 'bar'", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 INTO OUTFILE 'bar' CHARACTER SET demo", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FIRST_INTO, sql_section_new_from_string("INTO OUTFILE 'bar' CHARACTER SET demo", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
        PARSE_SQL_STR("SELECT 1 INTO OUTFILE 'bar' FIELDS TERMINATED BY 'd' LINES STARTING BY 'g'", parse_result),
        TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FIRST_INTO, sql_section_new_from_string("INTO OUTFILE 'bar' FIELDS TERMINATED BY 'd' LINES STARTING BY 'g'", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR(
        "SELECT 1 INTO OUTFILE 'bar' COLUMNS TERMINATED BY 'd' OPTIONALLY ENCLOSED BY 'g' ESCAPED BY 'f' LINES STARTING BY 'g' TERMINATED BY 'h'",
        parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FIRST_INTO, sql_section_new_from_string(
                    "INTO OUTFILE 'bar' COLUMNS TERMINATED BY 'd' OPTIONALLY ENCLOSED BY 'g' ESCAPED BY 'f' LINES STARTING BY 'g' TERMINATED BY 'h'", 0),
                    NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}


Test(tsqlp_parse, from_table) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    const char *sql = "SELECT 1 FROM "
                      "t, "
                      "t.t, "
                      "`t`, "
                      "(t, `t`), "
                      "(SELECT 2 FROM t), "
                      "(SELECT 1) AS g, "
                      "(SELECT 1) `g`, "
                      "(SELECT 1) t, "
                      "(SELECT 1) t (t, `t`), "
                      "t PARTITION (p1, p2), "
                      "t AS t, "
                      "t t, "
                      "a USE INDEX (t), "
                      "b USE INDEX FOR JOIN (c), "
                      "c USE INDEX (a, b), "
                      "d AS c USE INDEX (a, b), USE INDEX FOR JOIN (g), "
                      "d USE INDEX FOR ORDER BY (g), "
                      "d USE INDEX FOR GROUP BY (g), "
                      "d FORCE KEY (g), "
                      "d IGNORE KEY (g), "
                      "d FORCE INDEX FOR GROUP BY (g), "
                      "d FORCE KEY FOR GROUP BY (g)";

    cr_assert_eq(PARSE_SQL_STR(sql, parse_result), TSQLP_PARSE_OK);

    char *expected = "t, "
                     "t.t, "
                     "`t`, "
                     "(t, `t`), "
                     "(SELECT 2 FROM t), "
                     "(SELECT 1) AS g, "
                     "(SELECT 1) `g`, "
                     "(SELECT 1) t, "
                     "(SELECT 1) t (t, `t`), "
                     "t PARTITION (p1, p2), "
                     "t AS t, "
                     "t t, "
                     "a USE INDEX (t), "
                     "b USE INDEX FOR JOIN (c), "
                     "c USE INDEX (a, b), "
                     "d AS c USE INDEX (a, b), USE INDEX FOR JOIN (g), "
                     "d USE INDEX FOR ORDER BY (g), "
                     "d USE INDEX FOR GROUP BY (g), "
                     "d FORCE KEY (g), "
                     "d IGNORE KEY (g), "
                     "d FORCE INDEX FOR GROUP BY (g), "
                     "d FORCE KEY FOR GROUP BY (g)";

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string(expected, 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}


Test(tsqlp_parse, join_table) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    const char *sql = "SELECT 1 FROM "
                      "t NATURAL JOIN b, "
                      "t NATURAL INNER JOIN b, "
                      "t NATURAL LEFT JOIN b, "
                      "t NATURAL RIGHT JOIN b, "
                      "t NATURAL RIGHT OUTER JOIN b, "
                      "t LEFT JOIN t ON 1 = ?, "
                      "t RIGHT JOIN t ON 1 = `t`, "
                      "t RIGHT OUTER JOIN t ON 1 = `t`, "
                      "t LEFT JOIN t USING (a, `b`), "
                      "t JOIN t, "
                      "t JOIN t t, "
                      "t JOIN t ON ?, "
                      "t STRAIGHT JOIN t, "
                      "t INNER JOIN t, "
                      "t CROSS JOIN t, "
                      "t CROSS JOIN (SELECT 1) b, "
                      "t JOIN other AS t1 ON 2 = d LEFT JOIN other ON 2, "
                      "t NATURAL JOIN b NATURAL JOIN c";

    cr_assert_eq(PARSE_SQL_STR(sql, parse_result), TSQLP_PARSE_OK);

    char *expected = "t NATURAL JOIN b, "
                     "t NATURAL INNER JOIN b, "
                     "t NATURAL LEFT JOIN b, "
                     "t NATURAL RIGHT JOIN b, "
                     "t NATURAL RIGHT OUTER JOIN b, "
                     "t LEFT JOIN t ON 1 = ?, "
                     "t RIGHT JOIN t ON 1 = `t`, "
                     "t RIGHT OUTER JOIN t ON 1 = `t`, "
                     "t LEFT JOIN t USING (a, `b`), "
                     "t JOIN t, "
                     "t JOIN t t, "
                     "t JOIN t ON ?, "
                     "t STRAIGHT JOIN t, "
                     "t INNER JOIN t, "
                     "t CROSS JOIN t, "
                     "t CROSS JOIN (SELECT 1) b, "
                     "t JOIN other AS t1 ON 2 = d LEFT JOIN other ON 2, "
                     "t NATURAL JOIN b NATURAL JOIN c";

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string(expected, 2, 140, 267),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, where) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t WHERE a = 1 AND b = (SELECT ?)", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_WHERE, sql_section_new_from_string("a = 1 AND b = (SELECT ?)", 1, 22),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, group_by) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t GROUP BY f, `f` ASC, f.f DESC, 2, ?, ? ASC, SUM(1)", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_GROUP_BY, sql_section_new_from_string("f, `f` ASC, f.f DESC, 2, ?, ? ASC, SUM(1)", 2, 25, 28),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, having) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t HAVING a = 1 AND b = (SELECT ?)", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_HAVING, sql_section_new_from_string("a = 1 AND b = (SELECT ?)", 1, 22),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, order_by) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t ORDER BY f, `f` ASC, f.f DESC, 2, ?, SUM(1)", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_ORDER_BY, sql_section_new_from_string("f, `f` ASC, f.f DESC, 2, ?, SUM(1)", 1, 25),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, limit) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t LIMIT 1", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_LIMIT, sql_section_new_from_string("1", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t LIMIT ? 1", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_LIMIT, sql_section_new_from_string("? 1", 1, 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t LIMIT ? OFFSET ?", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_LIMIT, sql_section_new_from_string("? OFFSET ?", 2, 0, 9),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t LIMIT 2, 4", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_LIMIT, sql_section_new_from_string("2, 4", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, procedure) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t PROCEDURE a()", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_PROCEDURE, sql_section_new_from_string("a()", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t PROCEDURE a(?, 1 + ?, (SELECT 1))", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_PROCEDURE, sql_section_new_from_string("a(?, 1 + ?, (SELECT 1))", 2, 2, 9),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, second_into) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FROM t INTO DUMPFILE 'bar'", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_TABLES, sql_section_new_from_string("t", 0),
            SECTION_SECOND_INTO, sql_section_new_from_string("INTO DUMPFILE 'bar'", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, flags) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 FOR UPDATE", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FLAGS, sql_section_new_from_string("FOR UPDATE", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(PARSE_SQL_STR("SELECT 1 LOCK IN SHARE MODE", parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_COLUMNS, sql_section_new_from_string("1", 0),
            SECTION_FLAGS, sql_section_new_from_string("LOCK IN SHARE MODE", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, complete_example) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    const char *sql = "SELECT DISTINCT HIGH_PRIORITY "
                      "id, PI() pi, (SELECT COUNT(*) c FROM l WHERE g = ?) AS bar "
                      "FROM table t JOIN other AS o ON 1 = o LEFT JOIN also ON ? = ? "
                      "WHERE some = expression "
                      "GROUP BY col ASC, ? DESC, 4 "
                      "HAVING c > 5 "
                      "ORDER BY 2, 4 ASC, `d` DESC "
                      "LIMIT 2, 5 "
                      "PROCEDURE a(?) "
                      "INTO @var, @other_var "
                      "FOR UPDATE";

    cr_assert_eq(PARSE_SQL_STR(sql, parse_result), TSQLP_PARSE_OK);

    assert_parse_result_eq(
        parse_result,
        make_parse_result(
            SECTION_MODIFIERS, sql_section_new_from_string("DISTINCT HIGH_PRIORITY", 0),
            SECTION_COLUMNS, sql_section_new_from_string("id, PI() pi, (SELECT COUNT(*) c FROM l WHERE g = ?) AS bar", 1, 49),
            SECTION_TABLES, sql_section_new_from_string("table t JOIN other AS o ON 1 = o LEFT JOIN also ON ? = ?", 2, 51, 55),
            SECTION_WHERE, sql_section_new_from_string("some = expression", 0),
            SECTION_GROUP_BY, sql_section_new_from_string("col ASC, ? DESC, 4", 1, 9),
            SECTION_HAVING, sql_section_new_from_string("c > 5", 0),
            SECTION_ORDER_BY, sql_section_new_from_string("2, 4 ASC, `d` DESC", 0),
            SECTION_LIMIT, sql_section_new_from_string("2, 5", 0),
            SECTION_PROCEDURE, sql_section_new_from_string("a(?)", 1, 2),
            SECTION_SECOND_INTO, sql_section_new_from_string("INTO @var, @other_var", 0),
            SECTION_FLAGS, sql_section_new_from_string("FOR UPDATE", 0),
            NULL
        )
    );

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, parse_status_string) {
    cr_assert_str_eq(tsqlp_parse_status_to_message(TSQLP_PARSE_OK), "PARSE_OK");
    cr_assert_str_eq(tsqlp_parse_status_to_message(TSQLP_PARSE_ERROR_INVALID_ARGUMENT), "PARSE_ERROR_INVALID_ARGUMENT");
    cr_assert_str_eq(tsqlp_parse_status_to_message(TSQLP_PARSE_INVALID_SYNTAX), "PARSE_INVALID_SYNTAX");
    cr_assert_str_eq(tsqlp_parse_status_to_message(3232323), "UNKNOWN");
}

Test(tsqlp_parse, tsqlp_parse_result_serialize) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    tsqlp_sql_section_update("*", strlen("*"), tsqlp_placeholders_new(), &parse_result->columns);
    tsqlp_sql_section_update("table t", strlen("table t"), tsqlp_placeholders_new(), &parse_result->tables);

    struct tsqlp_placeholders where_placeholders = tsqlp_placeholders_new();
    tsqlp_placeholders_push(&where_placeholders, 4);
    tsqlp_sql_section_update("a = ?", strlen("a = ?"), where_placeholders, &parse_result->where);

#define EXPECTED_BUFF_LEN 1024
    char buff[EXPECTED_BUFF_LEN + 1];
    FILE *out_file = fmemopen((void *) buff, EXPECTED_BUFF_LEN, "w");

    tsqlp_parse_result_serialize(parse_result, out_file);

    buff[ftell(out_file)] = '\0';
    fclose(out_file);

    const char *expected = "columns 0 1 *\n"
                           "tables 0 7 table t\n"
                           "where 1 4 5 a = ?\n";

    cr_assert_str_eq(buff, expected);

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, tsqlp_parse_result_serialize_full) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    tsqlp_sql_section_update("DISTINCT SQL_CACHE", strlen("DISTINCT SQL_CACHE"), tsqlp_placeholders_new(),
                             &parse_result->modifiers);
    tsqlp_sql_section_update("id, SUM(money) m", strlen("id, SUM(money) n"), tsqlp_placeholders_new(),
                             &parse_result->columns);
    tsqlp_sql_section_update("table t", strlen("table t"), tsqlp_placeholders_new(), &parse_result->tables);
    tsqlp_sql_section_update("a = 1", strlen("a = 1"), tsqlp_placeholders_new(), &parse_result->where);
    tsqlp_sql_section_update("id ASC", strlen("id ASC"), tsqlp_placeholders_new(), &parse_result->group_by);
    tsqlp_sql_section_update("money > 0", strlen("money > 0"), tsqlp_placeholders_new(), &parse_result->having);
    tsqlp_sql_section_update("money DESC", strlen("money DESC"), tsqlp_placeholders_new(), &parse_result->order_by);
    tsqlp_sql_section_update("1", strlen("1"), tsqlp_placeholders_new(), &parse_result->limit);
    tsqlp_sql_section_update("INTO @user_id, @user_money", strlen("INTO @user_id, @user_money"),
                             tsqlp_placeholders_new(), &parse_result->second_into);
    tsqlp_sql_section_update("LOCK IN SHARE MODE", strlen("LOCK IN SHARE MODE"), tsqlp_placeholders_new(),
                             &parse_result->flags);

#define EXPECTED_BUFF_LEN 1024
    char buff[EXPECTED_BUFF_LEN + 1];
    FILE *out_file = fmemopen((void *) buff, EXPECTED_BUFF_LEN, "w");

    tsqlp_parse_result_serialize(parse_result, out_file);

    buff[ftell(out_file)] = '\0';
    fclose(out_file);

    const char *expected = "modifiers 0 18 DISTINCT SQL_CACHE\n"
                           "columns 0 16 id, SUM(money) m\n"
                           "tables 0 7 table t\n"
                           "where 0 5 a = 1\n"
                           "group_by 0 6 id ASC\n"
                           "having 0 9 money > 0\n"
                           "order_by 0 10 money DESC\n"
                           "second_into 0 26 INTO @user_id, @user_money\n"
                           "flags 0 18 LOCK IN SHARE MODE\n";

    cr_assert_str_eq(buff, expected);

    tsqlp_parse_result_free(parse_result);
}

Test(tsqlp_parse, placeholder_as_table_name) {
    struct tsqlp_parse_result *parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
    PARSE_SQL_STR(
                    "SELECT 1 FROM ?", parse_result
            ),
    TSQLP_PARSE_OK
    );

    assert_parse_result_eq(
            parse_result,
            make_parse_result(
                    SECTION_COLUMNS, sql_section_new_from_string("1", 0),
                    SECTION_TABLES, sql_section_new_from_string("?", 1, 0),
                    NULL
            )
    );

    tsqlp_parse_result_free(parse_result);

    parse_result = tsqlp_parse_result_new();

    cr_assert_eq(
    PARSE_SQL_STR(
                    "SELECT 1 FROM t LEFT JOIN ? ON 1", parse_result
            ),
    TSQLP_PARSE_OK
    );

    assert_parse_result_eq(
            parse_result,
            make_parse_result(
                    SECTION_COLUMNS, sql_section_new_from_string("1", 0),
                    SECTION_TABLES, sql_section_new_from_string("t LEFT JOIN ? ON 1", 1, 12),
                    NULL
            )
    );

    tsqlp_parse_result_free(parse_result);
}
