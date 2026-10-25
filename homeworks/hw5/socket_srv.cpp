// ***************************************************************************
//
// Program example for labs in subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2026
//
// Example of socket server.
//
// This program is example of socket server and it allows to connect and serve
// more clients concurrently. Every client is served by its own child process.
// The mandatory argument of program is port number for listening.
//
// ***************************************************************************

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <cerrno>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <string>
#include <vector>
#include <map>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "protocol.h"

#define STR_QUIT    "quit"
#define STR_STAT    "stat"

#define BATCH_SIZE  3       // python is run when at least so many expressions wait

// ***************************************************************************
// log messages

#define LOG_ERROR               0       // errors
#define LOG_INFO                1       // information and notifications
#define LOG_DEBUG               2       // debug messages

// debug flag
int g_debug = LOG_INFO;

/**
 * @brief Prints formatted log message with prefix by its level.
 *
 * Errors go to stderr together with errno and its description,
 * information and debug messages go to stdout. Message with level
 * higher than g_debug is not printed.
 *
 * @param t_log_level  LOG_ERROR, LOG_INFO or LOG_DEBUG.
 * @param t_form       printf-like format string.
 * @param ...          arguments for format string.
 */
void log_msg( int t_log_level, const char *t_form, ... )
{
    const char *out_fmt[] = {
            "ERR: (%d-%s) %s\n",
            "INF: %s\n",
            "DEB: %s\n" };

    if ( t_log_level && t_log_level > g_debug )
    {
        return;
    }

    char l_buf[ 4096 ];
    va_list l_arg;
    va_start( l_arg, t_form );

    vsnprintf( l_buf, sizeof( l_buf ), t_form, l_arg );

    va_end( l_arg );

    switch ( t_log_level )
    {
        case LOG_INFO:
        case LOG_DEBUG:
            fprintf( stdout, out_fmt[ t_log_level ], l_buf );
            break;

        case LOG_ERROR:
            fprintf( stderr, out_fmt[ t_log_level ], errno, strerror( errno ), l_buf );
            break;
    }
}

// ***************************************************************************
// help

const char g_help[] =
    "\n"
    "  Socket server example.\n"
    "\n"
    "  Use: %s [-h] [-d] port_number\n"
    "\n"
    "    -d  debug mode \n"
    "    -h  this help\n"
    "\n"
    ;

// ***************************************************************************
// reaping of finished child processes (prevents zombies)

/**
 * @brief SIGCHLD handler, reaps all finished child processes.
 *
 * Calls waitpid() with WNOHANG in loop, because more children can finish
 * while only one signal is delivered. errno of interrupted code is kept.
 * Its only (unnamed) parameter is number of signal (always SIGCHLD), not used.
 */
void sigchld_handler( int )
{
    int l_errno = errno;    // waitpid can change errno of interrupted code
    while ( waitpid( -1, nullptr, WNOHANG ) > 0 ) {}
    errno = l_errno;
}

/**
 * @brief Installs sigchld_handler() for SIGCHLD in parent process.
 *
 * SA_RESTART restarts interrupted system calls (e.g. accept), poll()
 * is never restarted and returns EINTR. SA_NOCLDSTOP: no signal for
 * stopped children. Program exits, when handler can not be installed.
 */
