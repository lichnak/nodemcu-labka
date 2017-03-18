// 

#include "module.h"
#include "lauxlib.h"
#include "luaconf.h" // buffer size
#include "c_string.h"
#include "vfs.h"

#define hexchar_to_dec(c) ((c) & 0x40 ? ((c) & 0x0F) + 9 : ((c) & 0x0F)) 

const char utils_hexbytes[] = "0123456789abcdef";

static const char EMPTYSTRING[] = "";
static const char EOL[]         = "\r\n";
static const char EOL2[]        = "\r\n\r\n";

#define iff(c,r1,r2) ((c)?(r1):(r2))

static int utils_findTextPos ( const char *s, const char *f ) {
  if ( s && f ) {
    char *p = strstr ( s, f );
    if ( p ) {
      return p - s;
    }
  } 
  return 0;
}

#define utils_luaL_addchar(B,c) \
  ((void)((B)->p + 1 < ((B)->buffer+LUAL_BUFFERSIZE)), \
   (*(B)->p++ = (char)(c)))

#define utils_luaL_add2chars(B,c1,c2) \
  ((void)((B)->p + 1 < ((B)->buffer+LUAL_BUFFERSIZE) || luaL_prepbuffer(B)), \
   (*(B)->p++ = (char)(c1)), (*(B)->p++ = (char)(c2)))

#define utils_luaL_add3chars(B,c1,c2,c3) \
  ((void)((B)->p + 1 < ((B)->buffer+LUAL_BUFFERSIZE) || luaL_prepbuffer(B)), \
   (*(B)->p++ = (char)(c1)), (*(B)->p++ = (char)(c2)), (*(B)->p++ = (char)(c3)))

void utils_luaL_addlstring (luaL_Buffer *B, const char *s, size_t l) {
  if ( (B)->p + l < ((B)->buffer + LUAL_BUFFERSIZE) || luaL_prepbuffer(B) ){  
    while ( l-- ){
      *(B)->p++ = (char)*s++;
    }
  }
}

void utils_luaL_addstring (luaL_Buffer *B, const char *s) {
  utils_luaL_addlstring(B, s, c_strlen(s));
}


// -----------------------------------------------------------------------------
// --- utils.uri ---------------------------------------------------------------
// -----------------------------------------------------------------------------

static int utils_uri_encode ( lua_State *L )
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
      luaL_addchar ( &b, utils_hexbytes[ ( chr >> 4 ) & 0x0f ] );
      luaL_addchar ( &b, utils_hexbytes[ chr & 0x0f ] );
    }
  }

  luaL_pushresult ( &b );
  return 1;
}

void internal_uri_decode ( lua_State *L, const char *s, size_t length ){

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
        if ( chr > 0 ){
          utils_luaL_addchar ( &b, chr );
        }
        index += 2;
      } 
      else if ( chr == '+' )
      {
        utils_luaL_addchar ( &b, ' ' );
      }
      else
      {
        utils_luaL_addchar ( &b, chr );
      }
    }
  }
  luaL_pushresult( &b );
}

static int utils_uri_decode ( lua_State *L )
{
  size_t length;
  const char *s = lua_tolstring ( L, 1, &length );
  internal_uri_decode ( L, s, length );
  return 1;
}

// -----------------------------------------------------------------------------
// --- utils.request -----------------------------------------------------------
// -----------------------------------------------------------------------------

static int utils_request_query ( lua_State *L )
{
  size_t length;
  size_t index;
  size_t varPos = 0;
  size_t sepPos = 0;
  char chr;
  const char *s = lua_tolstring( L, 1, &length );
  lua_createtable( L, 0, 0 );
  if ( s ) {
    for ( index = 0; index < length; index++ ){
      chr = s[index];
      if ( chr == '=' ){
        sepPos = index;
      } else if ( chr == '&' ){
        if ( varPos + 1 < sepPos && sepPos < index){
          lua_pushlstring     ( L, s + varPos, sepPos - varPos );
          internal_uri_decode ( L, s + sepPos + 1, index - sepPos - 1 );
          lua_settable        ( L, -3 );
        }
        varPos = index + 1;
      }
    }
    if ( varPos + 1 < sepPos && sepPos < index){
      lua_pushlstring     ( L, s + varPos, sepPos - varPos );
      internal_uri_decode ( L, s + sepPos + 1, index - sepPos - 1 );
      lua_settable        ( L, -3 );
    }
  }
  return 1;
}

