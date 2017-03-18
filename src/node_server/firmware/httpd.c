// Module for httpd

// #define NODE_DBG_HTTPD c_printf
#define NODE_DBG_HTTPD

#include "module.h"         // module related
#include "lauxlib.h"        // lua related
#include "lwip/ip_addr.h"   // ip address functions
#include "espconn.h"        // esp connection
#include "c_string.h"       // string functions
#include "c_stdlib.h"       // memory functions
#include "vfs.h"            // filesystem functions
#include "platform.h"       // need for system_get_time()

typedef struct socket_userdata
{
  struct espconn *pesp_conn;
  int id;
  int self_ref;
  int cb_send_ref;
  int post_type;
  char * path;
}socket_userdata;

#define REQUEST_UNKNOWN 0
#define REQUEST_NOT_ALLOWED 1
#define REQUEST_GET_QUERY 2
#define REQUEST_GET 3
#define REQUEST_POST_FORM 4
#define REQUEST_POST_JSON 5
#define REQUEST_POST_TEXT 6

#define HTTPD_PATH_DEFAULT 0
#define HTTPD_PATH_HOME 1
#define HTTPD_PATH_FAVICON 2
#define HTTPD_PATH_STATIC 3
#define HTTPD_PATH_DATA 4

#define MAX_SOCKET 4
static int socket_array[MAX_SOCKET];
static struct espconn *pTcpServer   = NULL;
static int tcpserver_cb_receive_ref = LUA_NOREF; // for socket receive callback
static uint16_t tcp_server_timeover = 30;
static int socket_id = 0;

static char * httpd_etag;
static char * httpd_if_none_match;
static char * httpd_etag_not_modified;
static char * httpd_auth;

static const char http_header_200[] = "HTTP/1.1 200 OK\r\n\r\n"; //
static const char http_header_204[] = "HTTP/1.1 204 No Content\r\n\r\n"; //
static const char http_header_301[] = "HTTP/1.1 301 Moved Permanently\r\n\r\n"; //
static const char http_header_302[] = "HTTP/1.1 302 Found\r\n\r\n"; //
static const char http_header_304[] = "HTTP/1.1 304 Not Modified\r\n\r\n"; //
static const char http_header_400[] = "HTTP/1.1 400 Bad Request\r\n\r\n"; // malformed syntax
static const char http_header_401[] = "HTTP/1.1 401 Authorization Required\r\nWWW-Authenticate: Basic realm=\"Secure Area\"\r\nContent-Type: text/plain\r\nContent-Length: 12\r\n\r\nUnauthorised";
static const char http_header_403[] = "HTTP/1.1 403 Forbidden\r\n\r\n";
static const char http_header_404[] = "HTTP/1.1 404 Not Found\r\n\r\n";
static const char http_header_405[] = "HTTP/1.1 405 Method Not Allowed\r\n\r\n"; // method specified in request line is not allowed
static const char http_header_410[] = "HTTP/1.1 410 Gone\r\n\r\n";
static const char http_header_500[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
static const char http_header_501[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
static const char http_header_503[] = "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 30\r\n\r\n"; //  send back Retry-After:

// =============================================================================

#define hexchar_to_dec(c) ((c) & 0x40 ? ((c) & 0x0F) + 9 : ((c) & 0x0F))
const char httpd_hexbytes[] = "0123456789abcdef";

static const char * httpd_mime [] = {
  "bin",  "application/octet-stream", // don't move this line in httpd mime list
  "css",  "text/css",
  "gif",  "image/gif",
  "htm",  "text/html",
  "html", "text/html",
  "ico",  "image/x-icon",
  "jpeg", "image/jpeg",
  "jpg",  "image/jpeg",
  "js",   "application/javascript",
  "json", "application/json",
  "pdf",  "application/pdf",
  "png",  "image/png",
  "svg",  "image/svg+xml",
  "text", "text/plain",
  "txt",  "text/plain",
  "txt",  "text/plain",
  "xml",  "application/xml",
  "zip",  "application/zip",
  NULL, NULL
};

static int httpd_find_text_pos ( const char *s, const char *f )
{
  if ( s && f )
  {
    char *p = strstr ( s, f );
    if ( p )
    {
      return p - s;
    }
  }
  return 0;
}

static int httpd_find_char_last_pos ( const char *s, int f )
{
  if ( s )
  {
    char *p = strrchr ( s, f );
    if ( p )
    {
      return p - s;
    }
  }
  return 0;
}

void httpd_uri_decode ( lua_State *L, const char *s, size_t length )
{
  luaL_Buffer b;
  luaL_buffinit ( L, &b );
  size_t i;
  char chr;
  if ( s && length > 0 && luaL_prepbuffer ( &b ) )
  {
    for ( i = 0; i < length; i++ )
    {
      chr = s[i];
      if ( chr == '%' && i < length - 2 && isxdigit( s[i + 1] ) && isxdigit( s[i + 2] ) )
      {
        chr = (unsigned char)( (hexchar_to_dec( s[i + 1] ) << 4) + hexchar_to_dec( s[i + 2] ) );
        i += 2;
      }
      else if ( chr == '+' )
      {
        chr = ' ';
      }
      if ( chr > 0 )
      {
        luaL_addchar ( &b, chr );
      }
    }
  }
  luaL_pushresult( &b );
}

void httpd_request_query_pair ( lua_State *L, const char * s, size_t var_pos, size_t sep_pos, size_t end_pos )
{
  if ( var_pos + 1 < sep_pos && sep_pos < end_pos )
  {
    lua_pushlstring     ( L, s + var_pos, sep_pos - var_pos );
    httpd_uri_decode    ( L, s + sep_pos + 1, end_pos - sep_pos - 1 );
    lua_settable        ( L, -3 );
  }
  else if ( var_pos + 2 <  end_pos )
  {
    lua_pushlstring     ( L, s + var_pos, end_pos - var_pos );
    lua_pushlstring     ( L, "", 0 );
    lua_settable        ( L, -3 );
  }
}

void httpd_request_query ( lua_State *L, const char *s, size_t length )
{
  size_t i;
  size_t var_pos = 0;
  size_t sep_pos = 0;
  char chr;
  lua_createtable( L, 0, 0 );
  if ( s )
  {
    for ( i = 0; i < length; i++ )
    {
      switch ( s[i] )
      {
        case '=':
          sep_pos = i;
          break;
        case '&':
          httpd_request_query_pair ( L, s, var_pos, sep_pos, i );
          var_pos = i + 1;
          break;
        default:
          break;
      }
    }
    httpd_request_query_pair ( L, s, var_pos, sep_pos, i );
  }
}

static const char *bytes64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64Encode ( char * data, unsigned int len )
{
  int out_len = ( 4 * ( ( len + 2 ) / 3 ) ) + 1;
  char *out = ( char * ) c_zalloc ( out_len );
  int j = 0, i;
  for ( i = 0; i < len; i += 3 )
  {
    bool lenm1 = ( i + 1 < len ) ? true : false;
    bool lenm2 = ( i + 2 < len ) ? true : false;
    int a    = data[i];
    int b    = lenm1 ? data[i + 1] : 0;
    int c    = lenm2 ? data[i + 2] : 0;
    out[j++] = bytes64[a >> 2];
    out[j++] = bytes64[( ( a & 3 ) << 4 ) | (b >> 4)];
    out[j++] = lenm1 ? bytes64[( ( b & 15 ) << 2 ) | ( c >> 6 )] : 61;
    out[j++] = lenm2 ? bytes64[( c & 63 )] : 61;
  }
  return out;
}

// =============================================================================

static void httpd_socket_sent_close_callback ( void *arg )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_sent_close_callback\n" );

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }
  if ( pesp_conn->proto.tcp->remote_port || pesp_conn->proto.tcp->local_port )
  {
    espconn_disconnect ( pesp_conn );
  }
  return;
}

