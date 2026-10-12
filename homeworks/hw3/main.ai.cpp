#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

void print_message(const char *caller, const char *message) {
  dprintf(STDERR_FILENO, "[%s] %s\n", caller, message);
}

std::string without_newline(const char *buffer, ssize_t length) {
  if (length > 0 && buffer[length - 1] == '\n') {
    --length;
  }
  return std::string(buffer, static_cast<size_t>(length));
}

bool write_all(int fd, const char *buffer, size_t length) {
  while (length > 0) {
    ssize_t written = write(fd, buffer, length);
    if (written < 0) {
      return false;
    }
    buffer += written;
    length -= static_cast<size_t>(written);
  }
  return true;
}

ssize_t read_line(int fd, char *buffer, size_t capacity) {
  size_t length = 0;
  while (length + 1 < capacity) {
    char character;
    ssize_t result = read(fd, &character, 1);
    if (result <= 0) {
      if (result < 0 || length == 0) {
        return result;
      }
      break;
    }
    buffer[length++] = character;
    if (character == '\n') {
      break;
    }
  }
  buffer[length] = '\0';
  return static_cast<ssize_t>(length);
}

void child1(int input_fd, int output_fd) {
  char buffer[512];
  size_t message_number = 1;

  while (true) {
    ssize_t length = read_line(input_fd, buffer, sizeof(buffer));
    if (length < 0) {
      perror("child 1: read");
      exit(EXIT_FAILURE);
    }
    if (length == 0) {
      break;
    }

    print_message("child1",
                  ("Received: \"" + without_newline(buffer, length) + "\"").c_str());
    for (ssize_t i = 0; i < length; ++i) {
      buffer[i] = static_cast<char>(
          std::tolower(static_cast<unsigned char>(buffer[i])));
    }

    char output[512];
    int output_length =
        snprintf(output, sizeof(output), "%zu. %.*s", message_number++,
                 static_cast<int>(length), buffer);

    if (output_length < 0 || static_cast<size_t>(output_length) >= sizeof(output)) {
      fprintf(stderr, "child 1: output buffer overflow\n");
      exit(EXIT_FAILURE);
    }

    print_message("child1",
                  ("Sending: \"" + without_newline(output, output_length) + "\"").c_str());

    if (!write_all(output_fd, output, static_cast<size_t>(output_length))) {
      perror("child 1: write error");
      exit(EXIT_FAILURE);
    }
  }
}

void child2(int input_fd, int output_fd) {
  char buffer[512];

  while (true) {
    ssize_t length = read_line(input_fd, buffer, sizeof(buffer));
    if (length < 0) {
      perror("child 2: read");
      exit(EXIT_FAILURE);
    }
    if (length == 0) {
      break;
    }

    std::string received = without_newline(buffer, length);
    print_message("child2", ("Received: \"" + received + "\"").c_str());
    size_t separator = received.find(". ");
    size_t name_length = separator == std::string::npos
                             ? received.length()
                             : received.length() - separator - 2;
    char output[1024];
    int output_length =
        snprintf(output, sizeof(output), "%s (%zu)\n", received.c_str(),
                 name_length);

    if (output_length < 0 || static_cast<size_t>(output_length) >= sizeof(output)) {
      fprintf(stderr, "child 2: output buffer overflow\n");
      exit(EXIT_FAILURE);
    }

    print_message("child2",
                  ("Sending: \"" + without_newline(output, output_length) + "\"").c_str());

    if (!write_all(output_fd, output, static_cast<size_t>(output_length))) {
      perror("child 2: write error");
      exit(EXIT_FAILURE);
    }
  }
}

