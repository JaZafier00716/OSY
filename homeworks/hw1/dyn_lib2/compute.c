#include "compute.h"


void compute(FILE* input_file) {
    char *line = NULL;
    size_t capacity = 0;
    int i = 0;

    while(getline(&line, &capacity, input_file) != -1) {
        char *token = strtok(line, " \t\n");
        int sum = 0;

        if (token == NULL) {
            printf("0\n");
            continue;
        }

        int current_number = atoi(token);

        while ((token = strtok(NULL, " \t\n")) != NULL) {
            sum += current_number;
            current_number = atoi(token);
        }

        if (sum != current_number) {
            printf("%d: %d\n", i, sum);
        }
        i++;
    }
    printf("\n");
}