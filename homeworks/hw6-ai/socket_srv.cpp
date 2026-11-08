// ***************************************************************************
//
// OSY - hw6: multithreaded chat server
//
// Based on the example socket_srv.cpp (Petr Olivka, VSB-TUO).
//
// OVERVIEW
// --------
// Everything runs in ONE process. Threads of one process share the same
// memory (global variables, heap, open file descriptors), so every thread
// can see the global client list g_clients[] and can write to any socket.
// (Compare with fork(): a child process gets its own COPY of memory.)
//
// Main thread:
//   - waits with poll() on two sources at once: stdin and the listen socket,
//   - new client  -> accept() + pthread_create() (thread is detached),
//   - line from stdin -> broadcast to all clients as "Server: <text>",
//   - "quit" from stdin -> send "close" to clients, close listen socket, exit.
//
// Client thread (one per connected client):
//   - adds the client into the shared list,
//   - asks the client for a nick,
//   - reads messages, prints them on the server, forwards them to the
//     others as "<nick>: <text>",
//   - when the connection ends, removes the client from the list and
//     closes the client socket.
//
// NOTE: the client list is NOT yet protected against race conditions
//       (as required by the assignment). A mutex comes in the next task.
//
// Usage: ./socket_srv [-h] [-d] port
//
// ***************************************************************************

#include <arpa/inet.h>  // inet_ntoa, htons, ntohs
#include <cerrno>       // errno
#include <csignal>      // signal, SIGPIPE
#include <cstdarg>      // va_list for log_msg
#include <cstdio>       // printf, snprintf
#include <cstdlib>      // malloc, free, exit, atoi
#include <cstring>      // strlen, strcmp, memchr, ...
#include <poll.h>       // poll
#include <pthread.h>    // pthread_create, pthread_detach, pthread_exit
#include <sys/socket.h> // socket, bind, listen, accept, send
#include <unistd.h>     // read, close

#define STR_CLOSE "close" // client command: leave the chat
#define STR_QUIT "quit"   // server console command: stop the server

#define MAX_CLIENTS 64 // capacity of the shared client list
#define NICK_LEN 32    // max nick length incl. '\0'
#define LINE_LEN 512   // max length of one message line

// ***************************************************************************
// LOGGING (taken from the demo)
//
// log_msg() works like printf, prefixes the text with ERR/INF/DEB.
// DEBUG messages are printed only when the server is started with -d.

#define LOG_ERROR 0 // errors
#define LOG_INFO 1  // information and notifications
#define LOG_DEBUG 2 // debug messages

int g_debug = LOG_INFO; // current log level, -d switches it to LOG_DEBUG

void log_msg(int t_log_level, const char* t_form, ...) {
    const char* out_fmt[] = {"ERR: (%d-%s) %s\n", "INF: %s\n", "DEB: %s\n"};

    // skip messages that are more detailed than the current level
    if (t_log_level && t_log_level > g_debug) {
        return;
    }

    // format variable arguments into a buffer
    char l_buf[4096];
    va_list l_arg;
    va_start(l_arg, t_form);
    vsnprintf(l_buf, sizeof(l_buf), t_form, l_arg);
    va_end(l_arg);

    // errors go to stderr together with errno description, rest to stdout
    switch (t_log_level) {
        case LOG_INFO:
        case LOG_DEBUG:
            fprintf(stdout, out_fmt[t_log_level], l_buf);
            break;
        case LOG_ERROR:
            fprintf(stderr, out_fmt[t_log_level], errno, strerror(errno), l_buf);
            break;
    }
    fflush(stdout); // print immediately (important when output is redirected)
}

// ***************************************************************************
// SHARED LIST OF CLIENTS
//
// A simple fixed-size array. It is a global variable, so all threads of the
// process work with the same array (shared memory of threads).
// A slot is free when used == 0.

struct client_t {
    int used;            // 1 = slot is occupied by a connected client
    int sock;            // client socket (file descriptor)
    char nick[NICK_LEN]; // empty string = nick not entered yet
};

client_t g_clients[MAX_CLIENTS];

// Find a free slot, store the socket there.
// Returns slot index, or -1 when the list is full.
int clients_add(int t_sock) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i].used) {
            g_clients[i].sock = t_sock;
            g_clients[i].nick[0] = '\0';
            g_clients[i].used = 1; // set last - slot becomes "visible"
            return i;
        }
    }
    return -1;
}

