#include "transform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void transform_lines(FILE *f) {
  char *line = NULL;
  size_t capacity = 0;

  while (getline(&line, &capacity, f) != -1) {
    transform_line(line);
  }
}

int main() {
    transform_lines(stdin);
    return 0;
}