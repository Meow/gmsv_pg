#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaValue.h>
#include <GarrysMod/Lua/LuaObject.h>
#include <GarrysMod/Lua/LuaEvent.h>
#include <pqxx/pqxx>
#include <chrono>
#include <thread>

#define STRING_GETTER(mn, fn) static int fn(lua_State *state) { \
  auto obj = Pop(state, 1);                                     \
  return LuaValue::Push(state, obj->mn);                        \
}

#define CHECK_CONNECTION auto obj = Pop(state, 1); \
if (obj->_connection == nullptr) {\
LUA->ThrowError("pg - no connection, connect to a database first.\n");\
return 0;\
}

#define LUA_METHOD(name) static int name(lua_State* state)

// copy-pasted from pg_type.h
#define  TYPTYPE_BASE		'b' /* base type (ordinary scalar type) */
#define  TYPTYPE_COMPOSITE	'c' /* composite (e.g., table's rowtype) */
#define  TYPTYPE_DOMAIN		'd' /* domain over another type */
#define  TYPTYPE_ENUM		'e' /* enumerated type */
#define  TYPTYPE_PSEUDO		'p' /* pseudo-type */
#define  TYPTYPE_RANGE		'r' /* range type */

#define  TYPCATEGORY_INVALID	'\0'	/* not an allowed category */
#define  TYPCATEGORY_ARRAY		'A'
#define  TYPCATEGORY_BOOLEAN	'B'
#define  TYPCATEGORY_COMPOSITE	'C'
#define  TYPCATEGORY_DATETIME	'D'
#define  TYPCATEGORY_ENUM		'E'
#define  TYPCATEGORY_GEOMETRIC	'G'
#define  TYPCATEGORY_NETWORK	'I'		/* think INET */
#define  TYPCATEGORY_NUMERIC	'N'
#define  TYPCATEGORY_PSEUDOTYPE 'P'
#define  TYPCATEGORY_RANGE		'R'
#define  TYPCATEGORY_STRING		'S'
#define  TYPCATEGORY_TIMESPAN	'T'
#define  TYPCATEGORY_USER		'U'
#define  TYPCATEGORY_BITSTRING	'V'		/* er ... "varbit"? */
#define  TYPCATEGORY_UNKNOWN	'X'