// Free the slot so it can be reused by another client.
void clients_remove(int t_inx) {
    g_clients[t_inx].used = 0;
    g_clients[t_inx].sock = -1;
    g_clients[t_inx].nick[0] = '\0';
}

// Returns 1 if some connected client already uses this nick.
int clients_nick_exists(const char* t_nick) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].used && strcmp(g_clients[i].nick, t_nick) == 0) {
            return 1;
        }
    }
    return 0;
}

// ***************************************************************************
// SENDING HELPERS

// Send the whole buffer. send() may write fewer bytes than requested,
// so we loop until everything is sent.
// MSG_NOSIGNAL: if the peer is already gone, return an error (EPIPE)
// instead of raising SIGPIPE, which would kill the whole process.
int send_all(int t_sock, const char* t_buf, int t_len) {
    int l_sent = 0;
    while (l_sent < t_len) {
        int l_ret = send(t_sock, t_buf + l_sent, t_len - l_sent, MSG_NOSIGNAL);
        if (l_ret < 0) {
            if (errno == EINTR) {
                continue; // interrupted by a signal - just try again
            }
            return -1;
        }
        l_sent += l_ret;
    }
    return l_sent;
}

// Convenience wrapper for C strings.
int send_str(int t_sock, const char* t_str) {
    return send_all(t_sock, t_str, strlen(t_str));
}

// Send a message to all clients in the list, except the one with socket
// t_except_sock (the sender should not get its own message back).
// t_except_sock = -1 means "send to everybody".
// Clients that have not entered a nick yet do not receive chat messages.
void broadcast(const char* t_msg, int t_except_sock) {
    int l_len = strlen(t_msg);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i].used && g_clients[i].nick[0] && g_clients[i].sock != t_except_sock) {
            if (send_all(g_clients[i].sock, t_msg, l_len) < 0) {
                // the client's own thread will notice the disconnect and clean up
                log_msg(LOG_DEBUG, "Unable to send to '%s'.", g_clients[i].nick);
            }
        }
    }
}

// Remove trailing CR/LF/spaces/tabs (e.g. "ahoj\r\n" -> "ahoj").
void strip(char* t_str) {
    int l_len = strlen(t_str);
    while (l_len > 0 && (t_str[l_len - 1] == '\n' || t_str[l_len - 1] == '\r'
                         || t_str[l_len - 1] == ' ' || t_str[l_len - 1] == '\t')) {
        t_str[--l_len] = '\0';
    }
}

// ***************************************************************************
// LINE READER
//
// TCP is a byte stream, not a stream of messages. One read() can return
// half a line, or two lines at once. So we keep received bytes in a buffer
// and hand out complete lines (terminated by '\n') one by one.
// Every client thread has its own line_reader_t (local variable on the
// thread's stack), so the buffers of different clients do not mix.

struct line_reader_t {
    int sock;           // socket to read from
    char buf[LINE_LEN]; // received but not yet processed bytes
    int len;            // number of bytes waiting in buf
};

// Read one line into t_line (without '\n', trailing whitespace stripped).
// Returns line length (0 = empty line), or -1 when the client closed the
// connection or an error occurred.
int read_line(line_reader_t* t_lr, char* t_line, int t_size) {
    while (1) {
        // Is there a complete line in the buffer? Or is the buffer full
        // (line too long)? Then return what we have.
        char* l_nl = (char*)memchr(t_lr->buf, '\n', t_lr->len);
        if (l_nl || t_lr->len == (int)sizeof(t_lr->buf)) {
            int l_line_len = l_nl ? l_nl - t_lr->buf : t_lr->len;
            int l_consumed = l_nl ? l_line_len + 1 : l_line_len; // +1 = the '\n'
            if (l_line_len >= t_size) {
                l_line_len = t_size - 1; // do not overflow caller's buffer
            }
            // copy the line out to the caller
            memcpy(t_line, t_lr->buf, l_line_len);
            t_line[l_line_len] = '\0';
            // shift the rest of the buffer to the beginning
            memmove(t_lr->buf, t_lr->buf + l_consumed, t_lr->len - l_consumed);
            t_lr->len -= l_consumed;
            strip(t_line);
            return strlen(t_line);
        }

        // No complete line yet -> read more data from the socket.
        // read() blocks only THIS thread; other threads keep running.
        int l_ret = read(t_lr->sock, t_lr->buf + t_lr->len, sizeof(t_lr->buf) - t_lr->len);
        if (l_ret == 0) {
            return -1; // 0 bytes = client closed the socket
        }
        if (l_ret < 0) {
            if (errno == EINTR) {
                continue; // interrupted by a signal - try again
            }
            return -1; // real error
        }
        log_msg(LOG_DEBUG, "Read %d bytes from socket %d.", l_ret, t_lr->sock);
        t_lr->len += l_ret;
    }
}

