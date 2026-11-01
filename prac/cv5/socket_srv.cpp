// ***************************************************************************
//
// Program example for labs in subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2026
//
// Example of socket server computing SHA1 and type of received files.
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
#include <cctype>
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

#define TMP_TEMPLATE    "/tmp/socket_srv_XXXXXX"   // received files are stored here

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
 * @brief Runs command in new process, waits for its end and returns its stdout.
 *
 * Command's stdout is redirected to pipe, which is read by parent until EOF.
 * Its stderr stays in server terminal. Client socket is closed in child,
 * so command never writes to client directly.
 *
 * @param t_args        program and its arguments, t_args[ 0 ] is searched in PATH.
 * @param t_close_fd    descriptor closed in child (client socket), -1 for none.
 * @param t_out         [out] complete stdout of command.
 * @return true when command finished with exit code 0, false when pipe,
 *         fork or exec failed or command ended with error.
 */
bool run_command( const std::vector<std::string> &t_args, int t_close_fd, std::string &t_out )
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
        log_msg( LOG_ERROR, "Unable to fork process for %s.", t_args[ 0 ].c_str() );
        close( l_pipe[ 0 ] );
        close( l_pipe[ 1 ] );
        return false;
    }

    if ( l_pid == 0 )
    { // child: becomes command
        if ( dup2( l_pipe[ 1 ], STDOUT_FILENO ) < 0 )
        {
            log_msg( LOG_ERROR, "Unable to redirect stdout of %s.", t_args[ 0 ].c_str() );
            _exit( EXIT_FAILURE );
        }
        close( l_pipe[ 0 ] );
        close( l_pipe[ 1 ] );
        if ( t_close_fd >= 0 ) close( t_close_fd );

        std::vector<char *> l_argv;
        for ( const std::string &l_arg : t_args )
            l_argv.push_back( const_cast<char *>( l_arg.c_str() ) );
        l_argv.push_back( nullptr );

        execvp( l_argv[ 0 ], l_argv.data() );
        log_msg( LOG_ERROR, "Unable to execute %s.", t_args[ 0 ].c_str() );
        _exit( EXIT_FAILURE );
    }

    // parent: read whole output of command
    close( l_pipe[ 1 ] );   // otherwise read never gets EOF
    t_out.clear();
    char l_buf[ 1024 ];
    int l_len;
    while ( ( l_len = read( l_pipe[ 0 ], l_buf, sizeof( l_buf ) ) ) > 0 )
        t_out.append( l_buf, l_len );
    if ( l_len < 0 ) log_msg( LOG_ERROR, "Unable to read output of %s.", t_args[ 0 ].c_str() );
    close( l_pipe[ 0 ] );

    int l_status = 0;
    if ( waitpid( l_pid, &l_status, 0 ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to wait for %s.", t_args[ 0 ].c_str() );
        return false;
    }

    return l_len == 0 && WIFEXITED( l_status ) && WEXITSTATUS( l_status ) == 0;
}

// ***************************************************************************

/**
 * @brief Sends one protocol message to client.
 *
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
 * @brief Computes SHA1 (sha1sum) and type (file) of received file
 *        and sends "RES: SHA1: <hash> | Typ souboru: <type>" to client.
 *
 * When one of commands fails, client receives "ERR: <name>: <reason>".
 *
 * @param t_sock_client  socket of client.
 * @param t_path         temporary file with received content.
 * @param t_name         name of file given by client (for messages only).
 */
void process_file( int t_sock_client, const std::string &t_path, const std::string &t_name )
{
    std::string l_out;

    // sha1sum prints "<hash>  <path>"
    if ( !run_command( { "sha1sum", t_path }, t_sock_client, l_out ) )
    {
        send_to_client( t_sock_client, proto_err( t_name, "sha1sum failed" ) );
        return;
    }
    std::string l_hash = l_out.substr( 0, l_out.find_first_of( " \t\n" ) );

    // file -b prints only description of type, without file name
    if ( !run_command( { "file", "-b", t_path }, t_sock_client, l_out ) )
    {
        send_to_client( t_sock_client, proto_err( t_name, "file failed" ) );
        return;
    }
    while ( !l_out.empty() && ( l_out.back() == '\n' || l_out.back() == '\r' ) ) l_out.pop_back();
    for ( char &l_c : l_out )
        if ( l_c == '\n' || l_c == '\r' ) l_c = ' ';  // answer must stay one line

    log_msg( LOG_INFO, "[%d] '%s': SHA1 %s, %s.", getpid(), t_name.c_str(), l_hash.c_str(), l_out.c_str() );
    send_to_client( t_sock_client, proto_msg( PROTO_RES, "SHA1: " + l_hash + " | Typ souboru: " + l_out ) );
}