static int httpd_socket_response_status ( lua_State *L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_response_status\n" );

  struct espconn *pesp_conn = NULL;
  const char * http_header;

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  unsigned status = luaL_checkinteger ( L, 2 );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
    pesp_conn = socket->pesp_conn;
    switch ( status )
    {
      case 200: http_header = http_header_200; break;
      case 204: http_header = http_header_204; break;
      case 301: http_header = http_header_301; break;
      case 302: http_header = http_header_302; break;
      case 304: http_header = http_header_304; break;
      case 400: http_header = http_header_400; break;
      case 401: http_header = http_header_401; break;
      case 403: http_header = http_header_403; break;
      case 404: http_header = http_header_404; break;
      case 405: http_header = http_header_405; break;
      case 410: http_header = http_header_410; break;
      case 500: http_header = http_header_500; break;
      case 501: http_header = http_header_501; break;
      case 503: http_header = http_header_503; break;
      default: http_header = http_header_500;  break;
    }
    espconn_regist_sentcb ( pesp_conn, httpd_socket_sent_close_callback );
    espconn_send ( pesp_conn, (unsigned char *) http_header, (uint16) c_strlen ( http_header ) );
  }
}

static int httpd_socket_response_redirect ( lua_State *L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_response_redirect\n" );

  struct espconn *pesp_conn = NULL;

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  size_t url_length;
  const char * url = luaL_checklstring ( L, 2, &url_length );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
    pesp_conn = socket->pesp_conn;
    espconn_regist_sentcb ( pesp_conn, httpd_socket_sent_close_callback );

    char * redirect = (char *) c_malloc ( 200 );
    if ( redirect != NULL)
    {
      int redirect_length = c_sprintf ( redirect, "HTTP/1.1 302 Found\r\nLocation: %s\r\n\r\n", url );
      espconn_regist_sentcb ( pesp_conn, httpd_socket_sent_close_callback );
      espconn_send ( pesp_conn, (unsigned char *) redirect, redirect_length );
      c_free ( redirect );
      return 0;
    }
    if ( pesp_conn->proto.tcp->remote_port || pesp_conn->proto.tcp->local_port )
    {
      espconn_disconnect ( pesp_conn );
    }
  }
  return 0;
}

