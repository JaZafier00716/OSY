// ***************************************************************************
//
// OSY cv7 - OSYLights server (multi-process, players take turns via semaphores)
//
// Use: server [-h] [-d] port_number COLSxROWS      (e.g. server 5000 5x5)
//
// Every client is served by its own child process (fork). The whole game
// (board, turn counter, game state, PID of the last mover, player PIDs and
// semaphores) lives in one anonymous shared memory block, no process keeps
// its own copy of the game.
//
// Each seat has a semaphore, the player may move only after his semaphore
// lets him through. After a valid move the turn is passed to the next
// connected player by sem_post(). Toggling a light flips it and its
// neighbours up, down, left and right.
//
// The game ends when all lights are off (WIN) or after 2 * cells moves (LOSS).
// Then every waiting player is woken up, so all clients learn the result.
//
// ***************************************************************************

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <csignal>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include "definitions.h"

#define MAX_CLIENTS 64
#define MAX_DIM 26 // columns are letters A..Z
#define SEAT_RESERVED -1

#define STR_LIGHT_ON "\U0001F4A1" // 💡 (2 terminal columns wide)
#define STR_LIGHT_OFF "·"    // ·  (1 terminal column wide)

enum game_state_t { GAME_RUNNING = 0, GAME_WON, GAME_LOST };

typedef struct game_t {
    // --- board ---
    int cols, rows;
    char board[MAX_DIM][MAX_DIM]; // 1 = light on, 0 = light off

    // --- game info ---
    int state;     // game_state_t
    int turns;     // moves already played
    int max_turns; // 2 * cols * rows
    int last_pid;  // PID of the player (server process) who made the last move
    char last_move[16];

    // --- players ---
    int PID[MAX_CLIENTS];
    sem_t SEM[MAX_CLIENTS];
    sem_t mutex;

    int lights_on() const {
        int count = 0;
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++)
                count += board[r][c];
        return count;
    }

    std::string board_to_string() const {
        std::string s = "\n========== OSY LIGHTS ==========\n\n";

        s += "      ";
        for (int c = 0; c < cols; c++) {
            s += char('A' + c);
            s += "   ";
        }
        s += "\n";

        std::string border = "    +" + std::string(4 * cols + 1, '-') + "+\n";
        s += border;
        for (int r = 0; r < rows; r++) {
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "%3d | ", r + 1);
            s += prefix;
            for (int c = 0; c < cols; c++) {
                // both variants take 4 terminal columns
                s += board[r][c] ? STR_LIGHT_ON "  " : STR_LIGHT_OFF "   ";
            }
            s += "|\n";
        }
        s += border;
        return s;
    }

    std::string status_to_string() const {
        std::string s = "\nTurn: " + std::to_string(turns) + " / " + std::to_string(max_turns);
        s += "\nLights ON: " + std::to_string(lights_on());
        if (last_pid > 0) {
            s += "\nLast move: " + std::string(last_move) + " by PID " + std::to_string(last_pid);
        }
        s += "\n";
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
            fprintf(stderr, out_format[t_log_level], errno, strerror(errno), l_buf);
            break;
    }
    fflush(stdout);
}

int send_str(int t_sock, const char* msg, int length) {
    return send(t_sock, msg, length, MSG_NOSIGNAL);
}

int send_str(int t_sock, std::string msg) {
    return send(t_sock, msg.c_str(), msg.length(), MSG_NOSIGNAL);
}

void broadcast(const char* message, int length) {
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        if (client_fd[i] >= 0) {
            send_str(client_fd[i], message, length);
        }
    }
}

void strip(char* s) {
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';
}

// **********************************************************************************************
// Helper functions

const char g_help[] = {"\n"
                       " OSYLights server\n"
                       "\n"
                       " Use: %s [-h] [-d] port_number COLSxROWS\n"
                       "\n"
                       "    COLSxROWS  board size, e.g. 5x5 or 8x6 (1..26 each)\n"
                       "    -d  debug_mod\n"
                       "    -h this help\n"
                       "\n"};

bool parse_size(const char* s, int* cols, int* rows) {
    char x;
    int used = 0;
    // "8x6" -> cols = 8, rows = 6; nothing may follow
    if (3 != sscanf(s, "%d%c%d%n", cols, &x, rows, &used) || s[used] != '\0') {
        return false;
    }
    if (x != 'x' && x != 'X') {
        return false;
    }
    return 1 <= *cols && *cols <= MAX_DIM && 1 <= *rows && *rows <= MAX_DIM;
}

// **********************************************************************************************
// Shared memory functions

void lock() {
    while (sem_wait(&game->mutex) < 0 && errno == EINTR)
        ;
}

void unlock() {
    sem_post(&game->mutex);
}

// flip one light and its 4 neighbours (borders are checked)
void toggle(int row, int col) {
    const int dr[] = {0, -1, 1, 0, 0};
    const int dc[] = {0, 0, 0, -1, 1};
    for (int i = 0; i < 5; i++) {
        int r = row + dr[i];
        int c = col + dc[i];
        if (0 <= r && r < game->rows && 0 <= c && c < game->cols) {
            game->board[r][c] ^= 1;
        }
    }
}

