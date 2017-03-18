 
 
#include "module.h"
#include "lauxlib.h"
#include "c_string.h"
#include "vfs.h"

#define hexchar_to_dec(c) ((c) & 0x40 ? ((c) & 0x0F) + 9 : ((c) & 0x0F)) 

#define httpd_luaL_addchar(B,c) \
  ((void)((B)->p + 1 < ((B)->buffer+LUAL_BUFFERSIZE)), \
   (*(B)->p++ = (char)(c)))

void httpd_luaL_addlstring (luaL_Buffer *B, const char *s, size_t l) {
  if ( l > 0 && (B)->p + l < ((B)->buffer + LUAL_BUFFERSIZE) || luaL_prepbuffer(B) ){  
    while ( l-- ){
      *(B)->p++ = (char)*s++;
    }
  }
}
void httpd_luaL_addstring (luaL_Buffer *B, const char *s) {
  size_t l = c_strlen(s);
  if ( l > 0 && (B)->p + l < ((B)->buffer + LUAL_BUFFERSIZE) || luaL_prepbuffer(B) ){  
    while ( l-- ){
      *(B)->p++ = (char)*s++;
    }
  }
}

const char httpd_hexbytes[] = "0123456789abcdef";
const char staticPath[] = "/static/";

/* ORDER RESERVED */
const char *const httpd_mime_ext [] = {
    "js", "css", "gif", "htm", "jpg",
    "pdf", "png", "svg", "txt", "xml",
    "zip", "html", "jpeg", "json", "text", 
    NULL
};

/* ORDER RESERVED */
const char *const httpd_mime_string [] = {
    "application/javascript", "text/css", "image/gif", "text/html", "image/jpeg",
    "application/pdf", "image/png", "image/svg+xml", "text/plain", "application/xml",
    "application/zip", "text/html", "image/jpeg", "application/json", "text/plain",
    NULL
};

static int httpd_findTextPos ( const char *s, const char *f ) {
  if ( s && f ) {
    char *p = strstr ( s, f );
    if ( p ) {
      return p - s;
    }
  } 
  return 0;
}

// -----------------------------------------------------------------------------
// --- httpd internal functions ------------------------------------------------
// -----------------------------------------------------------------------------

void internal_uri_decode ( lua_State *L, const char *s, size_t length )
{
  luaL_Buffer b;
  luaL_buffinit ( L, &b );
  size_t index;
  char chr;
  if ( s && length > 0 && luaL_prepbuffer ( &b ) ){
    for ( index = 0; index < length; index++ )
    {
      chr = s[index];
      if ( chr == '%' && index < length - 2 && isxdigit( s[index + 1] ) && isxdigit( s[index + 2] ) )
      {
        chr = (unsigned char)( (hexchar_to_dec( s[index + 1] ) << 4) + hexchar_to_dec( s[index + 2] ) );
        index += 2;
      } 
      else if ( chr == '+' )
      {
        chr = ' ';
      }
      if ( chr > 0 ){
        httpd_luaL_addchar ( &b, chr );
      }
    }
  }
  luaL_pushresult( &b );
}

void internal_request_query_pair ( lua_State *L, size_t varPos, size_t sepPos, size_t index )
{
  if ( varPos + 1 < sepPos && sepPos < index){
    lua_pushlstring     ( L, s + varPos, sepPos - varPos );
    internal_uri_decode ( L, s + sepPos + 1, index - sepPos - 1 );
    lua_settable        ( L, -3 );
  } else if ( varPos + 2 <  index ){
    lua_pushlstring     ( L, s + varPos, index - varPos );
    lua_pushlstring     ( L, "", 0 );
    lua_settable        ( L, -3 );
  }
}

void internal_request_query ( lua_State *L, const char *s, size_t length )
{
  size_t index;
  size_t varPos = 0;
  size_t sepPos = 0;
  char chr;
  lua_createtable( L, 0, 0 );
  if ( s ){
    for ( index = 0; index < length; index++ ){
      switch ( s[index] ){
        case '=':
          sepPos = index;
          break;
        case '&':
          internal_request_query_pair ( L, varPos, sepPos, index );
          varPos = index + 1;
          break;
        default:
          break;
      }
    }
    internal_request_query_pair ( L, varPos, sepPos, index );
  }
}