static int httpd_mime_type ( const char * extension )
{
  int i = 0;
  
  while ( httpd_mime[i] != NULL )
  {
    if ( c_strcmp ( httpd_mime[i], extension ) == 0 )
    {
      return i + 1;
    }
    i += 2;
  }
  return 1;
}

static int httpd_socket_response ( lua_State *L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_response\n" );

  struct espconn *pesp_conn = NULL;
  char * response_buffer;
  int mime = 1;
  int content_type_length;
  int body_length;

  int response_length;

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
    pesp_conn = socket->pesp_conn;
    espconn_regist_sentcb ( pesp_conn, httpd_socket_sent_close_callback );

    const char * content_type = luaL_checklstring ( L, 2, &content_type_length );
    const char * body         = luaL_checklstring ( L, 3, &body_length );

    response_buffer = (char *) c_malloc ( body_length + 100 );
    if ( response_buffer != NULL)
    {
      mime = httpd_mime_type ( content_type );
      response_length = c_sprintf ( response_buffer, "HTTP/1.1 200 OK\r\nContent-length: %d\r\nContent-Type: %s\r\n\r\n%s", body_length, httpd_mime[ mime ], body );
      espconn_send ( pesp_conn, (unsigned char *) response_buffer, response_length );
      c_free ( response_buffer );
    }
    else
    {
      espconn_send ( pesp_conn, (unsigned char *) http_header_500, (uint16) c_strlen ( http_header_500 ) );
    }
  }
}


// =============================================================================

#define TIMENOW(title) c_printf("%s time: %d\n", (const char *)title, (uint32_t) 0x7FFFFFFF & system_get_time() )

static void httpd_send_file ( void *arg, char *filename, bool enable_etag )
{
  NODE_DBG_HTTPD ( "function: httpd_send_file\n" );

  int err;
  int err2 = VFS_RES_ERR;
  int f;
  int file_size;
  int dot_pos;
  int mime      = 1;
  char * response_file;
  int response_file_length;

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }
  
  f = vfs_open ( filename, "r" );
  if ( ! f )
  {
    espconn_send ( pesp_conn, (unsigned char *) http_header_404, (uint16) c_strlen ( http_header_404 ) );
    return;
  }
  err       = vfs_lseek ( f, 0, VFS_SEEK_END );
  file_size = (int) vfs_tell ( f );
  err2      = vfs_lseek ( f, 0, VFS_SEEK_SET );
  if ( err2 != VFS_RES_ERR && file_size <= 2800 )
  {
    response_file = (char *) c_malloc ( file_size + 120 );
    if ( response_file != NULL)
    {
      dot_pos = httpd_find_char_last_pos ( (const char *)filename, '.' );
    
      if ( dot_pos > 0 )
      {
        mime = httpd_mime_type ( filename + dot_pos + 1 );
      }
    
      if ( httpd_etag && enable_etag == true )
      {
        response_file_length = c_sprintf ( response_file, "HTTP/1.1 200 OK\r\n%sContent-length: %d\r\nContent-Type: %s\r\n\r\n", httpd_etag, file_size, httpd_mime[ mime ] );
      }
      else
      {
        response_file_length = c_sprintf ( response_file, "HTTP/1.1 200 OK\r\nContent-length: %d\r\nContent-Type: %s\r\n\r\n", file_size, httpd_mime[ mime ] );
      }
      vfs_read ( f, &( response_file[response_file_length] ), file_size );
      vfs_close ( f );
      espconn_send ( pesp_conn, (unsigned char *) response_file, response_file_length + file_size );
      c_free ( response_file );
      return;
    }
  }

  vfs_close ( f );
  espconn_send ( pesp_conn, (unsigned char *) http_header_500, (uint16) c_strlen ( http_header_500 ) );
  return;
}


static bool httpd_string_starts_with ( const char *s, const char *c )
{
  NODE_DBG_HTTPD ( "function: httpd_string_starts_with\n" );
  int i;
  int length = 0;
  if ( s && c ){
    int length = c_strlen ( c ); 
    for ( i = 0; i < length; i++ )
    {
      if ( s[i] != c[i] )
      {
        break;
      }
    }
    return ( i == length ) ? true : false;
  }
  return false;
}

static int httpd_path_type ( char * path, int length )
{
  NODE_DBG_HTTPD ( "function: httpd_path_type\n" );
  
  if ( length == 1 && path[0] == '/' )
  {
    return HTTPD_PATH_HOME;
  }
  
  if ( length == 12 && httpd_string_starts_with ( path, "/favicon.ico" ) == true )
  {
    return HTTPD_PATH_FAVICON;
  }

  if ( length > 8 && httpd_string_starts_with ( path, "/static/" ) == true )
  {
    return HTTPD_PATH_STATIC;
  }
  
  if ( length > 6 && httpd_string_starts_with ( path, "/data/" ) == true )
  {
    return HTTPD_PATH_DATA;
  }
  return HTTPD_PATH_DEFAULT;
}