// new random game, called with the mutex locked and an empty table
void new_game() {
    memset(game->board, 0, sizeof(game->board));
    // Random presses from the dark board -> random lights and the game is always solvable.
    int cells = game->cols * game->rows;
    do {
        int presses = 1 + rand() % cells;
        for (int i = 0; i < presses; i++) {
            toggle(rand() % game->rows, rand() % game->cols);
        }
    } while (game->lights_on() == 0);

    game->state = GAME_RUNNING;
    game->turns = 0;
    game->max_turns = 2 * cells;
    game->last_pid = 0;
    game->last_move[0] = '\0';

    // nobody is sitting at the table -> reset the turn semaphores
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sem_destroy(&game->SEM[i]);
        sem_init(&game->SEM[i], 1, 0);
    }
    log_msg(LOG_INFO, "New game %dx%d, %d lights on.", game->cols, game->rows, game->lights_on());
}

void shm_init(int cols, int rows) {
    game = (game_t*)mmap(
        NULL, sizeof(*game), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (game == MAP_FAILED) {
        log_msg(LOG_ERROR, "mmap failed");
        exit(EXIT_FAILURE);
    }

    game->cols = cols;
    game->rows = rows;
    for (size_t i = 0; i < MAX_CLIENTS; i++) {
        game->PID[i] = 0;
        if (0 > sem_init(&game->SEM[i], 1, 0)) {
            log_msg(LOG_ERROR, "sem_init failed");
            exit(EXIT_FAILURE);
        }
    }
    sem_init(&game->mutex, 1, 1);

    lock();
    new_game();
    unlock();
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

    while (0 < (len = read(STDIN_FILENO, buf, sizeof(buf) - 1))) {
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

/* ---------- seats and turns ---------- */

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

// returns seat number, -1 = table full, -2 = finished game, players still leaving
int take_seat() {
    lock();
    bool was_empty = table_empty();
    if (game->state != GAME_RUNNING) {
        if (!was_empty) {
            unlock();
            return -2;
        }
        new_game(); // previous game is over and everybody left -> start a new one
    }

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

// player leaves during a running game, the turn must not get lost
void release_seat(int seat, bool had_turn) {
    lock();
    game->PID[seat] = 0;
    if (game->state == GAME_RUNNING && (had_turn || sem_trywait(&game->SEM[seat]) == 0)) {
        int next = next_player(seat);
        if (next >= 0) {
            sem_post(&game->SEM[next]);
        }
    }
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
            close(client_fd[i]);
        }
    }
}

// true when the client closed the connection
bool client_gone(int fd) {
    pollfd p = {fd, POLLRDHUP, 0};
    if (0 < poll(&p, 1, 0) && (p.revents & (POLLRDHUP | POLLHUP | POLLERR))) {
        return true;
    }
    return false;
}

// throw away everything typed while it was not the player's turn
void flush_input(int fd) {
    char junk[256];
    while (0 < recv(fd, junk, sizeof(junk), MSG_DONTWAIT))
        ;
}

// Wait on own semaphore, meanwhile watch whether the client is still connected.
bool wait_for_turn(int seat, int fd) {
    while (1) {
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 200 * 1000 * 1000; // 200 ms
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        if (0 == sem_timedwait(&game->SEM[seat], &ts)) {
            return true;
        }
        if (errno != ETIMEDOUT && errno != EINTR) {
            return false;
        }
        if (client_gone(fd)) {
            return false;
        }
    }
}

// parse "B-3" / "b3" -> row, col; false for invalid coordinates
bool parse_move(char* cmd, int* row, int* col) {
    strip(cmd);
    char* p = cmd;
    while (isspace((unsigned char)*p))
        p++;

    if (!isalpha((unsigned char)*p)) {
        return false;
    }
    *col = toupper((unsigned char)*p) - 'A';
    p++;
    if (*p == '-') {
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    char* end;
    long r = strtol(p, &end, 10);
    if (*end != '\0') {
        return false;
    }
    *row = (int)r - 1;
    return 0 <= *col && *col < game->cols && 0 <= *row && *row < game->rows;
}

// wake every other player (also those waiting on their semaphores)
void wake_everybody(int my_seat) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (i != my_seat && game->PID[i] != 0) {
            sem_post(&game->SEM[i]);
        }
    }
}

std::string result_to_string() {
    std::string s = "\n";
    if (game->state == GAME_WON) {
        s += "*** GAME OVER - ALL LIGHTS ARE OFF, PLAYERS WIN! ***\n";
        s += "Winning move: " + std::string(game->last_move) + " by PID " +
             std::to_string(game->last_pid) + "\n";
    } else {
        s += "*** GAME OVER - OUT OF MOVES, PLAYERS LOSE! ***\n";
        s += "Last move: " + std::string(game->last_move) + " by PID " +
             std::to_string(game->last_pid) + "\n";
    }
    s += "Turns played: " + std::to_string(game->turns) + " / " + std::to_string(game->max_turns) +
         ", lights still on: " + std::to_string(game->lights_on()) + "\n";
    return s;
}

// final message for a client, the game data are read under the mutex
void send_result(int fd) {
    lock();
    std::string msg = game->board_to_string() + result_to_string();
    unlock();
    send_str(fd, msg);
}

void handle_client(int fd, int seat) {
    char cmd[64];

    lock();
    std::string intro = game->board_to_string() + game->status_to_string();
    unlock();
    intro += "\nYou are player number " + std::to_string(seat) + ", your PID: " +
             std::to_string(getpid()) + "\nWaiting for your turn...\n";
    send_str(fd, intro);

    while (1) {
        if (!wait_for_turn(seat, fd)) {
            log_msg(LOG_INFO, "Player %d (PID %d) left while waiting.", seat, getpid());
            release_seat(seat, false); // disconnected while waiting for turn
            return;
        }

        lock();
        if (game->state != GAME_RUNNING) {
            // woken up because the game ended
            game->PID[seat] = 0;
            unlock();
            send_result(fd);
            return;
        }
        std::string screen = game->board_to_string() + game->status_to_string();
        unlock();
        flush_input(fd);
        screen += "Player PID: " + std::to_string(getpid()) + "\n";
        send_str(fd, screen);

        int row, col;
        while (1) {
            send_str(fd, "\nYour turn! Enter position (e.g. C-3): ");
            ssize_t len = read(fd, cmd, sizeof(cmd) - 1);
            if (0 >= len) {
                log_msg(LOG_INFO, "Player %d (PID %d) left during his turn.", seat, getpid());
                release_seat(seat, true); // disconnected during turn, pass it on
                return;
            }
            cmd[len] = '\0';
            if (parse_move(cmd, &row, &col)) {
                break;
            }
            send_str(fd, "Invalid position, try again.\n"); // turn is NOT passed
        }

        // --- make the move in shared memory ---
        lock();
        toggle(row, col);
        game->turns++;
        game->last_pid = getpid();
        snprintf(game->last_move, sizeof(game->last_move), "%c-%d", 'A' + col, row + 1);
        if (game->lights_on() == 0) {
            game->state = GAME_WON;
        } else if (game->turns >= game->max_turns) {
            game->state = GAME_LOST;
        }
        int state = game->state;
        log_msg(LOG_INFO, "Player %d (PID %d) played %s, turn %d/%d, lights on %d.",
                seat, getpid(), game->last_move, game->turns, game->max_turns, game->lights_on());

        if (state != GAME_RUNNING) {
            log_msg(LOG_INFO, "%s", state == GAME_WON ? "Game won!" : "Game lost!");
            game->PID[seat] = 0;
            wake_everybody(seat);
            unlock();
            send_result(fd);
            return;
        }

        std::string after = game->board_to_string() + game->status_to_string();
        int next = next_player(seat);
        sem_post(&game->SEM[next]); // pass the turn (may be myself if I am alone)
        unlock();

        send_str(fd, after + "Waiting for your turn...\n");
    }
}

/* ---------- thread 2: new clients ---------- */
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
            }
            continue;
        }

        int seat = take_seat();

        if (-2 == seat) {
            send_str(socket_client_fd, "Game has just finished, connect again in a moment.\n");
            close(socket_client_fd);
            continue;
        }
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
            log_msg(LOG_INFO, "Player %d connected, PID %d.", seat, getpid());

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
    }

    return NULL;
}

