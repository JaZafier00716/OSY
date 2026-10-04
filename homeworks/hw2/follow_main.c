#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <file_to_follow>\n", argv[0]);
    return -1;
  }

  const char *filename = argv[1];

  FILE *file = fopen(filename, "r");

  if (!file) {
    fprintf(stderr, "Error: Could not open file %s\n", filename);
    return -1;
  }

  fseek(file, 0, SEEK_END);
  struct stat file_stat;

  fstat(fileno(file), &file_stat);
  size_t initial_size = file_stat.st_size;

  while (1) {
    fstat(fileno(file), &file_stat);

    const size_t current_size = file_stat.st_size;

    if (current_size > initial_size) {
        size_t size = current_size - initial_size;
        
        unsigned char *buffer = malloc(size + 1); // + 1 for '\0' terminator
        if (!buffer) {
            fprintf(stderr, "Error: Memory allocation failed\n");
            fclose(file);
            return -1;
        }

        fread(buffer, sizeof(char), size, file);
        buffer[size] = '\0';

        
        printf("%s", buffer);
        free(buffer);

        initial_size = current_size; // Update initial_size to the new size
    } else if (current_size < initial_size) {
        rewind(file);
        initial_size = current_size; // Update initial_size to the new size
    }

    sleep(1);
  }

  fclose(file);

  return 0;
}