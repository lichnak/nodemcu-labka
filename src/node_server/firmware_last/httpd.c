// Module for httpd
// author: peterbay (Petr Vavrin) pb.pb(at)centrum.cz
// based on nodemcu modules a 

// #define NODE_DBG_HTTPD c_printf
#define NODE_DBG_HTTPD

// #define TIMENOW(title) c_printf("%s time: %d\n", (const char *)title, (uint32_t) 0x7FFFFFFF & system_get_time() )
#define TIMENOW

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
  int self_ref;
  int cb_receive_ref;
  int cb_send_ref;
  char * uri;
  char * content_data;
  int content_len;
  int content_len_left;
  int content_type;
}socket_userdata;

#define HTTPD_SOCKET "httpd.socket"

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
#define HTTPD_PATH_ADMIN 5

#define MAX_SOCKET 6
static struct espconn *httpd_server = NULL;
static int tcpserver_cb_receive_ref = LUA_NOREF;
static uint16_t tcp_server_timeover = 30;

static char * httpd_etag;
static char * httpd_if_none_match;
static char * httpd_etag_not_modified;
static char * httpd_auth_admin;
static char * httpd_auth_user;

static const char http_header_200[] = "HTTP/1.1 200 OK\r\n\r\n";
static const char http_header_204[] = "HTTP/1.1 204 No Content\r\n\r\n";
static const char http_header_301[] = "HTTP/1.1 301 Moved Permanently\r\n\r\n";
static const char http_header_302[] = "HTTP/1.1 302 Found\r\n\r\n";
static const char http_header_304[] = "HTTP/1.1 304 Not Modified\r\n\r\n";
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
// === BASIC AUTH ==============================================================
static const char *bytes64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char * httpd_base64_encode ( char * data, unsigned int len )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_base64_encode\n" );
  
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
    out[j++] = bytes64[( ( a & 3 ) << 4 ) | ( b >> 4 )];
    out[j++] = lenm1 ? bytes64[( ( b & 15 ) << 2 ) | ( c >> 6 )] : 61;
    out[j++] = lenm2 ? bytes64[( c & 63 )] : 61;
  }
  return out;
}

static char * httpd_basic_auth ( const char * auth, int auth_length )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_basic_auth\n" );
  char * p_basic_auth = NULL;
  if ( auth_length > 0 )
  {
    char *base64_auth = (char *) httpd_base64_encode ( (char *)auth, auth_length );
    int base64_auth_length = c_strlen ( base64_auth );

    if ( base64_auth && base64_auth_length > 6 )
    {
      p_basic_auth = (char *) c_zalloc( base64_auth_length + 25 );
      if ( p_basic_auth )
      {
        c_sprintf ( p_basic_auth, "Authorization: Basic %s\r\n", base64_auth );
      }
      c_free ( base64_auth );
    }
  }
  return p_basic_auth;
}
// === ETAG ====================================================================
static void httpd_build_etag ()
{
  NODE_DBG_HTTPD ( "FUNC: httpd_build_etag\n" );

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
    etag = NULL;
  }
  return;
}

static int httpd_reset_etag ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_reset_etag\n" );

  if ( httpd_etag )
  {
    c_free ( httpd_etag );
    httpd_etag = NULL;
  }
  if ( httpd_etag_not_modified )
  {
    c_free ( httpd_etag_not_modified );
    httpd_etag_not_modified = NULL;
  }
  if ( httpd_if_none_match )
  {
    c_free ( httpd_if_none_match );
    httpd_if_none_match = NULL;
  }
  httpd_build_etag ();
  return 0;
}
// === uri decode ==============================================================
void httpd_uri_decode ( lua_State *L, const char *s, int length )
{
  int i;
  int length2 = ( length >= 2 ) ? length - 2 : 0; 
  char chr;
  char *sDest = (char * ) s;
  int posDest = 0;
  if ( s && length > 0 )
  {
    for ( i = 0; i < length; i++ )
    {
      chr = s[i];
      if ( chr == '%' && i < length2 && isxdigit( s[ i + 1 ] ) && isxdigit( s[ i + 2 ] ) )
      {
        chr = (unsigned char)( (hexchar_to_dec( s[ i + 1 ] ) << 4) + hexchar_to_dec( s[ i + 2 ] ) );
        i += 2;
      }
      else if ( chr == '+' )
      {
        chr = ' ';
      }
      if ( chr > 0 )
      {
        sDest[posDest] = chr;
        posDest++;
      }
    }
  }
  lua_pushlstring ( L, sDest, posDest );
}