// ***************************************************************************

/**
 * @brief Temporary file being received from client.
 *
 * File is closed and removed automatically, also when client
 * disconnects in the middle of transfer.
 */
struct recv_file
{
    int m_fd = -1;                      // temporary file, -1 when no transfer runs
    std::string m_path;                 // its name
    std::string m_name;                 // name of file given by client
    unsigned long long m_remaining = 0; // bytes still expected

    /**
     * @brief Creates temporary file for new transfer.
     *
     * @return true on success, false when file can not be created.
     */
    bool start( const std::string &t_name, unsigned long long t_size )
    {
        char l_path[] = TMP_TEMPLATE;
        m_fd = mkstemp( l_path );
        if ( m_fd < 0 ) return false;
        m_path = l_path;
        m_name = t_name;
        m_remaining = t_size;
        return true;
    }

    /**
     * @brief Ends transfer: closes and removes temporary file.
     */
    void finish()
    {
        if ( m_fd >= 0 ) close( m_fd );
        if ( !m_path.empty() ) unlink( m_path.c_str() );
        m_fd = -1;
        m_path.clear();
        m_remaining = 0;
    }

    bool active() const { return m_fd >= 0; }

    ~recv_file() { finish(); }
};

/**
 * @brief Writes whole buffer into file (write can be partial).
 *
 * @return true when all bytes were written.
 */
bool write_all( int t_fd, const char *t_data, size_t t_len )
{
    while ( t_len > 0 )
    {
        ssize_t l_len = write( t_fd, t_data, t_len );
        if ( l_len < 0 )
        {
            if ( errno == EINTR ) continue;
            return false;
        }
        t_data += l_len;
        t_len -= l_len;
    }
    return true;
}

/**
 * @brief Parses value of FILE header "<size> <name>".
 *
 * @param t_value  value of message after "FILE: ".
 * @param t_size   [out] size of file.
 * @param t_name   [out] name of file, may contain spaces.
 * @return true when header is valid.
 */
bool parse_file_header( const std::string &t_value, unsigned long long &t_size, std::string &t_name )
{
    if ( t_value.empty() || !isdigit( ( unsigned char ) t_value[ 0 ] ) ) return false;

    char *l_end = nullptr;
    errno = 0;
    t_size = strtoull( t_value.c_str(), &l_end, 10 );
    if ( errno || *l_end != ' ' || l_end[ 1 ] == '\0' ) return false;

    t_name = l_end + 1;
    return true;
}

// ***************************************************************************
// statistics: children report processed files to parent through pipe

struct stat_msg
{
    pid_t m_pid;            // child serving client
    int m_count;            // files processed since last message
    bool m_finished;        // client disconnected, child exits
};

struct stat_table
{
    std::map<pid_t, int> m_clients;     // connected clients: pid -> files
    int m_total = 0;                    // files of all clients
};

