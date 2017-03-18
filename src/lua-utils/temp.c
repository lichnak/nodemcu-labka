// Zakladni a doplnkove C funkce
#include <stdio.h>
#include <stdlib.h>
 
// Zakladni a doplnkove funkce interpretu jazyka Lua
#include <lualib.h>
#include <lauxlib.h>
lua_State* L = luaL_newstate();

static int utils_split( lua_State *L) //fce split ma vstupni stav L, ktery je stavem VM objektu
{
  int length;
  const char * text = luaL_checklstring( L, 1, &length ); // vemze bufer L a narg index 1 prevedeho na string
  char * firstLineEnd = strstr( text, "\r\n"); // ulozi adresu prvniho vyskytu
  
  if ( firstLineEnd !== NULL ){  //pokud eni prazdny pak ...
      lua_pushlstring ( L, text, *firstLineEnd - *text );//pushni prvni radku zpatky do bufferu L
  } else { // ... jinak
      lua_pushnil ( L ); //pushni NULL do bufferu L
  }
  return 1;
} //konec metody

int main()
{
    int result;
    lua_State* L = luaL_newstate();
    const char * TEXT =
    "-- Demonstracni priklad: pouziti uzaveru\r\n"\
    " ktere nemusi byt zrovna jasne\n";
    result = utils_split(TEXT);
    puts(L);
    lua_close(L);
    if (result != 0)
    {
        printf("Chyba # %d\n", result);
    }
    return (result != 0);
}
  
/* 
LUALIB_API const char *luaL_checklstring (lua_State *L, int narg, size_t *len) {
  const char *s = lua_tolstring(L, narg, len);
  //Converts the Lua value at the given acceptable index to a C string.
  //If len is not NULL, it also sets *len with the string length.
  //The Lua value must be a string or a number; otherwise, the function returns NULL.
  //If the value is a number, then lua_tolstring also changes the actual value in the stack to a string.
  if (!s) tag_error(L, narg, LUA_TSTRING);
  return s;
}
*/