void internal_request_header ( lua_State *L, const char *s, size_t length )
{
  size_t index;
  size_t lineS       = 0;
  size_t lineE       = 0;
  size_t lineV       = 0;
  size_t lineVS      = 0;
  char chr;
  lua_createtable( L, 0, 0 );
  if ( s ) {
    for ( index = 0; index < length - 1; index++ ){
      chr = s[index];
      if ( chr == ':' && lineV <= lineS ) { // separator between variable: value
        lineV  = index;
        lineVS = index;
      } else if ( chr == ' ' && lineV == lineVS ) { // space after separator
        lineVS = index;
      }
  
      if ( ( chr == '\r' && s[index + 1] == '\n' ) || ( index + 1 == length ) ) { // line end
        lineE = index - 1;
        if ( ( lineV > lineS ) && ( lineE > lineS ) && ( lineE > lineV ) ) { // if line has separator ":"
          lua_pushlstring ( L, s + lineS, lineV - lineS );
          lua_pushlstring ( L, s + lineVS + 1, lineE - lineVS );
          lua_settable    ( L, -3 );
        }
        lineS = index + 2;
      }
    }
  }
}


static bool internal_is_static_path ( const char *s ){
  size_t index;
  size_t match = 0;
  for ( index = 0; index < 8; index++ ){
    if ( staticPath[index] == s[index] ){
      match += 1;
    }
  }
  return ( match == 8 ) ? true : false;
}

static int internal_enum_method ( const char *s, size_t length ){

  if ( length == 3 && s[0] == 'G' && s[1] == 'E' && s[2] == 'T' ){
    return 1;
  } else if ( length == 4 && s[0] == 'P' && s[1] == 'O' && s[2] == 'S' && s[3] == 'T' ){
    return 2;
  } else if ( length == 3 && s[0] == 'P' && s[1] == 'U' && s[2] == 'T' ){
    return 3;
  }
  return 0;
}

static int httpd_request ( lua_State *L )
{
  size_t length;
  size_t firstLineEnd;
  size_t headerEnd;
  size_t methodEnd;
  size_t uriEnd;
  size_t queryPos;
  size_t method;
  size_t contentForm;
  size_t contentJson;
  size_t contentText;
  
  const char *s = lua_tolstring( L, 1, &length );
  if ( s ) {
    firstLineEnd = httpd_findTextPos ( s, "\r\n" );
    methodEnd    = httpd_findTextPos ( s, " " );
    uriEnd       = httpd_findTextPos ( s, " HTTP" );

    if ( firstLineEnd > 0 && methodEnd > 2 && uriEnd > 0 && uriEnd < firstLineEnd ) {

      method = internal_enum_method ( s, methodEnd );
      
      lua_pushnumber  ( L, method ); // method - 0 = unknown, 1 = GET, 2 = POST, 3 = PUT
      lua_pushlstring ( L, s, methodEnd ); // method text
      lua_pushlstring ( L, s + methodEnd + 1, uriEnd - methodEnd - 1 ); // uri
      lua_pushlstring ( L, s + uriEnd + 1, firstLineEnd - uriEnd - 1 ); // proto

      if ( method == 1 && uriEnd - methodEnd - 1 > 8 && internal_is_static_path ( s + methodEnd + 1 ) ) { // method == GET && path starts with "/static/"
        lua_pushlstring ( L, s + methodEnd + 2, uriEnd - methodEnd - 2 ); // file
        return 5;
      } else {
        lua_pushnil ( L ); // file
      }

      queryPos = httpd_findTextPos ( s, "?" );
    
      if ( queryPos > methodEnd && queryPos < uriEnd ) {
        lua_pushlstring ( L, s + methodEnd + 1, queryPos - methodEnd - 1 ); // path
      } else {
        lua_pushlstring ( L, s + methodEnd + 1, uriEnd - methodEnd - 1 ); // path
      }
      
      if ( method == 1 && queryPos > methodEnd && queryPos < uriEnd ) {
        lua_pushstring ( L, "get" ); // data type
        internal_request_query ( L, s + queryPos + 1, uriEnd - queryPos - 1 ); // data
      
      } else if ( method == 2 ){
      
        headerEnd    = httpd_findTextPos ( s, "\r\n\r\n" );
        contentForm  = httpd_findTextPos ( s, "Content-Type: application/x-www-form-urlencoded\r\n" );
        contentJson  = httpd_findTextPos ( s, "Content-Type: application/json\r\n" );
        contentText  = httpd_findTextPos ( s, "Content-Type: text/" );

        if ( contentForm > firstLineEnd && contentForm < headerEnd ){
          lua_pushstring ( L, "form" ); // data type
          internal_request_query ( L, s + headerEnd + 4, length - headerEnd - 4 ); // form data

        } else if ( contentJson > firstLineEnd && contentJson < headerEnd ){
          lua_pushstring ( L, "json" ); // data type
          lua_pushlstring ( L, s + headerEnd + 4, length - headerEnd - 4 ); // json data
        } else if ( contentText > firstLineEnd && contentText < headerEnd ){
          lua_pushstring ( L, "text" ); // data type
          lua_pushlstring ( L, s + headerEnd + 4, length - headerEnd - 4 ); // text data
        } else {
          lua_pushstring ( L, "nil" ); // data type
          lua_pushnil(L); // data - empty
        }
      } else {
          lua_pushstring ( L, "nil" ); // data type
          lua_pushnil(L); // data - empty
      }
//       if ( headerEnd > firstLineEnd ){
//         internal_request_header ( L, s + firstLineEnd + 2, headerEnd - firstLineEnd - 2 ); // header
//       
//       } else {
//         lua_createtable ( L, 0, 0 ); // header
//       
//       }
      return 8;
    }
  }
  lua_pushnil ( L );
  return 1;
}


