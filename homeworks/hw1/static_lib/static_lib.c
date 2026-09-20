#include "static_lib.h"

void print_generated_rows(int rows_count, int num_count) {
    int max_value = 1000;
    int min_value = 10;

    for (int i = 0; i < rows_count; ++i) {
        for (int j = 0; j < num_count; ++j) {
            printf("%d ", rand() % (max_value - min_value + 1) + min_value);
        }
        printf("\n");
    }
}