void install_sigchld_handler()
{
    struct sigaction l_sa;
    memset( &l_sa, 0, sizeof( l_sa ) );
    l_sa.sa_handler = sigchld_handler;
    sigemptyset( &l_sa.sa_mask );
    l_sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if ( sigaction( SIGCHLD, &l_sa, nullptr ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to set SIGCHLD handler!" );
        exit( EXIT_FAILURE );
    }
}

// ***************************************************************************

/**
 * @brief Builds python code, which evaluates one expression.
 *
 * Code prints protocol message "RES: <expr>=<result>", or on runtime error
 * (e.g. 1/0) "ERR: <expr>: <reason>" (same format as proto_err()) and
 * description of exception to stderr (server terminal). Runtime error
 * does not stop following statements of batch.
 *
 * @param t_expr  arithmetic expression, e.g. "4+5*8"; it is not escaped.
 * @return Python statement (try/except block) terminated by '\n'.
 */
std::string build_python_statement( const std::string &t_expr )
{
    return "try:\n"
           "    print(\"" PROTO_RES + t_expr + "=\", " + t_expr + ", sep=\"\")\n"
           "except Exception as err:\n"
           "    print(\"" PROTO_ERR + t_expr + ": \" + str(err))\n"
           "    print(\"" + t_expr + ":\", repr(err), file=__import__(\"sys\").stderr)\n";
}

// ***************************************************************************

/**
 * @brief Runs python3 in new process and waits for its end.
 *
 * Python's stdin is redirected to pipe, which is fed with t_code,
 * its stdout is redirected to t_out_fd, so output goes directly to client.
 * All unused pipe ends and descriptors are closed in both processes.
 *
 * @param t_code    python code to execute.
 * @param t_out_fd  descriptor for python's stdout (client socket).
 * @return true when python finished with exit code 0, false when pipe,
 *         fork or exec failed or python ended with error (e.g. syntax error).
 */
bool run_python( const std::string &t_code, int t_out_fd )
{
    int l_pipe[ 2 ];
    if ( pipe( l_pipe ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to create pipe." );
        return false;
    }

    fflush( stdout );   // do not lose buffered output in child

    pid_t l_pid = fork();
    if ( l_pid < 0 )
    {
        log_msg( LOG_ERROR, "Unable to fork process for python." );
        close( l_pipe[ 0 ] );
        close( l_pipe[ 1 ] );
        return false;
    }

    if ( l_pid == 0 )
    { // child: becomes python3
        if ( dup2( l_pipe[ 0 ], STDIN_FILENO ) < 0 || dup2( t_out_fd, STDOUT_FILENO ) < 0 )
        {
            log_msg( LOG_ERROR, "Unable to redirect python stdin/stdout." );
            _exit( EXIT_FAILURE );
        }
        close( l_pipe[ 0 ] );
        close( l_pipe[ 1 ] );   // otherwise python never gets EOF on stdin
        if ( t_out_fd != STDOUT_FILENO ) close( t_out_fd );

        execlp( "python3", "python3", nullptr );
        log_msg( LOG_ERROR, "Unable to execute python3." );
        _exit( EXIT_FAILURE );
    }

    // parent: send code to python and wait for result
    close( l_pipe[ 0 ] );
    int l_len = write( l_pipe[ 1 ], t_code.data(), t_code.size() );
    if ( l_len != ( int ) t_code.size() )
    {
        log_msg( LOG_ERROR, "Unable to write code to python." );
    }
    close( l_pipe[ 1 ] );   // EOF for python, it starts to execute code

    int l_status = 0;
    if ( waitpid( l_pid, &l_status, 0 ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to wait for python." );
        return false;
    }

    return WIFEXITED( l_status ) && WEXITSTATUS( l_status ) == 0;
}

// ***************************************************************************

/**
 * @brief Sends one protocol message to client.
 *
 * Used for ERR and BYE messages, results (RES) are sent by python itself.
 * Failure is only logged.
 *
 * @param t_sock_client  socket of client.
 * @param t_msg          complete message including '\n' (see protocol.h).
 */
void send_to_client( int t_sock_client, const std::string &t_msg )
{
    if ( write( t_sock_client, t_msg.data(), t_msg.size() ) < 0 )
    {
        log_msg( LOG_ERROR, "[%d] Unable to send message to client.", getpid() );
    }
}

// ***************************************************************************

/**
 * @brief Evaluates all expressions by one run of python, results go to client.
 *
 * When python fails for whole batch (e.g. syntax error in one expression),
 * client receives "ERR: <expr>: batch failed" for every expression.
 *
 * @param t_sock_client  socket of client, python writes results into it.
 * @param t_exprs        expressions of batch.
 */
void evaluate_batch( int t_sock_client, const std::vector<std::string> &t_exprs )
{
    std::string l_code;
    for ( const std::string &l_expr : t_exprs )
        l_code += build_python_statement( l_expr );

    log_msg( LOG_INFO, "[%d] Evaluating batch of %d expressions.", getpid(), ( int ) t_exprs.size() );
    if ( !run_python( l_code, t_sock_client ) )
    {
        log_msg( LOG_INFO, "[%d] Python failed for batch.", getpid() );
        for ( const std::string &l_expr : t_exprs )
            send_to_client( t_sock_client, proto_err( l_expr, "batch failed" ) );
    }
}

// ***************************************************************************
// statistics: children report processed equations to parent through pipe

struct stat_msg
{
    pid_t m_pid;            // child serving client
    int m_count;            // equations processed since last message
    bool m_finished;        // client disconnected, child exits
};

struct stat_table
{
    std::map<pid_t, int> m_clients;     // connected clients: pid -> equations
    int m_total = 0;                    // equations of all clients
};

/**
 * @brief Child: reports processed equations to parent through statistics pipe.
 *
 * Message is smaller than PIPE_BUF, so write is atomic and messages
 * from more children never mix. Failure is only logged.
 *
 * @param t_stat_fd   write end of statistics pipe.
 * @param t_count     number of equations processed since last report.
 * @param t_finished  true when client disconnected and child exits.
 */
void stat_report( int t_stat_fd, int t_count, bool t_finished )
{
    stat_msg l_msg;
    memset( &l_msg, 0, sizeof( l_msg ) );   // no uninitialized padding bytes in pipe
    l_msg.m_pid = getpid();
    l_msg.m_count = t_count;
    l_msg.m_finished = t_finished;

    // message is smaller than PIPE_BUF, so write is atomic
    // and messages from more children never mix
    if ( write( t_stat_fd, &l_msg, sizeof( l_msg ) ) != sizeof( l_msg ) )
    {
        log_msg( LOG_ERROR, "[%d] Unable to report statistics.", getpid() );
    }
}

/**
 * @brief Parent: prints overview of all connected clients ('stat' command).
 *
 * @param t_table  statistics collected by stat_receive().
 */
void stat_print( const stat_table &t_table )
{
    log_msg( LOG_INFO, "STAT: %d client(s) connected, all clients processed %d equations.",
             ( int ) t_table.m_clients.size(), t_table.m_total );

    for ( const auto &l_client : t_table.m_clients )
    {
        log_msg( LOG_INFO, "STAT:   client [%d]: %d equations", l_client.first, l_client.second );
    }
}

/**
 * @brief Parent: receives one message from statistics pipe and updates table.
 *
 * Every update is logged. Finished client is removed from table,
 * its equations stay in total.
 *
 * @param t_stat_fd  read end of statistics pipe.
 * @param t_table    statistics to update.
 */
void stat_receive( int t_stat_fd, stat_table &t_table )
{
    stat_msg l_msg;
    if ( read( t_stat_fd, &l_msg, sizeof( l_msg ) ) != sizeof( l_msg ) )
    {
        log_msg( LOG_ERROR, "Unable to read statistics." );
        return;
    }

    int &l_count = t_table.m_clients[ l_msg.m_pid ];
    l_count += l_msg.m_count;
    t_table.m_total += l_msg.m_count;

    if ( l_msg.m_finished )
    {
        log_msg( LOG_INFO, "STAT: client [%d] finished, %d equations processed (all clients: %d).",
                 l_msg.m_pid, l_count, t_table.m_total );
        t_table.m_clients.erase( l_msg.m_pid );
    }
    else
    {
        log_msg( LOG_INFO, "STAT: client [%d] processed %d equations (all clients: %d).",
                 l_msg.m_pid, l_count, t_table.m_total );
    }
}

// ***************************************************************************

/**
 * @brief Communication with one client, runs in child process.
 *
 * Reads protocol messages line by line. REQ expressions are buffered and
 * evaluated by python when at least BATCH_SIZE of them wait. QUIT evaluates
 * rest of buffer, sends BYE and ends. Unknown message or too long line
 * is answered by ERR. Processed equations are reported to parent.
 *
 * @param t_sock_client  socket of client.
 * @param t_stat_fd      write end of statistics pipe.
 * @note Returns after QUIT, when client closed connection or on error;
 *       socket is closed by caller.
 */
void handle_client( int t_sock_client, int t_stat_fd )
{
    // list of fd sources
    pollfd l_read_poll[ 1 ];

    l_read_poll[ 0 ].fd = t_sock_client;
    l_read_poll[ 0 ].events = POLLIN;

    std::vector<std::string> l_pending;     // expressions waiting for evaluation
    LineBuffer l_lines;                     // joins messages split into more reads

    while ( 1 )
    { // communication
        char l_buf[ 256 ];

        // select from fds
        int l_poll = poll( l_read_poll, 1, -1 );

        if ( l_poll < 0 )
        {
            log_msg( LOG_ERROR, "Function poll failed!" );
            return;
        }

        // data from client?
        if ( l_read_poll[ 0 ].revents & ( POLLIN | POLLHUP | POLLERR ) )
        {
            // read data from socket
            int l_len = read( t_sock_client, l_buf, sizeof( l_buf ) );
            if ( l_len == 0 )
            {
                log_msg( LOG_DEBUG, "[%d] Client closed socket!", getpid() );
                return;
            }
            else if ( l_len < 0 )
            {
                log_msg( LOG_ERROR, "[%d] Unable to read data from client.", getpid() );
                return;
            }
            else
                log_msg( LOG_DEBUG, "[%d] Read %d bytes from client.", getpid(), l_len );

            bool l_end = false;     // client sent QUIT

            // every complete line is one message
            l_lines.feed( l_buf, l_len );
            std::string l_line, l_expr;
            line_status l_status;
            while ( !l_end && ( l_status = l_lines.next_line( l_line ) ) != LINE_NONE )
            {
                if ( l_status == LINE_TOO_LONG )
                {
                    log_msg( LOG_INFO, "[%d] Too long line '%s'.", getpid(), l_line.c_str() );
                    send_to_client( t_sock_client, proto_err( l_line, "line too long" ) );
                }
                else if ( l_line.empty() )
                {
                    continue;
                }
                else if ( l_line == PROTO_QUIT )
                {
                    log_msg( LOG_INFO, "[%d] Client sent " PROTO_QUIT " request to close connection.", getpid() );
                    l_end = true;
                }
                else if ( proto_parse( l_line, PROTO_REQ, l_expr ) && !l_expr.empty() )
                {
                    // expression waits in buffer for batch
                    l_pending.push_back( l_expr );
                    log_msg( LOG_INFO, "[%d] Buffered '%s' (%d/%d).", getpid(),
                             l_expr.c_str(), ( int ) l_pending.size(), BATCH_SIZE );
                }
                else
                {
                    log_msg( LOG_INFO, "[%d] Unknown message '%s'.", getpid(), l_line.c_str() );
                    send_to_client( t_sock_client, proto_err( l_line, "unknown message" ) );
                }
            }

            // enough expressions, or last ones before end?
            // python sends results to client
            if ( l_pending.size() >= BATCH_SIZE || ( l_end && !l_pending.empty() ) )
            {
                evaluate_batch( t_sock_client, l_pending );
                stat_report( t_stat_fd, l_pending.size(), false );
                l_pending.clear();
            }

            if ( l_end )
            {   // all results are sent, confirm end of connection
                send_to_client( t_sock_client, proto_cmd( PROTO_BYE ) );
                return;
            }
        }
    } // while communication
}

// ***************************************************************************

/**
 * @brief Forks new process serving client, parent returns immediately.
 *
 * Child closes listening socket and read end of statistics pipe, is
 * terminated when parent exits (PR_SET_PDEATHSIG), serves client by
 * handle_client(), reports its end and exits (never returns).
 * Parent closes client socket.
 *
 * @param t_sock_listen  listening socket (closed in child).
 * @param t_sock_client  socket of accepted client (closed in parent).
 * @param t_stat_pipe    statistics pipe, [0] read end, [1] write end.
 * @return pid of child in parent, -1 when fork failed.
 */
pid_t start_client_process( int t_sock_listen, int t_sock_client, const int t_stat_pipe[ 2 ] )
{
    fflush( stdout );   // do not duplicate buffered output in child

    pid_t l_parent = getpid();
    pid_t l_pid = fork();
    if ( l_pid < 0 )
    {
        log_msg( LOG_ERROR, "Unable to fork process for new client." );
        close( t_sock_client );
        return l_pid;
    }

    if ( l_pid == 0 )
    { // child: serves client only
        // terminate child when parent (server) exits
        prctl( PR_SET_PDEATHSIG, SIGTERM );
        if ( getppid() != l_parent ) exit( EXIT_FAILURE );  // parent died before prctl

        signal( SIGCHLD, SIG_DFL );     // child will wait for its own children
        signal( SIGPIPE, SIG_IGN );     // failed write to python returns error, no kill
        close( t_sock_listen );
        close( t_stat_pipe[ 0 ] );      // child only writes statistics

        log_msg( LOG_INFO, "[%d] Serving new client.", getpid() );
        handle_client( t_sock_client, t_stat_pipe[ 1 ] );

        stat_report( t_stat_pipe[ 1 ], 0, true );
        close( t_stat_pipe[ 1 ] );
        close( t_sock_client );
        log_msg( LOG_INFO, "[%d] Connection closed, process exits.", getpid() );
        exit( EXIT_SUCCESS );
    }

    // parent: client is served by child
    close( t_sock_client );
    return l_pid;
}

// ***************************************************************************

/**
 * @brief Socket server: accepts clients and forks process for each of them.
 *
 * Parent waits for new clients, commands on stdin ('quit', 'stat')
 * and statistics from children.
 *
 * @param t_argc  number of arguments.
 * @param t_argv  arguments: [-h] [-d] port_number.
 * @return EXIT_SUCCESS; program normally ends by exit() on 'quit' or stdin EOF.
 */
int main( int t_argc, char **t_argv )
{
    int l_port = 0;

    // parsing arguments
    for ( int inx = 1; inx < t_argc; inx++ )
    {
        if ( strcmp( t_argv[ inx ], "-d" ) == 0 )
        {
            g_debug = LOG_DEBUG;
        }

        if ( strcmp( t_argv[ inx ], "-h" ) == 0 )
        {
            printf( g_help, t_argv[ 0 ] );
            exit( EXIT_SUCCESS );
        }

        if ( *t_argv[ inx ] != '-' )
        {
            if ( l_port == 0 )
            {
                l_port = atoi( t_argv[ inx ] );
            }
        }
    }

    if ( l_port <= 0 || l_port > 65535 )
    {
        log_msg( LOG_INFO, "Bad or missing port number %d!", l_port );
        printf( g_help, t_argv[ 0 ] );
        exit( EXIT_FAILURE );
    }

    log_msg( LOG_INFO, "Server will listen on port: %d.", l_port );

    // socket creation
    int l_sock_listen = socket( AF_INET, SOCK_STREAM, 0 );
    if ( l_sock_listen < 0 )
    {
        log_msg( LOG_ERROR, "Unable to create socket.");
        exit( EXIT_FAILURE );
    }

    in_addr l_addr_any = { INADDR_ANY };
    sockaddr_in l_srv_addr;
    l_srv_addr.sin_family = AF_INET;
    l_srv_addr.sin_port = htons( l_port );
    l_srv_addr.sin_addr = l_addr_any;

    // Enable the port number reusing
    int l_opt = 1;
    if ( setsockopt( l_sock_listen, SOL_SOCKET, SO_REUSEADDR, &l_opt, sizeof( l_opt ) ) < 0 )
    {
      log_msg( LOG_ERROR, "Unable to set socket option!" );
    }

    // assign port number to socket
    if ( bind( l_sock_listen, (const sockaddr * ) &l_srv_addr, sizeof( l_srv_addr ) ) < 0 )
    {
        log_msg( LOG_ERROR, "Bind failed!" );
        close( l_sock_listen );
        exit( EXIT_FAILURE );
    }

    // listening on set port
    if ( listen( l_sock_listen, SOMAXCONN ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to listen on given port!" );
        close( l_sock_listen );
        exit( EXIT_FAILURE );
    }

    install_sigchld_handler();

    // pipe for statistics from children; parent keeps write end open too,
    // so read end never gets EOF; O_CLOEXEC: python3 does not inherit it
    int l_stat_pipe[ 2 ];
    if ( pipe2( l_stat_pipe, O_CLOEXEC ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to create pipe for statistics." );
        close( l_sock_listen );
        exit( EXIT_FAILURE );
    }
    stat_table l_stats;

    log_msg( LOG_INFO, "Enter 'quit' to quit server, '%s' for statistics.", STR_STAT );

    // go! Wait for clients and fork process for each of them.
    while ( 1 )
    {
        int l_sock_client = -1;

        // list of fd sources
        pollfd l_read_poll[ 3 ];

        l_read_poll[ 0 ].fd = STDIN_FILENO;
        l_read_poll[ 0 ].events = POLLIN;
        l_read_poll[ 1 ].fd = l_sock_listen;
        l_read_poll[ 1 ].events = POLLIN;
        l_read_poll[ 2 ].fd = l_stat_pipe[ 0 ];
        l_read_poll[ 2 ].events = POLLIN;

        while ( 1 ) // wait for new client
        {
            // select from fds
            int l_poll = poll( l_read_poll, 3, -1 );

            if ( l_poll < 0 )
            {
                if ( errno == EINTR ) continue;     // interrupted by SIGCHLD
                log_msg( LOG_ERROR, "Function poll failed!" );
                exit( EXIT_FAILURE );
            }

            if ( l_read_poll[ 0 ].revents & ( POLLIN | POLLHUP ) )
            { // data on stdin
                char buf[ 128 ];

                int l_len = read( STDIN_FILENO, buf, sizeof( buf) );
                if ( l_len == 0 )
                {
                    log_msg( LOG_DEBUG, "Stdin closed." );
                    exit( EXIT_SUCCESS );
                }
                if ( l_len < 0 )
                {
                    log_msg( LOG_DEBUG, "Unable to read from stdin!" );
                    exit( EXIT_FAILURE );
                }

                log_msg( LOG_DEBUG, "Read %d bytes from stdin", l_len );
                // request to quit?
                if ( l_len >= ( int ) strlen( STR_QUIT ) && !strncmp( buf, STR_QUIT, strlen( STR_QUIT ) ) )
                {
                    log_msg( LOG_INFO, "Request to 'quit' entered.");
                    close( l_sock_listen );
                    exit( EXIT_SUCCESS );
                }

                // request for statistics?
                if ( l_len >= ( int ) strlen( STR_STAT ) && !strncmp( buf, STR_STAT, strlen( STR_STAT ) ) )
                {
                    stat_print( l_stats );
                }
            }

            if ( l_read_poll[ 2 ].revents & POLLIN )
            { // statistics from child
                stat_receive( l_stat_pipe[ 0 ], l_stats );
            }

            if ( l_read_poll[ 1 ].revents & POLLIN )
            { // new client?
                sockaddr_in l_rsa;
                int l_rsa_size = sizeof( l_rsa );
                // new connection
                l_sock_client = accept( l_sock_listen, ( sockaddr * ) &l_rsa, ( socklen_t * ) &l_rsa_size );
                if ( l_sock_client < 0 )
                {   // failure of one connection must not stop the server
                    log_msg( LOG_ERROR, "Unable to accept new client." );
                    continue;
                }
                sockaddr_in l_log_addr;
                uint l_lsa = sizeof( l_log_addr );
                // my IP
                if ( getsockname( l_sock_client, ( sockaddr * ) &l_log_addr, &l_lsa ) == 0 )
                    log_msg( LOG_INFO, "My IP: '%s'  port: %d",
                                     inet_ntoa( l_log_addr.sin_addr ), ntohs( l_log_addr.sin_port ) );
                // client IP
                l_lsa = sizeof( l_log_addr );
                if ( getpeername( l_sock_client, ( sockaddr * ) &l_log_addr, &l_lsa ) == 0 )
                    log_msg( LOG_INFO, "Client IP: '%s'  port: %d",
                                     inet_ntoa( l_log_addr.sin_addr ), ntohs( l_log_addr.sin_port ) );

                break;
            }

        } // while wait for client

        // serve client in new process, parent continues waiting
        pid_t l_child = start_client_process( l_sock_listen, l_sock_client, l_stat_pipe );
        if ( l_child > 0 )
        {   // new client starts from 0 (pid could be reused after crashed child)
            l_stats.m_clients[ l_child ] = 0;
        }
    } // while ( 1 )

    return 0;
} // main

