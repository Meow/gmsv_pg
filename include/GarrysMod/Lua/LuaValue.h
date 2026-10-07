#ifndef _GLOO_LUA_VALUE_H_
#define _GLOO_LUA_VALUE_H_

#include <map>
#include <string>
#include <memory>
#include <cassert>
#include <variant>
#include <algorithm>
#include <stdexcept>
#include "GarrysMod/Lua/Interface.h"

namespace GarrysMod {
namespace Lua {

  class LuaValue
  {
  public:
    typedef bool                         bool_t;
    typedef std::map<LuaValue, LuaValue> table_t;
    typedef double                       number_t;
    typedef std::string                  string_t;
    typedef CFunc                        function_t;
    typedef std::variant<
      bool_t,
      table_t,
      number_t,
      string_t,
      function_t
    > value_t;
  private:
    int     _type;
    value_t _value;
  public:
    int type() const { return _type; }
  public:
    LuaValue() { _type = Type::Nil; }

    LuaValue(bool_t value) { _type = Type::Bool; _value = value; }
    LuaValue(table_t value) { _type = Type::Table; _value = std::move(value); }
    LuaValue(number_t value) { _type = Type::Number; _value = value; }
    LuaValue(string_t value) { _type = Type::String; _value = std::move(value); }
    LuaValue(function_t value) { _type = Type::Function; _value = value; }

    LuaValue(int value) { _type = Type::Number; _value = (number_t)value; }
    LuaValue(unsigned int value) { _type = Type::Number; _value = (number_t)value; }
    LuaValue(const char *value) { _type = Type::String; _value = std::string(value); }
  public:
    /**
     * @brief copy lua value from that
     * @param that - Lua value
     */
    void Copy(const LuaValue &that)
    {
      _type = that._type;
      _value = that._value;
    }

    /**
     * @brief Checks if type is equal to supplied type
     * @param type - Type to check
     * @throw std::runtime_error
     */
    void AssertType(int type) const
    {
      if (_type != type)
        throw std::runtime_error("Expected type #" + std::to_string(type) + " not type #" + std::to_string(_type));
    }

    /**
     * @brief Push table value to lua stack
     * @param LUA - Lua interface
     */
    void PushTable(ILuaBase *LUA) const
    {
      if (_type != Type::Table)
        throw std::runtime_error("Unable to push type '" + std::string(LUA->GetTypeName(_type)) + "' as table");

      // Create table
      LUA->CreateTable();

      // Iterate over table value
      for (const auto &pair : std::get<table_t>(_value))
      {
        // Push key and value to stack
        pair.first.Push(LUA);
        pair.second.Push(LUA);
        // Assign pair to table
        LUA->SetTable(-3);
      }
    }

    /**
     * @brief Push value to lua stack
     * @param LUA - Lua interface
     * @return number of items pushed to stack
     */
    int Push(ILuaBase *LUA) const
    {
      switch (_type)
      {
        case Type::Table: PushTable(LUA); break;
        case Type::Number: LUA->PushNumber(std::get<number_t>(_value)); break;
        case Type::Bool: LUA->PushBool(std::get<bool_t>(_value)); break;
        case Type::Function: LUA->PushCFunction(std::get<function_t>(_value)); break;
        case Type::String:
        {
          // Pushed with its length, strings may contain zero bytes
          const auto &value = std::get<string_t>(_value);
          LUA->PushString(value.c_str(), (unsigned int)value.size());
          break;
        }
        default:
          LUA->PushNil();
          break;
      }

      return 1;
    }
  public:
    inline bool operator< (const LuaValue& rhs) const
    {
      if (_type != rhs._type) return _type < rhs._type;

      switch (_type)
      {
        case Type::Bool: return std::get<bool_t>(_value) < std::get<bool_t>(rhs._value);
        case Type::Table: return std::get<table_t>(_value) < std::get<table_t>(rhs._value);
        case Type::Number: return std::get<number_t>(_value) < std::get<number_t>(rhs._value);
        case Type::String: return std::get<string_t>(_value) < std::get<string_t>(rhs._value);
        case Type::Function: return std::less<function_t>()(std::get<function_t>(_value), std::get<function_t>(rhs._value));
        default: return false;
      }
    }
    inline bool operator> (const LuaValue& rhs) const { return rhs < *this; }
    inline bool operator<=(const LuaValue& rhs) const { return !(rhs < *this); }
    inline bool operator>=(const LuaValue& rhs) const { return !(*this < rhs); }
    inline bool operator==(const LuaValue& rhs) const
    {
      if (_type != rhs._type)
        return false;

      switch (_type)
      {
        case Type::Bool: return std::get<bool_t>(_value) == std::get<bool_t>(rhs._value);
        case Type::Table: return std::get<table_t>(_value) == std::get<table_t>(rhs._value);
        case Type::Number: return std::get<number_t>(_value) == std::get<number_t>(rhs._value);
        case Type::String: return std::get<string_t>(_value) == std::get<string_t>(rhs._value);
        case Type::Function: return std::get<function_t>(_value) == std::get<function_t>(rhs._value);
        default: return true;
      }
    }
    inline bool operator!=(const LuaValue& rhs) const { return !(*this == rhs); }
    inline LuaValue& operator[](LuaValue idx)
    {
      AssertType(Type::Table);
      return std::get<table_t>(_value)[idx];
    }

