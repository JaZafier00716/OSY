// ***************************************************************************
//
// Program example for labs in subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2026
//
// Example of socket server.
//
// This program is example of socket server and it allows to connect and serve
// the only one client.
// The mandatory argument of program is port number for listening.
//
// ***************************************************************************

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <cstdarg>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#define STR_CLOSE "close"
#define STR_QUIT "quit"

// ***************************************************************************
// log messages

#define LOG_ERROR 0 // errors
#define LOG_INFO 1  // information and notifications
#define LOG_DEBUG 2 // debug messages

#define NICK_LEN 32  // max nick length incl. '\0'
#define LINE_LEN 512 // max length of one message line

// debug flag
int g_debug = LOG_INFO;

#define PLAYFIELD_SIZE 8

void log_msg(int t_log_level, const char* t_form, ...) {
    const char* out_fmt[] = {"ERR: (%d-%s) %s\n", "INF: %s\n", "DEB: %s\n"};

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
            fprintf(stdout, out_fmt[t_log_level], l_buf);
            break;

        case LOG_ERROR:
            fprintf(stderr, out_fmt[t_log_level], errno, strerror(errno), l_buf);
            break;
    }
}

int send_str(int t_sock, const char* t_str) {
    return send(t_sock, t_str, strlen(t_str), 0);
}

// ***************************************************************************
// Shared client list (for the server with multiple clients)
struct client_t {
    int sock;            // socket number of the client
    char nick[NICK_LEN]; // nick of the client (empty string if not entered yet)
    bool used;           // true if this slot is used, false if free
    size_t score = 0;
};

std::vector<client_t> g_clients; // shared list of clients

size_t clients_add(int t_sock) {
    for (size_t i = 0; i < g_clients.size(); i++) {
        if (!g_clients[i].used) {
            g_clients[i].sock = t_sock;
            g_clients[i].used = true;
            g_clients[i].nick[0] = '\0'; // empty nick
            return i;
        }
    }
    g_clients.push_back({t_sock, "", true});
    return g_clients.size() - 1;
}

void clients_remove(size_t t_inx) {
    if (t_inx < g_clients.size()) {
        g_clients[t_inx].used = false;
        g_clients[t_inx].nick[0] = '\0';
        g_clients[t_inx].sock = -1;
        close(g_clients[t_inx].sock);
    }
}

bool clients_nick_exists(const char* t_nick) {
    for (const auto& client : g_clients) {
        if (client.used && strcmp(client.nick, t_nick) == 0) {
            return true;
        }
    }
    return false;
}

void broadcast(const char* t_msg, int t_except_sock) {
    for (const auto& client : g_clients) {
        if (client.used && client.sock != t_except_sock) {
            send_str(client.sock, t_msg);
        }
    }
}

void strip(char* t_str) {
    int l_len = strlen(t_str);
    while (l_len > 0
           && (t_str[l_len - 1] == '\n' || t_str[l_len - 1] == '\r' || t_str[l_len - 1] == ' '
               || t_str[l_len - 1] == '\t')) {
        t_str[--l_len] = '\0';
    }
}

// ***************************************************************************
// Playfield and card definitions

size_t g_player_turn_id = 0; // ID of the player whose turn it is

enum card_state_t { CARD_SHOWN, CARD_HIDDEN, CARD_GUESSED }; // state of one card

struct card_t {
    int image_id;
    card_state_t state = CARD_HIDDEN;
};

card_t playfield[PLAYFIELD_SIZE][PLAYFIELD_SIZE]
    = {{{0}, {0}, {1}, {1}, {2}, {2}, {3}, {3}},
       {{4}, {4}, {5}, {5}, {6}, {6}, {7}, {7}},
       {{8}, {8}, {9}, {9}, {10}, {10}, {11}, {11}},
       {{12}, {12}, {13}, {13}, {14}, {14}, {15}, {15}},
       {{16}, {16}, {17}, {17}, {18}, {18}, {19}, {19}},
       {{20}, {20}, {21}, {21}, {22}, {22}, {23}, {23}},
       {{24}, {24}, {25}, {25}, {26}, {26}, {27}, {27}},
       {{28}, {28}, {29}, {29}, {30}, {30}, {31}, {31}}};

