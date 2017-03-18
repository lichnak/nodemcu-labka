// Module for interfacing with the OneWire interface

#include <string.h>
#include "module.h"
#include "lauxlib.h"


// Lua: utils.test( )
static int utils_test( lua_State *L )
{
  lua_pushstring(L, "hello world");
  return 1;
}

static int utils_http_request_line ( lua_State *L )
{

  const char * space[2] = " ";
  char * line;
  char * method;
  char * uri;
  char * ident;

  char * request = luaL_checkstring( L, 1 );
  line = strtok ( request, "\r\n" );
  if ( line != NULL ){
    method = strtok ( line, space );
    if ( method != NULL ){
      uri = strtok ( NULL, space );
      if ( uri != NULL ){
        ident = strtok ( NULL, space );
        if ( ident != NULL ){
          lua_pushstring(L, method);
          lua_pushstring(L, uri);
          lua_pushstring(L, ident);
          return 3;
        }
      }
    }
  }
  lua_pushnil(L);
  return 1;
}


// Module function map
static const LUA_REG_TYPE util_map[] = {
  { LSTRKEY( "test" ), LFUNCVAL( utils_test ) },
  { LSTRKEY( "http_request_line" ), LFUNCVAL( utils_http_request_line ) },
  { LNILKEY, LNILVAL }
};

NODEMCU_MODULE(UTILS, "utils", util_map, NULL);
