// ***************************************************************************
//
// Program example for subject Operating Systems
//
// Petr Olivka, Dept. of Computer Science, petr.olivka@vsb.cz, 2026
//
// Example of socket server/client.
//
// This program is example of socket client.
// The mandatory arguments of program is IP address or name of server and
// a port number.
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
#include <ctime>
#include <string>

#include "protocol.h"

#define STR_CLOSE               "close"
#define STR_QUIT                "quit"

#define POLL_TIMEOUT_MS         2000    // no user input -> auto equation; final value 5000
#define QUIT_TIMEOUT_MS         5000    // after 'quit' wait so long for server to close connection
#define AUTO_MAX_NUMBER         9       // auto equations use numbers 0..AUTO_MAX_NUMBER

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
    "  Socket client example.\n"
    "\n"
    "  Use: %s [-h] [-d] ip_or_name port_number\n"
    "\n"
    "    -d  debug mode \n"
    "    -h  this help\n"
    "\n"
    ;

// ***************************************************************************

/**
 * @brief Generates simple random equation with '+' or '-'.
 *
 * Both numbers are from 0 to AUTO_MAX_NUMBER.
 *
 * @return equation without '\n', e.g. "7-3".
 */
std::string generate_equation()
{
    int l_a = rand() % ( AUTO_MAX_NUMBER + 1 );
    int l_b = rand() % ( AUTO_MAX_NUMBER + 1 );
    char l_op = ( rand() % 2 ) ? '+' : '-';

    return std::to_string( l_a ) + l_op + std::to_string( l_b );
}

// ***************************************************************************

/**
 * @brief Checks, whether user wants to end connection.
 *
 * @param t_line  one line typed by user, without '\n'.
 * @return true for 'quit' or 'close' (case insensitive), otherwise false.
 */
bool is_end_command( const std::string &t_line )
{
    return !strcasecmp( t_line.c_str(), STR_QUIT ) || !strcasecmp( t_line.c_str(), STR_CLOSE );
}

// ***************************************************************************

/**
 * @brief Sends one protocol message to server; all messages go through it.
 *
 * @param t_sock_server  socket connected to server.
 * @param t_msg          complete message including '\n' (see protocol.h).
 * @return true when message was sent, false on error (logged).
 */
bool send_to_server( int t_sock_server, const std::string &t_msg )
{
    int l_len = write( t_sock_server, t_msg.data(), t_msg.size() );
    if ( l_len < 0 )
    {
        log_msg( LOG_ERROR, "Unable to send data to server." );
        return false;
    }

    log_msg( LOG_DEBUG, "Sent %d bytes to server.", l_len );
    return true;
}

/**
 * @brief Sends expression for evaluation as message "REQ: <expr>".
 *
 * @param t_sock_server  socket connected to server.
 * @param t_expr         expression without '\n', e.g. "4+5*8".
 * @return true when message was sent, false on error.
 */
bool send_request( int t_sock_server, const std::string &t_expr )
{
    return send_to_server( t_sock_server, proto_msg( PROTO_REQ, t_expr ) );
}

// ***************************************************************************

/**
 * @brief Generates equation and sends it to server; used when user typed
 *        nothing for POLL_TIMEOUT_MS.
 *
 * @param t_sock_server  socket connected to server.
 * @return true when equation was sent, false on error.
 */
bool send_auto_equation( int t_sock_server )
{
    std::string l_eq = generate_equation();
    log_msg( LOG_INFO, "Timeout, sending auto equation: %s", l_eq.c_str() );

    return send_request( t_sock_server, l_eq );
}

// ***************************************************************************

/**
 * @brief Socket client: sends equations typed by user (or generated after
 *        timeout) to server and displays its answers.
 *
 * 'quit' or 'close' sends QUIT, then client waits for BYE (at most
 * QUIT_TIMEOUT_MS) and ends.
 *
 * @param t_argc  number of arguments.
 * @param t_argv  arguments: [-h] [-d] ip_or_name port_number.
 * @return 0 when client ends; EXIT_FAILURE (by exit()) on bad arguments
 *         or connection failure.
 */