void httpd_request_query_pair ( lua_State *L, const char * s, int var_pos, int sep_pos, int end_pos )
{
  if ( var_pos + 1 < sep_pos && sep_pos < end_pos )
  {
    httpd_uri_decode ( L, s + var_pos, sep_pos - var_pos );
    httpd_uri_decode ( L, s + sep_pos + 1, end_pos - sep_pos - 1 );
    lua_settable     ( L, -3 );
  }
  else if ( var_pos + 2 <  end_pos )
  {
    httpd_uri_decode ( L, s + var_pos, end_pos - var_pos );
    lua_pushlstring  ( L, "", 0 );
    lua_settable     ( L, -3 );
  }
}

void httpd_request_query ( lua_State *L, const char *s, int length )
{
  int i;
  int var_pos = 0;
  int sep_pos = 0;
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
      }
    }
    httpd_request_query_pair ( L, s, var_pos, sep_pos, i );
  }
}
// =============================================================================

static void httpd_send_error ( void * arg, const char * err_message, int err_len )
{
  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL || err_message == NULL )
  {
    return;
  }
  
  if ( err_len == 0 )
  {
    err_len = c_strlen ( err_message );
  } 
  
  char * response_error = (char *) c_malloc ( 50 + err_len );
  if ( response_error != NULL )
  {
    int response_error_length = c_sprintf ( response_error, "HTTP/1.1 500 Internal Server Error\r\n\r\n%s", err_message );
    espconn_send ( pesp_conn, (unsigned char *) response_error, response_error_length );
    c_free ( response_error );
  }
  return;
}

static void httpd_socket_sent_close_callback ( void *arg )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_sent_close_callback\n" );

  espconn_disconnect ( arg );
  return;
}

static int httpd_socket_response_status ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_response_status\n" );

  const char * http_header;

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, HTTPD_SOCKET );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  unsigned status = luaL_checkinteger ( L, 2 );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
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
    espconn_regist_sentcb ( socket->pesp_conn, httpd_socket_sent_close_callback );
    espconn_send ( socket->pesp_conn, (unsigned char *) http_header, (uint16) c_strlen ( http_header ) );
  }
  return 0;
}

static int httpd_socket_response_redirect ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_response_redirect\n" );

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, HTTPD_SOCKET );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  int url_length;
  const char * url = luaL_checklstring ( L, 2, &url_length );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
    espconn_regist_sentcb ( socket->pesp_conn, httpd_socket_sent_close_callback );

    char * redirect = (char *) c_malloc ( url_length + 40 );
    if ( redirect != NULL )
    {
      int redirect_length = c_sprintf ( redirect, "HTTP/1.1 302 Found\r\nLocation: %s\r\n\r\n", url );
      espconn_send ( socket->pesp_conn, (unsigned char *) redirect, redirect_length );
      c_free ( redirect );
      return 0;
    }
    httpd_send_error ( socket->pesp_conn, "socket redirect - not enought memory", 0 );
  }
  return 0;
}

static int httpd_socket_response ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_response\n" );

  char * response_buffer;
  int mime = 1;
  int content_type_length;
  int body_length;
  int response_length;

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, HTTPD_SOCKET );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
    espconn_regist_sentcb ( socket->pesp_conn, httpd_socket_sent_close_callback );

    const char * content_type = luaL_checklstring ( L, 2, &content_type_length );
    const char * body         = luaL_checklstring ( L, 3, &body_length );

    response_buffer = (char *) c_malloc ( body_length + 100 );
    if ( response_buffer != NULL )
    {
      mime = httpd_mime_type ( content_type );
      response_length = c_sprintf ( response_buffer, "HTTP/1.1 200 OK\r\nContent-length: %d\r\nContent-Type: %s\r\n\r\n%s", body_length, httpd_mime[ mime ], body );
      espconn_send ( socket->pesp_conn, (unsigned char *) response_buffer, response_length );
      c_free ( response_buffer );
      return 0;
    }
