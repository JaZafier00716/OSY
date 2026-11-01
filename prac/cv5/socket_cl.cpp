// ***************************************************************************
//
// Program example for subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2026
//
// Example of socket server/client.
//
// This program is example of socket client. It sends files given as
// arguments to server, which returns their SHA1 hash and type.
// The mandatory arguments of program are IP address or name of server,
// a port number and at least one file.
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
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string>
#include <vector>

#include "protocol.h"


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
    "  Socket client: sends files to server, which returns their SHA1 and type.\n"
    "\n"
    "  Use: %s [-h] [-d] ip_or_name port_number file [file ...]\n"
    "\n"
    "    -d  debug mode \n"
    "    -h  this help\n"
    "\n"
    ;

// ***************************************************************************

/**
 * @brief Writes whole buffer into socket (write can be partial).
 *
 * @param t_fd    socket connected to server.
 * @param t_data  bytes to send.
 * @param t_len   number of bytes.
 * @return true when all bytes were sent, false on error (logged).
 */
bool send_all( int t_fd, const char *t_data, size_t t_len )
{
    while ( t_len > 0 )
    {
        ssize_t l_len = write( t_fd, t_data, t_len );
        if ( l_len < 0 )
        {
            if ( errno == EINTR ) continue;
            log_msg( LOG_ERROR, "Unable to send data to server." );
            return false;
        }
        t_data += l_len;
        t_len -= l_len;
    }
    return true;
}

/**
 * @brief Sends one protocol message to server.
 *
 * @param t_sock_server  socket connected to server.
 * @param t_msg          complete message including '\n' (see protocol.h).
 * @return true when message was sent, false on error.
 */
bool send_to_server( int t_sock_server, const std::string &t_msg )
{
    log_msg( LOG_DEBUG, "Sending: %s", t_msg.substr( 0, t_msg.size() - 1 ).c_str() );
    return send_all( t_sock_server, t_msg.data(), t_msg.size() );
}

// ***************************************************************************

/**
 * @brief Waits for next complete line from server (blocking).
 *
 * @param t_sock_server  socket connected to server.
 * @param t_lines        buffer of received data, keeps rest for next call.
 * @param t_line         [out] received line without '\n'.
 * @return true when line was received, false when server closed connection
 *         or on error.
 */
bool read_line( int t_sock_server, LineBuffer &t_lines, std::string &t_line )
{
    while ( 1 )
    {
        if ( t_lines.next_line( t_line ) != LINE_NONE ) return true;

        char l_buf[ 1024 ];
        int l_len = read( t_sock_server, l_buf, sizeof( l_buf ) );
        if ( l_len == 0 )
        {
            log_msg( LOG_INFO, "Server closed socket." );
            return false;
        }
        if ( l_len < 0 )
        {
            if ( errno == EINTR ) continue;
            log_msg( LOG_ERROR, "Unable to read data from server." );
            return false;
        }
        log_msg( LOG_DEBUG, "Read %d bytes from server.", l_len );
        t_lines.feed( l_buf, l_len );
    }
}

// ***************************************************************************

/**
 * @brief Sends one file to server as "FILE: <size> <name>" and its content.
 *
 * @param t_sock_server  socket connected to server.
 * @param t_name         name of file to send.
 * @param t_opened       [out] false when file could not be opened/read locally
 *                       (nothing was sent, connection is still usable).
 * @return true when whole file was sent, false otherwise.
 */
bool send_file( int t_sock_server, const char *t_name, bool &t_opened )
{
    t_opened = false;

    int l_fd = open( t_name, O_RDONLY );
    if ( l_fd < 0 )
    {
        log_msg( LOG_ERROR, "Unable to open file '%s'.", t_name );
        return false;
    }

    struct stat l_st;
    if ( fstat( l_fd, &l_st ) < 0 || !S_ISREG( l_st.st_mode ) )
    {
        log_msg( LOG_INFO, "'%s' is not regular file, skipped.", t_name );
        close( l_fd );
        return false;
    }
    t_opened = true;

    std::string l_name = t_name;
    for ( char &l_c : l_name )
        if ( l_c == '\n' || l_c == '\r' ) l_c = '?';    // header must stay one line

    unsigned long long l_size = l_st.st_size;
    log_msg( LOG_INFO, "Sending '%s' (%llu bytes).", t_name, l_size );

    bool l_ok = send_to_server( t_sock_server, proto_msg( PROTO_FILE, std::to_string( l_size ) + " " + l_name ) );

    // send exactly l_size bytes, as announced in header
    static char l_buf[ PROTO_CHUNK ];
    unsigned long long l_sent = 0;
    while ( l_ok && l_sent < l_size )
    {
        size_t l_want = sizeof( l_buf );
        if ( l_size - l_sent < l_want ) l_want = l_size - l_sent;

        int l_len = read( l_fd, l_buf, l_want );
        if ( l_len <= 0 )
        {   // file shrank or read error: server would wait forever, connection is broken
            log_msg( LOG_ERROR, "Unable to read file '%s'.", t_name );
            l_ok = false;
            break;
        }
        l_ok = send_all( t_sock_server, l_buf, l_len );
        l_sent += l_len;
    }

    close( l_fd );
    log_msg( LOG_DEBUG, "Sent %llu bytes of '%s'.", l_sent, t_name );
    return l_ok;
}

// ***************************************************************************