std::string images[32] = {"🍎", "🍌", "🍇", "🍒", "🍓", "🍉", "🍋", "🍑", "🥝", "🍍", "🥕",
                          "🌽", "🍄", "🌵", "🌻", "🌈", "🐶", "🐱", "🐸", "🐵", "🐧", "🐙",
                          "🦊", "🐢", "🔥", "💎", "🎈", "🎲", "🎸", "🚀", "🚗", "🔔"};

// std::string images[32] = {"AA", "BB", "CC", "DD", "EE", "FF", "GG", "HH", "II", "JJ", "KK",
//                           "LL", "MM", "NN", "OO", "PP", "QQ", "RR", "SS", "TT", "UU", "VV",
//                           "WW", "XX", "YY", "ZZ", "**", "@@", "$$", "&&", "##", "!!"};

void print_playfield() {
    std::string out;
    out.reserve(PLAYFIELD_SIZE * PLAYFIELD_SIZE * 4 + 64); // reserve enough space for the output

    char buf[64];

    for (int i = 0; i < PLAYFIELD_SIZE + 1; i++) {
        for (int j = 0; j < PLAYFIELD_SIZE + 1; j++) {
            if (i == 0 && j == 0) {
                out += "   "; // top-left corner
            } else if (i == 0) {
                snprintf(buf, sizeof(buf), "%2c ", 'a' + j - 1); // column headers
                out += buf;
            } else if (j == 0) {
                snprintf(buf, sizeof(buf), "%2d ", i - 1); // row headers
                out += buf;
            } else {
                const auto& card = playfield[i - 1][j - 1];
                if (card.state == CARD_GUESSED) {
                    // out += "   "; // empty space for guessed cards
                    out += images[card.image_id] + " "; // show the image for guessed cards
                } else if (card.state == CARD_SHOWN) {
                    out += images[card.image_id] + " "; // show the image for shown cards
                } else {
                    out += "❓ "; // hidden card
                }
            }
        }
        out += "\n";
    }
    broadcast(out.c_str(), -1); // broadcast to all clients

    std::string turn_msg
        = "It is player " + std::string(g_clients[g_player_turn_id].nick) + "'s turn.\n";

    broadcast(turn_msg.c_str(), -1);
}

bool is_game_over() {
    for (int i = 0; i < PLAYFIELD_SIZE; i++) {
        for (int j = 0; j < PLAYFIELD_SIZE; j++) {
            if (playfield[i][j].state != CARD_GUESSED) {
                return false; // found a card that is not guessed
            }
        }
    }
    return true; // all cards are guessed
}

void hide_shown_cards() {
    for (int i = 0; i < PLAYFIELD_SIZE; i++) {
        for (int j = 0; j < PLAYFIELD_SIZE; j++) {
            if (playfield[i][j].state == CARD_SHOWN) {
                playfield[i][j].state = CARD_HIDDEN;
            }
        }
    }
}

bool match_cards(int row1, int col1, int row2, int col2) {
    if (row1 < 0 || row1 >= PLAYFIELD_SIZE || col1 < 0 || col1 >= PLAYFIELD_SIZE || row2 < 0
        || row2 >= PLAYFIELD_SIZE || col2 < 0 || col2 >= PLAYFIELD_SIZE) {
        return false; // invalid coordinates
    }
    auto& card1 = playfield[row1][col1];
    auto& card2 = playfield[row2][col2];
    if (card1.image_id == card2.image_id) {
        // match found
        card1.state = CARD_GUESSED;
        card2.state = CARD_GUESSED;
        return true;
    } else {
        // no match, hide the cards again
        card1.state = CARD_SHOWN;
        card2.state = CARD_SHOWN;
        return false;
    }
}

bool is_my_turn(size_t player_id) {
    return g_player_turn_id == player_id;
}