//     espconn_send ( socket->pesp_conn, (unsigned char *) http_header_500_mem, (uint16) c_strlen ( http_header_500_mem ) );
    httpd_send_error ( socket->pesp_conn, "socket response - not enought memory", 0 );
  }
  return 0;
}
// =============================================================================
static void httpd_send_file ( void *arg, char *filename, bool enable_etag )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_send_file\n" );

  int err;
  int err2;
  int f;
  int file_size;
  int mime      = 1;
  char * response_file;
  int response_header_length;
  const char * etag;

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }

  if ( ! filename )
  {
    httpd_send_error ( pesp_conn, "file - missing filename", 0 );
    return;
  }

  char * p_dot = strrchr ( filename, '.' );
  if ( p_dot != NULL && p_dot != filename )
  {
    mime = httpd_mime_type ( p_dot + 1 );
  }
  etag = ( httpd_etag && enable_etag == true ) ?  httpd_etag : "";
  
  int frequency = ets_get_cpu_frequency();  
  REG_SET_BIT(0x3ff00014, BIT(0));
  ets_update_cpu_frequency(160);
  
  f = vfs_open ( filename, "r" );
  if ( ! f )
  {
    espconn_send ( pesp_conn, (unsigned char *) http_header_404, (uint16) c_strlen ( http_header_404 ) );
    return;
  }

  err       = vfs_lseek ( f, 0, VFS_SEEK_END );
  file_size = (int) vfs_tell ( f );
  err2      = vfs_lseek ( f, 0, VFS_SEEK_SET );

  if ( err2 != VFS_RES_ERR )
  {
    if ( file_size <= 2800 )
    {
      response_file = (char *) c_malloc ( file_size + 120 );
  
      if ( response_file != NULL)
      {
        response_header_length = c_sprintf ( response_file, "HTTP/1.1 200 OK\r\n%sContent-length: %d\r\nContent-Type: %s\r\n\r\n", etag, file_size, httpd_mime[ mime ] );
        vfs_read  ( f, &( response_file[response_header_length] ), file_size );
        vfs_close ( f );
  
        espconn_send ( pesp_conn, (unsigned char *) response_file, response_header_length + file_size );
        c_free ( response_file );
  
        REG_CLR_BIT(0x3ff00014,  BIT(0));
        ets_update_cpu_frequency(frequency);
  
        return;
      }
    }
    else
    {
      httpd_send_error ( pesp_conn, "file - file too long", 0 );
      return;
    }
  }
  vfs_close ( f );
  REG_CLR_BIT(0x3ff00014,  BIT(0));
  ets_update_cpu_frequency(frequency);

  httpd_send_error ( pesp_conn, "file - seek error", 0 );
  return;
}

static void httpd_socket_received_callback ( void *arg, char *pdata, unsigned short len )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_received_callback\n" );

