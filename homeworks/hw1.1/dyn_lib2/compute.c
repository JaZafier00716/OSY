#include "compute.h"

void compute(FILE* f) {
    char* line = NULL;
    size_t capacity = 0;
    int i = 0;

    while(getline(&line, &capacity, f) != -1) {
        char *token = strtok(line, " ");

        int curr = atoi(token);
        int sum = 0;
        while(token != NULL) {
            token = strtok(NULL, " ");
            if (token != NULL) {
                sum += curr;
                curr = atoi(token);
            } else {
                if (sum != curr) {
                    printf("%d: %d\n", i, sum);
                    break;
                }
            }
        }
        printf("%d\n", sum);
        i++;
    }
}