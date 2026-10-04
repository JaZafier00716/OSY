#include "fileinfo.h"

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s [-l] <filename>\n", argv[0]);
    return 1;
  }

  bool symlink = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-l") == 0) {
      symlink = true;
    }
  }

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-l") != 0) {
      print_file_info(argv[i], symlink);
      printf("-------------------------------\n");
    }
  }
  return 0;
}