void child3(int input_fd, int output_fd) {
  char buffer[1024];

  while (true) {
    ssize_t length = read_line(input_fd, buffer, sizeof(buffer));
    if (length < 0) {
      perror("child 3: read");
      exit(EXIT_FAILURE);
    }
    if (length == 0) {
      break;
    }

    print_message("child3",
                  ("Received: \"" + without_newline(buffer, length) + "\"").c_str());

    for (ssize_t i = 0; i < length; ++i) {
      if ((i == 0 || std::isspace(static_cast<unsigned char>(buffer[i - 1]))) &&
          std::islower(static_cast<unsigned char>(buffer[i]))) {
        buffer[i] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(buffer[i])));
      }
    }
    print_message("child3",
                  ("Output: \"" + without_newline(buffer, length) + "\"").c_str());

    if (!write_all(output_fd, buffer, static_cast<size_t>(length))) {
      perror("child 3: write error");
      exit(EXIT_FAILURE);
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    return EXIT_FAILURE;
  }

  print_message("parent", "Starting child1");

  int pipe_1[2];
  pipe(pipe_1);
  int pipe_2[2];
  pipe(pipe_2);
  int pipe_3[2];
  pipe(pipe_3);
  int pipe_4[2];
  pipe(pipe_4);

  pid_t child1_pid = fork();
  if (child1_pid == 0) {
    close(pipe_1[1]);
    close(pipe_2[0]);
    close(pipe_3[0]);
    close(pipe_3[1]);
    close(pipe_4[0]);
    close(pipe_4[1]);
    child1(pipe_1[0], pipe_2[1]);
    close(pipe_2[1]);
    close(pipe_1[0]);
    _exit(EXIT_SUCCESS);
  }
  if (child1_pid < 0) {
    perror("fork");
    return EXIT_FAILURE;
  }

  print_message("parent", "Starting child2");


  pid_t child2_pid = fork();
  if (child2_pid == 0) {
    close(pipe_1[0]);
    close(pipe_1[1]);
    close(pipe_2[1]);
    close(pipe_3[0]);
    child2(pipe_2[0], pipe_3[1]);
    close(pipe_2[0]);
    close(pipe_3[1]);
    close(pipe_4[0]);
    close(pipe_4[1]);
    _exit(EXIT_SUCCESS);
  }
  if (child2_pid < 0) {
    perror("fork");
    return EXIT_FAILURE;
  }

  print_message("parent", "Starting child3");


  pid_t child3_pid = fork();
  if (child3_pid == 0) {
    close(pipe_1[0]);
    close(pipe_1[1]);
    close(pipe_2[0]);
    close(pipe_2[1]);
    close(pipe_3[1]);
    close(pipe_4[0]);
    child3(pipe_3[0], pipe_4[1]);
    close(pipe_3[0]);
    close(pipe_4[1]);
    _exit(EXIT_SUCCESS);
  }
  if (child3_pid < 0) {
    perror("fork");
    return EXIT_FAILURE;
  }

  close(pipe_2[0]);
  close(pipe_2[1]);
  close(pipe_3[1]);
  close(pipe_4[1]);

  FILE *file = fopen(argv[1], "r");
  if (file == nullptr) {
    fprintf(stderr, "Failed to open file: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), file) != nullptr) {
    print_message("parent",
                  ("Reading: \"" + without_newline(buffer, strlen(buffer)) +
                   "\"")
                      .c_str());
    size_t input_length = strlen(buffer);
    bool needs_newline =
        input_length == 0 || buffer[input_length - 1] != '\n';
    if (!write_all(pipe_1[1], buffer, input_length) ||
        (needs_newline && !write_all(pipe_1[1], "\n", 1))) {
      perror("parent: write");
      fclose(file);
      close(pipe_1[1]);
      return EXIT_FAILURE;
    }

    char result[1024];
    ssize_t result_length = read_line(pipe_4[0], result, sizeof(result));
    if (result_length < 0) {
      perror("parent: read");
      fclose(file);
      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }
    if (result_length == 0) {
      fprintf(stderr, "parent: child pipeline ended unexpectedly\n");
      fclose(file);
      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }

    printf("\n");
  }
  fclose(file);
  close(pipe_1[1]);
  close(pipe_4[0]);

  waitpid(child1_pid, nullptr, 0);
  waitpid(child2_pid, nullptr, 0);
  waitpid(child3_pid, nullptr, 0);

  return EXIT_SUCCESS;
}
