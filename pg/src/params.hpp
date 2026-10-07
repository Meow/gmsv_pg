#ifndef _PARAMS_HPP
#define _PARAMS_HPP

#include "interfaces.h"

using namespace GarrysMod::Lua;

// Lua has a single number type. Whole numbers are sent the way an integer
// column expects them: 100000 instead of 1e+05.
inline void append_number(pqxx::params &params, double value) {
  if (value >= -0x1p63 && value < 0x1p63 && value == (double)(long long)value)
    params.append((long long)value);
  else
    params.append(value);
}

// Reads the values from stack position first on as the parameters of a
// statement, the ones that take the place of $1, $2 and so on. They are sent
// apart from the statement, nothing in them has to be escaped.
inline std::shared_ptr<pqxx::params> pop_params(ILuaBase *LUA, int first) {
  auto params = std::make_shared<pqxx::params>();

  for (int i = first, top = LUA->Top(); i <= top; i++) {
    std::string number = std::to_string(i - first + 1);

    switch (LUA->GetType(i)) {
    case Type::Nil:
      params->append();
      break;
    case Type::Bool:
      params->append(LUA->GetBool(i));
      break;
    case Type::Number:
      append_number(*params, LUA->GetNumber(i));
      break;
    case Type::String: {
      std::string value = check_string(LUA, i, "");

      // Text goes to the server zero terminated, the rest of the value would
      // be cut off without a word.
      if (value.find('\0') != std::string::npos)
        throw std::invalid_argument("pg - parameter #" + number + " contains a zero byte, binary data has to go through escape_bytea");

      params->append(std::move(value));
      break;
    }
    default:
      throw std::invalid_argument("pg - bad parameter #" + number + " (string, number, boolean or nil expected)");
    }
  }

  return params;
}

#endif
