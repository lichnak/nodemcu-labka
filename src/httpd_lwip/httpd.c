// Module for httpd
// LWIP WIKI - http://lwip.wikia.com/wiki/Raw/TCP

// TODO
//     c_printf ( "%d\n", __LINE__ );


#define NODE_DBG_HTTPD c_printf
// #define NODE_DBG_HTTPD


#include "module.h"  // module related 
#include "lauxlib.h" // lua related 
#include "platform.h"  // need for system_get_time() 
#include "vfs.h"            // filesystem functions
// #include "lmem.h"

#include "c_string.h" // string functions 
#include "c_stdlib.h" // memory functions 

// #include "c_types.h"
// #include "mem.h"
// #include "osapi.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/dns.h"
#include "lwip/igmp.h"
#include "lwip/tcp.h"

typedef enum httpd_type {
  TYPE_TCP_SERVER = 0,
  TYPE_TCP_SOCKET
} httpd_type;

#define HTTPD_CRLF "\r\n"

#define HTTPD_RESPONSE_NONE 0
#define HTTPD_RESPONSE_STATUS 1
#define HTTPD_RESPONSE_FILE 2
#define HTTPD_RESPONSE_REDIRECT 3
#define HTTPD_RESPONSE_GET 4
#define HTTPD_RESPONSE_POST 5

typedef struct httpd_userdata {
  enum httpd_type type;
  int self_ref;
  struct tcp_pcb * pcb;
  int cb_receive_ref;
  int cb_sent_ref;
  int retries;
  int poll_retries;
  int content_len;
  int content_len_left;
  int write_count;
//   int post_type;
  char * uri;
  const char * uri_ref;
  int uri_len;
  int response_type;
  int content_type;
  int status;
  char * content_data;
  bool etag_found;
  char * filename;
} httpd_userdata;

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

#define HTTPD_PROCESSING 0
#define HTTPD_FILE_CACHED 1
#define HTTPD_FILE 2

#define HTTPD_NO_POST 0
#define HTTPD_POST_FORM 4
#define HTTPD_POST_JSON 5
#define HTTPD_POST_TEXT 6

#define HTTPD_POLL_INTERVAL 4
#define HTTPD_MAX_RETRIES 4 // Maximum retries before the connection is aborted/closed

static err_t httpd_poll ( void *arg, struct tcp_pcb *pcb );

static char * httpd_etag;
static char * httpd_if_none_match;
static char * httpd_etag_not_modified;
static char * httpd_auth;

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

static const char http_header_302_index[] = "HTTP/1.1 302 Found\r\nLocation: /static/index.htm\r\n\r\n"; // redirection to index.htm

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

struct mg_str {
  const char *p; /* Memory chunk pointer */
  size_t len;    /* Memory chunk length */
};

const char *mg_skip ( const char *s, const char *end, const char *delims,
                    struct mg_str *v ) {
  v->p = s;
  while ( s < end && strchr ( delims, *(unsigned char *) s ) == NULL ) s++;
  v->len = s - v->p;
  while ( s < end && strchr ( delims, *(unsigned char *) s ) != NULL ) s++;
  return s;
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

// === ??? =====================================================================

char *strnstr ( const char *haystack, const char *needle, size_t len )
{
  int i;
  size_t needle_len;
  /* segfault here if needle is not NULL terminated */
  if ( 0 == ( needle_len = strlen ( needle ) ) )
  {
    return (char *) haystack;
  }
  for ( i = 0; i <= (int)( len - needle_len ); i++ )
  {
    if ( ( haystack[0] == needle[0] ) &&
      ( 0 == strncmp ( haystack, needle, needle_len ) ) )
    {
      return (char *) haystack;
    }
    haystack++;
  }
  return NULL;
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

static void httpd_basic_auth ( const char * auth, int auth_length )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_basic_auth\n" );
  
  if ( auth_length > 0 )
  {
    char *base64_auth = (char *) httpd_base64_encode ( (char *)auth, auth_length );
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
  }
  return;
}

static int httpd_reset_etag ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_reset_etag\n" );

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

// === ERROR HANDLING ==========================================================

static char * httpd_lwip_checkerr ( err_t err )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_lwip_checkerr\n" );
  
  switch ( err )
  {
    case ERR_OK:         return "";
    case ERR_MEM:        return "TCP ERROR: out of memory";
    case ERR_BUF:        return "TCP ERROR: buffer error";
    case ERR_TIMEOUT:    return "TCP ERROR: timeout";
    case ERR_RTE:        return "TCP ERROR: routing problem";
    case ERR_INPROGRESS: return "TCP ERROR: in progress";
    case ERR_VAL:        return "TCP ERROR: illegal value";
    case ERR_WOULDBLOCK: return "TCP ERROR: would block";
    case ERR_ABRT:       return "TCP ERROR: connection aborted";
    case ERR_RST:        return "TCP ERROR: connection reset";
    case ERR_CLSD:       return "TCP ERROR: connection closed";
    case ERR_CONN:       return "TCP ERROR: not connected";
    case ERR_ARG:        return "TCP ERROR: illegal argument";
    case ERR_USE:        return "TCP ERROR: address in use";
    case ERR_IF:         return "TCP ERROR: netif error";
    case ERR_ISCONN:     return "TCP ERROR: already connected";
    default:             return "TCP ERROR: unknown error";
  }
}

