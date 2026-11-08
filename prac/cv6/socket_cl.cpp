// ***************************************************************************
//
// OSY cv6 - Minesweeper client
//
// Use: socket_cl ip_or_name port_number [nick]
//
// No poll(): the main thread reads the keyboard and sends commands,
// a second thread reads the socket and prints the server messages.
// Commands: OPEN C4, BOARD (Ctrl+D ends the client).
//
// ***************************************************************************

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

int g_sock;

// thread reading from the server
void* reader_thread(void*) {
    FILE* in = fdopen(g_sock, "r");
    char line[256];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, "BOARD")) printf("\n");          // board follows, line by line
        else if (!strcmp(line, "END")) printf("> ");       // end of the board
        else if (!strcmp(line, "OK")) printf("Cell opened.\n");
        else if (!strcmp(line, "INVALID")) printf("Invalid command or position!\n> ");
        else if (!strcmp(line, "BOOM")) printf("\n💥 BOOM! Someone stepped on a mine. Game over.\n");
        else if (!strcmp(line, "WIN")) printf("\n🎉 WIN! All safe cells are uncovered.\n");
        else printf("%s\n", line);
        fflush(stdout);
    }
    printf("\nConnection closed by server.\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Use: %s ip_or_name port_number [nick]\n", argv[0]);
        return EXIT_FAILURE;
    }

    addrinfo hints{}, *ai;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(argv[1], argv[2], &hints, &ai) != 0) {
        fprintf(stderr, "Unknown host name!\n");
        return EXIT_FAILURE;
    }
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(g_sock, ai->ai_addr, ai->ai_addrlen) < 0) {
        perror("connect");
        return EXIT_FAILURE;
    }
    freeaddrinfo(ai);

    char line[256] = "";
    if (argc > 3) {
        snprintf(line, sizeof(line), "%s", argv[3]);
    } else {
        printf("Nick: ");
        if (!fgets(line, sizeof(line), stdin)) return EXIT_FAILURE;
        line[strcspn(line, "\r\n")] = '\0';
    }
    dprintf(g_sock, "HELLO %s\n", line);
    printf("Commands: OPEN C4, BOARD. Ctrl+D to quit.\n");

    pthread_t reader;
    pthread_create(&reader, nullptr, reader_thread, nullptr);

    // keyboard -> server
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (*line) dprintf(g_sock, "%s\n", line);
    }

    shutdown(g_sock, SHUT_WR); // server closes the connection, reader ends the program
    pthread_join(reader, nullptr);
    return 0;
}