static void httpd_post_second_packet ( void *arg, char *pdata, unsigned short len )
{
  NODE_DBG_HTTPD ( "function: http_process_post_packet\n" );

  const char * err_message;
  size_t err_len;
 
  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL)
  {
    return;
  }

  socket_userdata *socket = (socket_userdata *) pesp_conn->reverse;
  if ( socket != NULL )
  {
    if ( tcpserver_cb_receive_ref != LUA_NOREF && socket->self_ref != LUA_NOREF )
    {
      lua_State *L = lua_getstate ();
      lua_rawgeti    ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
      lua_rawgeti    ( L, LUA_REGISTRYINDEX, socket->self_ref );  // pass the userdata(socket) to callback func in lua
      lua_pushstring ( L, "POST" ); // method
      lua_pushstring ( L, socket->path ); // path
 
      switch ( socket->post_type )
      {
        case REQUEST_POST_FORM: lua_pushstring ( L, "form" ); break;
        case REQUEST_POST_JSON: lua_pushstring ( L, "json" ); break;
        case REQUEST_POST_TEXT: lua_pushstring ( L, "text" ); break;
      };
  
      httpd_request_query ( L, pdata, len ); // form data
  
      if ( lua_pcall ( L, 5, 0, 0) )
      {
        err_message = lua_tolstring ( L, -1, &err_len );
        if ( err_message )
        {
          char * response_error = (char *) c_malloc ( 50 + err_len );
          if ( response_error != NULL )
          {
            int response_error_length = c_sprintf ( response_error, "HTTP/1.1 500 Internal Server Error\r\n\r\n%s", err_message );
            espconn_send ( pesp_conn, (unsigned char *) response_error, response_error_length );
            c_free ( response_error );
          }
        }
      }
      return;
    }
    espconn_send ( pesp_conn, (unsigned char *) http_header_500, (uint16) c_strlen ( http_header_500 ) );
  }
  return;
}

static int httpd_post_type ( char * pdata, int header_end )
{
  int content_form;
  int content_json;
  int content_text;
  
  if ( pdata == NULL ){
    return REQUEST_UNKNOWN;
  }
  
  content_form = httpd_find_text_pos ( pdata, "Content-Type: application/x-www-form-urlencoded" );
  
  if ( content_form > 0 && content_form < header_end )
  {
    return REQUEST_POST_FORM;
  }

  content_json  = httpd_find_text_pos ( pdata, "Content-Type: application/json" );
  if ( content_json > 0 && content_json < header_end )
  {
    return REQUEST_POST_JSON;
  }

  content_text  = httpd_find_text_pos ( pdata, "Content-Type: text/" );
  if ( content_text> 0 && content_text < header_end )
  {
    return REQUEST_POST_TEXT;
  }
  return REQUEST_UNKNOWN;
}