int httpd_lwip_lua_checkerr ( lua_State *L, err_t err )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_lwip_lua_checkerr\n" );
  
  return luaL_error ( L, httpd_lwip_checkerr ( err ) ); 
} 

// === 

void httpd_uri_decode ( lua_State *L, const char *s, int length )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_uri_decode\n" );

  luaL_Buffer b;
  luaL_buffinit ( L, &b );
  int i;
  int i1;
  int i2;
  int length2 = ( length >= 2 ) ? length - 2 : 0; 
  char chr;
  if ( s && length > 0 && luaL_prepbuffer ( &b ) )
  {
    for ( i = 0; i < length; i++ )
    {
      i1 = i + 1;
      i2 = i + 2;
      chr = s[i];
      if ( chr == '%' && i < length2 && isxdigit( s[i1] ) && isxdigit( s[i2] ) )
      {
        chr = (unsigned char)( (hexchar_to_dec( s[i1] ) << 4) + hexchar_to_dec( s[i2] ) );
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

void httpd_query_string_parser ( lua_State *L, const char *s, int length )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_query_string_parser\n" );

  const char * s_end = s + length;
  struct mg_str keyvalue;
  struct mg_str key;
  struct mg_str value;
  const char * p_value;
  const char * p_value_end;
  int key_len;

  lua_createtable( L, 0, 0 );

  while ( s != s_end )
  {
    s = mg_skip ( s, s_end, "&", &keyvalue );
    if ( keyvalue.p != NULL && keyvalue.len > 0 )
    {
      p_value     = keyvalue.p;
      p_value_end = keyvalue.p + keyvalue.len;
      key_len     = 0;
      while ( p_value < p_value_end && strchr ( "=", *(unsigned char *) p_value ) == NULL ) p_value++;
      key_len = p_value - keyvalue.p;
      while ( p_value < p_value_end && strchr ( "=", *(unsigned char *) p_value ) != NULL ) p_value++;

      if ( key_len > 0 )
      {
        if ( keyvalue.p != p_value )
        {
          httpd_uri_decode ( L, keyvalue.p, key_len );
          httpd_uri_decode ( L, p_value, p_value_end - p_value );
          lua_settable     ( L, -3 );
        }
        else
        {
          httpd_uri_decode ( L, keyvalue.p, keyvalue.len );
          lua_pushlstring  ( L, "", 0 );
          lua_settable     ( L, -3 );
        }
      }
    } 
  }
}

static err_t httpd_close_conn ( struct tcp_pcb *pcb, struct httpd_userdata *ud )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_close_conn\n" );

  err_t err;

  if ( ud != NULL )
  {
    lua_State *L = lua_getstate();
    
    if ( ud->self_ref != LUA_NOREF )
    {
      lua_gc     ( L, LUA_GCSTOP, 0 );
      ud->cb_receive_ref == LUA_NOREF;
      if ( ud->self_ref != LUA_NOREF )
      {
        luaL_unref ( L, LUA_REGISTRYINDEX, ud->self_ref );
        ud->self_ref = LUA_NOREF;
      }
      if ( ud->cb_sent_ref != LUA_NOREF )
      {
        luaL_unref ( L, LUA_REGISTRYINDEX, ud->cb_sent_ref );
        ud->cb_sent_ref = LUA_NOREF;
      }
      lua_gc     ( L, LUA_GCRESTART, 0 );
    }
    
    ud->pcb            = NULL;
    ud->retries        = 0;
    ud->poll_retries        = 0;
    ud->content_len    = 0;
    ud->content_len_left = 0;
    ud->write_count    = 0;
    ud->uri_len        = 0;
    ud->uri_ref        = NULL;
    ud->etag_found     = false;
    if ( ud->uri != NULL )
    {
      c_free ( ud->uri );
      ud->uri = NULL;
    }
    
    if ( ud->content_data != NULL )
    {
      c_free ( ud->content_data );
      ud->content_data = NULL;
    }
    if ( ud->filename != NULL )
    {
      c_free ( ud->filename );
      ud->filename = NULL;
    }
    

  }
  
  if ( pcb != NULL )
  {
    tcp_arg   ( pcb, NULL );
    tcp_recv  ( pcb, NULL );
    tcp_err   ( pcb, NULL );
    tcp_poll  ( pcb, NULL, 0 );
    tcp_sent  ( pcb, NULL );
  
    err = tcp_close ( pcb );
  
    if ( err != ERR_OK )
    {
      tcp_poll ( pcb, httpd_poll, HTTPD_POLL_INTERVAL );
    }
  }
  return err;
} 


static err_t httpd_sent_close_cb ( void *arg, struct tcp_pcb *pcb, u16_t len )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_sent_close_cb\n" );

  httpd_userdata *ud = ( httpd_userdata * ) arg;
  if ( ! ud || ud->type != TYPE_TCP_SOCKET )
  { 
    return ERR_ABRT;
  }
  httpd_close_conn ( pcb, arg );
  return ERR_OK;
}


static bool httpd_string_starts_with ( const char *s, const char *c )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_string_starts_with\n" );
  
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

