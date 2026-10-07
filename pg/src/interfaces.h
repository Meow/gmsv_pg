#ifndef _INTERFACES_H
#define _INTERFACES_H

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/Lua/LuaValue.h>
#include <GarrysMod/Lua/LuaObject.h>
#include <GarrysMod/Lua/LuaEvent.h>
#include <libpq-fe.h>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Methods are free to throw C++ exceptions, Lua gets them as regular errors.
#define LUA_METHOD(name) GLOO_METHOD(name)

#define STRING_GETTER(mn, fn) LUA_METHOD(fn) {  \
  return LuaValue::Push(LUA, Pop(LUA, 1)->mn);  \
}

// copy-pasted from pg_type.h
#define  TYPCATEGORY_BOOLEAN	'B'
#define  TYPCATEGORY_NUMERIC	'N'

// PushString measures the string itself when told that the length is zero,
// which only works out when there is a string to measure.
inline void push_string(GarrysMod::Lua::ILuaBase *LUA, std::string_view value) {
  if (value.empty())
    LUA->PushString("");
  else
    LUA->PushString(value.data(), (unsigned int)value.size());
}

inline std::string check_string(GarrysMod::Lua::ILuaBase *LUA, int position, const char *error) {
  if (!LUA->IsType(position, GarrysMod::Lua::Type::String))
    throw std::invalid_argument(error);

  unsigned int length = 0;
  const char *value = LUA->GetString(position, &length);
  return std::string(value, length);
}

#endif