static void httpd_socket_received_callback ( void *arg, char *pdata, unsigned short len )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_received_callback\n" );

  int first_line_end;
  int method_end;
  int uri_end;
  int query_pos;
  int header_end;
  int if_none_match_pos;
  char * path;
  int path_length;
  int path_type;
  int content_length_zero_pos;
  int content_length_pos;
  int return_values;
  int auth_pos;
  const char * err_message;
  size_t err_len;
  int state = REQUEST_UNKNOWN;

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL)
  {
    return;
  }
  espconn_regist_sentcb ( pesp_conn, httpd_socket_sent_close_callback );

  socket_userdata *socket = (socket_userdata *) pesp_conn->reverse;
  if ( socket == NULL )
  {
    espconn_send ( pesp_conn, (unsigned char *) http_header_500, (uint16) c_strlen ( http_header_500 ) );
    return;
  }

  if ( ( socket->post_type == REQUEST_POST_FORM || socket->post_type == REQUEST_POST_JSON || socket->post_type == REQUEST_POST_TEXT ) && socket->path )
  {
    httpd_post_second_packet ( arg, pdata, len );
    return;
  }

  method_end        = httpd_find_text_pos ( pdata, " " );
  uri_end           = httpd_find_text_pos ( pdata, " HTTP" );
  first_line_end    = httpd_find_text_pos ( pdata, "\r\n" );

  if ( ! ( method_end > 0 && uri_end > method_end && uri_end < first_line_end ) )
  {
    // NO HTTP REQUEST LINE
    espconn_send ( pesp_conn, (unsigned char *) http_header_400, (uint16) c_strlen ( http_header_400 ) );
    return;
  }

  if ( httpd_auth )
  {
    auth_pos = httpd_find_text_pos ( pdata, httpd_auth );
    if ( auth_pos == 0 )
    {
      // auth is required, but not found in header 
      espconn_send ( pesp_conn, (unsigned char *) http_header_401, (uint16) c_strlen ( http_header_401 ) );
      return;
    }
  }

  header_end  = httpd_find_text_pos ( pdata, "\r\n\r\n" );
  path        = pdata   + method_end + 1;
  path_length = uri_end - method_end - 1;

  // method = GET
  if ( method_end == 3 && pdata[0] == 'G' && pdata[1] == 'E' && pdata[2] == 'T' )
  {
    path_type = httpd_path_type ( path, path_length );
    
    if ( path_type == HTTPD_PATH_HOME || path_type == HTTPD_PATH_FAVICON || path_type == HTTPD_PATH_STATIC )
    {
      if_none_match_pos = httpd_find_text_pos ( pdata, httpd_if_none_match );
      if ( httpd_etag_not_modified && if_none_match_pos > 0 && if_none_match_pos < header_end ) 
      {
        espconn_send ( pesp_conn, (unsigned char *) httpd_etag_not_modified, (uint16) c_strlen ( httpd_etag_not_modified ) );
        return;
      }
    }

    switch ( path_type )
    {
      case HTTPD_PATH_HOME:
        httpd_send_file ( arg, "static/index.htm", true );
        return;
        
      case HTTPD_PATH_FAVICON:
        httpd_send_file ( arg, "static/favicon.ico", true );
        return;
        
      case HTTPD_PATH_STATIC:
        pdata[uri_end] = '\0';
        httpd_send_file ( arg, path + 1, true );
        return;
        
      case HTTPD_PATH_DATA:
        pdata[uri_end] = '\0';
        httpd_send_file ( arg, path + 1, false );
        return;
        
      default:
        query_pos = httpd_find_text_pos ( pdata, "?" );
        state     = ( query_pos > method_end && query_pos < uri_end ) ? REQUEST_GET_QUERY : REQUEST_GET;
        break;
    }
  }
  // method = POST
  else if ( method_end == 4 && pdata[0] == 'P' && pdata[1] == 'O' && pdata[2] == 'S' && pdata[3] == 'T' )
  {
    state = httpd_post_type ( pdata, header_end );

    content_length_zero_pos = httpd_find_text_pos ( pdata, "Content-Length: 0" );
    content_length_pos      = httpd_find_text_pos ( pdata, "Content-Length: " );

    // if socket not contains post data, then wait for next socket
    if ( ! content_length_zero_pos && content_length_pos && header_end == len - 4 ) {
      char * path_to_socket = (char *) c_zalloc ( path_length + 1 );
      if ( path_to_socket ){
        c_memcpy ( path_to_socket, path, path_length );
        socket->post_type = state;
        socket->path      = path_to_socket;
      }
      return;
    }
  }
  else
  {
    // METHOD NOT ALLOWED
    espconn_send ( pesp_conn, (unsigned char *) http_header_405, (uint16) c_strlen ( http_header_405 ) );
    return;
  }

  if ( state == REQUEST_UNKNOWN )
  {
    // METHOD NOT IMPLEMENTED
    espconn_send ( pesp_conn, (unsigned char *) http_header_501, (uint16) c_strlen ( http_header_501 ) );
    return;
  } 
  
  if ( tcpserver_cb_receive_ref != LUA_NOREF && socket->self_ref != LUA_NOREF )
  {
    lua_State *L = lua_getstate ();
    lua_rawgeti ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
    lua_rawgeti ( L, LUA_REGISTRYINDEX, socket->self_ref );  // pass the userdata(socket) to callback func in lua
  
    switch ( state )
    {
      case REQUEST_GET_QUERY:
        lua_pushstring      ( L, "GET" ); // method
        lua_pushlstring     ( L, path, query_pos - method_end - 1 ); // path
        lua_pushstring      ( L, "get" ); // data type
        httpd_request_query ( L, pdata + query_pos + 1, uri_end - query_pos - 1 ); // data
        break;
  
      case REQUEST_GET:
        lua_pushstring      ( L, "GET" ); // method
        lua_pushlstring     ( L, path, path_length ); // path
        lua_pushnil         ( L ); // data type
        lua_pushnil         ( L ); // data
        break;
  
      case REQUEST_POST_FORM:
        lua_pushstring      ( L, "POST" ); // method
        lua_pushlstring     ( L, path, path_length ); // path
        lua_pushstring      ( L, "form" ); // data type
        httpd_request_query ( L, pdata + header_end + 4, len - header_end - 4 ); // form data
        break;
  
      case REQUEST_POST_JSON:
        lua_pushstring      ( L, "POST" ); // method
        lua_pushlstring     ( L, path, path_length ); // path
        lua_pushstring      ( L, "json" ); // data type
        lua_pushlstring     ( L, pdata + header_end + 4, len - header_end - 4 ); // json data - must be decoded in lua
        break;
  
      case REQUEST_POST_TEXT:
        lua_pushstring      ( L, "POST" ); // method
        lua_pushlstring     ( L, path, path_length ); // path
        lua_pushstring      ( L, "text" ); // data type
        lua_pushlstring     ( L, pdata + header_end + 4, len - header_end - 4 ); // text data
        break;
  
      default:
        lua_pushstring      ( L, "?" ); // method
        lua_pushstring      ( L, "?" ); // path
        lua_pushnil         ( L ); // data type
        lua_pushnil         ( L ); // data
        break;
    };
  
    if ( lua_pcall ( L, 5, 0, 0) )
    {
      err_message = lua_tolstring ( L, -1, &err_len );
      if ( err_message )
      {
        char * response_error = (char *) c_malloc ( 50 + err_len );
        if ( response_error != NULL )
        {
          int response_error_length = c_sprintf ( response_error, "HTTP/1.1 500 Internal Server Error\r\n\r\n%s", err_message );
          espconn_send ( pesp_conn, (unsigned char *) response_error, response_error_length );
          c_free ( response_error );
        }
      }
    }
    return;
  }
  espconn_send ( pesp_conn, (unsigned char *) http_header_500, (uint16) c_strlen ( http_header_500 ) );
  return;
}