static err_t httpd_send_file ( struct httpd_userdata *ud, struct tcp_pcb *pcb, bool enable_etag )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_send_file\n" );

  int err;
  int err2;
  int f;
  int file_size;
  int dot_pos = 0;
  int mime      = 1;
  char * response_file;
  int response_header_length;
  const char * etag;
  err_t ret_err;

  int frequency = ets_get_cpu_frequency();  
  REG_SET_BIT(0x3ff00014, BIT(0));
  ets_update_cpu_frequency(160);
  
  if ( ud->filename == NULL )
  {
    return tcp_write ( pcb, ( unsigned char * ) http_header_500, (uint16) c_strlen ( http_header_500 ), 0 );
  }

  char * p_dot = strrchr ( ud->filename, '.' );
  if ( p_dot != NULL && p_dot != ud->filename )
  {
    mime = httpd_mime_type ( p_dot + 1 );
  }

//   c_printf ( "\nOPEN FILE: %s\n", ud->filename );

  f = vfs_open ( ud->filename, "r" );
//   c_free ( ud->filename );
//   ud->filename = NULL;
  
  if ( ! f )
  {
    return tcp_write ( ud->pcb, ( unsigned char * ) http_header_404, (uint16) c_strlen ( http_header_404 ), 0 );
  }

  err       = vfs_lseek ( f, 0, VFS_SEEK_END );
  file_size = (int) vfs_tell ( f );
  err2      = vfs_lseek ( f, 0, VFS_SEEK_SET );

  if ( err2 != VFS_RES_ERR && file_size <= 2800 )
  {
    response_file = (char *) c_malloc ( file_size + 120 );

    if ( response_file != NULL)
    {
      etag = ( httpd_etag && enable_etag == true ) ?  httpd_etag : "";
      response_header_length = c_sprintf ( response_file, "HTTP/1.1 200 OK\r\n%sContent-length: %d\r\nContent-Type: %s\r\n\r\n", etag, file_size, httpd_mime[ mime ] );

      vfs_read ( f, &( response_file[response_header_length] ), file_size );
      vfs_close ( f );

      REG_CLR_BIT(0x3ff00014,  BIT(0));
      ets_update_cpu_frequency(frequency);

      ret_err = tcp_write ( ud->pcb, (unsigned char *) response_file, response_header_length + file_size, TCP_WRITE_FLAG_COPY );
      c_free ( response_file );
      return ret_err;
    }
  }

  vfs_close ( f );
  REG_CLR_BIT(0x3ff00014,  BIT(0));
  ets_update_cpu_frequency(frequency);

  return tcp_write ( pcb, ( unsigned char * ) http_header_500, (uint16) c_strlen ( http_header_500 ), 0 );

} 