// Returns 0 if the turn was successful, 1 if no match was found, and -1 for invalid input.
int play_turn(int t_sock, size_t player_id, const char* input) {
    // Implement the logic for a player's turn based on the input
    // For example, parse the input to select a card and update the playfield
    // This is a placeholder implementation

    char* col1 = strtok(const_cast<char*>(input), "-");
    if (col1 == nullptr) {
        send_str(t_sock, "Invalid input format. Use 'row-col' format.\n");
        return -1;
    }
    char* line1 = strtok(nullptr, "-");
    if (line1 == nullptr) {
        send_str(t_sock, "Invalid input format. Use 'row-col' format.\n");
        return -1;
    }
    char* col2 = strtok(nullptr, "-");
    if (col2 == nullptr) {
        send_str(t_sock, "Invalid input format. Use 'row-col' format.\n");
        return -1;
    }
    char* line2 = strtok(nullptr, "-");
    if (line2 == nullptr) {
        send_str(t_sock, "Invalid input format. Use 'row-col' format.\n");
        return -1;
    }
    int row1_idx = atoi(line1);
    int col1_idx = col1[0] - 'a';
    int row2_idx = atoi(line2);
    int col2_idx = col2[0] - 'a';

    if (row1_idx < 0 || row1_idx >= PLAYFIELD_SIZE || col1_idx < 0 || col1_idx >= PLAYFIELD_SIZE
        || row2_idx < 0 || row2_idx >= PLAYFIELD_SIZE || col2_idx < 0
        || col2_idx >= PLAYFIELD_SIZE) {
        send_str(t_sock, "Invalid card coordinates. Please try again.\n");
        return -1;
    }
    if (row1_idx == row2_idx && col1_idx == col2_idx) {
        send_str(t_sock, "You cannot select the same card twice. Please try again.\n");
        return -1;
    }

    if (playfield[row1_idx][col1_idx].state == CARD_GUESSED
        || playfield[row2_idx][col2_idx].state == CARD_GUESSED) {
        send_str(
            t_sock,
            "One or both of the selected cards have already been guessed. Please try again.\n");
        return -1;
    }

    return !match_cards(row1_idx, col1_idx, row2_idx, col2_idx);
}

// ***************************************************************************
// CLIENT THREAD

int ask_nick(int t_sock, size_t t_inx) {
    int l_sock = t_sock;
    char l_line[LINE_LEN];
    char l_msg[LINE_LEN + 64]; // +64 for extra message text

    send_str(l_sock, "Welcome to chat! Enter your nick: \n");
    while (1) {
        int l_len = read(l_sock, l_line, sizeof(l_line) - 1);
        if (l_len <= 0) {
            log_msg(LOG_INFO, "Client disconnected before entering nick.");
            return -1;
        }
        l_line[l_len] = '\0'; // ensure null termination right after the data

        strip(l_line);
        size_t line_len = strlen(l_line);

        if (line_len == 0) {
            send_str(l_sock, "Nick must not be empty. Enter your nick: \n");
            continue;
        }
        if (line_len >= NICK_LEN) {
            snprintf(l_msg,
                     sizeof(l_msg),
                     "Nick is too long (max %d). Enter your nick: \n",
                     NICK_LEN - 1);
            send_str(l_sock, l_msg);
            continue;
        }
        if (strncasecmp(l_line, STR_CLOSE, strlen(STR_CLOSE)) == 0) {
            send_str(l_sock, "Nick must not start with 'close'. Enter your nick: \n");
            continue;
        }
        if (strcasecmp(l_line, "Server") == 0 || clients_nick_exists(l_line)) {
            send_str(l_sock, "Nick is already used. Enter your nick: \n");
            continue;
        }
        strcpy(g_clients[t_inx].nick, l_line);
        return 0;
    }
}

