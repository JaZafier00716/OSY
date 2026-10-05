#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

bool is_regular_file(const char *path) {
  struct stat file_stat;

  int rval = stat(path, &file_stat);

  if (rval != 0) {
    perror("An error has occurred while trying to get file information - "
           "is_regular_file");
    return false;
  }

  return S_ISREG(file_stat.st_mode);
}

bool get_file_size(const char *path, size_t *size) {
  struct stat file_stat;

  int rval = stat(path, &file_stat);

  if (rval != 0) {
    return false;
    return -1;
  }
  *size = file_stat.st_size;
  return true;
}

bool get_file_size(FILE *file, size_t *size) {
  struct stat file_stat;

  int rval = fstat(fileno(file), &file_stat);
  if (rval != 0) {
    perror("An error has occurred while trying to get file information - file "
           "size");
    return false;
  }

  *size = file_stat.st_size;
  return true;
}

size_t get_inode_size(FILE *file) {
  struct stat file_stat;

  int rval = fstat(fileno(file), &file_stat);

  if (rval != 0) {
    perror("An error has occurred while trying to get file information - inode "
           "size");
    return -1;
  }

  return file_stat.st_ino;
}

size_t get_inode_size(const char *path) {
  struct stat file_stat;

  if (stat(path, &file_stat) != 0) {
    perror("An error has occurred while trying to get file information - inode "
           "size");
    return 0;
  }

  return file_stat.st_ino;
}

int get_file_permissions(FILE *file) {
  struct stat file_stat;

  int rval = fstat(fileno(file), &file_stat);

  if (rval != 0) {
    perror("An error has occurred while trying to get file information - file "
           "permissions");
    return -1;
  }

  return file_stat.st_mode;
}

int sync_file_permissions(FILE *source, FILE *destination) {
  struct stat src_stat;
  if (fstat(fileno(source), &src_stat) != 0) {
    perror("Error getting source file permissions");
    return -1;
  }

  if (fchmod(fileno(destination), src_stat.st_mode & 07777) != 0) {
    perror("Error setting destination file permissions");
    return -1;
  }

  return src_stat.st_mode;
}

void create_mirror(FILE *source, FILE *destination, bool sync_permissions) {

  char buffer[4096];
  size_t bytes;
  while ((bytes = fread(buffer, 1, sizeof(buffer), source)) > 0) {
    printf("Copying %zu bytes\n", bytes);
    size_t rval = fwrite(buffer, 1, bytes, destination);
    if (rval != bytes) {
      perror("Error writing to destination file");
      return;
    }
  }

  fflush(destination);

  if (sync_permissions) {
    sync_file_permissions(source, destination);
  }
}

void print_file_permissions(mode_t read, mode_t write, mode_t execute) {
  printf("%c%c%c", (read) ? 'r' : '-', (write) ? 'w' : '-',
         (execute) ? 'x' : '-');
}

bool file_exists(const char *path) {
  struct stat file_stat;
  return (stat(path, &file_stat) == 0);
}

