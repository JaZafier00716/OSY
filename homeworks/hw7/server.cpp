#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <csignal>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "definitions.h"
#define MAX_CLIENTS 64
#define BOARD_SIZE 9
#define SEAT_RESERVED -1

typedef struct game_t {
    char board[BOARD_SIZE][BOARD_SIZE];
    int PID[MAX_CLIENTS];
    sem_t SEM[MAX_CLIENTS];
    sem_t mutex;

    std::string board_to_string() const {
        std::string s;
        s += "     A B C D E F G H I\n";
        s += "   +-------------------+\n";
        for (int i = 0; i < BOARD_SIZE; i++) {
            s += ' ';
            s += char('1' + i); // row number 1..9
            s += " | ";
            for (int j = 0; j < BOARD_SIZE; j++) {
                s += board[i][j];
                s += ' ';
            }
            s += "|\n";
        }
        s += "   +-------------------+\n";
        return s;
    }
} game_t;

int client_fd[MAX_CLIENTS];

int g_should_run = 1;
game_t* game;

// **********************************************************************************************
// Print functions
int g_debug = LOG_INFO;

void log_msg(int t_log_level, const char* t_form, ...) {
    const char* out_format[] = {"ERR: (%d-%s) %s\n", "INF: %s\n", "DEB: %s\n"};

    if (t_log_level && t_log_level > g_debug) {
        return;
    }

    char l_buf[4096];
    va_list l_arg;
    va_start(l_arg, t_form);

    vsnprintf(l_buf, sizeof(l_buf), t_form, l_arg);

    va_end(l_arg);

    switch (t_log_level) {
        case LOG_INFO:
        case LOG_DEBUG:
            fprintf(stdout, out_format[t_log_level], l_buf);
            break;
        case LOG_ERROR:
            fprintf(stderr, out_format[t_log_level], l_buf);
            break;
    }
}

int send_str(int t_sock, const char* msg, int length) {
    return send(t_sock, msg, length, MSG_NOSIGNAL);
}

int send_str(int t_sock, std::string msg) {
    return send(t_sock, msg.c_str(), msg.length(), MSG_NOSIGNAL);
}

void broadcast(char* message, int length) {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        send_str(client_fd[i], message, length);
    }
}

void broadcast(std::string message) {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        send_str(client_fd[i], message);
    }
}

void send_board(int client_socket_fd) {
    send_str(client_socket_fd, game->board_to_string());
}

void send_seat_info(int client_socket_fd, int seat) {
    std::string str = "You are player number: " + std::to_string(seat);

    send_str(client_socket_fd, str);
}

void strip(char* s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

// **********************************************************************************************
// Helper functions

const char g_help[] = {"\n"
                       " Socket server example\n"
                       "\n"
                       " Use: %s [-h] [-d] port_number\n"
                       "\n"
                       "    -d  debug_mod\n"
                       "    -h this help\n"
                       "\n"};

// **********************************************************************************************
// Shared memory funtions

void shm_init() {
    game = (game_t*)mmap(
        NULL, sizeof(*game), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    memset(game->board, '.', sizeof(game->board)); // initialize the board to array of '.'
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        game->PID[i] = 0;
        if (0 > sem_init(&game->SEM[i], 1, 0)) {
            log_msg(LOG_ERROR, "sem_init failed");
            exit(EXIT_FAILURE);
        }
    }
    sem_init(&game->mutex, 1, 1);
}

void shm_destroy() {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        sem_destroy(&game->SEM[i]);
    }
    sem_destroy(&game->mutex);
    munmap(game, sizeof(*game));
}

// **********************************************************************************************
// Thread functions

pthread_mutex_t fd_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---------- thread 1: server terminal ---------- */
void* stdin_thread(void* arg) {
    int listening_socket = *(int*)arg;
    char buf[512];

    ssize_t len;

    while (0 < (len = read(STDIN_FILENO, buf, sizeof(buf)))) {
        buf[len] = '\0';
        buf[strcspn(buf, "\n")] = '\0';

        if (0 == strcmp(buf, STR_QUIT)) {
            log_msg(LOG_INFO, "Request to 'quit' entered.");
            break;
        }
        if ('\0' == buf[0]) {
            continue; // Empty message
        }

        char msg_for_clients[512 + 16];
        int message_len = snprintf(msg_for_clients, sizeof(msg_for_clients), "[SERVER] %s\n", buf);

        pthread_mutex_lock(&fd_lock);
        broadcast(msg_for_clients, message_len);
        pthread_mutex_unlock(&fd_lock);
    }

    // Thread closing means 'quit' or an error -> server shutdown
    g_should_run = 0;
    shutdown(listening_socket, SHUT_RDWR);

    if (0 == len) {
        log_msg(LOG_DEBUG, "Stdin closed.");
        return NULL;
    }
    if (0 > len) {
        log_msg(LOG_DEBUG, "Unable to read from stdin!");
    }

    return NULL;
}

