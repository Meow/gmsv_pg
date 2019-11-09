#ifndef _PG_LUA_SHARED_H
#define _PG_LUA_SHARED_H

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#endif

#include <gmodc/lua/interface.h>

#define GMOD_FUNC_WITH_STATE(FUNC)               \
  int FUNC##__Imp(lua_State *L, luabase_t *LUA); \
  int FUNC(lua_State *L)                         \
  {                                              \
    luabase_t *LUA = lua_get_base(L);            \
    lua_set_state(LUA, L);                       \
    return FUNC##__Imp(L, LUA);                  \
  }                                              \
  int FUNC##__Imp(lua_State *L, luabase_t *LUA)

/*
  Most of the definitions are taken from LuaJIT's source code.
  Some definitions were unavailable and hence are omitted here.

  The only definitions present are the ones lua_shared actually exports.
  This may not be up to par with what LuaJIT's API truly provides.
*/

#define LUA_IDSIZE	60	/* Size of lua_Debug.short_src. */

struct lua_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) `global', `local', `field', `method' */
  const char *what;	/* (S) `Lua', `C', `main', `tail' */
  const char *source;	/* (S) */
  int currentline;	/* (l) */
  int nups;		/* (u) number of upvalues */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  char short_src[LUA_IDSIZE]; /* (S) */
  /* private part */
  int i_ci;  /* active function */
};

typedef int (*lua_CFunction) (lua_State *L);
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);
typedef const char * (*lua_Reader) (lua_State *L, void *ud, size_t *sz);
typedef int (*lua_Writer) (lua_State *L, const void* p, size_t sz, void* ud);
typedef struct lua_Debug lua_Debug;  /* activation record */
typedef void (*lua_Hook) (lua_State *L, lua_Debug *ar);

typedef struct luaL_Buffer {
  char *p;			/* current position in buffer */
  int lvl;  /* number of strings in the stack (level) */
  lua_State *L;
  char buffer[8192];
} luaL_Buffer;

typedef struct luaL_Reg {
  const char *name;
  lua_CFunction func;
} luaL_Reg;

#ifdef _WIN32
typedef HMODULE module_handle;
#define GET_MOD_FUNC(what) what##_u.symbol = GetProcAddress(lua_shared, #what); \
  what = what##_u.function;
#else
typedef void* module_handle;
#define GET_MOD_FUNC(what) what##_u.symbol = dlsym(lua_shared, #what); \
  what = what##_u.function;
#endif