int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    printf("Wrong number of arguments\n");
    fprintf(stderr, "Usage: %s [-p] <source> <destination>\n", argv[0]);
    return 1;
  }

  bool sync_permissions = false;
  char *source = NULL;
  char *destination = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-p") == 0) {
      sync_permissions = true;
    } else {
      if (source == NULL) {
        source = argv[i];
      } else if (destination == NULL) {
        destination = argv[i];
      }
    }
  }

  if (source == NULL || destination == NULL || (sync_permissions && argc < 4)) {
    fprintf(stderr, "source or destination not specified\n");
    fprintf(stderr, "Usage: %s [-p] <source> <destination>\n", argv[0]);
    return 1;
  }

  if (!file_exists(source)) {
    fprintf(stderr, "Error: Source file '%s' does not exist.\n", source);
    return 1;
  }

  if (!is_regular_file(source)) {
    fprintf(stderr, "Error: source file '%s' is not a regular file.\n", source);
    return 1;
  }

  FILE *src_file = fopen(source, "r");
  if (src_file == NULL) {
    perror("Error opening source file");
    exit(EXIT_FAILURE);
  }

  FILE *dest_file = fopen(destination, "w");
  if (dest_file == NULL) {
    perror("Error opening destination file");
    fclose(src_file);
    exit(EXIT_FAILURE);
  }

  create_mirror(src_file, dest_file, sync_permissions);

  size_t mirror_size;
  if (!get_file_size(destination, &mirror_size)) {
    fclose(src_file);
    fclose(dest_file);
    return EXIT_FAILURE;
  }

  printf("Initial copy: %ld bytes\n", mirror_size);

  size_t initial_inode = get_inode_size(source);

  int initial_permissions = get_file_permissions(src_file);

  while (1) {
    if (!file_exists(source)) {
      sleep(1);
      if (!file_exists(source)) {
        printf("Source file disappeared\n");
        fclose(src_file);
        fclose(dest_file);
        return 1;
      }
    }

    // Check inode size to detect if the source file has been replaced
    size_t current_inode = get_inode_size(source);
    if (current_inode != initial_inode) {
      printf("Source file has been replaced. Reopening...\n");
      fclose(src_file);
      src_file = fopen(source, "r");
      if (src_file == NULL) {
        perror("Error reopening source file");
        exit(EXIT_FAILURE);
      }

      fclose(dest_file);
      remove(destination); // Remove the old mirror file
      dest_file = fopen(destination, "w");
      if (dest_file == NULL) {
        perror("Error reopening destination file");
        exit(EXIT_FAILURE);
      }
      create_mirror(src_file, dest_file, sync_permissions);
      if (!get_file_size(destination, &mirror_size)) {
        fclose(src_file);
        fclose(dest_file);
        return EXIT_FAILURE;
      }
      printf("Source replaced: inode %ld -> %ld\n", initial_inode,
             current_inode);

      initial_inode = current_inode;
      initial_permissions = get_file_permissions(src_file);
    }

    if (sync_permissions) {

      int current_permissions = get_file_permissions(src_file);
      if (initial_permissions != current_permissions) {

        print_file_permissions(initial_permissions & S_IRUSR,
                               initial_permissions & S_IWUSR,
                               initial_permissions & S_IXUSR);
        print_file_permissions(initial_permissions & S_IRGRP,
                               initial_permissions & S_IWGRP,
                               initial_permissions & S_IXGRP);
        print_file_permissions(initial_permissions & S_IROTH,
                               initial_permissions & S_IWOTH,
                               initial_permissions & S_IXOTH);
        printf(" -> ");
        print_file_permissions(current_permissions & S_IRUSR,
                               current_permissions & S_IWUSR,
                               current_permissions & S_IXUSR);
        print_file_permissions(current_permissions & S_IRGRP,
                               current_permissions & S_IWGRP,
                               current_permissions & S_IXGRP);
        print_file_permissions(current_permissions & S_IROTH,
                               current_permissions & S_IWOTH,
                               current_permissions & S_IXOTH);
        printf("\n");
        initial_permissions = sync_file_permissions(src_file, dest_file);
      }
    }

    size_t current_size;
    if (!get_file_size(source, &current_size)) {
      printf("Source file disappeared\n");
      fclose(src_file);
      fclose(dest_file);
      return 1;
    }
    if (current_size > mirror_size) {
      const size_t size = current_size - mirror_size;
      printf("+%ld bytes\n", size);

      unsigned char *buffer =
          (unsigned char *)malloc(size + 1); // + 1 for '\0' terminator
      if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(src_file);
        fclose(dest_file);
        return -1;
      }
      if (fseek(src_file, (long)mirror_size, SEEK_SET) != 0) {
        perror("Error seeking in source file");
        free(buffer);
        fclose(src_file);
        fclose(dest_file);
        return EXIT_FAILURE;
      }
      size_t bytes_read = fread(buffer, 1, size, src_file);
      if (bytes_read != size) {
        perror("Error reading source file");
        free(buffer);
        fclose(src_file);
        fclose(dest_file);
        return EXIT_FAILURE;
      }
      buffer[size] = '\0';

      if (fseek(dest_file, 0, SEEK_END) != 0) {
        perror("Error seeking in destination file");
        free(buffer);
        fclose(src_file);
        fclose(dest_file);
        return EXIT_FAILURE;
      }
      size_t bytes_written = fwrite(buffer, 1, size, dest_file);
      if (bytes_written != size) {
        fprintf(stderr, "Error: Failed to write to destination file\n");
        free(buffer);
        fclose(src_file);
        fclose(dest_file);
        return -1;
      }
      fflush(dest_file);

      free(buffer);
      mirror_size = current_size;
    } else if (current_size < mirror_size) {
      printf("Source truncated: %ld -> %ld bytes\n", mirror_size, current_size);

      if (fflush(dest_file) != 0 || truncate(destination, current_size) != 0) {
        perror("Error truncating destination file");
        fclose(src_file);
        fclose(dest_file);
        return EXIT_FAILURE;
      }

      rewind(src_file);
      rewind(dest_file);
      unsigned char *buffer =
          (unsigned char *)malloc(current_size + 1); // + 1 for '\0' terminator
      if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        fclose(src_file);
        fclose(dest_file);
        return -1;
      }

      size_t bytes_read = fread(buffer, 1, current_size, src_file);
      if (bytes_read != current_size) {
        perror("Error reading source file");
        free(buffer);
        fclose(src_file);
        fclose(dest_file);
        return EXIT_FAILURE;
      }

      size_t bytes_written = fwrite(buffer, 1, current_size, dest_file);
      if (bytes_written != current_size) {
        fprintf(stderr, "Error: Failed to write to destination file\n");
        free(buffer);
        fclose(src_file);
        fclose(dest_file);
        return -1;
      }

      free(buffer);
      mirror_size = current_size;
    }
  }

  fclose(src_file);
  fclose(dest_file);
  return 0;
}