int main( int t_argc, char **t_argv )
{
    // logs and server messages in right order also in file,
    // must be set before any output to stdout
    setvbuf( stdout, nullptr, _IOLBF, 0 );

    int l_port = 0;
    char *l_host = nullptr;

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
            if ( l_host == nullptr  )
            {
                l_host = t_argv[ inx ];
            }
            else if ( l_port == 0 )
            {
                l_port = atoi( t_argv[ inx ] );
            }
        }
    }

    if ( l_host == nullptr || l_port <= 0 || l_port > 65535 )
    {
        log_msg( LOG_INFO, "Host or port is missing!" );
        printf( g_help, t_argv[ 0 ] );
        exit( EXIT_FAILURE );
    }

    log_msg( LOG_INFO, "Connection to '%s':%d.", l_host, l_port );

    srand( time( nullptr ) ^ getpid() );   // different auto equations in every client
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
    log_msg( LOG_INFO, "My IP: '%s'  port: %d",
             inet_ntoa( l_cl_addr.sin_addr ), ntohs( l_cl_addr.sin_port ) );
    // server IP
    getpeername( l_sock_server, ( sockaddr * ) &l_cl_addr, &l_lsa );
    log_msg( LOG_INFO, "Server IP: '%s'  port: %d",
             inet_ntoa( l_cl_addr.sin_addr ), ntohs( l_cl_addr.sin_port ) );

    log_msg( LOG_INFO, "Enter 'quit' or 'close' to close application." );

    bool l_quitting = false;    // QUIT sent, waiting for server's BYE
    bool l_stop = false;        // end of main loop
    LineBuffer l_stdin_lines;   // user input, line by line
    LineBuffer l_server_lines;  // server messages, line by line

    // list of fd sources
    pollfd l_read_poll[ 2 ];

    l_read_poll[ 0 ].fd = STDIN_FILENO;
    l_read_poll[ 0 ].events = POLLIN;
    l_read_poll[ 1 ].fd = l_sock_server;
    l_read_poll[ 1 ].events = POLLIN;

    // go!
    while ( 1 )
    {
        char l_buf[ 128 ];

        // select from fds
        int l_poll = poll( l_read_poll, 2, l_quitting ? QUIT_TIMEOUT_MS : POLL_TIMEOUT_MS );
        if ( l_poll < 0 ) break;

        // timeout without any input?
        if ( l_poll == 0 )
        {
            if ( l_quitting )
            {
                log_msg( LOG_INFO, "Server did not close connection in time." );
                break;
            }
            if ( !send_auto_equation( l_sock_server ) ) break;
            continue;
        }

        // data on stdin?
        if ( l_read_poll[ 0 ].revents & ( POLLIN | POLLHUP | POLLERR | POLLNVAL ) )
        {
            //  read from stdin
            int l_len = read( STDIN_FILENO, l_buf, sizeof( l_buf ) );
            if ( l_len == 0 )
            {
                log_msg( LOG_DEBUG, "Stdin closed." );
                break;
            }
            if ( l_len < 0 )
            {
                log_msg( LOG_ERROR, "Unable to read from stdin." );
                break;
            }
            else
            {
                log_msg( LOG_DEBUG, "Read %d bytes from stdin.", l_len );
            }

            // every line is one expression or command
            l_stdin_lines.feed( l_buf, l_len );
            std::string l_line;
            line_status l_status;
            while ( !l_stop && !l_quitting && ( l_status = l_stdin_lines.next_line( l_line ) ) != LINE_NONE )
            {
                if ( l_status == LINE_TOO_LONG )
                {
                    log_msg( LOG_INFO, "Too long line '%s' ignored.", l_line.c_str() );
                }
                else if ( l_line.empty() )
                {
                    continue;
                }
                else if ( is_end_command( l_line ) )
                {
                    // server evaluates rest of equations and answers BYE
                    if ( !send_to_server( l_sock_server, proto_cmd( PROTO_QUIT ) ) ) l_stop = true;
                    log_msg( LOG_INFO, "Waiting for server to close connection..." );
                    l_quitting = true;
                    l_read_poll[ 0 ].fd = -1;   // poll ignores negative fd: no more user input
                }
                else if ( !send_request( l_sock_server, l_line ) )
                {
                    l_stop = true;
                }
            }
            if ( l_stop ) break;
        }

        // data from server?
        if ( l_read_poll[ 1 ].revents & ( POLLIN | POLLHUP | POLLERR ) )
        {
            // read data from server
            int l_len = read( l_sock_server, l_buf, sizeof( l_buf ) );
            if ( l_len == 0 )
            {
                log_msg( l_quitting ? LOG_INFO : LOG_DEBUG, "Server closed socket." );
                break;
            }
            else if ( l_len < 0 )
            {
                log_msg( LOG_ERROR, "Unable to read data from server." );
                break;
            }
            else
            {
                log_msg( LOG_DEBUG, "Read %d bytes from server.", l_len );
            }

            // display every complete message on stdout as it is
            l_server_lines.feed( l_buf, l_len );
            std::string l_line;
            line_status l_status;
            while ( !l_stop && ( l_status = l_server_lines.next_line( l_line ) ) != LINE_NONE )
            {
                std::string l_out = l_line + "\n";
                if ( write( STDOUT_FILENO, l_out.data(), l_out.size() ) < 0 )
                {
                    log_msg( LOG_ERROR, "Unable to write to stdout." );
                    l_stop = true;
                }

                // server confirmed QUIT, all results are received
                if ( l_status == LINE_OK && l_line == PROTO_BYE )
                {
                    log_msg( LOG_INFO, "Connection closed by server." );
                    l_stop = true;
                }
            }
            if ( l_stop ) break;
        }
    }

    // close socket
    close( l_sock_server );

    return 0;
} // main