static void httpd_receive_processing ( struct httpd_userdata *ud, struct tcp_pcb *tpcb, struct pbuf *p, err_t err )
{ 
  NODE_DBG_HTTPD ( "FUNC: httpd_receive_processing\n" );

//   if ( p->payload != NULL )
//   {
//     c_printf ( "\n=>%s<=\n", p->payload ); 
//   } 

  if ( ud->content_len_left > 0 ) 
  {
    ud->retries++;
    if ( p->payload != NULL && ud->content_len_left == p->len )
    {
      int offset = ud->content_len - ud->content_len_left;
      
      if ( ud->content_data == NULL )
      {
        ud->content_data = c_malloc ( ud->content_len + 1 );
      }
      if ( ud->content_data != NULL )
      {
        c_memcpy ( ud->content_data + offset, p->payload, p->len );
      }
      ud->content_len_left = 0;
      return;
    }
    else
    {
      ud->response_type = HTTPD_RESPONSE_STATUS;
      ud->status = 400;
      return;
    }
  }

  if ( ! p->payload || p->len < 7 ) // no data or empty data
  {
    ud->response_type = HTTPD_RESPONSE_STATUS;
    ud->status = 400;
    return;
  }  

  if ( httpd_auth != NULL && strstr ( p->payload, httpd_auth ) == NULL ) // auth is required, but not found in header 
  {
    ud->response_type = HTTPD_RESPONSE_STATUS;
    ud->status = 401;
    return;
  }
  
  char *data     = (char *) p->payload;
  u16_t data_len = p->len;
  char * crlf    = strnstr ( data, HTTPD_CRLF, data_len );

  if ( crlf != NULL )
  {
    int is_post = 0;
    char *sp1, *uri_end;
    u16_t left_len, uri_len;
    /* parse method */
    if ( ! strncmp ( data, "GET ", 4 ) ) // received GET request
    {
      sp1 = data + 3;
      ud->response_type = HTTPD_RESPONSE_GET;
    }
    else if ( ! strncmp ( data, "POST ", 5 ) ) // received POST request
    {
      is_post = 1;
      sp1 = data + 4;
      ud->response_type = HTTPD_RESPONSE_POST;
    }
    else // unsupported method!
    {
      ud->response_type = HTTPD_RESPONSE_STATUS;
      ud->status = 405;
      return;
    }

    left_len = data_len - ( ( sp1 + 1 ) - data );
    uri_end  = strnstr ( sp1 + 1, " ", left_len );
    uri_len  = uri_end - ( sp1 + 1 );
    if ( ( uri_end != 0 ) && ( uri_end > sp1 ) )
    {
      char *uri = sp1 + 1;
      *sp1 = 0;
      uri[uri_len] = 0;
    
      ud->uri = c_zalloc ( uri_len + 1 );
      if ( ud->uri != NULL )
      {
        c_memcpy ( ud->uri, uri, uri_len );
        ud->uri_len = uri_len;
      }

      if ( is_post == 0 )
      {
        char* crlfcrlf = strnstr ( uri_end + 1, HTTPD_CRLF HTTPD_CRLF, data_len - ( uri_end + 1 - data ) ); // search for end-of-header (first double-CRLF)

        if ( crlfcrlf != NULL && httpd_if_none_match != NULL && ( strnstr ( uri_end + 1, httpd_if_none_match, crlfcrlf - ( uri_end + 1 ) ) != NULL ) ) 
        {
          ud->etag_found = true;
        }
        return;      
      }
      else
      {
        char* crlfcrlf = strnstr ( uri_end + 1, HTTPD_CRLF HTTPD_CRLF, data_len - ( uri_end + 1 - data ) ); // search for end-of-header (first double-CRLF)

        if ( crlfcrlf != NULL )
        {
          char * scontent_type = strnstr ( uri_end + 1, "Content-Type: ", crlfcrlf - ( uri_end + 1 ) );
          if ( scontent_type != NULL )
          {
            char * content_type_name = scontent_type + 14;
            if ( ! strncmp ( content_type_name, "application/x-www-form-urlencoded\r\n", 35 ) )
            {
              ud->content_type = HTTPD_POST_FORM;
            }
            else if ( ! strncmp ( content_type_name, "application/json\r\n", 18 ) )
            {
              ud->content_type = HTTPD_POST_JSON;
            }
            else if ( ! strncmp ( content_type_name, "text/", 5 ) )
            {
              ud->content_type = HTTPD_POST_TEXT;
            } 
            else
            {
              ud->response_type = HTTPD_RESPONSE_STATUS;
              ud->status = 400;
              return;
            }
          }
          char *scontent_len = strnstr ( uri_end + 1, "Content-Length: ", crlfcrlf - ( uri_end + 1 ) );
          if ( scontent_len != NULL )
          {
            char *scontent_len_end = strnstr ( scontent_len + 16, HTTPD_CRLF, 10 );
            if ( scontent_len_end != NULL )
            {
              char *conten_len_num = scontent_len + 16;
              *scontent_len_end = 0;
              int content_len = atoi ( conten_len_num );
      
              if ( content_len == 0 )
              {
                ud->response_type = HTTPD_RESPONSE_POST;
                return;
              }
              else if ( content_len > 512 )
              {
                ud->response_type = HTTPD_RESPONSE_STATUS;
                ud->status = 501;
                return;
              }
              else if ( content_len < 0 )
              {
                ud->response_type = HTTPD_RESPONSE_STATUS;
                ud->status = 400;
                return;
              }
              else
              {
                ud->content_len = content_len;
                char * content_data = crlfcrlf + 4;
                data = data + data_len; 
                int content_data_len = data - content_data;
                
                ud->content_data = c_malloc ( content_len + 1 );
                if ( ud->content_data != NULL )
                {
                  c_memcpy ( ud->content_data, content_data, content_data_len );
                  ud->content_len_left = content_len - content_data_len;
                  return;
                }
                // cant allocate memory for ud->content_data
                ud->response_type = HTTPD_RESPONSE_STATUS;
                ud->status = 501;
                return;
              }
            }
          }
        }
      }
    } 
    else // wrongu uri
    {
      ud->response_type = HTTPD_RESPONSE_STATUS;
      ud->status = 400;
      return;
    }
  }
  ud->response_type = HTTPD_RESPONSE_STATUS;
  ud->status = 501;
  return;
}



// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

static void httpd_err_cb ( void *arg, err_t err )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_err_cb\n" );
  
  httpd_userdata *ud = ( httpd_userdata * ) arg;
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ud->self_ref == LUA_NOREF )
  { 
    return;
  }
//   lua_State *L = lua_getstate();
//   lua_gc     ( L, LUA_GCSTOP, 0 );
//   luaL_unref ( L, LUA_REGISTRYINDEX, ud->self_ref );
//   ud->self_ref = LUA_NOREF;
//   lua_gc     ( L, LUA_GCRESTART, 0 );
}

static err_t httpd_sent_cb ( void *arg, struct tcp_pcb *tpcb, u16_t len )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_sent_cb\n" );

  httpd_userdata *ud = ( httpd_userdata * ) arg;
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ud->self_ref == LUA_NOREF )
  { 
    return ERR_ABRT;
  }
  if ( ud->cb_sent_ref == LUA_NOREF )
  {
    return ERR_OK;
  }
  lua_State *L = lua_getstate();
  lua_rawgeti ( L, LUA_REGISTRYINDEX, ud->cb_sent_ref );
  lua_rawgeti ( L, LUA_REGISTRYINDEX, ud->self_ref );
  lua_call    ( L, 1, 0 );
  return ERR_OK;
}


static err_t httpd_poll ( void *arg, struct tcp_pcb *pcb )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_poll\n" );

  struct httpd_userdata *ud = (struct httpd_userdata *) arg;

  if ( ud == NULL )
  {
    err_t closed;
    closed = httpd_close_conn ( pcb, ud );
    if ( closed == ERR_MEM )
    {
      tcp_abort ( pcb );
      return ERR_ABRT;
    }
    return ERR_OK;
  }
  else
  {
    ud->poll_retries++;
    if ( ud->poll_retries >= HTTPD_MAX_RETRIES )
    {
      httpd_close_conn ( pcb, ud );
      return ERR_OK;
    }
  }

  return ERR_OK;
} 

