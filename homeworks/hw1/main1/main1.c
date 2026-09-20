#include "static_lib.h"
#include <stdio.h>
#include <stdlib.h>


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <rows_count> <num_count>\n", argv[0]);
        return 1;
    }

    int rows_count = atoi(argv[1]);
    int num_count = atoi(argv[2]);

    if (rows_count <= 0 || num_count <= 0) {
        fprintf(stderr, "Error: rows_count and num_count must be positive integers.\n");
        return 1;
    }

    print_generated_rows(rows_count, num_count);

    return 0;
}