// ***************************************************************************
// CLIENT THREAD
//
// The work of one client thread is split into three functions:
//   ask_nick()      - ask the client for a nick until a valid one is entered,
//   chat_loop()     - read messages and forward them to the others,
//   client_thread() - thread entry point: register, call the two above,
//                     clean up.

// Ask the client for a nick until a valid one is entered and store it
// into the shared list at slot t_inx.
// Returns 0 when the nick was set, -1 when the client disconnected.
int ask_nick(line_reader_t* t_lr, int t_inx) {
    int l_sock = t_lr->sock;
    char l_line[LINE_LEN];
    char l_msg[LINE_LEN + 64];

    send_str(l_sock, "Welcome to chat! Enter your nick: \n");
    while (1) {
        int l_len = read_line(t_lr, l_line, sizeof(l_line));
        if (l_len < 0) {
            // disconnected before choosing a nick
            log_msg(LOG_INFO, "Client on socket %d left before entering nick.", l_sock);
            return -1;
        }
        if (l_len == 0) {
            send_str(l_sock, "Nick must not be empty. Enter your nick: \n");
            continue;
        }
        if (l_len >= NICK_LEN) {
            snprintf(l_msg, sizeof(l_msg), "Nick is too long (max %d). Enter your nick: \n",
                     NICK_LEN - 1);
            send_str(l_sock, l_msg);
            continue;
        }
        // demo client closes on any message starting with "close" -> forbid such nick
        // (otherwise "closer: hi" would disconnect everybody)
        if (strncasecmp(l_line, STR_CLOSE, strlen(STR_CLOSE)) == 0) {
            send_str(l_sock, "Nick must not start with 'close'. Enter your nick: \n");
            continue;
        }
        // "Server" is reserved for console messages, nicks must be unique
        if (strcasecmp(l_line, "Server") == 0 || clients_nick_exists(l_line)) {
            send_str(l_sock, "Nick is already used. Enter your nick: \n");
            continue;
        }
        // valid nick -> store it into the shared list; from now on the
        // client receives broadcasts (see broadcast())
        strcpy(g_clients[t_inx].nick, l_line);
        return 0;
    }
}

// Communication with a client that already has a nick.
// Returns when the client sends "close" or disconnects.
void chat_loop(line_reader_t* t_lr, int t_inx) {
    int l_sock = t_lr->sock;
    const char* l_nick = g_clients[t_inx].nick;
    char l_line[LINE_LEN];                // one received line
    char l_msg[LINE_LEN + NICK_LEN + 16]; // formatted outgoing message

    // greet the client and announce it to the others
    log_msg(LOG_INFO, "Client '%s' joined (socket %d).", l_nick, l_sock);
    snprintf(l_msg, sizeof(l_msg), "Hello %s! Type 'close' to leave.\n", l_nick);
    send_str(l_sock, l_msg);
    snprintf(l_msg, sizeof(l_msg), "* %s joined the chat\n", l_nick);
    broadcast(l_msg, l_sock);

    while (1) {
        int l_len = read_line(t_lr, l_line, sizeof(l_line));
        if (l_len < 0) {
            // client disconnected (Ctrl+C, closed terminal, ...)
            log_msg(LOG_INFO, "Client '%s' closed connection.", l_nick);
            break;
        }
        if (l_len == 0) {
            continue; // ignore empty lines
        }
        if (strncasecmp(l_line, STR_CLOSE, strlen(STR_CLOSE)) == 0) {
            // client asked to leave
            log_msg(LOG_INFO, "Client '%s' sent 'close' request.", l_nick);
            break;
        }

        // format "<nick>: <text>", show it on the server console
        // and forward it to everybody except the sender
        snprintf(l_msg, sizeof(l_msg), "%s: %s\n", l_nick, l_line);
        write(STDOUT_FILENO, l_msg, strlen(l_msg));
        broadcast(l_msg, l_sock);
    }

    // announce the departure to the others (the client is still in the
    // list here, so l_nick is valid; the sender itself is skipped)
    snprintf(l_msg, sizeof(l_msg), "* %s left the chat\n", l_nick);
    broadcast(l_msg, l_sock);
}