static void httpd_socket_sent_callback ( void *arg )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_sent_callback\n" );

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }
  socket_userdata *socket = (socket_userdata *) pesp_conn->reverse;

  if ( socket != NULL && socket->cb_send_ref != LUA_NOREF && socket->self_ref != LUA_NOREF )
  {
    lua_State *L = lua_getstate();
    lua_rawgeti ( L, LUA_REGISTRYINDEX, socket->cb_send_ref );
    lua_rawgeti ( L, LUA_REGISTRYINDEX, socket->self_ref );
    lua_call    ( L, 1, 0 );
  }
  return;
}

static void httpd_socket_disconnected_callback ( void *arg )    // for tcp server only
{
  NODE_DBG_HTTPD ( "function: httpd_socket_disconnected_callback\n" );

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }
  socket_userdata * socket = ( socket_userdata *) pesp_conn->reverse;
  if ( socket == NULL )
  {
    return;
  }

  lua_State *L = lua_getstate();

  int i;
  lua_gc ( L, LUA_GCSTOP, 0 );
  for ( i = 0; i < MAX_SOCKET; i++ )
  {
    if ( ( LUA_NOREF != socket_array[i] ) && ( socket_array[i] == socket->self_ref ) )
    {
      // found the saved client
      socket->pesp_conn->reverse = NULL;
      socket->pesp_conn          = NULL;    // the espconn is made by low level sdk, do not need to free, delete() will not free it.
      socket->self_ref           = LUA_NOREF;   // unref this, and the httpd.socket userdata will delete it self
      socket->post_type          = 0;
      if ( socket->path != NULL ){
        c_free ( socket->path );
        socket->path = NULL;
      }
      luaL_unref ( L, LUA_REGISTRYINDEX, socket_array[i] );
      socket_array[i] = LUA_NOREF;
      break;
    }
  }
  lua_gc ( L, LUA_GCRESTART, 0 );
}

static void httpd_socket_reconnected_callback ( void *arg, sint8_t err )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_reconnected_callback\n" );

  httpd_socket_disconnected_callback ( arg );
}

static void httpd_server_connected_callback ( void *arg ) // for tcp only
{
  NODE_DBG_HTTPD ( "function: httpd_server_connected_callback\n" );

  int i = 0;

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }

  for ( i = 0; i < MAX_SOCKET; i++ )
  {
    if ( socket_array[i] == LUA_NOREF ) // found empty slot
    {
      lua_State *L = lua_getstate();
    
      socket_userdata * socket = (socket_userdata *) lua_newuserdata ( L, sizeof ( socket_userdata ) );
      if ( ! socket )
      {
        NODE_ERR ( "Can't allocate httpd.socket useradata\n" );
        lua_pop ( L, 1 );
        return;
      }
      // set its metatable
      luaL_getmetatable ( L, "httpd.socket" );
      lua_setmetatable  ( L, -2 );
    
      socket->self_ref    = luaL_ref ( L, LUA_REGISTRYINDEX ); // ref to it self, for module api to find the userdata
      socket->cb_send_ref = LUA_NOREF;
      socket->pesp_conn   = pesp_conn; // point to the espconn made by low level sdk
      socket->id          = socket_id;
      socket_id           = socket_id + 1;
      socket->post_type   = REQUEST_UNKNOWN;
      socket->path        = NULL;
      pesp_conn->reverse  = socket; // let espcon carray the info of this userdata(httpd.socket)
      socket_array[i]     = socket->self_ref;
    
      espconn_regist_recvcb   ( pesp_conn, httpd_socket_received_callback     );
      espconn_regist_sentcb   ( pesp_conn, httpd_socket_sent_callback         );
      espconn_regist_disconcb ( pesp_conn, httpd_socket_disconnected_callback );
      espconn_regist_reconcb  ( pesp_conn, httpd_socket_reconnected_callback  );
      return;
    }
  }
  pesp_conn->reverse = NULL;
  if ( pesp_conn->proto.tcp->remote_port || pesp_conn->proto.tcp->local_port )
  {
    espconn_disconnect ( pesp_conn );
  }
  return;
}

