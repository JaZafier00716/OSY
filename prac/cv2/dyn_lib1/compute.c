#include "compute.h"

void compute(FILE* f) {
    char* line = NULL;
    size_t capacity = 0;
    while(getline(&line, &capacity, f) != -1) {
        char *token = strtok(line, " ");

        int sum = 0;
        while(token != NULL) {
            int curr = atoi(token);
            if(curr < 0) {
                fprintf(stderr, "Error: Negative number encountered: %d\n", curr);
                free(line);
                return;
            }
            sum += curr;
            token = strtok(NULL, " ");
            printf("%d ", curr);
        }
        printf("%d\n", sum);
    }
}