// Thread entry point, started by pthread_create() in main() for every new
// client. t_arg points to a malloc'ed int with the client socket.
// The thread owns the socket: it is the one who closes it at the end.
void* client_thread(void* t_arg) {
    // take the socket number and free the memory allocated by main()
    int l_sock = *(int*)t_arg;
    free(t_arg);

    // ---- 1) register the client in the shared list ----------------------
    int l_inx = clients_add(l_sock);
    if (l_inx < 0) {
        // list is full - tell the client, close the socket, end the thread
        log_msg(LOG_INFO, "Too many clients, connection %d refused.", l_sock);
        send_str(l_sock, "Server is full.\n");
        close(l_sock);
        pthread_exit(nullptr);
    }
    log_msg(LOG_DEBUG, "Thread %lu serves socket %d (slot %d).", pthread_self(), l_sock, l_inx);

    // private line buffer of this client (lives on this thread's stack)
    line_reader_t l_lr;
    l_lr.sock = l_sock;
    l_lr.len = 0;

    // ---- 2) nick, 3) chat (chat only if the nick was entered) -----------
    if (ask_nick(&l_lr, l_inx) == 0) {
        chat_loop(&l_lr, l_inx);
    }

    // ---- 4) cleanup: remove from list, close the client socket ----------
    clients_remove(l_inx);
    close(l_sock); // the thread closes its client socket, listen socket stays open
    log_msg(LOG_DEBUG, "Thread for socket %d finished.", l_sock);
    pthread_exit(nullptr); // end only this thread, the process keeps running
}

// ***************************************************************************
// HELP

const char g_help[] = "\n"
                      "  Multithreaded chat server.\n"
                      "\n"
                      "  Use: %s [-h] [-d] port_number\n"
                      "\n"
                      "    -d  debug mode \n"
                      "    -h  this help\n"
                      "\n";

// ***************************************************************************
// MAIN THREAD

