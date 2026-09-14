#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int is_not_valid_letter(char *token) {
  size_t length = strlen(token);
  if (length == 0 || length > 1) {
    return 1; // Invalid if empty or more than one character
  }
  return !((token[0] >= 'a' && token[0] <= 'z') ||
           (token[0] >= 'A' && token[0] <= 'Z'));
}

int is_not_valid_number(char *token) {
  for (int i = 0; token[i] != '\0'; i++) {
    if (token[i] == '-' && i == 0) {
      continue; // Allow negative sign at the beginning
    }
    if (token[i] < '0' || token[i] > '9') {
      return 1;
    }
  }
  return 0;
}

int process_line(char *line, int add_mode, int letter_mode) {
  char *token = strtok(line, " \n");
  while (token != NULL) {
    if ((letter_mode && is_not_valid_letter(token)) ||
        (!letter_mode && is_not_valid_number(token))) {
      fprintf(stderr, "Error: mixed input types\n");
      return -1;
    }

    if (letter_mode) {
      char c = token[0];
      if (add_mode) {
        if (c == 'z') {
          c = 'a';
        } else if (c == 'Z') {
          c = 'A';
        } else {
          c++;
        }
      } else if (c >= 'a' && c <= 'z') {
        c -= 'a' - 'A';
      } else {
        c += 'a' - 'A';
      }
      printf("%c ", c);
    } else {
      int number = atoi(token);
      printf("%d ", add_mode ? number + 1 : -number);
    }
    token = strtok(NULL, " \n");
  }
  printf("\n");
  return 0;
}

int run_command(char *line, int add_mode) {
  char *copy = strdup(line);
  if (copy == NULL) {
    return 1;
  }
  char *token = strtok(copy, " \n");
  if (token == NULL) {
    free(copy);
    return 1; // No input provided
  }
  if (is_not_valid_letter(token) && is_not_valid_number(token)) {
    fprintf(stderr, "Error: mixed input types\n");
    free(copy);
    return -1;
  }
  int letter_mode = !is_not_valid_letter(token);
  int number_mode = !is_not_valid_number(token);
  while (token != NULL) {
    if ((letter_mode && is_not_valid_letter(token)) ||
        (number_mode && is_not_valid_number(token))) {
      fprintf(stderr, "Error: mixed input types\n");
      free(copy);
      return -1;
    }
    token = strtok(NULL, " \n");
  }
  free(copy);
  if (letter_mode || number_mode) {
    return process_line(line, add_mode, letter_mode);
  }
  return -1;
}

int main(int, const char **argv) {
  char *line = NULL;
  size_t capacity = 0;

  const char *program = strrchr(argv[0], '/');
  program = program ? program + 1 : argv[0];

  if (getline(&line, &capacity, stdin) == -1) {
    free(line);
    return 1;
  }

  if (strcmp(program, "add") == 0) {
    int result = run_command(line, 1);
    free(line);
    return result;
  }
  if (strcmp(program, "inv") == 0) {
    int result = run_command(line, 0);
    free(line);
    return result;
  }

  fprintf(stderr, "Invalid command: %s\n", argv[0]);
  free(line);
  return 1;
}