/* ---------- thread 2: new clients ---------- */

void lock() {
    while (sem_wait(&game->mutex) < 0 && errno == EINTR)
        ;
}

void unlock() {
    sem_post(&game->mutex);
}

int next_player(int from) {
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        int next = (from + i) % MAX_CLIENTS;
        if (game->PID[next] != 0) {
            return next;
        }
    }
    return -1; // no active players
}

bool table_empty() {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (game->PID[i] != 0) {
            return false;
        }
    }
    return true;
}

int take_seat() {
    lock();
    bool was_empty = table_empty();
    int seat = -1;
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (game->PID[i] == 0) {
            seat = (int)i;
            break;
        }
    }
    if (0 <= seat) {
        game->PID[seat] = SEAT_RESERVED;
        if (was_empty) {
            // if table was empty, new player gets the turn
            sem_post(&game->SEM[seat]);
        }
    }
    unlock();
    return seat;
}

void release_seat(int seat, bool had_turn) {
    lock();
    game->PID[seat] = 0;
    if (had_turn || sem_trywait(&game->SEM[seat]) == 0) {
        int next = next_player(seat);
        if (next >= 0) {
            sem_post(&game->SEM[next]);
        }
    }
    unlock();
}

void pass_turn(int seat) {
    lock();
    int next = next_player(seat);
    sem_post(&game->SEM[next]);
    unlock();
}

void claim_seat(int seat) {
    lock();
    game->PID[seat] = getpid();
    unlock();
}

void close_all_fd_except(int seat) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i != seat && client_fd[i] >= 0) {
            // if not except fd and fd is active, close it
            close(client_fd[i]);
        }
    }
}

bool wait_for_turn(int seat) {
    if (0 == sem_wait(&game->SEM[seat])) {
        return true;
    }
    return false;
}

bool try_move(char* cmd) {
    strip(cmd);

    if (strlen(cmd) != 5 || cmd[1] != '-' || cmd[3] != '-') {
        return false;
    }

    char col = toupper(cmd[0]);
    char row = cmd[2];
    char letter = toupper(cmd[4]);

    if ('A' > col || col > 'A' + BOARD_SIZE - 1) { // A..I
        return false;
    }
    if ('1' > row || row > '0' + BOARD_SIZE) { // 1..9
        return false;
    }
    if ('A' > letter || letter > 'Z') {
        return false;
    }

    char& cell = game->board[row - '1'][col - 'A'];
    if (cell != '.') {
        return false; // already taken
    }

    cell = letter;
    return true;
}

bool board_full() {
    for (size_t i = 0; i < BOARD_SIZE; i++) {
        for (size_t j = 0; j < BOARD_SIZE; j++) {
            if (game->board[i][j] == '.') {
                return false;
            }
        }
    }
    return true;
}

void handle_client(int client_socket_fd, int seat) {
    char cmd[64];

    send_board(client_socket_fd);
    send_seat_info(client_socket_fd, seat);

    while (1) {
        if (!wait_for_turn(seat)) {
            release_seat(seat, false); // disconnected while waiting for turn
            return;
        }

        send_board(client_socket_fd);
        while (1) {
            // Player's turn
            send_str(client_socket_fd,
                     "Move structure: [column letter]-[row number]-[character]\n");
            send_str(client_socket_fd, "Your move (e.g. A-5-Q): ");
            if (0 >= read(client_socket_fd, &cmd, sizeof(cmd) - 1)) {
                release_seat(seat, true); // disconnected during turn, pass it on
                return;
            }
            if (try_move(cmd)) {
                break;
            }
            send_str(client_socket_fd, "Invalid move, try again.\n");
        }
        send_board(client_socket_fd);
        lock();
        unlock();
        pass_turn(seat);
    }
}

