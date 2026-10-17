#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s filter\n", argv[0]);
    return 1;
  }

  // if odd, then modulo_2 = 1, else if even, then modulo_2 = 0, else modulo_2 =
  // -1
  int modulo_2 =
      strcmp(argv[1], "odd") == 0 ? 1 : (strcmp(argv[1], "even") == 0 ? 0 : -1);

  if (modulo_2 == -1) {
    fprintf(stderr, "Invalid filter: %s\n", argv[1]);
    return 1;
  }

  int number;
  while (read(STDIN_FILENO, &number, sizeof(int))) {
    if (number % 2 == modulo_2) {
        write(STDOUT_FILENO, &number, sizeof(int));
    }
  }

  return EXIT_SUCCESS;
}