#define HTTPD_URI_PROCESSING 0
#define HTTPD_URI_REDIRECT_HOME 1
#define HTTPD_URI_FAVICON 2
#define HTTPD_URI_NO_CHANGE 3
#define HTTPD_URI_FILE 4
#define HTTPD_URI_FILE_ETAG 5

static int httpd_identify_uri ( struct httpd_userdata *ud )
{
  if ( ud->uri_len == 1 && ud->uri != NULL && ud->uri[0] == '/' )
  {
    return HTTPD_URI_REDIRECT_HOME;
  }
  else if ( ! strncmp ( ud->uri, "/favicon.ico", 12 ) )
  {
    return ( ud->etag_found == true ) ? HTTPD_URI_NO_CHANGE : HTTPD_URI_FAVICON;
  }
  else if ( ! strncmp ( ud->uri, "/static/", 8 ) )
  {
    return ( ud->etag_found == true ) ? HTTPD_URI_NO_CHANGE : HTTPD_URI_FILE_ETAG;
  }
  else if ( ! strncmp ( ud->uri, "/data/", 8 ) )
  {
    return HTTPD_URI_FILE;
  }
  return HTTPD_URI_PROCESSING;
}

err_t httpd_send_data ( struct httpd_userdata *ud, struct tcp_pcb *pcb )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_send_data\n" );

  err_t err = ERR_OK;

  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ud->self_ref == LUA_NOREF )
  { 
    httpd_close_conn ( pcb, ud );
  }

  if ( ud->response_type == HTTPD_RESPONSE_NONE )
  {
    return err;
  }

  if ( ud->content_len_left > 0 ) 
  {
    if ( ud->retries == 0 ) // wait for post data
    {
      return ERR_OK;
    
    }
    else if ( ud->retries > 1 )
    {
      ud->response_type = HTTPD_RESPONSE_STATUS;
      ud->status = 501;
    }
  }

  tcp_sent ( pcb, httpd_sent_close_cb );
  
  if ( ud->response_type == HTTPD_RESPONSE_STATUS )
  {
    const char * http_header;
    switch ( ud->status )
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
    err = tcp_write ( pcb, (unsigned char *) http_header, (uint16) c_strlen ( http_header ), 0 );
    return err;
  }

  if ( ud->response_type == HTTPD_RESPONSE_REDIRECT && ud->uri_len > 0 && ( ud->uri != NULL || ud->uri_ref != NULL ) )
  {
    const char * uri = ( ud->uri != NULL ) ? ud->uri : ud->uri_ref;
    char * redirect = (char *) c_malloc ( ud->uri_len + 40 );
    if ( redirect != NULL )
    {
      int redirect_length = c_sprintf ( redirect, "HTTP/1.1 302 Found\r\nLocation: %s\r\n\r\n", uri );
      err = tcp_write ( pcb, (unsigned char *) redirect, redirect_length , TCP_WRITE_FLAG_COPY );
      c_free ( redirect );
      ud->write_count++;
      return err;
    } 
  }
  
  lua_State *L;
  
  if ( ud->response_type == HTTPD_RESPONSE_GET )
  {
    if ( ud->uri != NULL )
    {
      int uri_type = httpd_identify_uri ( ud );
    
      switch ( uri_type )
      {
        case HTTPD_URI_REDIRECT_HOME:
          ud->response_type = HTTPD_RESPONSE_REDIRECT;
          ud->uri_ref = "/static/index.htm";
          c_free ( ud->uri );
          ud->uri = NULL;
          return httpd_send_data ( ud, pcb );

        case HTTPD_URI_FAVICON:
          ud->filename = c_malloc ( 19 );
          if ( ud->filename != NULL )
          {
            c_memcpy ( ud->filename, "static/favicon.ico", 18 );
          }
          httpd_send_file ( ud, pcb, true );
          return ERR_OK;

        case HTTPD_URI_NO_CHANGE:
          return tcp_write ( pcb, (unsigned char *) httpd_etag_not_modified, (uint16) c_strlen ( httpd_etag_not_modified ), 0 );
          
        case HTTPD_URI_FILE:
        case HTTPD_URI_FILE_ETAG:
          ud->filename = c_zalloc ( ud->uri_len + 1 );
          if ( ud->filename != NULL )
          {
            c_memcpy ( ud->filename, ud->uri + 1, ud->uri_len - 1 );
          }
          httpd_send_file ( ud, pcb, ( uri_type == HTTPD_URI_FILE_ETAG ? true : false ) );
          return ERR_OK;
      
        case HTTPD_URI_PROCESSING:
          L = lua_getstate ();        
          lua_rawgeti ( L, LUA_REGISTRYINDEX, ud->cb_receive_ref );
          lua_rawgeti ( L, LUA_REGISTRYINDEX, ud->self_ref );  // pass the userdata(socket) to callback func in lua

          lua_pushlstring     ( L, "GET", 3 ); // method

          const char * qs = (char *) memchr ( ud->uri, '?', ud->uri_len );
        
          if ( qs != NULL )
          {
            lua_pushlstring     ( L, (char * ) ud->uri, qs - ud->uri ); // path
            lua_pushlstring     ( L, "get", 3 ); // data type
            httpd_query_string_parser ( L, (char * ) qs + 1, &ud->uri[ud->uri_len] - (qs + 1) ); // data
          }
          else
          {
            lua_pushlstring ( L, (char * ) ud->uri, ud->uri_len ); // path
            lua_pushnil         ( L ); // data type 
            lua_pushnil         ( L ); // data 
          }

          if ( lua_pcall ( L, 5, 0, 0 ) )
          {
            int err_len;
            const char * err_message = lua_tolstring ( L, -1, &err_len );
            if ( err_message )
            {
              char * response_error = (char *) c_malloc ( 50 + err_len );
              if ( response_error != NULL )
              {
                int response_error_length = c_sprintf ( response_error, "HTTP/1.1 500 Internal Server Error\r\n\r\n%s", err_message );
                tcp_write ( pcb, (unsigned char *) response_error, response_error_length, TCP_WRITE_FLAG_COPY );
                c_free ( response_error );
                return ERR_OK;
              }
            }
          }
          if ( ud->write_count == 0 )
          {
            return tcp_write ( pcb, ( unsigned char * ) http_header_404, (uint16) c_strlen ( http_header_404 ), 0 );
          }
          return ERR_OK;
      } // END switch ( uri_type )
    }
    return tcp_write ( pcb, ( unsigned char * ) http_header_500, (uint16) c_strlen ( http_header_500 ), 0 );
  }
  
  if ( ud->response_type == HTTPD_RESPONSE_POST )
  {
    if ( ud->uri == NULL )
    {
      return tcp_write ( pcb, ( unsigned char * ) http_header_500, (uint16) c_strlen ( http_header_500 ), 0 );
    }
    
    L = lua_getstate ();
    lua_rawgeti ( L, LUA_REGISTRYINDEX, ud->cb_receive_ref );
    lua_rawgeti ( L, LUA_REGISTRYINDEX, ud->self_ref );  // pass the userdata(socket) to callback func in lua
    lua_pushlstring ( L, "POST", 4 ); // method
    lua_pushlstring ( L, (char * ) ud->uri, ud->uri_len ); // path
  
    switch ( ud->content_type )
    {
      case HTTPD_POST_FORM: lua_pushlstring ( L, "form", 4 ); break;
      case HTTPD_POST_JSON: lua_pushlstring ( L, "json", 4 ); break;
      case HTTPD_POST_TEXT: lua_pushlstring ( L, "text", 4 ); break;
      default: lua_pushlstring ( L, "", 0 ); break;
    }
    if ( ud->content_data != NULL )
    {
      if ( ud->content_type == HTTPD_POST_FORM )
      {
        httpd_query_string_parser ( L, ud->content_data, ud->content_len ); // form data
      }
      else
      {
        lua_pushlstring ( L, ud->content_data, ud->content_len );
      }
      c_free (  ud->content_data );
      ud->content_data = NULL;
      ud->content_len = 0;
    }
    else
    {
      lua_pushlstring ( L, "", 0 );
    }
    
    if ( lua_pcall ( L, 5, 0, 0 ) )
    {
      int err_len;
      const char * err_message = lua_tolstring ( L, -1, &err_len );
      if ( err_message )
      {
        char * response_error = (char *) c_malloc ( 50 + err_len );
        if ( response_error != NULL )
        {
          int response_error_length = c_sprintf ( response_error, "HTTP/1.1 500 Internal Server Error\r\n\r\n%s", err_message );
          tcp_write ( pcb, (unsigned char *) response_error, response_error_length, TCP_WRITE_FLAG_COPY );
          c_free ( response_error );
          return ERR_OK;
        }
      }
    }
    if ( ud->write_count == 0 )
    {
      return tcp_write ( pcb, ( unsigned char * ) http_header_404, (uint16) c_strlen ( http_header_404 ), 0 );
    }
    return ERR_OK;
  }

  httpd_close_conn ( pcb, ud );
  return err;
}