void chat_loop(int t_sock, size_t t_inx) {
    int l_sock = t_sock;
    const char* l_nick = g_clients[t_inx].nick;
    char l_line[LINE_LEN];
    char l_msg[LINE_LEN + NICK_LEN + 16];

    snprintf(l_msg, sizeof(l_msg), "Hello %s! Type 'close' to leave.\n", l_nick);
    send_str(l_sock, l_msg);

    snprintf(l_msg, sizeof(l_msg), "* %s joined the chat\n", l_nick);
    broadcast(l_msg, l_sock);

    bool await_back = false;

    while (1) {
        int l_len = read(l_sock, l_line, sizeof(l_line) - 1);
        if (l_len <= 0) {
            log_msg(LOG_INFO, "Client '%s' disconnected.", l_nick);
            break;
        }
        l_line[l_len] = '\0'; // ensure null termination

        if (strncasecmp(l_line, STR_CLOSE, strlen(STR_CLOSE)) == 0) {
            log_msg(LOG_INFO, "Client '%s' sent 'close' request.", l_nick);
            break;
        }
        strip(l_line);

        print_playfield(); // Print the current playfield state after each message
        if (is_my_turn(t_inx)) {
            // Handle the player's turn logic here
            // For example, you can prompt the player to select a card
            // and update the playfield accordingly.
            if (await_back) {
                if (strncasecmp(l_line, "back", 4) == 0) {
                    await_back = false;
                    g_player_turn_id = (g_player_turn_id + 1) % g_clients.size();
                    hide_shown_cards();
                    print_playfield();
                    continue;
                } else {
                    send_str(
                        l_sock,
                        "You must enter 'back' to hide cards and allow next player to play.\n");
                    continue;
                }
            }

            auto ret = play_turn(
                l_sock, t_inx, l_line); // Implement this function to handle the player's turn
            if (ret == -1) {
                send_str(l_sock, "Invalid input. Please try again.\n");
                continue;
            } else if (ret == 1) {
                await_back = true;
                print_playfield();
                send_str(
                    l_sock,
                    "No match found. Enter 'back' to hide cards and allow next player to play.\n");
                continue;
            } else {
                g_clients[t_inx].score++;
                send_str(l_sock,
                         ("Match found! Your score is now " + std::to_string(g_clients[t_inx].score)
                          + ".\n")
                             .c_str());
                await_back = false; // matched, next player's turn
                g_player_turn_id = (g_player_turn_id + 1) % g_clients.size();
                print_playfield();
            }
        }

        if (is_game_over()) {
            std::string winner_msg = "Game over! Final scores:\n";
            for (const auto& client : g_clients) {
                if (client.used) {
                    winner_msg
                        += std::string(client.nick) + ": " + std::to_string(client.score) + "\n";
                }
            }
            broadcast(winner_msg.c_str(), -1);
            break;
        }

        snprintf(l_msg, sizeof(l_msg), "%s: %s\n", l_nick, l_line);
        send_str(STDOUT_FILENO, l_msg);
        broadcast(l_msg, l_sock);
    }

    snprintf(l_msg, sizeof(l_msg), "* %s left the chat\n", l_nick);
    broadcast(l_msg, l_sock);
}

void* client_thread(void* t_arg) {
    int l_sock = *(int*)t_arg;
    free(t_arg);

    // 1) register the client in the shared list
    size_t l_inx = clients_add(l_sock);

    log_msg(LOG_DEBUG, "Thread %lu serves socket %d (slot %zu).", pthread_self(), l_sock, l_inx);

    // 2) ask for nick, 3) chat (chat only if the nick was entered)
    if (ask_nick(l_sock, l_inx) == 0) {
        print_playfield(); // Print the playfield when a new player joins
        chat_loop(l_sock, l_inx);
    }

    // 4) cleanup: remove from list, close the client socket
    clients_remove(l_inx);
    close(l_sock);
    log_msg(LOG_DEBUG, "Thread for socket %d finished.", l_sock);
    pthread_exit(nullptr);
}

// ***************************************************************************
// help

const char g_help[] = "\n"
                      "  Socket server example.\n"
                      "\n"
                      "  Use: %s [-h] [-d] port_number\n"
                      "\n"
                      "    -d  debug mode \n"
                      "    -h  this help\n"
                      "\n";

// ***************************************************************************

