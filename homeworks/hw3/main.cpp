#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

ssize_t read_text(int read_pipe_end, char *buffer, size_t buffer_size) {
  size_t text_length = 0;

  while (text_length + 1 < buffer_size) {
    char character;
    auto result = read(read_pipe_end, &character, 1);

    if (result <= 0) {
      // If read returns 0, it means EOF. If it returns -1, it's an error.
      if (result < 0 || text_length == 0) {
        return result;
      }
      break;
    }

    if (character == '\n' || character == '\0') {
      break;
    }
    buffer[text_length++] = character;
  }
  buffer[text_length] = '\0';
  return text_length;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    return EXIT_FAILURE;
  }

  int pipe_1[2];
  int pipe_2[2];
  int pipe_3[2];
  int pipe_4[2];

  pipe(pipe_1);
  pipe(pipe_2);
  pipe(pipe_3);
  pipe(pipe_4);

  pid_t child1_pid = fork();
  if (child1_pid == 0) {
    close(pipe_1[1]);
    close(pipe_2[0]);
    close(pipe_3[0]);
    close(pipe_3[1]);
    close(pipe_4[0]);
    close(pipe_4[1]);

    char buffer[8192];
    size_t message_number = 1;

    while (true) {
      auto length = read_text(pipe_1[0], buffer, sizeof(buffer));
      if (length < 0) {
        perror("[child 1] read error");
        close(pipe_1[0]);
        close(pipe_2[1]);
        exit(EXIT_FAILURE);
      }
      if (length == 0) {
        break;
      }
      printf("[child 1] Received: %s\n", buffer);

      // Convert to lowercase
      for (ssize_t i = 0; i < length; ++i) {
        buffer[i] =
            static_cast<char>(tolower(static_cast<unsigned char>(buffer[i])));
      }

      char output[8192];

      auto output_length = snprintf(output, sizeof(output), "%zu. %s\n",
                                    message_number++, buffer);

      printf("[child 1] Sending: %s", output);

      if (write(pipe_2[1], output, output_length) !=
          static_cast<ssize_t>(output_length)) {
        perror("[child 1] write error");
        close(pipe_1[0]);
        close(pipe_2[1]);
        exit(EXIT_FAILURE);
      }
    }
    close(pipe_1[0]);
    close(pipe_2[1]);
    _exit(EXIT_SUCCESS);
  }

  pid_t child2_pid = fork();
  if (child2_pid == 0) {
    close(pipe_1[0]);
    close(pipe_1[1]);
    close(pipe_2[1]);
    close(pipe_3[0]);
    close(pipe_4[0]);
    close(pipe_4[1]);

    char buffer[8192];

    while (1) {
      auto length = read_text(pipe_2[0], buffer, sizeof(buffer));
      if (length < 0) {
        perror("[child 2] read error");
        close(pipe_2[0]);
        close(pipe_3[1]);
        exit(EXIT_FAILURE);
      }
      if (length == 0) {
        break;
      }
      printf("[child 2] Received: %s\n", buffer);

      char output[8192];

      auto output_length =
          snprintf(output, sizeof(output), "%s (%zu)\n", buffer, length);

      printf("[child 2] Sending: %s", output);

      if (write(pipe_3[1], output, output_length) !=
          static_cast<ssize_t>(output_length)) {
        perror("[child 2] write error");
        close(pipe_2[0]);
        close(pipe_3[1]);
        exit(EXIT_FAILURE);
      }
    }
    close(pipe_2[0]);
    close(pipe_3[1]);
    _exit(EXIT_SUCCESS);
  }

  pid_t child3_pid = fork();
  if (child3_pid == 0) {
    close(pipe_1[0]);
    close(pipe_1[1]);
    close(pipe_2[0]);
    close(pipe_2[1]);
    close(pipe_3[1]);
    close(pipe_4[0]);

    char buffer[8192];

    while (1) {
      auto length = read_text(pipe_3[0], buffer, sizeof(buffer));
      if (length < 0) {
        perror("[child 3] read error");
        close(pipe_3[0]);
        close(pipe_4[1]);
        exit(EXIT_FAILURE);
      }
      if (length == 0) {
        break;
      }
      printf("[child 3] Received: %s\n", buffer);

      char output[8192];
      bool prev_space = false;

      for (ssize_t i = 0; i < length; i++) {
        if (buffer[i] == ' ') {
          output[i] = buffer[i];
          prev_space = true;
          continue;
        }
        if (prev_space && buffer[i] >= 'a' && buffer[i] <= 'z') {
          output[i] = buffer[i] - ('a' - 'A');
        } else {
          output[i] = buffer[i];
        }
        prev_space = false;
      }
      output[length] = '\n';
      output[length + 1] = '\0';

      printf("[child 3] Output: %s", output);

      if (write(pipe_4[1], output, strlen(output)) !=
          static_cast<ssize_t>(strlen(output))) {
        perror("[child 3] write error");
        close(pipe_3[0]);
        close(pipe_4[1]);
        exit(EXIT_FAILURE);
      }
    }
    close(pipe_3[0]);
    close(pipe_4[1]);
    _exit(EXIT_SUCCESS);
  }

  close(pipe_1[0]);
  close(pipe_2[0]);
  close(pipe_2[1]);
  close(pipe_3[0]);
  close(pipe_3[1]);
  close(pipe_4[1]);

  FILE *f = fopen(argv[1], "r");

  if (!f) {
    fprintf(stderr, "[parent] Failed to open file %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  char buffer[8192];

  while (fgets(buffer, sizeof(buffer), f) != nullptr) {

    // Send message to child 1
    printf("[parent] Reading: %s", buffer);
    if (write(pipe_1[1], buffer, strlen(buffer)) !=
        static_cast<ssize_t>(strlen(buffer))) {
      perror("[parent] write error");
      fclose(f);

      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }

    // Wait for final output from child 3
    auto length = read_text(pipe_4[0], buffer, sizeof(buffer));
    if (length < 0) {
      perror("[parent] read error\n");
      fclose(f);

      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }
    if (length == 0) {
      fprintf(stderr, "[parent] child pipeline ended unexpectedly\n");
      fclose(f);

      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }
    printf("\n");
  }
  

  close(pipe_1[1]);
  close(pipe_4[0]);

  fclose(f);
  waitpid(child1_pid, nullptr, 0);
  waitpid(child2_pid, nullptr, 0);
  waitpid(child3_pid, nullptr, 0);
  exit(EXIT_SUCCESS);
}