#ifndef _PARAMS_HPP
#define _PARAMS_HPP

#include "interfaces.h"

using namespace GarrysMod::Lua;

// The value of a parameter the way it goes to the server, which is as text.
// One without a value is NULL.
typedef std::optional<std::string> param_t;

// Lua has a single number type. Whole numbers are sent the way an integer
// column expects them: 100000 instead of 1e+05. Everything else is sent in
// the shortest form that reads back as the same number.
inline std::string number_param(double value) {
  // The names that the server knows these by.
  if (std::isnan(value))
    return "NaN";

  if (std::isinf(value))
    return value < 0 ? "-Infinity" : "Infinity";

  char text[32];
  std::to_chars_result end;

  if (value >= -0x1p63 && value < 0x1p63 && value == (double)(long long)value)
    end = std::to_chars(text, text + sizeof(text), (long long)value);
  else
    end = std::to_chars(text, text + sizeof(text), value);

  return std::string(text, end.ptr);
}

// Reads the values from stack position first on as the parameters of a
// statement, the ones that take the place of $1, $2 and so on. They are sent
// apart from the statement, nothing in them has to be escaped.
inline std::vector<param_t> pop_params(ILuaBase *LUA, int first) {
  std::vector<param_t> params;

  for (int i = first, top = LUA->Top(); i <= top; i++) {
    std::string number = std::to_string(i - first + 1);

    switch (LUA->GetType(i)) {
    case Type::Nil:
      params.emplace_back();
      break;
    case Type::Bool:
      params.emplace_back(LUA->GetBool(i) ? "true" : "false");
      break;
    case Type::Number:
      params.emplace_back(number_param(LUA->GetNumber(i)));
      break;
    case Type::String: {
      std::string value = check_string(LUA, i, "");

      // Text goes to the server zero terminated, the rest of the value would
      // be cut off without a word.
      if (value.find('\0') != std::string::npos)
        throw std::invalid_argument("pg - parameter #" + number + " contains a zero byte, binary data has to go through escape_bytea");

      params.emplace_back(std::move(value));
      break;
    }
    default:
      throw std::invalid_argument("pg - bad parameter #" + number + " (string, number, boolean or nil expected)");
    }
  }

  return params;
}

#endif
