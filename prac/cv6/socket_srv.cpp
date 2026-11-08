// ***************************************************************************
//
// OSY cv6 - Minesweeper server (multiple players, one shared board)
//
// Use: socket_srv port_number
//
// Every client is served by its own thread, the shared board is protected
// by a mutex. Protocol (lines ending with '\n'):
//   client -> server:  HELLO <nick> | OPEN <col><row> (e.g. OPEN C4) | BOARD
//   server -> client:  OK | INVALID | BOOM | WIN | BOARD ... END
//
// ***************************************************************************

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define SIZE 8
#define MINES 10
#define HIDDEN "🟦"
#define MINE "💣"

const char* symbols[] = {"⬜", "１", "２", "３", "４", "５", "６", "７", "８"};

int board[SIZE][SIZE];        // -1 = mine, 0..8 = number of neighbouring mines
bool revealed[SIZE][SIZE];    // true = uncovered
int g_safe_left;              // safe cells that are still hidden
std::vector<int> g_clients;   // sockets of players that sent HELLO
pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER; // guards everything above

bool inside(int r, int c) { return r >= 0 && r < SIZE && c >= 0 && c < SIZE; }

int count_neighbors(int row, int col) {
    int count = 0;
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
            if ((dr || dc) && inside(row + dr, col + dc) && board[row + dr][col + dc] == -1)
                count++;
    return count;
}

void print_board(FILE* out, bool show_all) {
    fprintf(out, "   ");
    for (int c = 0; c < SIZE; c++) fprintf(out, "  %c", 'A' + c);
    fprintf(out, "\n");
    for (int r = 0; r < SIZE; r++) {
        fprintf(out, "%2d ", r + 1);
        for (int c = 0; c < SIZE; c++) {
            bool show = show_all || revealed[r][c];
            fprintf(out, " %s", !show ? HIDDEN : board[r][c] == -1 ? MINE : symbols[board[r][c]]);
        }
        fprintf(out, "\n");
    }
}

void init_board() {
    memset(board, 0, sizeof(board));
    memset(revealed, 0, sizeof(revealed));
    for (int placed = 0; placed < MINES;) {
        int r = rand() % SIZE, c = rand() % SIZE;
        if (board[r][c] != -1) board[r][c] = -1, placed++;
    }
    for (int r = 0; r < SIZE; r++)
        for (int c = 0; c < SIZE; c++)
            if (board[r][c] != -1) board[r][c] = count_neighbors(r, c);
    g_safe_left = SIZE * SIZE - MINES;

    fprintf(stderr, "=== DEBUG: Complete board ===\n");
    print_board(stderr, true);
}

// "BOARD\n<board>END\n" as one string, so it can be sent at once
std::string board_msg(bool show_all) {
    char* buf = nullptr;
    size_t len = 0;
    FILE* f = open_memstream(&buf, &len);
    fprintf(f, "BOARD\n");
    print_board(f, show_all);
    fprintf(f, "END\n");
    fclose(f);
    std::string s(buf, len);
    free(buf);
    return s;
}

void send_str(int sock, const std::string& s) { send(sock, s.data(), s.size(), MSG_NOSIGNAL); }

void broadcast(const std::string& s) {
    for (int sock : g_clients) send_str(sock, s);
}

// uncover a cell, empty cells (0) uncover their neighbours too
void reveal(int r, int c) {
    if (!inside(r, c) || revealed[r][c] || board[r][c] == -1) return;
    revealed[r][c] = true;
    g_safe_left--;
    if (board[r][c] == 0)
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) reveal(r + dr, c + dc);
}

// end of game: show everything, disconnect all players and prepare a new board
void game_over(const char* result) {
    broadcast(std::string(result) + "\n" + board_msg(true));
    for (int sock : g_clients) shutdown(sock, SHUT_RDWR); // their threads end on EOF
    g_clients.clear();
    printf("Game over: %s. New game is ready.\n", result);
    init_board();
}

// handles one command, called with the mutex locked
void handle(int sock, char* nick, const char* cmd, const char* arg) {
    if (!strcasecmp(cmd, "HELLO") && *arg && !*nick) {
        snprintf(nick, 32, "%s", arg);
        g_clients.push_back(sock);
        printf("Player '%s' joined.\n", nick);
        send_str(sock, board_msg(false));
    } else if (!*nick) {
        send_str(sock, "INVALID\n"); // HELLO must come first
    } else if (!strcasecmp(cmd, "BOARD")) {
        send_str(sock, board_msg(false));
    } else if (!strcasecmp(cmd, "OPEN")) {
        int c = toupper(arg[0]) - 'A', r = -1;
        char end;
        if (sscanf(arg + (*arg != 0), "%d%c", &r, &end) != 1 || !inside(--r, c) || revealed[r][c]) {
            send_str(sock, "INVALID\n");
        } else if (board[r][c] == -1) {
            printf("Player '%s' opened %s: BOOM!\n", nick, arg);
            game_over("BOOM");
        } else {
            reveal(r, c);
            printf("Player '%s' opened %s.\n", nick, arg);
            send_str(sock, "OK\n");
            if (g_safe_left == 0) game_over("WIN");
            else broadcast(board_msg(false));
        }
    } else {
        send_str(sock, "INVALID\n");
    }
}

void* client_thread(void* t_arg) {
    int sock = (int)(intptr_t)t_arg;
    FILE* in = fdopen(dup(sock), "r");
    char line[256], nick[32] = "";

    while (fgets(line, sizeof(line), in)) {
        char cmd[16] = "", arg[64] = "";
        sscanf(line, "%15s %63s", cmd, arg);
        pthread_mutex_lock(&g_mtx);
        handle(sock, nick, cmd, arg);
        pthread_mutex_unlock(&g_mtx);
    }

    pthread_mutex_lock(&g_mtx);
    g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), sock), g_clients.end());
    pthread_mutex_unlock(&g_mtx);
    printf("Client %s disconnected.\n", *nick ? nick : "(no nick)");
    fclose(in);
    close(sock);
    return nullptr;
}

int main(int argc, char** argv) {
    int port = argc > 1 ? atoi(argv[1]) : 0;
    if (port <= 0) {
        fprintf(stderr, "Use: %s port_number\n", argv[0]);
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    srand(time(nullptr));
    init_board();

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(listen_sock, (sockaddr*)&addr, sizeof(addr)) < 0 || listen(listen_sock, 8) < 0) {
        perror("bind/listen");
        return EXIT_FAILURE;
    }
    printf("Minesweeper server listening on port %d.\n", port);

    while (1) {
        int sock = accept(listen_sock, nullptr, nullptr);
        if (sock < 0) continue;
        pthread_t thread;
        if (pthread_create(&thread, nullptr, client_thread, (void*)(intptr_t)sock) != 0) close(sock);
        else pthread_detach(thread);
    }
}