// Lua: httpd.createServer ( port, basicAuth, function ( socket ) )
static int httpd_server_create( lua_State* L )
{
  NODE_DBG_HTTPD ( "function: httpd_server_create\n" );

  unsigned port = luaL_checkinteger ( L, 1 );

  size_t auth_length;
  const char * auth = luaL_checklstring ( L, 2, &auth_length );

  if ( auth_length > 0 )
  {
    char *base64_auth = (char *) base64Encode ( (char *)auth, auth_length );
    int base64_auth_length = c_strlen ( base64_auth );

    if ( base64_auth && base64_auth_length > 6 )
    {
      httpd_auth = (char *) c_zalloc( base64_auth_length + 25 );
      if ( httpd_auth )
      {
        c_sprintf ( httpd_auth, "Authorization: Basic %s\r\n", base64_auth );
      }
      c_free ( base64_auth );
    }
  }

  pTcpServer = (struct espconn *) c_zalloc ( sizeof ( struct espconn ) );

  if ( lua_type ( L, 3 ) == LUA_TFUNCTION || lua_type ( L, 3 ) == LUA_TLIGHTFUNCTION )
  {
    lua_pushvalue ( L, 3 );  // copy argument (func) to the top of stack
    if ( tcpserver_cb_receive_ref != LUA_NOREF )
    {
      luaL_unref ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
    }
    tcpserver_cb_receive_ref = luaL_ref ( L, LUA_REGISTRYINDEX );
  }

  if ( ! pTcpServer )
  {
    return luaL_error ( L, "!!! NOT ENOUGHT MEMORY - struct espconn !!!" );
  }

  pTcpServer->type       = ESPCONN_TCP;
  pTcpServer->state      = ESPCONN_NONE;
  pTcpServer->proto.udp  = NULL;
  pTcpServer->proto.tcp  = (esp_tcp *) c_zalloc ( sizeof ( esp_tcp ) );

  if ( ! pTcpServer->proto.tcp )
  {
    c_free ( pTcpServer );
    pTcpServer = NULL;
    return luaL_error ( L, "!!! NOT ENOUGHT MEMORY - proto.tcp !!!" );
  }

  pTcpServer->proto.tcp->local_port = port;

  espconn_regist_connectcb      ( pTcpServer, httpd_server_connected_callback );
  espconn_accept                ( pTcpServer );
  espconn_regist_time           ( pTcpServer, tcp_server_timeover, 0 );
  espconn_tcp_set_max_con_allow ( pTcpServer, (uint8) MAX_SOCKET );

  return 0;
}

// Lua: server:close()
static int httpd_server_close( lua_State* L )
{
  NODE_DBG_HTTPD ( "function: httpd_server_close\n" );

  if ( tcpserver_cb_receive_ref != LUA_NOREF )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
    tcpserver_cb_receive_ref = LUA_NOREF;
  }
  
  if ( pTcpServer )
  {
    if ( pTcpServer->proto.tcp != NULL )
    {
      c_free ( pTcpServer->proto.tcp );
    }
    c_free ( pTcpServer );
  }
  
  pTcpServer = NULL;
  return 0;
}

// =============================================================================

// Lua: socket:close()
static int httpd_socket_close( lua_State* L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_close\n" );

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL && socket->pesp_conn != NULL && ( socket->pesp_conn->proto.tcp->remote_port || socket->pesp_conn->proto.tcp->local_port ) )
  {
    espconn_disconnect ( socket->pesp_conn );
  }
  return 0;
}

// Lua: socket:delete()
static int httpd_socket_delete( lua_State* L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_delete\n" );

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL && socket->pesp_conn != NULL )
  { 
  	socket->pesp_conn->reverse = NULL;
    if ( socket->pesp_conn->proto.tcp )
    {
      c_free ( socket->pesp_conn->proto.tcp );
    }
    socket->pesp_conn->proto.tcp = NULL;
    c_free ( socket->pesp_conn );
    socket->pesp_conn = NULL;    // for socket, it will free this when disconnected
    
    socket->post_type          = 0;
    if ( socket->path != NULL ){
      c_free ( socket->path );
      socket->path = NULL;
    }
  }

  lua_gc ( L, LUA_GCSTOP, 0 );
  // free (unref) callback ref
  if ( LUA_NOREF != socket->cb_send_ref )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, socket->cb_send_ref );
    socket->cb_send_ref = LUA_NOREF;
  }
  if ( LUA_NOREF != socket->self_ref )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, socket->self_ref );
    socket->self_ref = LUA_NOREF;
  }
  lua_gc ( L, LUA_GCRESTART, 0 );
  return 0;
}

// Lua: socket:onSent( function(socket) )
static int httpd_socket_on_sent ( lua_State* L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_on_sent\n" );

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck(L, socket, 1, "Socket expected");
  if ( socket == NULL )
  {
  	NODE_DBG_HTTPD ( "userdata is nil.\n" );
  	return 0;
  }

  luaL_checkanyfunction ( L, 2 );
  lua_pushvalue ( L, 2 );  // copy argument (func) to the top of stack

  if ( socket->cb_send_ref != LUA_NOREF )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, socket->cb_send_ref );
  }
  socket->cb_send_ref = luaL_ref ( L, LUA_REGISTRYINDEX );

  return 0;
}

// Lua: socket:send( string, function() )
static int httpd_socket_send( lua_State* L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_send\n" );
  struct espconn *pesp_conn = NULL;
  size_t length;

  socket_userdata * socket = ( socket_userdata * ) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
    pesp_conn = socket->pesp_conn;

    const char *payload = luaL_checklstring( L, 2, &length );
    if ( payload == NULL )
    {
      return luaL_error( L, "Socket:send payload is nil." );
    }

    if ( lua_type ( L, 3 ) == LUA_TFUNCTION || lua_type ( L, 3 ) == LUA_TLIGHTFUNCTION )
    {
      lua_pushvalue ( L, 3 );  // copy argument (func) to the top of stack
      if ( socket->cb_send_ref != LUA_NOREF )
      {
        luaL_unref(L, LUA_REGISTRYINDEX, socket->cb_send_ref);
      }
      socket->cb_send_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    espconn_send ( pesp_conn, (unsigned char *) payload, length );
  }
  return 0;
}