static err_t httpd_tcp_recv_cb ( void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_tcp_recv_cb\n" );

  httpd_userdata *ud = ( httpd_userdata * ) arg;
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ud->self_ref == LUA_NOREF || p == NULL  )
  { 
    return ERR_ABRT;
  }
  
  tcp_recved ( pcb, TCP_WND );

  httpd_receive_processing ( ud, pcb, p, err );
  
  httpd_send_data ( ud, pcb );

  pbuf_free  ( p );
  
  return ERR_OK;
}

static err_t httpd_accept_cb ( void *arg, struct tcp_pcb *new_pcb, err_t err )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_accept_cb\n" );

  httpd_userdata *ud = ( httpd_userdata * ) arg;
  if ( ! ud || ud->type != TYPE_TCP_SERVER || ud->self_ref == LUA_NOREF )
  { 
    return ERR_ABRT;
  }
  lua_State *L = lua_getstate();
  httpd_userdata *sud = ( httpd_userdata * ) lua_newuserdata ( L, sizeof ( httpd_userdata ) );
  if ( ! sud )
  {
    return ERR_ABRT;
  }
  luaL_getmetatable ( L, "httpd_socket" );
  lua_setmetatable  ( L, -2 );
  sud->self_ref       = luaL_ref ( L, LUA_REGISTRYINDEX );
  sud->type           = TYPE_TCP_SOCKET;
  sud->cb_sent_ref    = LUA_NOREF;
  sud->cb_receive_ref = ud->cb_receive_ref;
  sud->pcb            = new_pcb;
  sud->write_count    = 0;
  sud->content_data   = NULL;
  sud->filename       = NULL;
  sud->uri            = NULL;
  sud->uri_ref        = NULL;
  sud->content_len    = 0;
  sud->content_len_left = 0;
  sud->retries = 0;
  sud->poll_retries = 0;
  sud->response_type    = HTTPD_RESPONSE_NONE;

  tcp_accepted ( sud->pcb                    );
  tcp_setprio  ( sud->pcb, TCP_PRIO_MIN      );
  tcp_arg      ( sud->pcb, sud               );
  tcp_recv     ( sud->pcb, httpd_tcp_recv_cb );
  tcp_err      ( sud->pcb, httpd_err_cb      );
  tcp_poll     ( sud->pcb, httpd_poll, HTTPD_POLL_INTERVAL );
  tcp_sent     ( sud->pcb, httpd_sent_cb     );

