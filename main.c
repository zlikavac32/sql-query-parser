#include <string.h>
#include <malloc.h>
#include <stdlib.h>

#include "tsqlp.h"

#define BUFF_LEN 1024

int main(int argc, char *argv[]) {

    char buff[BUFF_LEN];
    char *sql = NULL;
    size_t total_read_size = 0;
    size_t read_size = 0;

    while (!feof(stdin) && (read_size = fread(buff, sizeof(char), BUFF_LEN, stdin)) > 0) {
        sql = realloc(sql, total_read_size + read_size);

        if (sql == NULL) {
            exit(1);
        }

        memcpy(sql + total_read_size, buff, read_size);
        total_read_size += read_size;
    }

    struct parse_result parse_result = tsqlp_parse_result_new();

    parse_status_type parse_status = tsqlp_parse(sql, total_read_size, &parse_result);

    free(sql);

    if (parse_status != PARSE_OK) {
        tsql_parse_result_free(&parse_result);

        fprintf(stderr, "%s\n", tsqlp_parse_status_to_message(parse_status));
        exit(2);
    }

    parse_result_serialize(&parse_result, stdout);
    tsql_parse_result_free(&parse_result);

    return 0;
}