/**
 * @brief Child: reports processed files to parent through statistics pipe.
 *
 * Message is smaller than PIPE_BUF, so write is atomic and messages
 * from more children never mix. Failure is only logged.
 *
 * @param t_stat_fd   write end of statistics pipe.
 * @param t_count     number of files processed since last report.
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
    log_msg( LOG_INFO, "STAT: %d client(s) connected, all clients processed %d files.",
             ( int ) t_table.m_clients.size(), t_table.m_total );

    for ( const auto &l_client : t_table.m_clients )
    {
        log_msg( LOG_INFO, "STAT:   client [%d]: %d files", l_client.first, l_client.second );
    }
}

/**
 * @brief Parent: receives one message from statistics pipe and updates table.
 *
 * Every update is logged. Finished client is removed from table,
 * its files stay in total.
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
        log_msg( LOG_INFO, "STAT: client [%d] finished, %d files processed (all clients: %d).",
                 l_msg.m_pid, l_count, t_table.m_total );
        t_table.m_clients.erase( l_msg.m_pid );
    }
    else
    {
        log_msg( LOG_INFO, "STAT: client [%d] processed %d files (all clients: %d).",
                 l_msg.m_pid, l_count, t_table.m_total );
    }
}

// ***************************************************************************

/**
 * @brief Communication with one client, runs in child process.
 *
 * Reads protocol messages line by line. FILE header switches to receiving
 * of binary content, which is stored into temporary file. When whole file
 * is received, its SHA1 and type are sent to client and line mode continues.
 * QUIT sends BYE and ends. Unknown message, bad header or too long line
 * is answered by ERR. Processed files are reported to parent.
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

    LineBuffer l_lines;                     // joins messages split into more reads
    recv_file l_file;                       // file being received
    static char l_buf[ PROTO_CHUNK ];

    while ( 1 )
    { // communication
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
                if ( l_file.active() )
                    log_msg( LOG_INFO, "[%d] Client closed socket during transfer of '%s'.",
                             getpid(), l_file.m_name.c_str() );
                else
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

            l_lines.feed( l_buf, l_len );
            while ( !l_end )
            {
                if ( l_file.active() )
                {   // content of file
                    std::string l_data = l_lines.take( l_file.m_remaining );
                    if ( l_data.empty() ) break;    // wait for more data

                    if ( !write_all( l_file.m_fd, l_data.data(), l_data.size() ) )
                    {
                        log_msg( LOG_ERROR, "[%d] Unable to write temporary file.", getpid() );
                        send_to_client( t_sock_client, proto_err( l_file.m_name, "unable to store file" ) );
                        return;     // rest of file can not be skipped reliably
                    }
                    l_file.m_remaining -= l_data.size();

                    if ( l_file.m_remaining == 0 )
                    {   // whole file received
                        close( l_file.m_fd );
                        l_file.m_fd = -1;
                        process_file( t_sock_client, l_file.m_path, l_file.m_name );
                        l_file.finish();
                        stat_report( t_stat_fd, 1, false );
                    }
                    continue;
                }

                // every complete line is one message
                std::string l_line, l_value;
                line_status l_status = l_lines.next_line( l_line );
                if ( l_status == LINE_NONE ) break;

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
                else if ( proto_parse( l_line, PROTO_FILE, l_value ) )
                {
                    unsigned long long l_size;
                    std::string l_name;
                    if ( !parse_file_header( l_value, l_size, l_name ) )
                    {   // size is unknown, data can not be skipped
                        log_msg( LOG_INFO, "[%d] Bad file header '%s'.", getpid(), l_line.c_str() );
                        send_to_client( t_sock_client, proto_err( l_line, "bad file header" ) );
                        return;
                    }
                    if ( !l_file.start( l_name, l_size ) )
                    {
                        log_msg( LOG_ERROR, "[%d] Unable to create temporary file.", getpid() );
                        send_to_client( t_sock_client, proto_err( l_name, "unable to store file" ) );
                        return;
                    }
                    log_msg( LOG_INFO, "[%d] Receiving '%s' (%llu bytes) into %s.", getpid(),
                             l_name.c_str(), l_size, l_file.m_path.c_str() );

                    if ( l_size == 0 )
                    {   // empty file, no data follow
                        close( l_file.m_fd );
                        l_file.m_fd = -1;
                        process_file( t_sock_client, l_file.m_path, l_file.m_name );
                        l_file.finish();
                        stat_report( t_stat_fd, 1, false );
                    }
                }
                else
                {
                    log_msg( LOG_INFO, "[%d] Unknown message '%s'.", getpid(), l_line.c_str() );
                    send_to_client( t_sock_client, proto_err( l_line, "unknown message" ) );
                }
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
        signal( SIGPIPE, SIG_IGN );     // failed write to client returns error, no kill
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
    // so read end never gets EOF; O_CLOEXEC: sha1sum and file do not inherit it
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