#define LUA_SHARED_FUNC(type, name, ...)  \
typedef type( *name##_t )( __VA_ARGS__ ); \
union { \
  void *symbol; \
  name##_t function; \
} name##_u; \
extern name##_t name

LUA_SHARED_FUNC(lua_State*, luaL_newstate, void);
LUA_SHARED_FUNC(int, luaL_loadstring, lua_State *L, const char* code);
LUA_SHARED_FUNC(lua_CFunction, lua_atpanic, lua_State *L, lua_CFunction panicf);
LUA_SHARED_FUNC(void, lua_call, lua_State *L, int nargs, int nresults);
LUA_SHARED_FUNC(int, lua_checkstack, lua_State *L, int size);
LUA_SHARED_FUNC(void, lua_close, lua_State *L);
LUA_SHARED_FUNC(void, lua_concat, lua_State*L,int n);
LUA_SHARED_FUNC(int, lua_cpcall, lua_State *L, lua_CFunction func, void *ud);
LUA_SHARED_FUNC(void, lua_createtable, lua_State *L, int narray, int nrec);
LUA_SHARED_FUNC(int, lua_dump, lua_State *L, lua_Writer writer, void *data);
LUA_SHARED_FUNC(int, lua_equal, lua_State *L, int idx1, int idx2);
LUA_SHARED_FUNC(int, lua_error, lua_State *L);
LUA_SHARED_FUNC(int, lua_gc, lua_State *L, int what, int data);
LUA_SHARED_FUNC(lua_Alloc, lua_getallocf, lua_State *L, void **ud);
LUA_SHARED_FUNC(void, lua_getfenv, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_getfield, lua_State *L, int idx, const char *k);
LUA_SHARED_FUNC(lua_Hook, lua_gethook, lua_State *L);
LUA_SHARED_FUNC(int, lua_gethookcount, lua_State *L);
LUA_SHARED_FUNC(int, lua_gethookmask, lua_State *L);
LUA_SHARED_FUNC(int, lua_getinfo, lua_State *L, const char *what, lua_Debug *ar);
LUA_SHARED_FUNC(const char*, lua_getlocal, lua_State *L, const lua_Debug *ar, int n);
LUA_SHARED_FUNC(int, lua_getmetatable, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_getstack, lua_State *L, int level, lua_Debug *ar);
LUA_SHARED_FUNC(void, lua_gettable, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_gettop, lua_State *L);
LUA_SHARED_FUNC(const char*, lua_getupvalue, lua_State *L, int idx, int n);
LUA_SHARED_FUNC(void, lua_insert, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_iscfunction, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_isnumber, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_isstring, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_isuserdata, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_lessthan, lua_State *L, int idx1, int idx2);
LUA_SHARED_FUNC(int, lua_load, lua_State *L, lua_Reader reader, void *dt, const char *chunkname);
LUA_SHARED_FUNC(int, lua_loadx, lua_State *L, lua_Reader reader, void *dt, const char *chunkname, const char *mode);
LUA_SHARED_FUNC(lua_State*, lua_newstate, lua_Alloc f, void *ud);
LUA_SHARED_FUNC(lua_State*, lua_newthread, lua_State *L);
LUA_SHARED_FUNC(void*, lua_newuserdata, lua_State *L, size_t size);
LUA_SHARED_FUNC(int, lua_next, lua_State *L, int idx);
LUA_SHARED_FUNC(size_t, lua_objlen, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_pcall, lua_State *L, int nargs, int nresults, int errfunc);
LUA_SHARED_FUNC(void, lua_pushboolean, lua_State *L, int b);
LUA_SHARED_FUNC(void, lua_pushcclosure, lua_State *L, lua_CFunction f, int n);
LUA_SHARED_FUNC(const char*, lua_pushfstring, lua_State *L, const char *fmt, ...);
LUA_SHARED_FUNC(void, lua_pushlightuserdata, lua_State *L, void *p);
LUA_SHARED_FUNC(void, lua_pushlstring, lua_State *L, const char *str, size_t len);
LUA_SHARED_FUNC(void, lua_pushnil, lua_State *L);
LUA_SHARED_FUNC(void, lua_pushnumber, lua_State *L, double n);
LUA_SHARED_FUNC(void, lua_pushstring, lua_State *L, const char *str);
LUA_SHARED_FUNC(int, lua_pushthread, lua_State *L);
LUA_SHARED_FUNC(void, lua_pushvalue, lua_State *L, int idx);
LUA_SHARED_FUNC(const char*, lua_pushvfstring, lua_State *L, const char *fmt, char* argp);
LUA_SHARED_FUNC(int, lua_rawequal, lua_State *L, int idx1, int idx2);
LUA_SHARED_FUNC(void, lua_rawget, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_rawgeti, lua_State *L, int idx, int n);
LUA_SHARED_FUNC(void, lua_rawset, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_rawseti, lua_State *L, int idx, int n);
LUA_SHARED_FUNC(void, lua_remove, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_replace, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_setallocf, lua_State *L, lua_Alloc f, void *ud);
LUA_SHARED_FUNC(int, lua_setfenv, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_setfield, lua_State *L, int idx, const char *k);
LUA_SHARED_FUNC(int, lua_sethook, lua_State *L, lua_Hook func, int mask, int count);
LUA_SHARED_FUNC(const char *, lua_setlocal, lua_State *L, const lua_Debug *ar, int n);
LUA_SHARED_FUNC(int, lua_setmetatable, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_settable, lua_State *L, int idx);
LUA_SHARED_FUNC(void, lua_settop, lua_State *L, int idx);
LUA_SHARED_FUNC(const char*, lua_setupvalue, lua_State *L, int idx, int n);
LUA_SHARED_FUNC(int, lua_status, lua_State *L);
LUA_SHARED_FUNC(int, lua_toboolean, lua_State *L, int idx);
LUA_SHARED_FUNC(lua_CFunction, lua_tocfunction, lua_State *L, int idx);
LUA_SHARED_FUNC(const char*, lua_tolstring, lua_State *L, int idx, size_t *len);
LUA_SHARED_FUNC(double, lua_tonumber, lua_State *L, int idx);
LUA_SHARED_FUNC(const void*, lua_topointer, lua_State *L, int idx);
LUA_SHARED_FUNC(lua_State*, lua_tothread, lua_State *L, int idx);
LUA_SHARED_FUNC(void*, lua_touserdata, lua_State *L, int idx);
LUA_SHARED_FUNC(int, lua_type, lua_State *L, int idx);
LUA_SHARED_FUNC(const char*, lua_typename, lua_State *L, int t);
LUA_SHARED_FUNC(void*, lua_upvalueid, lua_State *L, int idx, int n);
LUA_SHARED_FUNC(void, lua_upvaluejoin, lua_State *L, int idx1, int n1, int idx2, int n2);
LUA_SHARED_FUNC(void, lua_xmove, lua_State *from, lua_State *to, int n);
LUA_SHARED_FUNC(int, lua_yield, lua_State *L, int nresults);
LUA_SHARED_FUNC(int, luaJIT_setmode, lua_State *L, int idx, int mode);
LUA_SHARED_FUNC(void, luaL_addlstring, luaL_Buffer *B, const char *s, size_t l);
LUA_SHARED_FUNC(void, luaL_addstring, luaL_Buffer *B, const char *s);
LUA_SHARED_FUNC(void, luaL_addvalue, luaL_Buffer *B);
LUA_SHARED_FUNC(int, luaL_argerror, lua_State *L, int narg, const char *msg);
LUA_SHARED_FUNC(void, luaL_buffinit, lua_State *L, luaL_Buffer *B);
LUA_SHARED_FUNC(int, luaL_callmeta, lua_State *L, int idx, const char *field);
LUA_SHARED_FUNC(void, luaL_checkany, lua_State *L, int idx);
LUA_SHARED_FUNC(const char*, luaL_checklstring, lua_State *L, int idx, size_t *len);
LUA_SHARED_FUNC(double, luaL_checknumber, lua_State *L, int idx);
LUA_SHARED_FUNC(int, luaL_checkoption, lua_State *L, int idx, const char *def, const char *const lst[]);
LUA_SHARED_FUNC(void, luaL_checkstack, lua_State *L, int size, const char *msg);
LUA_SHARED_FUNC(void, luaL_checktype, lua_State *L, int idx, int tt);
LUA_SHARED_FUNC(void*, luaL_checkudata, lua_State *L, int idx, const char *tname);
LUA_SHARED_FUNC(int, luaL_error, lua_State *L, const char *fmt, ...);
LUA_SHARED_FUNC(int, luaL_execresult, lua_State *L, int stat);
LUA_SHARED_FUNC(int, luaL_fileresult, lua_State *L, int stat, const char *fname);
LUA_SHARED_FUNC(const char*, luaL_findtable, lua_State *L, int idx, const char *fname, int szhint);
LUA_SHARED_FUNC(int, luaL_getmetafield, lua_State *L, int idx, const char *field);
LUA_SHARED_FUNC(const char*, luaL_gsub, lua_State *L, const char *s, const char *p, const char *r);
LUA_SHARED_FUNC(int, luaL_loadbuffer, lua_State *L, const char *buff, size_t sz, const char *name);
LUA_SHARED_FUNC(int, luaL_loadbufferx, lua_State *L, const char *buff, size_t sz, const char *name, const char *mode);
LUA_SHARED_FUNC(int, luaL_loadfile, lua_State *L, const char *filename);
LUA_SHARED_FUNC(int, luaL_loadfilex, lua_State *L, const char *filename, const char *mode);
LUA_SHARED_FUNC(int, luaL_newmetatable, lua_State *L, const char *tname);
LUA_SHARED_FUNC(void, luaL_openlib, lua_State *L, const char *libname, const luaL_Reg *l, int nup);
LUA_SHARED_FUNC(int, luaL_openlibs, lua_State *L);
LUA_SHARED_FUNC(const char*, luaL_optlstring, lua_State *L, int idx, const char *def, size_t *len);
LUA_SHARED_FUNC(double, luaL_optnumber, lua_State *L, int idx, double def);
LUA_SHARED_FUNC(char*, luaL_prepbuffer, luaL_Buffer *B);
LUA_SHARED_FUNC(void, luaL_pushresult, luaL_Buffer *B);
LUA_SHARED_FUNC(int, luaL_ref, lua_State *L, int t);
LUA_SHARED_FUNC(void, luaL_register, lua_State *L, const char *libname, const luaL_Reg *l);
LUA_SHARED_FUNC(void, luaL_traceback, lua_State *L, lua_State *L1, const char *msg, int level);
LUA_SHARED_FUNC(int, luaL_typerror, lua_State *L, int narg, const char *tname);
LUA_SHARED_FUNC(void, luaL_unref, lua_State *L, int t, int ref);
LUA_SHARED_FUNC(void, luaL_where, lua_State *L, int lvl);
LUA_SHARED_FUNC(int, luaopen_base, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_bit, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_debug, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_jit, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_math, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_os, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_package, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_string, lua_State *L);
LUA_SHARED_FUNC(int, luaopen_table, lua_State *L);

void load_lua_shared();

#endif