//   if ( pdata != NULL )
//   {
//     c_printf ( "\n=>%s<=\n", pdata );
//   }

  int first_line_end;
  int method_end;
  int uri_end;
  int query_pos;
  int header_end;
  int if_none_match_pos;
  char * path;
  int path_len;
  int path_type;
  char * qs;
  const char * err_message;
  int err_len;
  bool post_finished    = false;
  bool auth_admin_set   = false;
  bool auth_admin_state = false;
  bool auth_user_set    = false;
  bool auth_user_state  = false;

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL)
  {
    return;
  }
  espconn_regist_sentcb ( pesp_conn, httpd_socket_sent_close_callback );

  socket_userdata *socket = (socket_userdata *) pesp_conn->reverse;
  if ( socket == NULL )
  {
    httpd_send_error ( pesp_conn, "receive - no socket", 0 );
    return;
  }
  if ( socket->uri && ( socket->content_type == REQUEST_POST_FORM || socket->content_type == REQUEST_POST_JSON || socket->content_type == REQUEST_POST_TEXT ) )
  {
    if ( socket->content_len_left > 0 )
    {
      if ( socket->content_len_left == len )
      {
        int offset = socket->content_len - socket->content_len_left;
        
        if ( socket->content_data == NULL )
        {
          socket->content_data = c_malloc ( socket->content_len + 1 );
        }
        if ( socket->content_data != NULL )
        {
          c_memcpy ( socket->content_data + offset, pdata, len );
        }
        socket->content_len_left = 0; 
        post_finished = true;
      }
    }
    if ( post_finished == false )
    {
      espconn_send ( pesp_conn, (unsigned char *) http_header_400, (uint16) c_strlen ( http_header_400 ) );
      return;
    }
  }
  else
  {
    method_end     = httpd_find_text_pos ( pdata, " " );
    uri_end        = httpd_find_text_pos ( pdata, " HTTP" );
    first_line_end = httpd_find_text_pos ( pdata, "\r\n" );
  
    if ( ! ( method_end > 0 && uri_end > method_end && uri_end < first_line_end ) ) // NO HTTP REQUEST LINE
    {
      espconn_send ( pesp_conn, (unsigned char *) http_header_400, (uint16) c_strlen ( http_header_400 ) );
      return;
    }
    if ( httpd_auth_admin )
    {
      auth_admin_set = true;
      if ( strstr ( pdata, httpd_auth_admin ) != NULL )
      {
        auth_admin_state = true;
      }
    }
    if ( httpd_auth_user )
    {
      auth_user_set = true;
      if ( strstr ( pdata, httpd_auth_user ) != NULL ) 
      {
        auth_user_state = true;
      }
    }
//     if ( httpd_auth && strstr ( pdata, httpd_auth ) == NULL ) // auth is required, but not found in header 
//     {
//       espconn_send ( pesp_conn, (unsigned char *) http_header_401, (uint16) c_strlen ( http_header_401 ) );
//       return;
//     }
    header_end  = httpd_find_text_pos ( pdata, "\r\n\r\n" );
    path        = pdata   + method_end + 1;
    path_len    = uri_end - method_end - 1;
  
    if ( ! strncmp ( pdata, "GET ", 4 ) ) // received GET request
    {
      path_type = HTTPD_PATH_DEFAULT;
      
      if ( path_len == 1 && path[0] == '/' )
      {
        path_type = HTTPD_PATH_HOME;
      }
      else if ( path_len == 12 && ! strncmp ( path, "/favicon.ico", 12 ) ) 
      {
        path_type = HTTPD_PATH_FAVICON;
      }
      else if ( path_len > 7 && ! strncmp ( path, "/admin/", 7 ) ) 
      {
        path_type = HTTPD_PATH_ADMIN;
      }
      else if ( path_len > 8 && ! strncmp ( path, "/static/", 8 ) ) 
      {
        path_type = HTTPD_PATH_STATIC;
      }
      else if ( path_len > 6 && ! strncmp ( path, "/data/", 8 ) ) 
      {
        path_type = HTTPD_PATH_DATA;
      }
      
      if ( path_type == HTTPD_PATH_HOME || path_type == HTTPD_PATH_FAVICON || path_type == HTTPD_PATH_STATIC )
      {
        if_none_match_pos = httpd_find_text_pos ( pdata, httpd_if_none_match );
        if ( httpd_etag_not_modified && if_none_match_pos > 0 && if_none_match_pos < header_end ) 
        {
          espconn_send ( pesp_conn, (unsigned char *) httpd_etag_not_modified, (uint16) c_strlen ( httpd_etag_not_modified ) );
          return;
        }
      }
      
      if ( path_type == HTTPD_PATH_ADMIN )
      {
        if ( auth_admin_set == false || ( auth_admin_set == true && auth_admin_state == true ) )
        {
          pdata[uri_end] = '\0';
          httpd_send_file ( arg, path + 1, false );
          return;
        }
        espconn_send ( pesp_conn, (unsigned char *) http_header_401, (uint16) c_strlen ( http_header_401 ) );
        return;
      }
      
      if ( auth_user_set == true && auth_user_state == false )
      {
        espconn_send ( pesp_conn, (unsigned char *) http_header_401, (uint16) c_strlen ( http_header_401 ) );
        return;
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
          qs = strstr ( path, "?" );
          if ( qs != NULL )
          {
            qs[0] = '\0';
          }
          httpd_send_file ( arg, path + 1, true );
          return;
          
        case HTTPD_PATH_DATA:
          pdata[uri_end] = '\0';
          httpd_send_file ( arg, path + 1, false );
          return;
      }
      if ( tcpserver_cb_receive_ref == LUA_NOREF || socket->self_ref == LUA_NOREF )
      {
        httpd_send_error ( pesp_conn, "receive - no ref", 0 );
        return;
      }
      lua_State *L = lua_getstate ();
      lua_rawgeti ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
      lua_rawgeti ( L, LUA_REGISTRYINDEX, socket->self_ref );  // pass the userdata(socket) to callback func in lua
      
      if ( auth_admin_set == true && auth_admin_state == true )
      {
        lua_pushinteger ( L, 2 );
      }
      else if ( auth_user_set == true && auth_user_state == true )
      {
        lua_pushinteger ( L, 1 );
      }
      else
      {
        lua_pushinteger ( L, 0 );
      }
      lua_pushlstring ( L, "GET", 3 ); // method
  
      query_pos = httpd_find_text_pos ( pdata, "?" );
           
      if ( query_pos > method_end && query_pos < uri_end )
      {
        lua_pushlstring     ( L, path, query_pos - method_end - 1 ); // path
        lua_pushlstring     ( L, "get", 3 ); // data type
        httpd_request_query ( L, pdata + query_pos + 1, uri_end - query_pos - 1 ); // data
      }
      else
      {
        lua_pushlstring     ( L, path, path_len ); // path
        lua_pushnil         ( L ); // data type
        lua_pushnil         ( L ); // data
      };
      if ( lua_pcall ( L, 6, 0, 0 ) )
      {
        err_message = lua_tolstring ( L, -1, &err_len );
        httpd_send_error ( pesp_conn, err_message, err_len );
      }
      return;
    }
    else if ( ! strncmp ( pdata, "POST ", 5 ) && header_end > 0 ) // received POST request
    {
      socket->uri = c_zalloc ( path_len + 1 );
      if ( socket->uri == NULL )
      {
        espconn_send ( pesp_conn, (unsigned char *) http_header_501, (uint16) c_strlen ( http_header_501 ) );
        return;
      }
      c_memcpy ( socket->uri, path, path_len );
          
      char * p_header_end   = pdata + header_end;
      socket->content_type  = REQUEST_UNKNOWN;
      char * p_content_type = strstr ( pdata, "Content-Type: " ); 
      if ( p_content_type != NULL && p_content_type < p_header_end )
      {
        p_content_type += 14;
        int header_left = p_header_end - p_content_type;
        
        if ( header_left > 34 && ! strncmp ( p_content_type, "application/x-www-form-urlencoded\r\n", 35 ) )
        {
          socket->content_type = REQUEST_POST_FORM;
        }
        else if ( header_left > 17 && ! strncmp ( p_content_type, "application/json\r\n", 18 ) )
        {
          socket->content_type = REQUEST_POST_JSON;
        }
        else if ( header_left > 4 && ! strncmp ( p_content_type, "text/", 5 ) )
        {
          socket->content_type = REQUEST_POST_TEXT;
        }
      }
      if ( socket->content_type == REQUEST_UNKNOWN )
      {
        espconn_send ( pesp_conn, (unsigned char *) http_header_501, (uint16) c_strlen ( http_header_501 ) );
        return;
      } 
      char * p_content_length = strstr ( pdata, "Content-Length: " ); 
      if ( p_content_length != NULL && p_content_length < p_header_end )
      {
        p_content_length += 16;
        char * p_content_length_end = strstr ( p_content_length, "\r\n" );
        
        if ( p_content_length_end != NULL )
        {
          *p_content_length_end = 0;
          int content_len = atoi ( p_content_length );
          
          if ( content_len == 0 )
          {
            post_finished = true;
            socket->content_len_left = 0;
          }
          else if ( content_len > 0 && content_len < 1024 )
          {
            socket->content_len = content_len;
            char * content_data = p_header_end + 4;
            char * pdata_end = pdata + len; 
            int content_data_len = pdata_end - content_data;
                
            socket->content_data = c_malloc ( content_len + 1 );
            if ( socket->content_data != NULL )
            {
              c_memcpy ( socket->content_data, content_data, content_data_len );
              socket->content_len_left = content_len - content_data_len;
              if ( socket->content_len_left > 0 )
              {
                return;
              }
              post_finished = true;
            }
          }
          else
          {
            espconn_send ( pesp_conn, (unsigned char *) http_header_400, (uint16) c_strlen ( http_header_400 ) );
            return;    
          }
        }
      }
    }
  }
  
  if ( post_finished == true && socket->uri != NULL )
  {
    if ( tcpserver_cb_receive_ref == LUA_NOREF || socket->self_ref == LUA_NOREF  )
    {
      httpd_send_error ( pesp_conn, "receive - no ref", 0 );
      return;
    }
    if ( socket->content_len_left != 0 )
    {
      httpd_send_error ( pesp_conn, "receive - missing content", 0 );
      return;
    }
    lua_State *L = lua_getstate ();
    lua_rawgeti ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
    lua_rawgeti ( L, LUA_REGISTRYINDEX, socket->self_ref );  // pass the userdata(socket) to callback func in lua

    if ( auth_admin_set == true && auth_admin_state == true )
    {
      lua_pushinteger ( L, 2 );
    }
    else if ( auth_user_set == true && auth_user_state == true )
    {
      lua_pushinteger ( L, 1 );
    }
    else
    {
      lua_pushinteger ( L, 0 );
    }

    lua_pushlstring ( L, "POST", 4 ); // method
    lua_pushlstring ( L, socket->uri, c_strlen ( socket->uri ) ); // path
    
    switch ( socket->content_type ) // data type
    {
      case REQUEST_POST_FORM: lua_pushlstring ( L, "form", 4 ); break;
      case REQUEST_POST_JSON: lua_pushlstring ( L, "json", 4 ); break;
      case REQUEST_POST_TEXT: lua_pushlstring ( L, "text", 4 ); break;
    };
    if ( socket->content_data != NULL )
    {
      switch ( socket->content_type )
      {
        case REQUEST_POST_FORM: 
          httpd_request_query ( L, socket->content_data, socket->content_len ); 
          break;
        default: 
          lua_pushlstring ( L, socket->content_data, socket->content_len ); 
          break;
      }
    }
    else
    {
      lua_pushlstring ( L, "", 0 );
    }
    if ( lua_pcall ( L, 6, 0, 0 ) )
    {
      err_message = lua_tolstring ( L, -1, &err_len );
      httpd_send_error ( pesp_conn, err_message, err_len );
    }
    return;
  }
  espconn_send ( pesp_conn, (unsigned char *) http_header_501, (uint16) c_strlen ( http_header_501 ) );
  return;
}

