// ZAM0074
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * Hlidat vstupy
 * neomezene vstupy
 * validace vstupu
 * funkce add a inv misto spagetti
 */

int main(int argc, const char** argv) {
    int add = 0;
    int inv = 0;
    const char *command = argv[0];
    const char *slash = strrchr(command, '/');

    if (slash != NULL) {
        command = slash + 1;
    }

    if (argc > 1) {
        command = argv[1];
    }

    if (strcmp(command, "add") == 0) {
        add = 1;
    } else if (strcmp(command, "inv") == 0) {
        inv = 1;
    } else {
        fprintf(stderr, "Invalid command: %s\n", command);
        return 1;
    }


    char *line;
    line = (char *)malloc(100 * sizeof(char));
    if (line == NULL || fgets(line, 100, stdin) == NULL) {
        free(line);
        return 1;
    }

    char *token = strtok(line, " \n");
    if (token == NULL) {
        free(line);
        return 1;
    }

    int letter = -1;

    if (*token >= 'a' && *token <= 'z') {
        letter = 1;
    } else if (*token >= 'A' && *token <= 'Z') {
        letter = 1;
    } else if (*token >= '0' && *token <= '9' || (*token == '-' && *(token + 1) >= '0' && *(token + 1) <= '9')) {
        letter = 0;
    }

    if (letter == -1) {
        perror("Invalid character");
        return -1;
    }


    while (token != NULL) {
        if (letter == 1 && *token >= '0' && *token <= '9' || letter == 0 && ((*token >= 'a' && *token <= 'z') || (*token >= 'A' && *token <= 'Z'))) {
            perror("Error: mixed input types");
            return -1;
        }

        if (add == 1) {
            if (letter == 1) {
                char c = token[0];
                c += 1;
                if (c > 'z') {
                    c = 'a';
                } else if (c > 'Z' && c < 'a') {
                    c = 'A';
                }
                printf("%c ", c);
            } else {
                int num = atoi(token);
                num += 1;
                printf("%d ", num);
            }
        } else if (inv == 1) {
            if (letter == 1) {
                char c = token[0];
                if (c >= 'a' && c <= 'z') {
                    c = c + ('A' - 'a');
                } else if (c >= 'A' && c <= 'Z') {
                    c = c - ('A' - 'a');
                }
                printf("%c ", c);
            } else {
                int num = atoi(token);
                num = -num;
                printf("%d ", num);
            }
        }



        token = strtok(NULL, " \n");
    }

    free(line);

    return 0;
}