// -----------------------------------------------------------------------------
// --- httpd.uri ---------------------------------------------------------------
// -----------------------------------------------------------------------------

static int httpd_uri_encode ( lua_State *L )
{
  size_t length;
  size_t index;
  luaL_Buffer b;
  char chr;
  const char *s = luaL_checklstring ( L, 1, &length );
  luaL_buffinit ( L, &b );

  for ( index = 0; index < length; index++ ){
    chr = s[index];
    if ( isalnum( chr ) )
    {
      luaL_addchar ( &b, chr );
    } 
    else 
    {
      luaL_addchar ( &b, '%' );
      luaL_addchar ( &b, httpd_hexbytes[ ( chr >> 4 ) & 0x0f ] );
      luaL_addchar ( &b, httpd_hexbytes[ chr & 0x0f ] );
    }
  }

  luaL_pushresult ( &b );
  return 1;
}

static int httpd_uri_decode ( lua_State *L )
{
  size_t length;
  const char *s = lua_tolstring ( L, 1, &length );
  internal_uri_decode ( L, s, length );
  return 1;
}


// -----------------------------------------------------------------------------
// --- httpd.response ----------------------------------------------------------
// -----------------------------------------------------------------------------

static int httpd_response ( lua_State *L )
{
  size_t ctLength;
  size_t bodyLength;
  char bodyLengthStr [15];
  const char *k;
  const char *v;
  size_t kl;
  size_t vl;
  luaL_Buffer b;
  luaL_buffinit ( L, &b );
  const char *contentType;
  const char *body;

  int statusCode = lua_tonumber ( L, 1 );

  httpd_luaL_addlstring ( &b, "HTTP/1.1 ", 9 );
  switch ( statusCode ){
    case 200: httpd_luaL_addlstring ( &b, "200 OK", 6                     ); break;
    case 204: httpd_luaL_addlstring ( &b, "204 No Content", 14            ); break;
    case 302: httpd_luaL_addlstring ( &b, "302 Found", 9                  ); break;
    case 304: httpd_luaL_addlstring ( &b, "304 Not Modified", 16          ); break;
    case 400: httpd_luaL_addlstring ( &b, "400 Bad Request", 15           ); break;
    case 401: httpd_luaL_addlstring ( &b, "401 Unauthorized", 16          ); break;
    case 403: httpd_luaL_addlstring ( &b, "403 Forbidden", 13             ); break;
    case 404: httpd_luaL_addlstring ( &b, "404 Not Found", 13             ); break;
    case 500: httpd_luaL_addlstring ( &b, "500 Internal Server Error", 25 ); break;
    case 501: httpd_luaL_addlstring ( &b, "501 Not Implemented", 19       ); break;
    case 503: httpd_luaL_addlstring ( &b, "503 Service Unavailable", 23   ); break;
    default:  httpd_luaL_addlstring ( &b, "503 Service Unavailable", 23   ); break;
  }
  httpd_luaL_addlstring ( &b, "\r\n", 2 );
  
  if ( lua_gettop(L) == 1 ){
    httpd_luaL_addlstring ( &b, "\r\n", 2 );
    luaL_pushresult ( &b );
    return 1;
  }
  
  contentType = luaL_checklstring ( L, 2, &ctLength );
  body        = luaL_checklstring ( L, 4, &bodyLength );
  
  if ( contentType ) {
    httpd_luaL_addlstring ( &b, "Content-Type: ", 14 );
    httpd_luaL_addlstring ( &b, contentType, ctLength );
    httpd_luaL_addlstring ( &b, "\r\n", 2 );
  }
  
  switch ( lua_type ( L, 3 ) ){
    case LUA_TTABLE:
      lua_pushnil(L);  /* first key */
      while ( lua_next ( L, 3 ) ) {
        k = lua_tolstring( L, -2, &kl );
        v = lua_tolstring( L, -1, &vl );
        if ( k && v ) {
          httpd_luaL_addlstring ( &b, k, kl );
          httpd_luaL_addlstring ( &b, ": ", 2 );
          httpd_luaL_addlstring ( &b, v, vl );
          httpd_luaL_addlstring ( &b, "\r\n", 2 );
        }
        lua_pop(L, 1);
      }
      break;
    default:
      v = lua_tolstring( L, 3, &vl );
      if ( v > 0 ){
        httpd_luaL_addlstring ( &b, v, vl );
      };
      break;
  }
  
  if ( body ) {
    c_sprintf ( bodyLengthStr, "%d", bodyLength );
    httpd_luaL_addlstring ( &b, "Content-Length: ", 16 );
    httpd_luaL_addstring  ( &b, bodyLengthStr );
    httpd_luaL_addlstring ( &b, "\r\n\r\n", 4 );
    httpd_luaL_addlstring ( &b, body, bodyLength );
  } else {
    httpd_luaL_addlstring ( &b, "\r\n", 2 );
  }
  luaL_pushresult ( &b );
  return 1;
 
}