static void httpd_socket_sent_callback ( void *arg )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_sent_callback\n" );

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

static void httpd_socket_disconnected_callback ( void *arg )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_disconnected_callback\n" );

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }
  socket_userdata * socket = ( socket_userdata *) pesp_conn->reverse;
  if ( socket == NULL || socket->self_ref == LUA_NOREF )
  {
    return;
  }
  lua_State *L = lua_getstate();

  int i;
  lua_gc ( L, LUA_GCSTOP, 0 );
  if ( socket->self_ref != LUA_NOREF )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, socket->self_ref );
    socket->self_ref           = LUA_NOREF;
  }
  socket->pesp_conn->reverse = NULL;
  socket->pesp_conn          = NULL;
  socket->content_type       = 0;
  if ( socket->uri != NULL )
  {
    c_free ( socket->uri );
    socket->uri = NULL;
  }
  if ( socket->content_data != NULL )
  {
    c_free ( socket->content_data );
    socket->content_data = NULL;
  }
  lua_gc ( L, LUA_GCRESTART, 0 );
}

static void httpd_socket_reconnected_callback ( void *arg, sint8_t err )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_reconnected_callback\n" );

  httpd_socket_disconnected_callback ( arg );
}

static void httpd_server_connected_callback ( void *arg )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_server_connected_callback\n" );

  struct espconn *pesp_conn = arg;
  if ( pesp_conn == NULL )
  {
    return;
  }
  uint32 heap = system_get_free_heap_size();

  if ( heap > 10000 )
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
    luaL_getmetatable ( L, HTTPD_SOCKET );
    lua_setmetatable  ( L, -2 );
    
    socket->self_ref     = luaL_ref ( L, LUA_REGISTRYINDEX );
    socket->cb_send_ref  = LUA_NOREF;
    socket->pesp_conn    = pesp_conn;
    socket->content_type = REQUEST_UNKNOWN;
    socket->content_data = NULL;
    socket->uri          = NULL;
    pesp_conn->reverse   = socket;
    
    espconn_regist_recvcb   ( pesp_conn, httpd_socket_received_callback     );
    espconn_regist_sentcb   ( pesp_conn, httpd_socket_sent_callback         );
    espconn_regist_disconcb ( pesp_conn, httpd_socket_disconnected_callback );
    espconn_regist_reconcb  ( pesp_conn, httpd_socket_reconnected_callback  );
    espconn_set_opt         ( pesp_conn, 0x03 ); // 	ESPCONN_REUSEADDR = 0x01, ESPCONN_NODELAY = 0x02, ESPCONN_COPY = 0x04, ESPCONN_KEEPALIVE = 0x08
    return;
  }
  espconn_disconnect ( pesp_conn );
  return;
}