static int utils_request_split ( lua_State *L )
{
  size_t length;
  size_t firstLineEnd;
  size_t headerEnd;
  const char *s = lua_tolstring( L, 1, &length );
  if ( s ) {
    firstLineEnd = utils_findTextPos ( s, EOL );
    headerEnd    = utils_findTextPos ( s, EOL2 );
    iff ( 
      firstLineEnd > 0,
      lua_pushlstring ( L, s, firstLineEnd ),
      lua_pushnil     ( L )
    );
    iff (
      firstLineEnd > 0 && headerEnd > 0,
      lua_pushlstring ( L, s + firstLineEnd + 2, headerEnd - firstLineEnd - 2 ),
      lua_pushlstring ( L, EMPTYSTRING, 0 )
    );
    iff (
      headerEnd > 0 && headerEnd < length - 4,
      lua_pushlstring ( L, s + headerEnd + 4, length - headerEnd - 4 ),
      lua_pushlstring ( L, EMPTYSTRING, 0 )
    );
    return 3;
  }
  lua_pushnil ( L );
  return 1;
}

static int utils_request_get ( lua_State *L )
{
  size_t length;
  size_t methodEnd;
  size_t uriEnd;
  const char *s = lua_tolstring( L, 1, &length );
  if ( s && length > 12 ) {
    methodEnd   = utils_findTextPos ( s, " " );
    uriEnd      = utils_findTextPos ( s, " HTTP" );
    if ( methodEnd > 0 && uriEnd > methodEnd + 1 && uriEnd + 6 < length ){
      lua_pushlstring ( L, s, methodEnd );
      lua_pushlstring ( L, s + methodEnd + 1, uriEnd - methodEnd - 1 );
      lua_pushlstring ( L, s + uriEnd + 1, length - uriEnd - 1 );
      return 3;
    }    
  }
  lua_pushnil ( L );
  return 1;
}

static int utils_request_uri ( lua_State *L )
{
  size_t length;
  size_t queryPos;
  size_t fragmentPos;
  const char *s = lua_tolstring( L, 1, &length );
  if ( s ) {
    queryPos    = utils_findTextPos ( s, "?" );
    fragmentPos = utils_findTextPos ( s, "#" );
    if ( queryPos > 0 ){
      if ( fragmentPos > 0 && fragmentPos > queryPos && fragmentPos + 1 < length ){
        lua_pushlstring ( L, s, queryPos );
        lua_pushlstring ( L, s + queryPos + 1, fragmentPos - queryPos - 1 );
        lua_pushlstring ( L, s + fragmentPos + 1, length - fragmentPos - 1 );
      } else {
        lua_pushlstring ( L, s, queryPos );
        lua_pushlstring ( L, s + queryPos + 1, length - queryPos - 1 );
        lua_pushlstring ( L, EMPTYSTRING, 0 );
      }
    } else {
      if ( fragmentPos > 0 ){
        lua_pushlstring ( L, s, fragmentPos );
        lua_pushlstring ( L, EMPTYSTRING, 0 );
        lua_pushlstring ( L, s + fragmentPos + 1, length - fragmentPos - 1 );
      } else {
        lua_pushlstring ( L, s, length );
        lua_pushlstring ( L, EMPTYSTRING, 0 );
        lua_pushlstring ( L, EMPTYSTRING, 0 );
      }
    }
    return 3;
  }
  lua_pushnil ( L );
  return 1;
}

static int utils_request_header ( lua_State *L )
{
  size_t length;
  size_t index;
  size_t lineS       = 0;
  size_t lineE       = 0;
  size_t lineV       = 0;
  size_t lineVS      = 0;
  char chr;
  const char *s = lua_tolstring( L, 1, &length );
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
  return 1;
}

// -----------------------------------------------------------------------------
// --- utils.response ----------------------------------------------------------
// -----------------------------------------------------------------------------
   
