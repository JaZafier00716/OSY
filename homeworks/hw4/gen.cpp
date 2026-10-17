#include <cstdio>
#include <cstdlib>
#include <random>
#include <unistd.h>


int main (int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int N = atoi(argv[1]);

    std::random_device randomDevice;
    std::mt19937 generator(randomDevice());
    std::uniform_int_distribution<int> distribution(1, N+1);

    for (int i = 0; i < N; ++i) {
        int randomValue = distribution(generator);
        write(STDOUT_FILENO, &randomValue, sizeof(int));
    }

    EXIT_SUCCESS;
}
