#ifndef _GLOO_LUA_OBJECT_H_
#define _GLOO_LUA_OBJECT_H_

#include <tuple>
#include <memory>
#include <string>
#include <functional>
#include "LuaValue.h"
#include "GarrysMod/Lua/Interface.h"

namespace GarrysMod {
namespace Lua {

  template<unsigned char TType, class TChildObject>
  class LuaObject :
    public std::enable_shared_from_this<TChildObject>
  {
  private:
    std::map<std::string, CFunc> _getters;
    std::map<std::string, CFunc> _setters;
    std::map<std::string, CFunc> _methods;
    std::map<lua_State*, int>    _references;
    std::map<std::string, CFunc> _metamethods;
  public:
    int type() { return TType; }
    virtual std::string name() { return "LuaObject"; }
  public:
    LuaObject() {
      AddMetaMethod("__gc", __gc);
      AddMetaMethod("__index", __index);
      AddMetaMethod("__newindex", __newindex);
      AddMetaMethod("__tostring", __tostring);
    }
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
     * @brief define metamethod
     * @param name - name of metamethod
     * @param fn   - callback when metamethod is invoked
     */
    void AddMetaMethod(std::string name, CFunc fn) { _metamethods[name] = fn; }

    /**
     * @brief push object to lua stack
     * @param state - lua state
     */
    int Push(lua_State *state)
    {
      registerObject(state);
      LUA->ReferencePush(_references[state]);
      return 1;
    }

    virtual void Destroy(lua_State *state) {}
  private:
    void registerObject(lua_State *state)
    {
      if (_references.find(state) != _references.end())
        return;

      auto self = std::static_pointer_cast<TChildObject>(std::enable_shared_from_this<TChildObject>::shared_from_this());

      UserData *ud = (UserData*)LUA->NewUserdata(sizeof(UserData));
        ud->data = (void*)new std::shared_ptr<TChildObject>(self);
        ud->type = TType;
      LUA->CreateTable();
        for (const auto &metamethod : _metamethods)
        {
          LUA->PushCFunction(metamethod.second);
          LUA->SetField(-2, metamethod.first.c_str());
        }
      LUA->SetMetaTable(-2);

      _references[state] = LUA->ReferenceCreate();
    }
  public:
    /**
     * @brief pop child object shared_ptr from stack
     * @param state - lua state
     * @param position - lua stack position holding value
     * @return shared_ptr to TChildObject
     */
    static std::shared_ptr<TChildObject> Pop(lua_State *state, int position = 1)
    {
      LUA->CheckType(position, TType);
      UserData *ud = (UserData*)LUA->GetUserdata(position);
      return *(std::shared_ptr<TChildObject>*)ud->data;
    }

    /**
     * @brief create LuaObject with supplied parameters
     * @param args - var args
     * @return new LuaObject
     */
    template<typename ...Args>
    static std::shared_ptr<TChildObject> Make(Args&&... args)
    {
      return std::make_shared<TChildObject>(args...);
    }
  private:
    inline static int __gc(lua_State *state)
    {
      // Manual pop child shared_ptr
      LUA->CheckType(1, TType);
      UserData* obj_data = (UserData*)LUA->GetUserdata(1);
      std::shared_ptr<TChildObject> *obj_ptr = (std::shared_ptr<TChildObject>*)obj_data->data;
      std::shared_ptr<TChildObject> obj = *obj_ptr;

      obj->Destroy(state);

      // Free reference
      LUA->ReferenceFree(obj->_references[state]);
      obj->_references.erase(state);

      // Release shared_ptr
      delete obj_ptr;

      return 0;
    }

    static int __index(lua_State *state)
    {
      auto obj = Pop(state, 1);
      auto name = LuaValue::Pop(state, 2);

      // Index getter/method members
      if (name.type() == Type::STRING) {
        auto method = obj->_methods.find(name);
        auto getter = obj->_getters.find(name);

        if (method != obj->_methods.end())
          return LuaValue::Push(state, method->second);
        if (getter != obj->_getters.end())
          return getter->second(state);
      }
      
      return 0;
    }

    static int __newindex(lua_State *state)
    {
      auto obj = Pop(state, 1);
      auto name = LuaValue::Pop(state, 2);

      // Index setter member
      if (name.type() == Type::STRING) {
        auto setter = obj->_setters.find(name);

        if (setter != obj->_setters.end())
          return setter->second(state);
      }

      return 0;
    }

    static int __tostring(lua_State *state)
    {
      auto obj = Pop(state, 1);

      return LuaValue::Push(state, obj->name());
    }
  }; // LuaObject

}} // GarrysMod::Lua

#endif//_GLOO_LUA_OBJECT_H_