    operator bool_t() const { return std::get<bool_t>(_value); }
    operator table_t() const { return std::get<table_t>(_value); }
    operator number_t() const { return std::get<number_t>(_value); }
    operator string_t() const { return std::get<string_t>(_value); }
    operator function_t() const { return std::get<function_t>(_value); }

    operator int() const { return (int)std::get<number_t>(_value); }
  public:
    /**
     * @brief pop lua table from stack
     * @param LUA      - Lua interface
     * @param position - Lua stack position
     * @return new lua table value
     */
    static inline LuaValue PopTable(ILuaBase *LUA, int position = 1)
    {
      int     table_ref;
      auto    table_value = LuaValue(table_t());
      int     type = LUA->GetType(position);

      if (type != Type::Table)
        throw std::runtime_error("Unable to pop type '" + std::string(LUA->GetTypeName(type)) + "' as table");

      // Create table ref and push ref to stack
      LUA->Push(position);
      table_ref = LUA->ReferenceCreate();
      LUA->ReferencePush(table_ref);
      // Push nil as first key
      LUA->PushNil();

      // Increment lua iterator
      while (LUA->Next(-2))
      {
        LuaValue key;
        LuaValue value;

        // Push key and table ref
        LUA->Push(-2);
        LUA->ReferencePush(table_ref);

        // Ensure key is not equal to table ref
        if (LUA->Equal(-1, -2))
          throw std::runtime_error("Unable to pop table with cyclic reference");
        else
          key = LuaValue::Pop(LUA, -2);

        // Ensure value is not equal to table ref
        if (LUA->Equal(-1, -3))
          throw std::runtime_error("Unable to pop table with cyclic reference");
        else
          value = LuaValue::Pop(LUA, -3);

        // Store key/value pair
        std::get<table_t>(table_value._value)[key] = value;

        // Pop key copy, table ref, and value
        LUA->Pop(3);
      }

      // Pop table and free table ref
      LUA->Pop();
      LUA->ReferenceFree(table_ref);

      return table_value;
    }

    /**
     * @brief pop lua value from stack
     * @param LUA      - Lua interface
     * @param position - Lua stack position
     * @returns new lua value
     */
    static inline LuaValue Pop(ILuaBase *LUA, int position = 1)
    {
      int type = LUA->GetType(position);

      switch (type)
      {
        case Type::Bool:
          return LuaValue(LUA->GetBool(position));
        case Type::Table:
          return PopTable(LUA, position);
        case Type::Number:
          return LuaValue(LUA->GetNumber(position));
        case Type::String:
        {
          unsigned int length = 0;
          const char *value = LUA->GetString(position, &length);
          return LuaValue(std::string(value, length));
        }
        case Type::Function:
          // Functions written in Lua have no C function to hold on to
          if (CFunc value = LUA->GetCFunction(position))
            return LuaValue(value);
          break;
      }

      return LuaValue();
    }

    /**
     * @brief creates empty LuaValue
     * @param type - Lua type
     */
    static inline LuaValue Make(int type)
    {
      switch (type)
      {
        case Type::Bool:
          return LuaValue(false);
        case Type::Table:
          return LuaValue(table_t());
        case Type::Number:
          return LuaValue(0.0);
        case Type::String:
          return LuaValue(string_t());
        case Type::Function:
          return LuaValue(__empty);
        default:
          return LuaValue();
      }
    }

    /**
     * @brief push value to lua stack
     * @param LUA   - Lua interface
     * @param value - lua value
     * @return number of items pushed to stack
     */
    template<typename T>
    static int Push(ILuaBase *LUA, T value)
    {
      return LuaValue(std::move(value)).Push(LUA);
    }
  private:
    static int __empty(lua_State *) { return 0; }
  }; // LuaValue

}} // GarrysMod::Lua

#endif//_GLOO_LUA_VALUE_H_
