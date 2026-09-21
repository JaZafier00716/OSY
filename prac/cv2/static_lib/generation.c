#include "generation.h"

void generate_number_grid(int M, int N) {
    srand(time(NULL));
    int min_value = 10;
    int max_value = 1000;

    for(int i = 0; i < M; i++) {
        for(int j = 0; j < N; j++) {
            int random_number = rand() % (max_value - min_value + 1) + min_value;
            printf("%d ", random_number);
        }
        printf("\n");
    }

}