/**
 * @brief Socket client: sends files given as arguments one after another
 *        to server and prints their SHA1 and type.
 *
 * After every file client waits for server's answer (RES or ERR).
 * After last file QUIT is sent and client waits for BYE.
 *
 * @param t_argc  number of arguments.
 * @param t_argv  arguments: [-h] [-d] ip_or_name port_number file [file ...].
 * @return 0 when all files were processed, 1 when some failed;
 *         EXIT_FAILURE (by exit()) on bad arguments or connection failure.
 */
int main( int t_argc, char **t_argv )
{
    // logs and server messages in right order also in file,
    // must be set before any output to stdout
    setvbuf( stdout, nullptr, _IOLBF, 0 );

    int l_port = 0;
    char *l_host = nullptr;
    std::vector<char *> l_files;

    // parsing arguments
    for ( int inx = 1; inx < t_argc; inx++ )
    {
        if ( strcmp( t_argv[ inx ], "-d" ) == 0 )
        {
            g_debug = LOG_DEBUG;
            continue;
        }

        if ( strcmp( t_argv[ inx ], "-h" ) == 0 )
        {
            printf( g_help, t_argv[ 0 ] );
            exit( EXIT_SUCCESS );
        }

        if ( l_host == nullptr )
        {
            l_host = t_argv[ inx ];
        }
        else if ( l_port == 0 )
        {
            l_port = atoi( t_argv[ inx ] );
            if ( l_port <= 0 ) break;
        }
        else
        {
            l_files.push_back( t_argv[ inx ] );     // every other argument is file
        }
    }

    if ( l_host == nullptr || l_port <= 0 || l_port > 65535 )
    {
        log_msg( LOG_INFO, "Host or port is missing!" );
        printf( g_help, t_argv[ 0 ] );
        exit( EXIT_FAILURE );
    }

    if ( l_files.empty() )
    {
        log_msg( LOG_INFO, "No file to send!" );
        printf( g_help, t_argv[ 0 ] );
        exit( EXIT_FAILURE );
    }

    log_msg( LOG_INFO, "Connection to '%s':%d.", l_host, l_port );

    signal( SIGPIPE, SIG_IGN );     // write to closed connection returns error, no kill

    addrinfo l_ai_req, *l_ai_ans;
    bzero( &l_ai_req, sizeof( l_ai_req ) );
    l_ai_req.ai_family = AF_INET;
    l_ai_req.ai_socktype = SOCK_STREAM;

    int l_get_ai = getaddrinfo( l_host, nullptr, &l_ai_req, &l_ai_ans );
    if ( l_get_ai )
    {
        log_msg( LOG_ERROR, "Unknown host name!" );
        exit( EXIT_FAILURE );
    }

    // get the first address
    sockaddr_in l_cl_addr =  *( sockaddr_in * ) l_ai_ans->ai_addr;
    l_cl_addr.sin_port = htons( l_port );
    freeaddrinfo( l_ai_ans );

    // socket creation
    int l_sock_server = socket( AF_INET, SOCK_STREAM, 0 );
    if ( l_sock_server < 0 )
    {
        log_msg( LOG_ERROR, "Unable to create socket.");
        exit( EXIT_FAILURE );
    }

    // connect to server
    if ( connect( l_sock_server, ( sockaddr * ) &l_cl_addr, sizeof( l_cl_addr ) ) < 0 )
    {
        log_msg( LOG_ERROR, "Unable to connect server." );
        exit( EXIT_FAILURE );
    }

    uint l_lsa = sizeof( l_cl_addr );
    // my IP
    getsockname( l_sock_server, ( sockaddr * ) &l_cl_addr, &l_lsa );
    log_msg( LOG_DEBUG, "My IP: '%s'  port: %d",
             inet_ntoa( l_cl_addr.sin_addr ), ntohs( l_cl_addr.sin_port ) );
    // server IP
    getpeername( l_sock_server, ( sockaddr * ) &l_cl_addr, &l_lsa );
    log_msg( LOG_DEBUG, "Server IP: '%s'  port: %d",
             inet_ntoa( l_cl_addr.sin_addr ), ntohs( l_cl_addr.sin_port ) );

    LineBuffer l_server_lines;  // server messages, line by line
    bool l_connected = true;    // false after fatal error, nothing more is sent
    int l_failed = 0;           // files without SHA1

    // go! files one after another
    for ( char *l_file : l_files )
    {
        bool l_opened;
        if ( !send_file( l_sock_server, l_file, l_opened ) )
        {
            l_failed++;
            if ( !l_opened ) continue;  // nothing sent, try next file
            l_connected = false;
            break;
        }

        std::string l_line, l_value;
        if ( !read_line( l_sock_server, l_server_lines, l_line ) )
        {
            l_failed++;
            l_connected = false;
            break;
        }

        if ( proto_parse( l_line, PROTO_RES, l_value ) )
        {   // "SHA1: <hash> | Typ souboru: <type>"
            printf( "%s: %s\n", l_file, l_value.c_str() );
        }
        else
        {
            printf( "%s: %s\n", l_file, l_line.c_str() );
            l_failed++;
        }
    }

    if ( l_connected && send_to_server( l_sock_server, proto_cmd( PROTO_QUIT ) ) )
    {   // wait for confirmation of end
        std::string l_line;
        while ( read_line( l_sock_server, l_server_lines, l_line ) )
        {
            if ( l_line == PROTO_BYE )
            {
                log_msg( LOG_DEBUG, "Connection closed by server." );
                break;
            }
            printf( "%s\n", l_line.c_str() );
        }
    }

    // close socket
    close( l_sock_server );

    return l_failed ? 1 : 0;
} // main
