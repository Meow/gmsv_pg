#ifndef GARRYSMOD_LUA_CCOMPAT_H
#define GARRYSMOD_LUA_CCOMPAT_H

#include <gmodc/lua/types.h>
#include <gmodc/lua/lua.h>
#include <gmodc/lua/source_compat.h>

#ifdef __cplusplus
#include "LuaBase.h"
#include "SourceCompat.h"
extern "C" {
#endif

  typedef void luabase_t;

  typedef struct {
    void *data;
    unsigned char type;
  } userdata_t;

  typedef int (*cfunc_t)(lua_State *L);

  /*
    For documentation, please refer to ./include/GarrysMod/Lua/LuaBase.h
  */

  luabase_t *lua_get_base(lua_State *L);
  int lua_top(luabase_t *L);
  void lua_push(luabase_t *L, int stack_pos);
  void lua_pop(luabase_t *L, int amt);
  void lua_get_table(luabase_t *L, int stack_pos);
  void lua_get_field(luabase_t *L, int stack_pos, const char *name);
  void lua_set_field(luabase_t *L, int stack_pos, const char *name);
  void lua_create_table(luabase_t *L);
  void lua_set_table(luabase_t *L, int stack_pos);
  void lua_set_meta_table(luabase_t *L, int stack_pos);
  int lua_get_meta_table(luabase_t *L, int stack_pos);
  void lua_gmod_call(luabase_t *L, int args, int results);
  int lua_gmod_pcall(luabase_t *L, int args, int results, int error_func);
  int lua_gmod_equal(luabase_t *L, int a, int b);
  int lua_raw_equal(luabase_t *L, int a, int b);
  void lua_gmod_insert(luabase_t *L, int stack_pos);
  void lua_gmod_remove(luabase_t *L, int stack_pos);
  int lua_gmod_next(luabase_t *L, int stack_pos);
  void lua_throw_error(luabase_t *L, const char *message);
  void lua_check_type(luabase_t *L, int stack_pos, int type);
  void lua_arg_error(luabase_t *L, int arg_num, const char *message);
  void lua_raw_get(luabase_t *L, int stack_pos);
  void lua_raw_set(luabase_t *L, int stack_pos);
  const char* lua_get_string(luabase_t *L, int stack_pos, unsigned int *out_len);
  double lua_get_number(luabase_t *L, int stack_pos);
  int lua_get_bool(luabase_t *L, int stack_pos);
  cfunc_t lua_get_cfunc(luabase_t *L, int stack_pos);
  void lua_push_nil(luabase_t *L);
  void lua_push_string(luabase_t *L, const char *val, unsigned int len);
  void lua_push_number(luabase_t *L, double val);
  void lua_push_bool(luabase_t *L, int val);
  void lua_push_cfunc(luabase_t *L, cfunc_t val);
  void lua_push_cclosure(luabase_t *L, cfunc_t val, int vars);
  void lua_push_userdata(luabase_t *L, void *val);
  int lua_reference_create(luabase_t *L);
  void lua_reference_free(luabase_t *L, int ref);
  void lua_reference_push(luabase_t *L, int ref);
  void lua_push_special(luabase_t *L, int type);
  int lua_is_type(luabase_t *L, int stack_pos, int type);
  const char* lua_get_type_name(luabase_t *L, int type);
  const char* lua_check_string(luabase_t *L, int stack_pos);
  double lua_check_number(luabase_t *L, int stack_pos);
  int lua_obj_len(luabase_t *L, int stack_pos);
  /*
  const QAngle *lua_get_angle(luabase_t *L, int stack_pos);
  const Vector *lua_get_vector(luabase_t *L, int stack_pos);
  void lua_push_angle(luabase_t *L, QAngle *val);
  void lua_push_vector(luabase_t *L, Vector *val);
  */
  void lua_set_state(luabase_t *L, lua_State *state);
  int lua_create_meta_table(luabase_t *L, const char *name);
  int lua_push_meta_table(luabase_t *L, int type);
  void lua_push_user_type(luabase_t *L, void *data, int type);
  void lua_set_user_type(luabase_t *L, int stack_pos, void *data);

#ifdef __cplusplus
}
#endif

#endif
