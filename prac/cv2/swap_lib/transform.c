#include "transform.h"
#include "charutil.h"

void transform_line(char *line) {
  char *new_line = malloc(strlen(line) + 1);
  strcpy(new_line, line);

  for (int i = 0; new_line[i] != '\0'; i++) {
    char curr = new_line[i];
    if (is_upper(curr)) {
      new_line[i] = to_lower(curr);
    } else if (is_lower(curr)) {
      new_line[i] = to_upper(curr);
    }
  }
  printf("%s", new_line);
  free(new_line);
}