#include "compute.h"

void compute(FILE* input_file) {
    char *line = NULL;
    size_t capacity = 0;

    while(getline(&line, &capacity, input_file) != -1) {
        char *token = strtok(line, " \t\n");
        int sum = 0;

        while(token != NULL) {
            int current_number = atoi(token);
            printf("%d ", current_number);
            sum += current_number;
            token = strtok(NULL, " \t\n");
        }

        printf("%d\n", sum);
    }
}