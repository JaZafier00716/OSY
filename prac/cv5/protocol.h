// ***************************************************************************
//
// Simple line based protocol between socket_cl and socket_srv.
// Every message is one line terminated by '\n', only FILE header is
// followed by binary content of file.
//
// client -> server:
//      FILE: <size> <name>         header, exactly <size> bytes of file follow
//      QUIT                        end of connection
//
// server -> client:
//      RES: SHA1: <hash> | Typ souboru: <type>
//                                  result for one file (sha1sum and file)
//      ERR: <what>: <reason>       processing failed, unknown message, line too long
//      BYE                         answer to QUIT, then server closes connection
//
// Client waits for RES/ERR of one file before it sends next one.
//
// ***************************************************************************

#pragma once

#include <cstring>
#include <string>

#define PROTO_FILE          "FILE: "
#define PROTO_RES           "RES: "
#define PROTO_ERR           "ERR: "
#define PROTO_QUIT          "QUIT"
#define PROTO_BYE           "BYE"

#define PROTO_MAX_LINE      1024    // longer line is discarded
#define PROTO_SHOW_LONG     32      // start of too long line shown in error
#define PROTO_CHUNK         65536   // size of blocks for file transfer

// ***************************************************************************
// building of messages

/**
 * @brief Builds message with value, e.g. "REQ: 4+5*8\n".
 *
 * @param t_prefix  type of message with ": ", e.g. PROTO_REQ.
 * @param t_value   value of message without '\n'.
 * @return complete message including '\n'.
 */
inline std::string proto_msg( const char *t_prefix, const std::string &t_value )
{
    return t_prefix + t_value + "\n";
}

/**
 * @brief Builds message without value, e.g. "QUIT\n".
 *
 * @param t_cmd  type of message, e.g. PROTO_QUIT or PROTO_BYE.
 * @return complete message including '\n'.
 */
inline std::string proto_cmd( const char *t_cmd )
{
    return std::string( t_cmd ) + "\n";
}

/**
 * @brief Builds error message "ERR: <what>: <reason>\n".
 *
 * @param t_what    failed expression or received line.
 * @param t_reason  short description of error, e.g. "unknown message".
 * @return complete message including '\n'.
 */
inline std::string proto_err( const std::string &t_what, const std::string &t_reason )
{
    return PROTO_ERR + t_what + ": " + t_reason + "\n";
}

// ***************************************************************************
// parsing of messages

/**
 * @brief Checks type of received message and extracts its value.
 *
 * @param t_line    received line without '\n'.
 * @param t_prefix  expected type of message with ": ", e.g. PROTO_REQ.
 * @param t_value   [out] rest of line after prefix; set only on success.
 * @return true when line starts with t_prefix, otherwise false.
 */
inline bool proto_parse( const std::string &t_line, const char *t_prefix, std::string &t_value )
{
    size_t l_len = strlen( t_prefix );
    if ( t_line.compare( 0, l_len, t_prefix ) != 0 ) return false;

    t_value = t_line.substr( l_len );
    return true;
}

// ***************************************************************************
// collects received bytes and returns complete lines,
// so message split into more reads (TCP is stream) is joined again

enum line_status
{
    LINE_NONE,          // no complete line yet
    LINE_OK,            // t_line contains line without '\n' and '\r'
    LINE_TOO_LONG,      // t_line contains start of too long line, rest is discarded
};

/**
 * @brief Collects received bytes and returns them as complete lines.
 *
 * Message split into more reads (TCP is stream) is joined again.
 * Line longer than PROTO_MAX_LINE is reported once and discarded.
 */
class LineBuffer
{
public:
    /**
     * @brief Adds received data to buffer.
     *
     * @param t_data  received bytes, need not be terminated by '\0' or '\n'.
     * @param t_len   number of bytes.
     */
    void feed( const char *t_data, int t_len )
    {
        m_buf.append( t_data, t_len );
    }

    /**
     * @brief Takes next complete line from buffer.
     *
     * Call it repeatedly after feed(), until it returns LINE_NONE.
     *
     * @param t_line  [out] line without '\n' and '\r' (LINE_OK), or start
     *                of too long line followed by "..." (LINE_TOO_LONG).
     * @return LINE_OK, LINE_TOO_LONG, or LINE_NONE when no complete line
     *         is in buffer yet (t_line is then not valid).
     */
    line_status next_line( std::string &t_line )
    {
        while ( 1 )
        {
            size_t l_end = m_buf.find( '\n' );

            if ( l_end == std::string::npos )
            {
                if ( m_buf.size() <= PROTO_MAX_LINE ) return LINE_NONE;

                // too long line without end: report once, discard it until '\n'
                bool l_report = !m_discarding;
                t_line = m_buf.substr( 0, PROTO_SHOW_LONG ) + "...";
                m_buf.clear();
                m_discarding = true;
                if ( l_report ) return LINE_TOO_LONG;
                return LINE_NONE;
            }

            t_line = m_buf.substr( 0, l_end );
            m_buf.erase( 0, l_end + 1 );

            if ( m_discarding )
            {   // end of too long line, already reported
                m_discarding = false;
                continue;
            }

            if ( !t_line.empty() && t_line.back() == '\r' ) t_line.pop_back();

            if ( t_line.size() > PROTO_MAX_LINE )
            {
                t_line = t_line.substr( 0, PROTO_SHOW_LONG ) + "...";
                return LINE_TOO_LONG;
            }
            return LINE_OK;
        }
    }

    /**
     * @brief Takes raw (binary) bytes from buffer, e.g. content of file,
     *        which arrived in the same read as its FILE header.
     *
     * @param t_max  at most so many bytes are taken.
     * @return taken bytes, empty when buffer is empty.
     */
    std::string take( size_t t_max )
    {
        std::string l_data = m_buf.substr( 0, t_max );
        m_buf.erase( 0, l_data.size() );
        return l_data;
    }

private:
    std::string m_buf;              // received data without complete line yet
    bool m_discarding = false;      // rest of too long line is being discarded
};