int main(int t_argc, char** t_argv) {
    int l_port = 0;

    // ---- parse command line arguments ----------------------------------
    for (int inx = 1; inx < t_argc; inx++) {
        if (strcmp(t_argv[inx], "-d") == 0) {
            g_debug = LOG_DEBUG; // enable debug messages
        }
        if (strcmp(t_argv[inx], "-h") == 0) {
            printf(g_help, t_argv[0]);
            exit(EXIT_SUCCESS);
        }
        if (*t_argv[inx] != '-' && l_port == 0) {
            l_port = atoi(t_argv[inx]); // first non-option argument = port
        }
    }

    if (l_port <= 0) {
        log_msg(LOG_INFO, "Bad or missing port number %d!", l_port);
        printf(g_help, t_argv[0]);
        exit(EXIT_FAILURE);
    }

    // Writing to a socket whose client already disconnected raises SIGPIPE,
    // whose default action terminates the WHOLE process (all threads).
    // Ignore it; send() then just returns an error.
    signal(SIGPIPE, SIG_IGN);

    // ---- initialize the shared client list (all slots free) -----------
    for (int i = 0; i < MAX_CLIENTS; i++) {
        g_clients[i].used = 0;
        g_clients[i].sock = -1;
        g_clients[i].nick[0] = '\0';
    }

    log_msg(LOG_INFO, "Server will listen on port: %d.", l_port);

    // ---- create the listen socket (TCP over IPv4) ----------------------
    int l_sock_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (l_sock_listen < 0) {
        log_msg(LOG_ERROR, "Unable to create socket.");
        exit(EXIT_FAILURE);
    }

    // address to listen on: any local IP, given port (network byte order)
    in_addr l_addr_any = {INADDR_ANY};
    sockaddr_in l_srv_addr;
    l_srv_addr.sin_family = AF_INET;
    l_srv_addr.sin_port = htons(l_port);
    l_srv_addr.sin_addr = l_addr_any;

    // allow reusing the port immediately after the server restarts
    int l_opt = 1;
    if (setsockopt(l_sock_listen, SOL_SOCKET, SO_REUSEADDR, &l_opt, sizeof(l_opt)) < 0) {
        log_msg(LOG_ERROR, "Unable to set socket option!");
    }

    // assign the address/port to the socket
    if (bind(l_sock_listen, (const sockaddr*)&l_srv_addr, sizeof(l_srv_addr)) < 0) {
        log_msg(LOG_ERROR, "Bind failed!");
        close(l_sock_listen);
        exit(EXIT_FAILURE);
    }

    // start listening; 16 = queue of connections waiting for accept()
    if (listen(l_sock_listen, 16) < 0) {
        log_msg(LOG_ERROR, "Unable to listen on given port!");
        close(l_sock_listen);
        exit(EXIT_FAILURE);
    }

    log_msg(LOG_INFO, "Enter 'quit' to quit server, any other text is sent to all clients.");

    // ---- sources watched by poll(): [0] keyboard, [1] new connections --
    // Client sockets are NOT here - each one is handled by its own thread.
    pollfd l_read_poll[2];
    l_read_poll[0].fd = STDIN_FILENO;
    l_read_poll[0].events = POLLIN;
    l_read_poll[1].fd = l_sock_listen;
    l_read_poll[1].events = POLLIN;

    // ---- main loop: server console + accepting new clients -------------
    while (1) {
        // wait (no timeout) until stdin or the listen socket has data
        if (poll(l_read_poll, 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_msg(LOG_ERROR, "Function poll failed!");
            break;
        }

        // ---- text typed on the server console ----
        if (l_read_poll[0].revents & POLLIN) {
            char l_buf[LINE_LEN];
            int l_len = read(STDIN_FILENO, l_buf, sizeof(l_buf) - 1);
            if (l_len <= 0) {
                log_msg(LOG_DEBUG, "Stdin closed."); // Ctrl+D
                break;
            }
            l_buf[l_len] = '\0';
            strip(l_buf);

            // "quit" -> leave the main loop and shut down
            if (strncmp(l_buf, STR_QUIT, strlen(STR_QUIT)) == 0) {
                log_msg(LOG_INFO, "Request to 'quit' entered.");
                break;
            }
            // any other non-empty text -> send to all clients as "Server"
            if (l_buf[0]) {
                char l_msg[LINE_LEN + 16];
                snprintf(l_msg, sizeof(l_msg), "Server: %s\n", l_buf);
                broadcast(l_msg, -1);
            }
        }

        // ---- new client connecting ----
        if (l_read_poll[1].revents & POLLIN) {
            sockaddr_in l_rsa; // filled with client's address by accept()
            socklen_t l_rsa_size = sizeof(l_rsa);

            int l_sock_client = accept(l_sock_listen, (sockaddr*)&l_rsa, &l_rsa_size);
            if (l_sock_client < 0) {
                log_msg(LOG_ERROR, "Unable to accept new client.");
                continue; // listen socket stays open, keep serving
            }

            log_msg(LOG_INFO, "New client IP: '%s' port: %d (socket %d).",
                    inet_ntoa(l_rsa.sin_addr), ntohs(l_rsa.sin_port), l_sock_client);

            // Pass the socket number to the thread via heap memory.
            // Passing &l_sock_client would be wrong: the local variable
            // can be overwritten by the next accept() before the new thread
            // reads it. The thread frees this memory.
            int* l_arg = (int*)malloc(sizeof(int));
            *l_arg = l_sock_client;

            // start a new thread running client_thread(l_arg)
            pthread_t l_tid;
            if (pthread_create(&l_tid, nullptr, client_thread, l_arg) != 0) {
                log_msg(LOG_ERROR, "Unable to create thread.");
                free(l_arg);
                close(l_sock_client);
                continue;
            }
            // Detached thread: its resources are released automatically
            // when it ends, so nobody has to call pthread_join() on it.
            pthread_detach(l_tid);
        }
    }

    // ---- shutdown --------------------------------------------------------
    // tell clients to close (the demo client exits on a "close" message)
    broadcast(STR_CLOSE "\n", -1);
    close(l_sock_listen);
    log_msg(LOG_INFO, "Server finished.");
    // exit() ends the whole process, including all still running threads
    // (pthread_exit() in main would instead wait for the other threads)
    exit(EXIT_SUCCESS);
}
