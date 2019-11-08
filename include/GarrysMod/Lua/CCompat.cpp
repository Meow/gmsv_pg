#include <stdlib.h>
#include "LuaBase.h"
#include "Interface.h"
#include "CCompat.h"

#define GET_LUABASE(l) \
  GarrysMod::Lua::ILuaBase *l = (GarrysMod::Lua::ILuaBase*)(L);

luabase_t *lua_get_base(lua_State *L) {
  return (luabase_t*)L->luabase;
}

int lua_top(luabase_t *L) {
  GET_LUABASE(l)
  return l->Top();
}

void lua_push(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->Push(stack_pos);
}

void lua_pop(luabase_t *L, int amt) {
  GET_LUABASE(l)
  l->Pop(amt);
}

void lua_get_table(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->GetTable(stack_pos);
}

void lua_get_field(luabase_t *L, int stack_pos, const char *name) {
  GET_LUABASE(l)
  l->GetField(stack_pos, name);
}

void lua_set_field(luabase_t *L, int stack_pos, const char *name) {
  GET_LUABASE(l)
  l->SetField(stack_pos, name);
}

void lua_create_table(luabase_t *L) {
  GET_LUABASE(l)
  l->CreateTable();
}

void lua_set_table(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->SetTable(stack_pos);
}

void lua_set_meta_table(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->SetMetaTable(stack_pos);
}

int lua_get_meta_table(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return (int)l->GetMetaTable(stack_pos);
}

void lua_gmod_call(luabase_t *L, int args, int results) {
  GET_LUABASE(l)
  l->Call(args, results);
}

int lua_gmod_pcall(luabase_t *L, int args, int results, int error_func) {
  GET_LUABASE(l)
  return l->PCall(args, results, error_func);
}

int lua_gmod_equal(luabase_t *L, int a, int b) {
  GET_LUABASE(l)
  return l->Equal(a, b);
}

int lua_raw_equal(luabase_t *L, int a, int b) {
  GET_LUABASE(l)
  return l->RawEqual(a, b);
}

void lua_gmod_insert(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->Insert(stack_pos);
}

void lua_gmod_remove(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->Remove(stack_pos);
}

int lua_gmod_next(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return l->Next(stack_pos);
}

void lua_throw_error(luabase_t *L, const char *message) {
  GET_LUABASE(l)
  l->ThrowError(message);
}

void lua_check_type(luabase_t *L, int stack_pos, int type) {
  GET_LUABASE(l)
  l->CheckType(stack_pos, type);
}

void lua_arg_error(luabase_t *L, int arg_num, const char *message) {
  GET_LUABASE(l)
  l->ArgError(arg_num, message);
}

void lua_raw_get(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->RawGet(stack_pos);
}

void lua_raw_set(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  l->RawSet(stack_pos);
}

const char* lua_get_string(luabase_t *L, int stack_pos, unsigned int *out_len) {
  GET_LUABASE(l)
  return l->GetString(stack_pos, out_len);
}

double lua_get_number(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return l->GetNumber(stack_pos);
}

int lua_get_bool(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return (int)l->GetBool(stack_pos);
}

cfunc_t lua_get_cfunc(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return (cfunc_t)l->GetCFunction(stack_pos);
}

void lua_push_nil(luabase_t *L) {
  GET_LUABASE(l)
  l->PushNil();
}

void lua_push_string(luabase_t *L, const char *val, unsigned int len) {
  GET_LUABASE(l)
  l->PushString(val, len);
}

void lua_push_number(luabase_t *L, double val) {
  GET_LUABASE(l)
  l->PushNumber(val);
}

void lua_push_bool(luabase_t *L, int val) {
  GET_LUABASE(l)
  l->PushBool((bool)val);
}

void lua_push_cfunc(luabase_t *L, cfunc_t val) {
  GET_LUABASE(l)
  l->PushCFunction((GarrysMod::Lua::CFunc)val);
}

void lua_push_cclosure(luabase_t *L, cfunc_t val, int vars) {
  GET_LUABASE(l)
  l->PushCClosure((GarrysMod::Lua::CFunc)val, vars);
}

void lua_push_userdata(luabase_t *L, void *val) {
  GET_LUABASE(l)
  l->PushUserdata(val);
}

int lua_reference_create(luabase_t *L) {
  GET_LUABASE(l)
  return l->ReferenceCreate();
}

void lua_reference_free(luabase_t *L, int ref) {
  GET_LUABASE(l)
  l->ReferenceFree(ref);
}

void lua_reference_push(luabase_t *L, int ref) {
  GET_LUABASE(l)
  l->ReferencePush(ref);
}

void lua_push_special(luabase_t *L, int type) {
  GET_LUABASE(l)
  l->PushSpecial(type);
}

int lua_is_type(luabase_t *L, int stack_pos, int type) {
  GET_LUABASE(l)
  return l->IsType(stack_pos, type);
}

const char* lua_get_type_name(luabase_t *L, int type) {
  GET_LUABASE(l)
  return l->GetTypeName(type);
}

const char* lua_check_string(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return l->CheckString(stack_pos);
}

double lua_check_number(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return l->CheckNumber(stack_pos);
}

int lua_obj_len(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return l->ObjLen(stack_pos);
}

/*
const QAngle *lua_get_angle(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return l->GetAngle(stack_pos);
}

const Vector *lua_get_vector(luabase_t *L, int stack_pos) {
  GET_LUABASE(l)
  return *l->GetVector(stack_pos);
}

void lua_push_angle(luabase_t *L, QAngle val) {
  GET_LUABASE(l)
  l->PushAngle(&val);
}

void lua_push_vector(luabase_t *L, Vector val) {
  GET_LUABASE(l)
  l->PushVector(val);
}
*/

void lua_set_state(luabase_t *L, lua_State *state) {
  GET_LUABASE(l)
  l->SetState(state);
}

int lua_create_meta_table(luabase_t *L, const char *name) {
  GET_LUABASE(l)
  return l->CreateMetaTable(name);
}

int lua_push_meta_table(luabase_t *L, int type) {
  GET_LUABASE(l)
  return l->PushMetaTable(type);
}

void lua_push_user_type(luabase_t *L, void *data, int type) {
  GET_LUABASE(l)
  l->PushUserType(data, type);
}

void lua_set_user_type(luabase_t *L, int stack_pos, void *data) {
  GET_LUABASE(l)
  l->SetUserType(stack_pos, data);
}