static int utils_response_status ( lua_State *L )
{
  int statusCode = lua_tonumber ( L, 1 );
  switch ( statusCode ){
    case 200: lua_pushstring ( L, "HTTP/1.1 200 OK\r\n"                    ); break;
    case 204: lua_pushstring ( L, "HTTP/1.1 204 No Content\r\n"            ); break;
    case 302: lua_pushstring ( L, "HTTP/1.1 302 Found\r\n"                 ); break;
    case 304: lua_pushstring ( L, "HTTP/1.1 304 Not Modified\r\n"          ); break;
    case 400: lua_pushstring ( L, "HTTP/1.1 400 Bad Request\r\n"           ); break;
    case 401: lua_pushstring ( L, "HTTP/1.1 401 Unauthorized\r\n"          ); break;
    case 403: lua_pushstring ( L, "HTTP/1.1 403 Forbidden\r\n"             ); break;
    case 404: lua_pushstring ( L, "HTTP/1.1 404 Not Found\r\n"             ); break;
    case 500: lua_pushstring ( L, "HTTP/1.1 500 Internal Server Error\r\n" ); break;
    case 501: lua_pushstring ( L, "HTTP/1.1 501 Not Implemented\r\n"       ); break;
    case 503: lua_pushstring ( L, "HTTP/1.1 503 Service Unavailable\r\n"   ); break;
    default:  lua_pushstring ( L, "HTTP/1.1 503 Service Unavailable\r\n"   ); break;
  }
  return 1;  
}

static int utils_response_header_line ( lua_State *L )
{
  size_t lengthS;
  size_t lengthV;
  const char *s = lua_tolstring( L, 1, &lengthS );
  const char *v = lua_tolstring( L, 2, &lengthV );
  if ( s && v ){
    luaL_Buffer b;
    luaL_buffinit ( L, &b );
    
    utils_luaL_addlstring ( &b, s, lengthS );
    utils_luaL_addlstring ( &b, ": ", 2 );
    utils_luaL_addlstring ( &b, v, lengthV );
    utils_luaL_addlstring ( &b, EOL, 2 );
    
//     luaL_addlstring ( &b, s, lengthS );
//     luaL_addstring ( &b, ": " );
//     luaL_addlstring ( &b, v, lengthV );
//     luaL_addstring ( &b, "\r\n" );
    luaL_pushresult( &b );
  } else {
    lua_pushlstring ( L, EMPTYSTRING, 0 );
  }
  return 1;  
}

static int utils_response_header ( lua_State *L )
{
  luaL_Buffer b;
  luaL_buffinit ( L, &b );
  const char *k;
  const char *v;
  size_t kl;
  size_t vl;
  
  switch ( lua_type ( L, 1 ) ){
    case LUA_TTABLE:
      lua_pushnil(L);  /* first key */
      while ( lua_next ( L, 1 ) ) {
        k = lua_tolstring( L, -2, &kl );
        v = lua_tolstring( L, -1, &vl );
        if ( k && v ) {
          utils_luaL_addlstring ( &b, k, kl );
          utils_luaL_addlstring ( &b, ": ", 2 );
          utils_luaL_addlstring ( &b, v, vl );
          utils_luaL_addlstring ( &b, EOL, 2 );
          
//           luaL_addlstring ( &b, k, kl );
//           luaL_addstring  ( &b, ": " );
//           luaL_addlstring ( &b, v, vl );
//           luaL_addstring  ( &b, "\r\n" );
        }
        lua_pop(L, 1);
      }
      luaL_pushresult( &b );
      break;
    default:
      v = lua_tolstring( L, 1, &vl );
      iff ( 
        v > 0,
        lua_pushlstring ( L, v, vl ),
        lua_pushlstring ( L, EMPTYSTRING, 0 )
      );
      break;
  }
  return 1;
}
  
// -----------------------------------------------------------------------------
// ---  --------------------------------------------------
// -----------------------------------------------------------------------------

    /* ORDER RESERVED */
const char *const utilsMimeExt [] = {
    "js", "css", "gif", "htm", "jpg",
    "pdf", "png", "svg", "txt", "xml",
    "zip", "html", "jpeg", "json", "text", 
    NULL
};

const char *const utilsMime [] = {
    "application/javascript", "text/css", "image/gif", "text/html", "image/jpeg",
    "application/pdf", "image/png", "image/svg+xml", "text/plain", "application/xml",
    "application/zip", "text/html", "image/jpeg", "application/json", "text/plain",
    NULL
};