//   sud->pcb->so_options |= SOF_KEEPALIVE;
//   sud->pcb->keep_idle   = 30000;
//   sud->pcb->keep_cnt    = 1;
//   tcp_nagle_enable( sud->pcb ); 
 
  return ERR_OK;
}

static int httpd_create_server ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_create_server\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_newuserdata ( L, sizeof ( httpd_userdata ) );
  if ( ! ud ) { return 0; }
  
  luaL_getmetatable ( L, "httpd_server" );
  lua_setmetatable  ( L, -2 );
  ud->self_ref       = luaL_ref ( L, LUA_REGISTRYINDEX );
  ud->type           = TYPE_TCP_SERVER;
  ud->cb_receive_ref = LUA_NOREF;
  
  uint16_t port     = lua_tointeger ( L, 1 );
  int stack = 2;
  
  if ( lua_isstring ( L, stack ) )
  {
    int auth_length;
    const char * auth = luaL_checklstring ( L, stack, &auth_length );
    httpd_basic_auth ( auth, auth_length );
    stack++;
  }
  
  if ( lua_isfunction ( L, stack ) || lua_islightfunction ( L, stack ) )
  {
    lua_pushvalue ( L, stack );
    luaL_unref ( L, LUA_REGISTRYINDEX, ud->cb_receive_ref );
    ud->cb_receive_ref = luaL_ref ( L, LUA_REGISTRYINDEX );
    stack++;
  }

  ud->pcb = tcp_new();

  if ( ! ud->pcb )
  {
    return luaL_error ( L, "cannot allocate PCB" );
  }

  ip_addr_t addr;
  ipaddr_aton ( "0.0.0.0", &addr );
  err_t err = tcp_bind ( ud->pcb, &addr, port );

  if ( err == ERR_OK )
  {
    struct tcp_pcb *pcb = tcp_listen ( ud->pcb );
    if ( pcb )
    {
      ud->pcb = pcb;
      tcp_accept ( ud->pcb, httpd_accept_cb );
      tcp_arg    ( ud->pcb, ud );
    }
    else
    {
      err = ERR_MEM;
    }
  }
  if ( err != ERR_OK )
  {
    tcp_close ( ud->pcb );
    ud->pcb = NULL;
    return httpd_lwip_lua_checkerr ( L, err );
  }
  return 0;
}

static int httpd_socket_on_sent ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_on_sent\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_touserdata ( L, 1 );
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ud->self_ref == LUA_NOREF )
  { 
    return 0;
  }
  luaL_checkanyfunction ( L, 2 );
  lua_pushvalue         ( L, 2 );
  if ( ud->cb_sent_ref != LUA_NOREF )
  {
    luaL_unref ( L, LUA_REGISTRYINDEX, ud->cb_sent_ref );
  }
  ud->cb_sent_ref = luaL_ref ( L, LUA_REGISTRYINDEX );
  return 0; 
}
 
int httpd_socket_send ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_send\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_touserdata ( L, 1 );
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ! ud->pcb )
  { 
    return 0;
  }
  int length;
  const char *payload = luaL_checklstring( L, 2, &length );
  if ( payload == NULL )
  {
    return luaL_error( L, "Socket:send payload is nil." );
  }
  if ( lua_type ( L, 3 ) == LUA_TFUNCTION || lua_type ( L, 3 ) == LUA_TLIGHTFUNCTION )
  {
    lua_pushvalue ( L, 3 );
    if ( ud->cb_sent_ref != LUA_NOREF )
    {
      luaL_unref ( L, LUA_REGISTRYINDEX, ud->cb_sent_ref );
    }
    ud->cb_sent_ref = luaL_ref ( L, LUA_REGISTRYINDEX );
  }
  err_t err = tcp_write ( ud->pcb, (unsigned char *) payload, length, TCP_WRITE_FLAG_COPY );
  ud->write_count++;
  return httpd_lwip_lua_checkerr ( L, err );
}

int httpd_socket_send_status ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_send_status\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_touserdata ( L, 1 );
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ! ud->pcb )
  { 
    return 0;
  }

  ud->response_type = HTTPD_RESPONSE_STATUS;
  ud->status = luaL_checkinteger ( L, 2 );
  
  err_t err = httpd_send_data ( ud, ud->pcb );
  ud->write_count++;
    
  return httpd_lwip_lua_checkerr ( L, err );
}