// Lua: ip,port = socket:getpeer()
static int httpd_socket_getpeer( lua_State* L )
{
  NODE_DBG_HTTPD ( "function: httpd_socket_getpeer\n" );

  socket_userdata * socket = ( socket_userdata * ) luaL_checkudata ( L, 1, "httpd.socket" );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
      char temp[20] = {0};
      c_sprintf ( temp, IPSTR, IP2STR( &(socket->pesp_conn->proto.tcp->remote_ip) ) );
      if ( socket->pesp_conn->proto.tcp->remote_port != 0 )
      {
        lua_pushstring  ( L, temp );
        lua_pushinteger ( L, socket->pesp_conn->proto.tcp->remote_port );
        return 2;
      }
  }
  lua_pushnil( L );
  lua_pushnil( L );
  return 2;
}

static void httpd_build_etag ()
{
  int i;
  char * etag = (char *) c_zalloc ( 9 );

  if ( etag != NULL )
  {
    srand( (uint32_t) 0x7FFFFFFF & system_get_time() );
    for ( i = 0; i < 8; i += 2 )
    {
      etag[i]   = (unsigned char)( 97 + ( ( rand() % RAND_MAX ) % 24 ) );
      etag[i+1] = (unsigned char)( 48 + ( ( rand() % RAND_MAX ) % 10 ) );
    }

    httpd_etag = (char *) c_zalloc ( 19 );
    if ( httpd_etag )
    {
      c_sprintf ( httpd_etag, "ETag: \"%s\"\r\n", etag );
      NODE_DBG_HTTPD ( httpd_etag );
    }

    httpd_etag_not_modified = (char *) c_zalloc ( 66 );
    if ( httpd_etag_not_modified )
    {
      c_sprintf ( httpd_etag_not_modified, "HTTP/1.1 304 Not Modified\r\nETag: \"%s\"\r\nContent-Length: 0\r\n", etag );
      NODE_DBG_HTTPD ( httpd_etag_not_modified );
    }

    httpd_if_none_match = (char *) c_zalloc ( 28 );
    if ( httpd_if_none_match )
    {
      c_sprintf ( httpd_if_none_match, "If-None-Match: \"%s\"\r\n", etag );
      NODE_DBG_HTTPD ( httpd_if_none_match );
    }

    c_free ( etag );
  }
  return;
}

static int httpd_reset_etag ( lua_State *L )
{
  if ( httpd_etag )
  {
    c_free ( httpd_etag );
  }
  if ( httpd_etag_not_modified )
  {
    c_free ( httpd_etag_not_modified );
  }
  if ( httpd_if_none_match )
  {
    c_free ( httpd_if_none_match );
  }
  
  httpd_build_etag ();
  
  return 0;
}

// ============================================================================= 

// Module function map
static const LUA_REG_TYPE httpd_socket_map[] = {
  { LSTRKEY( "close" ),        LFUNCVAL( httpd_socket_close ) },
  { LSTRKEY( "onSent" ),       LFUNCVAL( httpd_socket_on_sent ) },
  { LSTRKEY( "send" ),         LFUNCVAL( httpd_socket_send ) },
  { LSTRKEY( "getpeer" ),      LFUNCVAL( httpd_socket_getpeer ) },
  { LSTRKEY( "sendStatus" ),   LFUNCVAL( httpd_socket_response_status ) },
  { LSTRKEY( "sendResponse" ), LFUNCVAL( httpd_socket_response ) },
  { LSTRKEY( "sendRedirect" ), LFUNCVAL( httpd_socket_response_redirect ) },
  { LSTRKEY( "__gc" ),         LFUNCVAL( httpd_socket_delete ) },
  { LSTRKEY( "__index" ),      LROVAL( httpd_socket_map ) },
  { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE httpd_map[] = {
  { LSTRKEY( "createServer" ),    LFUNCVAL( httpd_server_create ) },
  { LSTRKEY( "closeServer" ),     LFUNCVAL( httpd_server_close ) },
  { LSTRKEY( "resetEtag" ),       LFUNCVAL( httpd_reset_etag ) },
  { LSTRKEY( "__metatable" ),     LROVAL( httpd_map ) },
  { LNILKEY, LNILVAL }
};

int luaopen_httpd( lua_State *L )
{
  int i;
  for ( i = 0; i < MAX_SOCKET; i++ )
  {
    socket_array[i] = LUA_NOREF;
  }

  luaL_rometatable ( L, "httpd.socket", (void *)httpd_socket_map );  // create metatable for httpd.socket

  httpd_build_etag();

  return 0;
}

NODEMCU_MODULE(HTTPD, "httpd", httpd_map, luaopen_httpd);