static int utils_mime ( lua_State *L )
{
  size_t length;
  size_t i;
  size_t f = 99;
  const char *s = lua_tolstring( L, 1, &length );
  
  if ( s ){
    for ( i = 0; i < 15; i++ ) {
      if ( c_strcmp ( utilsMimeExt[i], s ) == 0 ){
        f = i;
        break;
      }
    }
  }
  iff (
    f != 99,
    lua_pushstring ( L, utilsMime[ f ] ),
    lua_pushstring ( L, "application/octet-stream" )
  );
  return 1;  
}

// -----------------------------------------------------------------------------
// --- utils.file --------------------------------------------------------------
// -----------------------------------------------------------------------------

static int utils_file_size ( lua_State *L )
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

static int utils_file_mime ( lua_State *L )
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
        if ( c_strcmp ( utilsMimeExt[i], s + dotPos + 1 ) == 0 ){
          f = i;
          break;
        }
      }
    }
  }
  iff (
    f != 99,
    lua_pushstring ( L, utilsMime[ f ] ),
    lua_pushstring ( L, "application/octet-stream" )
  );
  return 1;

}


// -----------------------------------------------------------------------------
// --- utils.html --------------------------------------------------------------
// -----------------------------------------------------------------------------



// -----------------------------------------------------------------------------
// --- register lua functions --------------------------------------------------
// -----------------------------------------------------------------------------

static int utils_dev( lua_State *L )
{
  size_t length;

  const char *s = lua_tolstring( L, 1, &length );
 
  lua_pushnumber ( L, utils_findTextPos ( s, "ahoj" ) );
  return 1;
}

// Lua: utils.test( )
static int utils_test( lua_State *L )
{
  lua_pushstring(L, "hello world");
  return 1;
}

static const LUA_REG_TYPE utils_request_map[] = {
  { LSTRKEY( "split" ),    LFUNCVAL( utils_request_split ) },
  { LSTRKEY( "get" ),      LFUNCVAL( utils_request_get ) },
  { LSTRKEY( "uri" ),      LFUNCVAL( utils_request_uri ) },
  { LSTRKEY( "header" ),   LFUNCVAL( utils_request_header ) },
  { LSTRKEY( "query" ),    LFUNCVAL( utils_request_query ) },
  { LNILKEY, LNILVAL }
};
static const LUA_REG_TYPE utils_response_map[] = {
  { LSTRKEY( "status" ),    LFUNCVAL( utils_response_status ) },
  { LSTRKEY( "header" ),    LFUNCVAL( utils_response_header ) },
  { LSTRKEY( "header_line" ),    LFUNCVAL( utils_response_header_line ) },
  { LNILKEY, LNILVAL }
};
static const LUA_REG_TYPE utils_uri_map[] = {
  { LSTRKEY( "decode" ),  LFUNCVAL( utils_uri_decode ) },
  { LSTRKEY( "encode" ),  LFUNCVAL( utils_uri_encode ) },
  { LNILKEY, LNILVAL }
};
static const LUA_REG_TYPE utils_file_map[] = {
  { LSTRKEY( "size" ),  LFUNCVAL( utils_file_size ) },
  { LSTRKEY( "mime" ),  LFUNCVAL( utils_file_mime ) },
  { LNILKEY, LNILVAL }
};
// static const LUA_REG_TYPE utils_html_map[] = {
//   { LSTRKEY( "entity" ),  LFUNCVAL( utils_html_entities_decode ) },
//   { LNILKEY, LNILVAL }
// };

// Module function map
static const LUA_REG_TYPE util_map[] = {
  { LSTRKEY( "dev" ), LFUNCVAL( utils_dev ) },
  { LSTRKEY( "test" ), LFUNCVAL( utils_test ) },
  
  { LSTRKEY( "mime" ), LFUNCVAL( utils_mime ) },
  
  { LSTRKEY( "request" ), LROVAL( utils_request_map ) },
  { LSTRKEY( "response" ), LROVAL( utils_response_map ) },
  { LSTRKEY( "uri" ), LROVAL( utils_uri_map ) },
  { LSTRKEY( "file" ), LROVAL( utils_file_map ) },
//   { LSTRKEY( "html" ), LROVAL( utils_html_map ) },
  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(UTILS, "utils", util_map, NULL);