// -----------------------------------------------------------------------------
// --- httpd.file --------------------------------------------------------------
// -----------------------------------------------------------------------------

static int httpd_file_size ( lua_State *L )
{
  size_t length;
  vfs_dir  *dir;
  vfs_item *item;
  int fileSize = 0;
  const char *s = lua_tolstring( L, 1, &length );
  
  if ( s ){
    if ( dir = vfs_opendir("") ) {
      while ( item = vfs_readdir(dir) ) {
        if ( c_strcmp ( s, vfs_item_name(item) ) == 0 ){
          fileSize = vfs_item_size(item);
          vfs_closeitem(item);
          break;
        }        
        vfs_closeitem(item);
      }
      vfs_closedir(dir);
    }
  }
  lua_pushinteger ( L, fileSize );
  return 1;
}

static int httpd_file_mime ( lua_State *L )
{
  size_t length;
  size_t index;
  size_t dotPos = 0;
  size_t i;
  size_t f = 99;
  
  const char *s = lua_tolstring( L, 1, &length );
  
  if ( s ){
    for ( index = 0; index < length - 1; index++ ){
      if ( s[index] == '.' ){
        dotPos = index;
      }
    }
    if ( dotPos > 0 ){
      for ( i = 0; i < 15; i++ ) {
        if ( c_strcmp ( httpd_mime_ext[i], s + dotPos + 1 ) == 0 ){
          f = i;
          break;
        }
      }
    }
  }
  if ( f != 99 ){
    lua_pushstring ( L, httpd_mime_string[ f ] );
  } else {
    lua_pushstring ( L, "application/octet-stream" );
  };
  return 1;

}

