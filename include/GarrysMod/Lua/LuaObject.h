#ifndef _GLOO_LUA_OBJECT_H_
#define _GLOO_LUA_OBJECT_H_

#include <map>
#include <tuple>
#include <memory>
#include <string>
#include <cstdio>
#include <stdexcept>
#include <functional>
#include "LuaValue.h"
#include "GarrysMod/Lua/Interface.h"

/**
 * @brief declares a static member function that Lua can call
 *
 * Works like LUA_FUNCTION, except that the function is free to throw C++
 * exceptions: they reach Lua as regular Lua errors.
 */
#define GLOO_METHOD(FUNC)                                           \
  static int FUNC(lua_State *L)                                     \
  {                                                                 \
    return GarrysMod::Lua::LuaProtect(L, FUNC##_impl);              \
  }                                                                 \
  static int FUNC##_impl(GarrysMod::Lua::ILuaBase *LUA)

namespace GarrysMod {
namespace Lua {

  /**
   * @brief the state that the function of the module that is running was
   *  called on, which is the one that LUA is set to. It says nothing while
   *  no such function is running. Main thread only.
   */
  inline lua_State *&LuaState()
  {
    static lua_State *state = nullptr;
    return state;
  }

  /**
   * @brief calls fn and turns any C++ exception it throws into a Lua error
   *
   * Lua raises errors by jumping out of the function, which is not guaranteed
   * to run destructors. Throwing a C++ exception instead and letting it land
   * here unwinds the stack properly first.
   *
   * @param L  - lua state
   * @param fn - function to call
   * @return number of values fn pushed to the stack
   */
  inline int LuaProtect(lua_State *L, int (*fn)(ILuaBase *))
  {
    ILuaBase *LUA = L->luabase;
    LuaState() = L;
    LUA->SetState(L);

    // Nothing that needs a destructor may be alive when the error is raised
    char message[1024];

    try
    {
      return fn(LUA);
    }
    catch (const std::exception &e)
    {
      snprintf(message, sizeof(message), "%s", e.what());
    }

    LUA->ThrowError(message);
    return 0;
  }

  /**
   * @brief PCall for a function of the module that carries on afterwards
   *
   * What is called may call a function of the module from a coroutine, which
   * sets LUA to the state of that coroutine. Garry's Mod has been seen to set
   * it back when the call returns, but nothing says that it has to. Without
   * that the caller would carry on with the stack of the coroutine, or with
   * what is left of one that was collected. So LUA is set back here, to the
   * state that the caller was called on.
   *
   * @param LUA        - Lua interface
   * @param args       - number of arguments on the stack
   * @param results    - number of results to leave there
   * @param error_func - lua stack position of the error handler, 0 for none
   * @return what PCall returned, which is 0 if there was no error
   */
  inline int LuaPCall(ILuaBase *LUA, int args, int results, int error_func = 0)
  {
    lua_State *L = LuaState();
    int status = LUA->PCall(args, results, error_func);

    // There is none outside of the functions of the module, as when it is
    // opened
    if (L != nullptr)
    {
      LuaState() = L;
      LUA->SetState(L);
    }

    return status;
  }

  template<class TChildObject>
  class LuaObject :
    public std::enable_shared_from_this<TChildObject>
  {
  private:
    std::map<std::string, CFunc> _getters;
    std::map<std::string, CFunc> _setters;
    std::map<std::string, CFunc> _methods;

    // Type ID that Garry's Mod assigned to this class, see Register
    static int &typeID() { static int id = Type::None; return id; }
    static std::string &typeName() { static std::string name; return name; }
  public:
    virtual std::string name() { return "LuaObject"; }
    virtual ~LuaObject() {}
  public:
    /**
     * @brief define getter method to be used in __index metamethod
     * @param name - name of getter method
     * @param fn   - callback when obj is indexed in lua with the supplied name
     */
    void AddGetter(std::string name, CFunc fn) { _getters[name] = fn; }

    /**
     * @brief define setter method to be used in __newindex metamethod
     * @param name - name of setter method
     * @param fn   - callback when obj is assigned a value in lua to the supplied name
     */
    void AddSetter(std::string name, CFunc fn) { _setters[name] = fn; }

    /**
     * @brief define method
     * @param name - name of method
     * @param fn   - callback when lua invokes a method on obj
     */
    void AddMethod(std::string name, CFunc fn) { _methods[name] = fn; }

    /**
     * @brief register the class with Garry's Mod, which gives it a type of
     *  its own. Has to be called before any object is pushed, every time the
     *  module is opened.
     * @param LUA  - Lua interface
     * @param name - type name, should be unique to the module
     */
    static void Register(ILuaBase *LUA, const char *name)
    {
      typeID() = LUA->CreateMetaTable(name);
      typeName() = name;
      LUA->Pop();
    }

    /**
     * @brief push object to lua stack
     *
     * The userdata shares ownership of the object. Every userdata gets a
     * metatable of its own, which is where Lua values that belong to the
     * object can be kept (see LuaEvent.h).
     *
     * @param LUA - Lua interface
     */
    int Push(ILuaBase *LUA)
    {
      auto self = new std::shared_ptr<TChildObject>(this->shared_from_this());

      LUA->PushUserType(self, typeID());
      LUA->CreateTable();
        LUA->PushCFunction(__gc);
        LUA->SetField(-2, "__gc");
        LUA->PushCFunction(__index);
        LUA->SetField(-2, "__index");
        LUA->PushCFunction(__newindex);
        LUA->SetField(-2, "__newindex");
        LUA->PushCFunction(__tostring);
        LUA->SetField(-2, "__tostring");
      LUA->SetMetaTable(-2);

      return 1;
    }
  public:
    /**
     * @brief get child object shared_ptr from stack
     * @param LUA      - Lua interface
     * @param position - lua stack position holding value
     * @return shared_ptr to TChildObject
     * @throw std::invalid_argument if the value is not a TChildObject
     */
    static std::shared_ptr<TChildObject> Pop(ILuaBase *LUA, int position = 1)
    {
      auto self = LUA->GetUserType<std::shared_ptr<TChildObject>>(position, typeID());

      if (self == nullptr)
        throw std::invalid_argument("bad argument #" + std::to_string(position) + " (" + typeName() + " expected)");

      return *self;
    }

    /**
     * @brief create LuaObject with supplied parameters
     * @param args - var args
     * @return new LuaObject
     */
    template<typename ...Args>
    static std::shared_ptr<TChildObject> Make(Args&&... args)
    {
      return std::make_shared<TChildObject>(std::forward<Args>(args)...);
    }
  private:
    GLOO_METHOD(__gc)
    {
      auto self = LUA->GetUserType<std::shared_ptr<TChildObject>>(1, typeID());

      if (self != nullptr)
      {
        // Release shared_ptr
        LUA->SetUserType(1, nullptr);
        delete self;
      }

      return 0;
    }

    GLOO_METHOD(__index)
    {
      CFunc getter = nullptr;

      // Index getter/method members
      if (LUA->IsType(2, Type::String)) {
        auto obj = Pop(LUA, 1);
        auto method = obj->_methods.find(LUA->GetString(2));

        if (method != obj->_methods.end()) {
          LUA->PushCFunction(method->second);
          return 1;
        }

        auto found = obj->_getters.find(LUA->GetString(2));

        if (found != obj->_getters.end())
          getter = found->second;
      }

      if (getter == nullptr)
        return 0;

      // Call getter with the arguments of __index. It is a function of the
      // module that runs on this state, LUA is where it was afterwards
      LUA->PushCFunction(getter);
      LUA->Push(1);
      LUA->Push(2);
      LUA->Call(2, 1);
      return 1;
    }

    GLOO_METHOD(__newindex)
    {
      CFunc setter = nullptr;

      // Index setter member
      if (LUA->IsType(2, Type::String)) {
        auto obj = Pop(LUA, 1);
        auto found = obj->_setters.find(LUA->GetString(2));

        if (found != obj->_setters.end())
          setter = found->second;
      }

      if (setter == nullptr)
        return 0;

      // Call setter with the arguments of __newindex
      LUA->PushCFunction(setter);
      LUA->Push(1);
      LUA->Push(2);
      LUA->Push(3);
      LUA->Call(3, 0);
      return 0;
    }

    GLOO_METHOD(__tostring)
    {
      return LuaValue::Push(LUA, Pop(LUA, 1)->name());
    }
  }; // LuaObject

}} // GarrysMod::Lua

#endif//_GLOO_LUA_OBJECT_H_
