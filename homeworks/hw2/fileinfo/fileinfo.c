#include "fileinfo.h"

void print_file_permissions(mode_t read, mode_t write, mode_t execute) {
  printf("%c%c%c", (read) ? 'r' : '-', (write) ? 'w' : '-',
         (execute) ? 'x' : '-');
}

void print_file_type(mode_t mode) {
  switch (mode & S_IFMT) {
  case S_IFBLK:
    printf("block device\n");
    break;
  case S_IFCHR:
    printf("character device\n");
    break;
  case S_IFDIR:
    printf("directory\n");
    break;
  case S_IFIFO:
    printf("FIFO/pipe\n");
    break;
  case S_IFLNK:
    printf("symlink\n");
    break;
  case S_IFREG:
    printf("regular file\n");
    break;
  case S_IFSOCK:
    printf("socket\n");
    break;
  default:
    printf("unknown?\n");
    break;
  }
}

int print_file_info(const char *filename, bool symlink) {
  struct stat file_stat;
  int rval;

  if (symlink) {
    rval = lstat(filename, &file_stat);
  } else {
    rval = stat(filename, &file_stat);
  }

  if (rval != 0) {
    perror("An error has occurred while trying to get file information");
    return rval;
  }
  printf("Name: %s\n", filename);
  printf("Type: ");
  print_file_type(file_stat.st_mode);
  printf("Size: %jd\n", (intmax_t)file_stat.st_size);
  printf("Permissions: ");
  print_file_permissions(file_stat.st_mode & S_IRUSR, file_stat.st_mode & S_IWUSR,
                         file_stat.st_mode & S_IXUSR);
  print_file_permissions(file_stat.st_mode & S_IRGRP, file_stat.st_mode & S_IWGRP,
                         file_stat.st_mode & S_IXGRP);
  print_file_permissions(file_stat.st_mode & S_IROTH, file_stat.st_mode & S_IWOTH,
                         file_stat.st_mode & S_IXOTH);
  printf("\n");
  printf("Inode: %ju\n", (uintmax_t)file_stat.st_ino);
  return rval;
}