#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s transform\n", argv[0]);
        return EXIT_FAILURE;
    }

    int number;
    while (read(STDIN_FILENO, &number, sizeof(int))) {
        if (strcmp(argv[1], "square") == 0) {
            // Do something
            number = number * number;
        } else if (strcmp(argv[1], "sqrt") == 0) {
            // Do something
            number = (int)sqrt((double)number);
        } else if (strcmp(argv[1], "neg") == 0) {
            // Do something
            number = -number;
        } else {
            fprintf(stderr, "Invalid transform_mode: %s\n", argv[1]);
            return EXIT_FAILURE;
        }
        write(STDOUT_FILENO, &number, sizeof(int));
    }

    return EXIT_SUCCESS;
}
