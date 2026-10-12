#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

typedef struct
{
  int id;
  int length;
  int matches[3];
  char name[64];
} record_t;

void print_struct(const record_t r, bool is_match = false)
{
  if (r.id >= 0)
  {
    printf("[%d] ", r.id);
  }
  else
  {
    printf("[] ");
  }

  printf("%s ", r.name);

  if (r.length >= 0)
  {
    printf("(%d) ", r.length);
  }
  else
  {
    printf("() ");
  }
  if(is_match) {
    printf("[%d,%d,%d] MATCH\n", r.matches[0], r.matches[1], r.matches[2]);
  } else {
    printf("[%d,%d,%d]\n", r.matches[0], r.matches[1], r.matches[2]);
  }
}

ssize_t read_text(int read_pipe_end, char *buffer, size_t buffer_size)
{
  size_t text_length = 0;

  while (text_length + 1 < buffer_size)
  {
    char character;
    auto result = read(read_pipe_end, &character, 1);

    if (result <= 0)
    {
      // If read returns 0, it means EOF. If it returns -1, it's an error.
      if (result < 0 || text_length == 0)
      {
        return result;
      }
      break;
    }

    if (character == '\n' || character == '\0')
    {
      break;
    }
    buffer[text_length++] = character;
  }
  buffer[text_length] = '\0';
  return text_length;
}

int main(int argc, char *argv[])
{
  if (argc != 5)
  {
    fprintf(stderr, "Usage: %s <filename> <min_name_length> <first_letter> <character_contained>\n", argv[0]);
    return EXIT_FAILURE;
  }

  int min_name_length = atoi(argv[2]);
  char start_letter = argv[3][0];
  char c_contained = argv[4][0];

  int pipe_1[2];
  int pipe_2[2];
  int pipe_3[2];
  int pipe_4[2];

  pipe(pipe_1);
  pipe(pipe_2);
  pipe(pipe_3);
  pipe(pipe_4);

  pid_t child1_pid = fork();
  if (child1_pid == 0)
  {
    close(pipe_1[1]);
    close(pipe_2[0]);
    close(pipe_3[0]);
    close(pipe_3[1]);
    close(pipe_4[0]);
    close(pipe_4[1]);

    size_t message_number = 1;

    while (true)
    {
      record_t record;

      auto length = read(pipe_1[0], &record, sizeof(record));
      if (length < 0)
      {
        perror("[child 1] read error");
        close(pipe_1[0]);
        close(pipe_2[1]);
        exit(EXIT_FAILURE);
      }
      if (length == 0)
      {
        break;
      }
      // printf("[child 1] Received: ");
      // print_struct(record);

      record.id = message_number;

      // Convert to lowercase
      for (ssize_t i = 0; i < length; ++i)
      {
        record.name[i] =
            static_cast<char>(tolower(record.name[i]));
      }

      if (record.name[0] == tolower(start_letter))
      {
        record.matches[0] = 1;
      }

      char output[8192];

      // printf("[child 1] Sending: ");
      // print_struct(record);

      if (write(pipe_2[1], &record, sizeof(record)) != sizeof(record))
      {
        perror("[child 1] write error");
        close(pipe_1[0]);
        close(pipe_2[1]);
        exit(EXIT_FAILURE);
      }
      message_number++;
    }
    close(pipe_1[0]);
    close(pipe_2[1]);
    _exit(EXIT_SUCCESS);
  }

  pid_t child2_pid = fork();
  if (child2_pid == 0)
  {
    close(pipe_1[0]);
    close(pipe_1[1]);
    close(pipe_2[1]);
    close(pipe_3[0]);
    close(pipe_4[0]);
    close(pipe_4[1]);

    while (1)
    {
      record_t record;
      auto length = read(pipe_2[0], &record, sizeof(record));
      if (length < 0)
      {
        perror("[child 2] read error");
        close(pipe_2[0]);
        close(pipe_3[1]);
        exit(EXIT_FAILURE);
      }
      if (length == 0)
      {
        break;
      }
      // printf("[child 2] Received: ");
      // print_struct(record);

      record.length = strlen(record.name);

      if (min_name_length <= record.length)
      {
        record.matches[1] = 1;
      }

      // printf("[child 2] Sending: ");
      // print_struct(record);

      if (write(pipe_3[1], &record, sizeof(record)) != sizeof(record))
      {
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
  if (child3_pid == 0)
  {
    close(pipe_1[0]);
    close(pipe_1[1]);
    close(pipe_2[0]);
    close(pipe_2[1]);
    close(pipe_3[1]);
    close(pipe_4[0]);

    while (1)
    {
      record_t record;
      auto length = read(pipe_3[0], &record, sizeof(record));
      if (length < 0)
      {
        perror("[child 3] read error");
        close(pipe_3[0]);
        close(pipe_4[1]);
        exit(EXIT_FAILURE);
      }
      if (length == 0)
      {
        break;
      }
      // printf("[child 3] Received: ");
      // print_struct(record);

      bool prev_space = true;


      for (ssize_t i = 0; i < record.length; i++)
      {
        if (record.name[i] == tolower(c_contained))
        {
          record.matches[2] = 1;
        }
        if (record.name[i] == ' ')
        {
          prev_space = true;
          continue;
        }
        if (prev_space && record.name[i] >= 'a' && record.name[i] <= 'z')
        {
          record.name[i] = toupper(record.name[i]);
        }
        prev_space = false;
      }


      // printf("[child 3] Sending: ");
      // print_struct(record);

      if (write(pipe_4[1], &record, sizeof(record)) != sizeof(record))
      {
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

  if (!f)
  {
    fprintf(stderr, "[parent] Failed to open file %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  char buffer[8192];

  int match_count = 0;
  int last_id = -1;

  while (fgets(buffer, sizeof(buffer), f) != nullptr)
  {
    bool is_match = false;
    record_t record;

    record.matches[0] = 0;
    record.matches[1] = 0;tolower
    record.matches[2] = 0;

    int l = strlen(buffer);
    if (buffer[l - 1] == '\n')
    {
      buffer[l - 1] = '\0';
      buffer[l] = '\0';
    }

    record.id = -1;
    record.length = -1;
    strcpy(record.name, buffer);
    // Send message to child 1
    // printf("[parent] Reading: ");
    // print_struct(record);

    if (write(pipe_1[1], &record, sizeof(record)) != sizeof(record))
    {
      perror("[parent] write error");
      fclose(f);

      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }

    // Wait for final output from child 3
    auto length = read(pipe_4[0], &record, sizeof(record));
    if (length < 0)
    {
      perror("[parent] read error\n");
      fclose(f);

      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }
    if (length == 0)
    {
      fprintf(stderr, "[parent] child pipeline ended unexpectedly\n");
      fclose(f);

      close(pipe_1[1]);
      close(pipe_4[0]);
      return EXIT_FAILURE;
    }

    is_match = record.matches[0] && record.matches[1] && record.matches[2];
    last_id = record.id;

    if(is_match) {
      match_count++;
    }

    printf("[parent] Output: ");
    print_struct(record, is_match);
    // printf("\n");
  }

  printf("\n--------------------------\n");
  printf("Zpracovano jmen: %d\n", last_id);
  printf("Vyhovujicich: %d\n", match_count);



  close(pipe_1[1]);
  close(pipe_4[0]);

  fclose(f);
  waitpid(child1_pid, nullptr, 0);
  waitpid(child2_pid, nullptr, 0);
  waitpid(child3_pid, nullptr, 0);
  exit(EXIT_SUCCESS);
}