static int httpd_mime ( lua_State *L )
{
  size_t length;
  size_t i;
  size_t f = 99;
  const char *s = lua_tolstring( L, 1, &length );
  
  if ( s ){
    for ( i = 0; i < 15; i++ ) {
      if ( c_strcmp ( httpd_mime_ext[i], s ) == 0 ){
        f = i;
        break;
      }
    }
  }
  if ( f != 99 ){
    lua_pushstring ( L, httpd_mime_string[ f ] );
  } else {
    lua_pushstring ( L, "application/octet-stream" );
  };
  return 1;  
}

// -----------------------------------------------------------------------------

// #include "vfs.h"
// static int file_fd = 0;
// 
// int static httpd_serve_file ( const char *fs, size_t fl )
// {
// 
//   static int file_fd;
//   luaL_Buffer b;
//   luaL_buffinit(L, &b);
// 
//   char *fname = (char *)c_malloc(fl + 1); 
//   c_memcpy( filename, fs, fl );
// 
//   const char *basename = vfs_basename( fname );
//   luaL_argcheck(L, c_strlen(basename) <= FS_OBJ_NAME_LEN && c_strlen(fname) == fl, 1, "filename invalid");
//  
//   file_fd = vfs_open(fname, "r");
//  
//   if(file_fd){
//     char *p = luaL_prepbuffer(&b);
//     n = vfs_read ( file_fd, p, LUAL_BUFFERSIZE);
//  
//  //     luaL_pushresult(&b);  /* close buffer */
//  //     return (lua_objlen(L, -1) > 0);  /* check whether read something */
//      
//     vfs_close(file_fd);
//   }
// 
//   luaL_pushresult(&b);
// 
// 
// }


// int static httpd_httpd ( lua_State *L )
// {
//   size_t length;
//   size_t firstLineEnd;
//   size_t methodEnd;
//   size_t uriEnd;
//   size_t method;
//   
//   const char *s = lua_tolstring( L, 1, &length );
//   if ( s ) {
//     firstLineEnd = httpd_findTextPos ( s, "\r\n" );
//     methodEnd    = httpd_findTextPos ( s, " " );
//     uriEnd       = httpd_findTextPos ( s, " HTTP" );
// 
//     if ( firstLineEnd > 0 && methodEnd > 2 && uriEnd > 0 && uriEnd < firstLineEnd ) {
//       method = internal_enum_method ( s, methodEnd );
//       if ( method == 1 && uriEnd - methodEnd - 1 > 8 && internal_is_static_path ( s + methodEnd + 1 ) ) { // method == GET && path starts with "/static/"
//         
// 
// //         lua_pushlstring ( L, s + methodEnd + 2, uriEnd - methodEnd - 2 ); // file
//       }
//     }
//   
//   
//   }
//   
// //   if (lua_type(L, stack) == LUA_TFUNCTION || lua_type(L, stack) == LUA_TLIGHTFUNCTION){
// //     lua_pushvalue(L, stack);  // copy argument (func) to the top of stack
// //     lua_pushvalue(L, -2);  // copy the self_ref(userdata) to the top
// //     lua_call(L, 1, 0);
// //   }
// 
// 
// 
// }


static const LUA_REG_TYPE httpd_uri_map[] = {
  { LSTRKEY( "decode" ),  LFUNCVAL( httpd_uri_decode ) },
  { LSTRKEY( "encode" ),  LFUNCVAL( httpd_uri_encode ) },
  { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE httpd_file_map[] = {
  { LSTRKEY( "size" ),  LFUNCVAL( httpd_file_size ) },
  { LSTRKEY( "mime" ),  LFUNCVAL( httpd_file_mime ) },
  { LNILKEY, LNILVAL }
};

static const LUA_REG_TYPE httpd_map[] = {
  { LSTRKEY( "httpd"  ), LFUNCVAL( httpd_httpd ) },
  { LSTRKEY( "request"  ), LFUNCVAL( httpd_request ) },
  { LSTRKEY( "response"  ), LFUNCVAL( httpd_response ) },
//   { LSTRKEY( "response" ), LROVAL( httpd_response_map ) },
  { LSTRKEY( "mime"     ), LFUNCVAL( httpd_mime ) },
  { LSTRKEY( "uri"      ), LROVAL( httpd_uri_map ) },
  { LSTRKEY( "file"     ), LROVAL( httpd_file_map ) },
//   { LSTRKEY( "html" ), LROVAL( httpd_html_map ) },
  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(HTTPD, "httpd", httpd_map, NULL);