int main(int argc, char** argv) {
    int port_number = 0;
    int cols = 0, rows = 0;

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
            } else if (cols == 0) {
                if (!parse_size(argv[i], &cols, &rows)) {
                    log_msg(LOG_INFO, "Bad board size '%s'!", argv[i]);
                    printf(g_help, argv[0]);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    if (port_number <= 0) {
        log_msg(LOG_INFO, "Bad or missing port number %d!", port_number);
        printf(g_help, argv[0]);
        exit(EXIT_FAILURE);
    }
    if (cols == 0) {
        log_msg(LOG_INFO, "Missing board size!");
        printf(g_help, argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN); // a write to a closed client won't kill the process
    signal(SIGCHLD, SIG_IGN); // finished children are reaped automatically, no zombies

    srand(time(NULL) ^ getpid());

    // Initialize the shared memory
    shm_init(cols, rows);
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
    if (0 > setsockopt(listening_socket, SOL_SOCKET, SO_REUSEADDR, &optional, sizeof(optional))) {
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
    if (0 > listen(listening_socket, 8)) {
        log_msg(LOG_ERROR, "Unable to listen on given port!");
        shm_destroy();
        close(listening_socket);
        exit(EXIT_FAILURE);
    }

    pthread_t t_in;
    pthread_create(&t_in, NULL, stdin_thread, (void*)&listening_socket);

    pthread_t t_acc;
    pthread_create(&t_acc, NULL, accept_thread, (void*)&listening_socket);

    pthread_join(t_in, NULL);
    pthread_join(t_acc, NULL);

    pthread_mutex_lock(&fd_lock);
    const char bye[] = "[SERVER] Server is shutting down.\n";
    broadcast(bye, sizeof(bye) - 1);
    pthread_mutex_unlock(&fd_lock);

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