int main(int t_argc, char** t_argv) {
    int l_port = 0;

    // parsing arguments
    for (int inx = 1; inx < t_argc; inx++) {
        if (strcmp(t_argv[inx], "-d") == 0) {
            g_debug = LOG_DEBUG;
        }

        if (strcmp(t_argv[inx], "-h") == 0) {
            printf(g_help, t_argv[0]);
            exit(EXIT_SUCCESS);
        }

        if (*t_argv[inx] != '-' && l_port == 0) {
            l_port = atoi(t_argv[inx]);
        }
    }

    if (l_port <= 0) {
        log_msg(LOG_INFO, "Bad or missing port number %d!", l_port);
        printf(g_help, t_argv[0]);
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN); // ignore SIGPIPE, handle EPIPE errors instead

    log_msg(LOG_INFO, "Server will listen on port: %d.", l_port);

    // socket creation
    int l_sock_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (l_sock_listen < 0) {
        log_msg(LOG_ERROR, "Unable to create socket.");
        exit(EXIT_FAILURE);
    }

    in_addr l_addr_any = {INADDR_ANY};
    sockaddr_in l_srv_addr;
    l_srv_addr.sin_family = AF_INET;
    l_srv_addr.sin_port = htons(l_port);
    l_srv_addr.sin_addr = l_addr_any;

    // Enable the port number reusing
    int l_opt = 1;
    if (setsockopt(l_sock_listen, SOL_SOCKET, SO_REUSEADDR, &l_opt, sizeof(l_opt)) < 0) {
        log_msg(LOG_ERROR, "Unable to set socket option!");
    }

    // assign port number to socket
    if (bind(l_sock_listen, (const sockaddr*)&l_srv_addr, sizeof(l_srv_addr)) < 0) {
        log_msg(LOG_ERROR, "Bind failed!");
        close(l_sock_listen);
        exit(EXIT_FAILURE);
    }

    // listening on set port
    if (listen(l_sock_listen, 1) < 0) {
        log_msg(LOG_ERROR, "Unable to listen on given port!");
        close(l_sock_listen);
        exit(EXIT_FAILURE);
    }

    log_msg(LOG_INFO, "Enter 'quit' to quit server.");

    // list of fd sources
    pollfd l_read_poll[2];
    l_read_poll[0].fd = STDIN_FILENO;
    l_read_poll[0].events = POLLIN;
    l_read_poll[1].fd = l_sock_listen;
    l_read_poll[1].events = POLLIN;

    // main loop: wait for stdin or new client connection
    while (1) {
        // wait (no timeout) until stdin or the listen socket has data
        if (poll(l_read_poll, 2, -1) < 0) {
            if (errno == EINTR) {
                continue; // interrupted by a signal - just try again
            }
            log_msg(LOG_ERROR, "Function poll failed!");
            break;
        }

        // data on stdin?
        if (l_read_poll[0].revents & POLLIN) {
            char l_buf[LINE_LEN];
            // read data from stdin
            int l_len = read(STDIN_FILENO, l_buf, sizeof(l_buf) - 1);

            if (l_len == 0) {
                log_msg(LOG_DEBUG, "Stdin closed.");
                break;
            }
            if (l_len < 0) {
                log_msg(LOG_ERROR, "Unable to read data from stdin.");
                break;
            }

            l_buf[l_len] = '\0'; // ensure null termination
            strip(l_buf);        // remove trailing newline

            // "quit" -> leave the main loop and shut down
            if (strncmp(l_buf, STR_QUIT, strlen(STR_QUIT)) == 0) {
                log_msg(LOG_INFO, "Request to 'quit' entered.");
                break;
            }

            // any other non-empty text -> send to the client
            if (l_buf[0]) {
                char l_msg[LINE_LEN + 16]; // +16 for extra message text
                snprintf(l_msg, sizeof(l_msg), "Server: %s\n", l_buf);
                broadcast(l_msg, -1);
            }
        }

        // data from client?
        if (l_read_poll[1].revents & POLLIN) {
            sockaddr_in l_rsa; // filled with client's address by accept()
            socklen_t l_rsa_size = sizeof(l_rsa);

            int l_sock_client = accept(l_sock_listen, (sockaddr*)&l_rsa, &l_rsa_size);
            if (l_sock_client < 0) {
                log_msg(LOG_ERROR, "Unable to accept new client.");
                continue;
            }

            log_msg(LOG_INFO,
                    "New Client IP: '%s'  port: %d (socket %d).",
                    inet_ntoa(l_rsa.sin_addr),
                    ntohs(l_rsa.sin_port),
                    l_sock_client);

            // Pass the socket number to the thread via heap memory.
            // Passing &l_sock_client would be wrong: the local variable
            // can be overwritten by the next accept() before the new thread
            // reads it. The thread frees this memory.
            int* l_arg = (int*)malloc(sizeof(int));
            *l_arg = l_sock_client;
            pthread_t l_thread;
            if (pthread_create(&l_thread, nullptr, client_thread, l_arg) != 0) {
                log_msg(LOG_ERROR, "Unable to create thread for new client.");
                free(l_arg);
                close(l_sock_client);
                continue;
            }

            pthread_detach(l_thread); // no need to join, thread cleans up itself
        }
    }
    // Detached thread: its resources are released automatically
    // when it ends, so nobody has to call pthread_join() on it.
    broadcast("Server is shutting down.\n", -1);
    close(l_sock_listen);
    log_msg(LOG_INFO, "Server is shutting down.");
    exit(EXIT_SUCCESS);
} // main