void* accept_thread(void* arg) {
    int listening_socket = *(int*)arg;
    while (g_should_run) {
        sockaddr_in l_rsa;
        int l_rsa_size = sizeof(l_rsa);
        int socket_client_fd = accept(listening_socket, (sockaddr*)&l_rsa, (socklen_t*)&l_rsa_size);
        if (socket_client_fd < 0) {
            if (!g_should_run) {
                break;
            }
            if (errno != EINTR) {
                log_msg(LOG_ERROR, "Unable to accept new client.");
                continue;
            }
        }

        int seat = take_seat();

        if (0 > seat) {
            send_str(socket_client_fd, "Table is full.\n");
            close(socket_client_fd);
            continue;
        }

        pthread_mutex_lock(&fd_lock);
        if (0 <= client_fd[seat]) {
            close(client_fd[seat]);
        }
        client_fd[seat] = socket_client_fd;
        pthread_mutex_unlock(&fd_lock);

        pid_t child_process = fork();
        if (0 == child_process) {
            // Child process
            close(listening_socket);
            close_all_fd_except(seat);
            claim_seat(seat);

            /* ---=== Handle client ===--- */
            handle_client(socket_client_fd, seat);

            shutdown(socket_client_fd, SHUT_RDWR);
            _exit(0);
        }
        if (child_process < 0) {
            log_msg(LOG_ERROR, "Fork error. Could not create child process.");
            release_seat(seat, false);
            continue;
        }

        if (board_full()) {
            broadcast("Game Over!");
            memset(game->board, '.', sizeof(game->board));
        }
    }

    return NULL;
}

int main(int argc, char** argv) {
    int port_number = 0;

    for (int i = 1; i < argc; i++) {
        if (0 == strcmp(argv[i], "-d")) {
            g_debug = LOG_DEBUG;
            continue;
        }
        if (0 == strcmp(argv[i], "-h")) {
            printf(g_help, argv[0]);
            exit(EXIT_SUCCESS);
        }

        if (*argv[i] != '-') {
            if (port_number == 0) {
                port_number = atoi(argv[i]);
            }
        }
    }

    if (port_number <= 0) {
        log_msg(LOG_INFO, "Bad or missing port number %d!", port_number);
        printf(g_help, argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN); // a write to a closed client won't kill the process
    signal(SIGCHLD, SIG_IGN); // finished children are reaped automatically, no zombies

    // Initialize the shared memory
    shm_init();
    memset(client_fd, -1, sizeof(client_fd));

    log_msg(LOG_INFO, "Server will listen on port: %d.", port_number);

    int listening_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (listening_socket < 0) {
        log_msg(LOG_ERROR, "Unable to create socket.");
        shm_destroy();
        exit(EXIT_FAILURE);
    }

    in_addr any_address = {INADDR_ANY};
    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port_number);
    server_address.sin_addr = any_address;

    int optional = 1;
    // Enable port number reusing
    if (0 > setsockopt(
            listening_socket, SOL_SOCKET, SO_REUSEADDR, &optional, sizeof(server_address))) {
        log_msg(LOG_ERROR, "Unable to set socket option!");
        shm_destroy();
        close(listening_socket);
        exit(EXIT_FAILURE);
    }

    // assign port number to the socket
    if (0 > bind(listening_socket, (const sockaddr*)&server_address, sizeof(server_address))) {
        log_msg(LOG_ERROR, "Bind failed!");
        shm_destroy();
        close(listening_socket);
        exit(EXIT_FAILURE);
    }

    // Listen on on the port
    if (0 > listen(listening_socket, 1)) {
        log_msg(LOG_ERROR, "Unable to listen on given port!");
        shm_destroy();
        close(listening_socket);
        exit(EXIT_FAILURE);
    }

    pollfd reading_poll[2];
    reading_poll[0].fd = STDIN_FILENO;
    reading_poll[0].events = POLLIN;
    reading_poll[1].fd = listening_socket;
    reading_poll[1].events = POLLIN;

    pthread_t t_in;
    pthread_create(&t_in, NULL, stdin_thread, (void*)&listening_socket);

    pthread_t t_acc;
    pthread_create(&t_acc, NULL, accept_thread, (void*)&listening_socket);

    pthread_join(t_in, NULL);
    pthread_join(t_acc, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (game->PID[i] > 0) {
            kill(game->PID[i], SIGTERM);
        }
    }

    log_msg(LOG_INFO, "Server is shutting down");
    shm_destroy();
    close(listening_socket);
    exit(EXIT_SUCCESS);
}
