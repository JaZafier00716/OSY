#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

int main (int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s N filter_mode transform_mode [-v]\n", argv[0]);
        return EXIT_FAILURE;
    }

    bool v = false;

    if (argc == 5 && strcmp(argv[4], "-v") == 0) {
        fprintf(stderr, "Verbose mode enabled\n");
        v = true;
    }


    int pipe_0[2];
    int pipe_1[2];
    int pipe_2[2];

    if (pipe(pipe_0) == -1 || pipe(pipe_1) == -1 || pipe(pipe_2) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    printf("[%s] Starting ./gen %s\n", argv[0], argv[1]);
    pid_t child_0 = fork();
    if (child_0 == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }
    if (child_0 == 0) {
        if (dup2(pipe_0[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipe_0[0]);
        close(pipe_0[1]);
        close(pipe_1[0]);
        close(pipe_1[1]);
        close(pipe_2[0]);
        close(pipe_2[1]);

        char *const args[] = {const_cast<char *>("./gen"), argv[1], nullptr};
        execvp(args[0], args);
        perror("execvp ./gen");
        _exit(EXIT_FAILURE);
    }

    printf("[%s] Starting ./filter %s\n", argv[0], argv[2]);
    pid_t child_1 = fork();
    if (child_1 == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }
    if (child_1 == 0) {
        if (dup2(pipe_0[0], STDIN_FILENO) == -1 ||
            dup2(pipe_1[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipe_0[0]);
        close(pipe_0[1]);
        close(pipe_1[0]);
        close(pipe_1[1]);
        close(pipe_2[0]);
        close(pipe_2[1]);

        char *const args[] = {const_cast<char *>("./filter"), argv[2], nullptr};
        execvp(args[0], args);
        perror("execvp ./filter");
        _exit(EXIT_FAILURE);
    }

    printf("[%s] Starting ./transform %s\n", argv[0], argv[3]);
    pid_t child_2 = fork();
    if (child_2 == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }
    if (child_2 == 0) {
        if (dup2(pipe_1[0], STDIN_FILENO) == -1 ||
            dup2(pipe_2[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipe_0[0]);
        close(pipe_0[1]);
        close(pipe_1[0]);
        close(pipe_1[1]);
        close(pipe_2[0]);
        close(pipe_2[1]);

        char *const args[] = {const_cast<char *>("./transform"), argv[3], nullptr};
        execvp(args[0], args);
        perror("execvp ./transform");
        _exit(EXIT_FAILURE);
    }

    printf("[%s] Starting ./sum\n", argv[0]);
    pid_t child_3 = fork();
    if (child_3 == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }
    if (child_3 == 0) {
        if (dup2(pipe_2[0], STDIN_FILENO) == -1) {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }

        close(pipe_0[0]);
        close(pipe_0[1]);
        close(pipe_1[0]);
        close(pipe_1[1]);
        close(pipe_2[0]);
        close(pipe_2[1]);

        char *const args[] = {const_cast<char *>("./sum"), nullptr};
        execvp(args[0], args);
        perror("execvp ./sum");
        _exit(EXIT_FAILURE);
    }

    close(pipe_0[0]);
    close(pipe_0[1]);
    close(pipe_1[0]);
    close(pipe_1[1]);
    close(pipe_2[0]);
    close(pipe_2[1]);



    waitpid(child_0, nullptr, 0);
    waitpid(child_1, nullptr, 0);
    waitpid(child_2, nullptr, 0);
    waitpid(child_3, nullptr, 0);

    return EXIT_SUCCESS;
}