int httpd_socket_send_response ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_send_status\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_touserdata ( L, 1 );
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ! ud->pcb )
  { 
    return 0;
  }
  err_t err = ERR_OK;
  int content_type_length;
  int body_length;
  const char * content_type = luaL_checklstring ( L, 2, &content_type_length );
  const char * body         = luaL_checklstring ( L, 3, &body_length );
    
  tcp_sent  ( ud->pcb, httpd_sent_close_cb );
  
  char * response_buffer = (char *) c_malloc ( body_length + 100 );
  if ( response_buffer != NULL )
  {
    int mime = httpd_mime_type ( content_type );
    int response_length = c_sprintf ( response_buffer, "HTTP/1.1 200 OK\r\nContent-length: %d\r\nContent-Type: %s\r\n\r\n%s", body_length, httpd_mime[ mime ], body );
    err = tcp_write ( ud->pcb, (unsigned char *) response_buffer, response_length, TCP_WRITE_FLAG_COPY );
    c_free ( response_buffer );
  }
  else
  {
    err = tcp_write ( ud->pcb, (unsigned char *) http_header_500, (uint16) c_strlen ( http_header_500 ), 0 );
  }
  ud->write_count++;
  return httpd_lwip_lua_checkerr ( L, err );
}

int httpd_socket_send_redirect ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_socket_send_status\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_touserdata ( L, 1 );
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ! ud->pcb )
  { 
    return 0;
  }
  
  int uri_len;
  ud->response_type = HTTPD_RESPONSE_REDIRECT;
  ud->uri_ref = luaL_checklstring ( L, 2, &uri_len );
  ud->uri_len = uri_len;

  err_t err = httpd_send_data ( ud, ud->pcb );
  ud->write_count++;
  
  return httpd_lwip_lua_checkerr ( L, err );
}

int httpd_socket_getaddr ( lua_State *L )
{
  NODE_DBG_HTTPD ( "httpd_socket_getaddr\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_touserdata ( L, 1 );
  if ( ! ud || ud->type != TYPE_TCP_SOCKET || ! ud->pcb )
  { 
    lua_pushnil ( L );
    lua_pushnil ( L );
    return 2;
  }
  ip_addr_t addr = ud->pcb->local_ip;
  uint16_t port  = ud->pcb->local_port;
  if ( port == 0 )
  {
    lua_pushnil ( L );
    lua_pushnil ( L );
  } else {
    char addr_str[16];
    bzero ( addr_str, 16 );
    ets_sprintf ( addr_str, IPSTR, IP2STR ( &addr.addr ) );
    lua_pushinteger ( L, port );
    lua_pushstring  ( L, addr_str );
  }
  return 2;
}

int httpd_close_lua_conn ( lua_State *L )
{
  NODE_DBG_HTTPD ( "FUNC: httpd_close_lua_conn\n" );

  httpd_userdata *ud = ( httpd_userdata * ) lua_touserdata ( L, 1 );
  if ( ! ud )
  { 
    return 0;
  }
  
  httpd_close_conn ( ud->pcb, ud ); 
  return 0;
}

// Module function map
static const LUA_REG_TYPE httpd_tcpserver_map[] = {
  { LSTRKEY( "close" ),   LFUNCVAL( httpd_close_lua_conn ) },
  { LSTRKEY( "__gc" ),    LFUNCVAL( httpd_close_lua_conn ) },
  { LSTRKEY( "__index" ), LROVAL( httpd_tcpserver_map ) },
  { LNILKEY, LNILVAL }
};
 
static const LUA_REG_TYPE httpd_tcpsocket_map[] = {
  { LSTRKEY( "close" ),        LFUNCVAL( httpd_close_lua_conn ) },
  { LSTRKEY( "onsent" ),       LFUNCVAL( httpd_socket_on_sent ) },
  { LSTRKEY( "send" ),         LFUNCVAL( httpd_socket_send ) },
  { LSTRKEY( "sendStatus" ),   LFUNCVAL( httpd_socket_send_status ) },
  { LSTRKEY( "sendResponse" ), LFUNCVAL( httpd_socket_send_response ) },
  { LSTRKEY( "sendRedirect" ), LFUNCVAL( httpd_socket_send_redirect ) },
  { LSTRKEY( "getaddr" ),      LFUNCVAL( httpd_socket_getaddr ) },
  { LSTRKEY( "__gc" ),         LFUNCVAL( httpd_close_lua_conn ) },
  { LSTRKEY( "__index" ),      LROVAL( httpd_tcpsocket_map ) },
  { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE httpd_map[] = {
  { LSTRKEY( "createServer" ), LFUNCVAL( httpd_create_server ) },
  { LSTRKEY( "resetEtag" ),    LFUNCVAL( httpd_reset_etag ) },
  { LSTRKEY( "__metatable" ),  LROVAL( httpd_map ) },
  { LNILKEY, LNILVAL }
};

int luaopen_httpd ( lua_State *L ) {
  igmp_init();
  luaL_rometatable ( L, "httpd_server", (void *) httpd_tcpserver_map );
  luaL_rometatable ( L, "httpd_socket", (void *) httpd_tcpsocket_map );
  httpd_build_etag();
  return 0;
}

NODEMCU_MODULE ( HTTPD, "httpd", httpd_map, luaopen_httpd );