// Lua: httpd:closeServer()
static int httpd_server_close( lua_State* L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_server_close\n" );

  if ( tcpserver_cb_receive_ref != LUA_NOREF )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
    tcpserver_cb_receive_ref = LUA_NOREF;
  }
  if ( httpd_server )
  {
    if ( httpd_server->proto.tcp != NULL )
    {
      os_free ( httpd_server->proto.tcp );
      httpd_server->proto.tcp = NULL;
    }
    c_free ( httpd_server );
  }
  httpd_server = NULL;
  return 0;
}


// Lua: httpd.createServer ( port, basicAuth, function ( socket ) )
static int httpd_server_create( lua_State* L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_server_create\n" );

  const char * auth;
  int auth_length;
    
  httpd_server_close( L );
  
  httpd_server = (struct espconn *) c_zalloc ( sizeof ( struct espconn ) );

  uint16_t port     = lua_tointeger ( L, 1 );
  int stack = 2;
  
  if ( lua_isstring ( L, stack ) )
  {
    auth = luaL_checklstring ( L, stack, &auth_length );
    httpd_auth_user = httpd_basic_auth ( auth, auth_length );
    stack++;
  }
  if ( lua_isstring ( L, stack ) )
  {
    auth = luaL_checklstring ( L, stack, &auth_length );
    httpd_auth_admin = httpd_basic_auth ( auth, auth_length );
    stack++;
  }
  if ( lua_isfunction ( L, stack ) || lua_islightfunction ( L, stack ) )
  {
    lua_pushvalue ( L, stack );
    if ( tcpserver_cb_receive_ref != LUA_NOREF )
    {
      luaL_unref ( L, LUA_REGISTRYINDEX, tcpserver_cb_receive_ref );
    }
    tcpserver_cb_receive_ref = luaL_ref ( L, LUA_REGISTRYINDEX );
  }
  if ( ! httpd_server )
  {
    return luaL_error ( L, "!!! NOT ENOUGHT MEMORY - struct espconn !!!" );
  }
  httpd_server->type       = ESPCONN_TCP;
  httpd_server->state      = ESPCONN_NONE;
  httpd_server->proto.udp  = NULL;
  httpd_server->proto.tcp  = (esp_tcp *) c_zalloc ( sizeof ( esp_tcp ) );

  if ( ! httpd_server->proto.tcp )
  {
    c_free ( httpd_server );
    httpd_server = NULL;
    return luaL_error ( L, "!!! NOT ENOUGHT MEMORY - proto.tcp !!!" );
  }
  httpd_server->proto.tcp->local_port = port;

  espconn_regist_connectcb      ( httpd_server, httpd_server_connected_callback );
  espconn_accept                ( httpd_server );
  espconn_regist_time           ( httpd_server, tcp_server_timeover, 0 );
  espconn_tcp_set_max_con_allow ( httpd_server, (uint8) MAX_SOCKET );
  espconn_set_opt ( httpd_server, 0x03 ); // 	ESPCONN_REUSEADDR = 0x01, ESPCONN_NODELAY = 0x02, ESPCONN_COPY = 0x04, ESPCONN_KEEPALIVE = 0x08
  
  return 0;
}
// =============================================================================
// Lua: socket:close()
static int httpd_socket_close( lua_State* L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_close\n" );

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, HTTPD_SOCKET );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL )
  {
    espconn_disconnect ( socket->pesp_conn );
  }
  return 0;
}
// Lua: socket:delete()
static int httpd_socket_delete( lua_State* L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_delete\n" );

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, HTTPD_SOCKET );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL )
  { 
    if ( socket->pesp_conn != NULL )
    {
      if ( socket->pesp_conn->proto.tcp )
      {
        os_free ( socket->pesp_conn->proto.tcp );
      }
      socket->pesp_conn->proto.tcp = NULL;
      os_free ( socket->pesp_conn );
      socket->pesp_conn          = NULL;    // for socket, it will free this when disconnected
      socket->pesp_conn->reverse = NULL;
    }
    
    socket->content_type = 0;
    if ( socket->uri != NULL )
    {
      c_free ( socket->uri );
      socket->uri = NULL;
    }
    if ( socket->content_data != NULL )
    {
      c_free ( socket->content_data );
      socket->content_data = NULL;
    }
  }
  lua_gc ( L, LUA_GCSTOP, 0 );
  // free (unref) callback ref
  if ( socket->cb_send_ref != LUA_NOREF )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, socket->cb_send_ref );
    socket->cb_send_ref = LUA_NOREF;
  }
  if ( socket->self_ref != LUA_NOREF )
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
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_on_sent\n" );

  socket_userdata * socket = (socket_userdata *) luaL_checkudata ( L, 1, HTTPD_SOCKET );
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
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_send\n" );
  int length;

  socket_userdata * socket = ( socket_userdata * ) luaL_checkudata ( L, 1, HTTPD_SOCKET );
  luaL_argcheck ( L, socket, 1, "Socket expected" );

  if ( socket != NULL && socket->pesp_conn != NULL )
  {
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
    espconn_send ( socket->pesp_conn, (unsigned char *) payload, length );
  }
  return 0;
}
// Lua: ip,port = socket:getpeer()
static int httpd_socket_getpeer( lua_State* L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_getpeer\n" );

  socket_userdata * socket = ( socket_userdata * ) luaL_checkudata ( L, 1, HTTPD_SOCKET );
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
  luaL_rometatable ( L, HTTPD_SOCKET, (void *)httpd_socket_map );  // create metatable for httpd.socket
  httpd_build_etag();
  return 0;
}

NODEMCU_MODULE(HTTPD, "httpd", httpd_map, luaopen_httpd);