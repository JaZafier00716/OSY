#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

int main() {
    int sum = 0;
    int number;
    while (read(STDIN_FILENO, &number, sizeof(int))) {
        sum += number;
    }
    printf("Sum: %d\n", sum);
    // write(STDOUT_FILENO, &sum, sizeof(int));

    return EXIT_SUCCESS;
}
