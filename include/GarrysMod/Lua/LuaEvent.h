#ifndef _GLOO_LUA_EVENT_H_
#define _GLOO_LUA_EVENT_H_

#include <set>
#include <deque>
#include <tuple>
#include <mutex>
#include <vector>
#include <memory>
#include <sstream>
#include <algorithm>
#include "LuaValue.h"
#include "LuaObject.h"
#include "GarrysMod/Lua/Interface.h"

namespace GarrysMod {
namespace Lua {

  class ILuaEventEmitter
  {
  public:
    virtual void Think(lua_State *state) = 0;
  }; // ILuaEventEmitter

  class LuaEventEmitterManager
  {
  private:
    std::set<std::weak_ptr<ILuaEventEmitter>> _emitters;
    std::string _hook_name()
    {
      std::ostringstream ss;
      ss << "LuaEventEmitterManager_" << reinterpret_cast<const void*>(this);

      return ss.str();
    }
    bool _hooked;
  public:
    /**
     * @brief called every tick
     * @param state - lua state
     */
    void Think(lua_State *state)
    {
      // Begin iteration of emitters
      for (auto iter = _emitters.begin(); iter != _emitters.end();)
      {
        // Attempt to get shared_ptr
        if (auto emitter = iter->lock())
        {
          emitter->Think(state);
          ++iter;
        }
        else
          // Shared_ptr not accessable, remove from set
          iter = _emitters.erase(iter);
      }

      // If zero emitters stored, remove Think hook
      if (_emitters.size() == 0)
        resetThink(state);
    }

    /**
     * @brief inserts emitter weak_ptr to stored set and hooks Think if not
     *  already hooked
     * @param state - lua state
     * @param emitter - emitter to store
     */
    void RegisterEmitter(lua_State *state, std::weak_ptr<ILuaEventEmitter> emitter)
    {
      _emitters.insert(emitter);
      hookThink(state);
    }
  private:
    void hookThink(lua_State *state)
    {
      if (_hooked)
        return;

      LUA->PushSpecial(SPECIAL_GLOB);
        LUA->GetField(-1, "hook");
          LUA->GetField(-1, "Add");
            LUA->PushString("Think");
            LUA->PushString(_hook_name().c_str());
            LUA->PushCFunction(think);
            LUA->Call(3, 0);
      
      _hooked = true;
    }

    void resetThink(lua_State *state)
    {
      if (!_hooked)
        return;

      LUA->PushSpecial(SPECIAL_GLOB);
        LUA->GetField(-1, "hook");
          LUA->GetField(-1, "Remove");
            LUA->PushString("Think");
            LUA->PushString(_hook_name().c_str());
            LUA->Call(2, 0);

      _hooked = false;
    }
  private:
    static int think(lua_State *state)
    {
      Current(state).Think(state);
      return 0;
    }
  public:
    static LuaEventEmitterManager& Current(lua_State *state)
    {
      static std::map<lua_State*, LuaEventEmitterManager> _managers;
      return _managers[state];
    }
  }; // LuaEventEmitterManager

  template<unsigned char TType, class TChildObject>
  class LuaEventEmitter :
    public LuaObject<TType, TChildObject>
  , public ILuaEventEmitter
  {
  private:
    std::map<std::string, std::vector<std::tuple<bool, int>>> _listeners;
    std::mutex _listeners_mtx;
    std::deque<std::tuple<std::string, std::vector<LuaValue>>> _events;
    std::mutex _events_mtx;
  private:
    int _max_events_per_tick;
  protected:
    /**
     * @brief get maximum number of events to process for each Think call
     */
    int max_events_per_tick() { return _max_events_per_tick; }

    /**
     * @brief set maximum number of events to process for each Think call
     */
    void max_events_per_tick(int value) { _max_events_per_tick = value; }
  public:
    LuaEventEmitter() :
      LuaObject<TType, TChildObject>(),
      _max_events_per_tick(100)
    {
      LuaObject<TType, TChildObject>::AddMethod("on", on);
      LuaObject<TType, TChildObject>::AddMethod("once", once);
      LuaObject<TType, TChildObject>::AddMethod("add_listener", add_listener);
      LuaObject<TType, TChildObject>::AddMethod("remove_listeners", remove_listeners);
    }
  public:
    /**
     * @brief enqueue event with supplied arguments
     * @param name - event name
     * @param args - event args
     */
    template<typename... Args>
    void Emit(std::string name, Args ...args)
    {
      std::unique_lock<std::mutex> lock(_events_mtx);

      std::vector<LuaValue> argv = { LuaValue(args)... };

      _events.push_back(
        std::make_tuple(name, argv)
      );
    }

    /**
     * @param called via LuaEventEmitterManager
     * @param state - lua state
     */
    void Think(lua_State *state) override
    {
      std::unique_lock<std::mutex> events_lock(_events_mtx);

      if (_events.empty())
        return;
      
      // Limited event iteration
      for (int i = 0; i < std::min((int)_events.size(), _max_events_per_tick); i++)
      {
        // Pop first event
        auto event = _events.front();
        auto name = std::get<0>(event);
        auto args = std::get<1>(event);
        _events.pop_front();

        // Lock listeners vector
        std::unique_lock<std::mutex> listeners_lock(_listeners_mtx);

        // Iterate listeners
        std::vector<std::tuple<bool, int>> &listeners = _listeners[name];
        for (auto iter = listeners.begin(); iter != listeners.end();)
        {
          auto argc = 0;
          auto once = std::get<0>(*iter);
          auto ref = std::get<1>(*iter);

          // Push reference to callback
          LUA->ReferencePush(ref);

          // Push args and increment argc
          for (auto &arg : args)
            argc += arg.Push(state);
          
          // Invoke callback with args count
          LUA->Call(argc, 0);

          // Remove if once bit is set
          if (once)
            iter = listeners.erase(iter);
          else
            ++iter;
        }
      }
    }

    /**
     * @brief clears listeners vector and unrefs all supplied listeners
     * @param state - lua state
     */
    void Destroy(lua_State *state) override
    {
      removeListeners(state);
    }
  private:
    void addListener(lua_State *state, std::string name, int fn_ref, bool once)
    {
      std::unique_lock<std::mutex> lock(_listeners_mtx);

      // Store listener
      _listeners[name].push_back(std::make_tuple(once, fn_ref));

      // Register this in event emitter manager
      LuaEventEmitterManager::Current(state)
        .RegisterEmitter(
          state,
          this->shared_from_this()
        );
    }

    void removeListeners(lua_State *state)
    {
      std::unique_lock<std::mutex> lock(_listeners_mtx);

      for (auto &listeners : _listeners)
      {
        for (auto &listener : listeners.second)
        {
          LUA->ReferenceFree(std::get<1>(listener));
        }
      }

      _listeners.clear();
    }
  private:
    static int on(lua_State *state)
    {
      LUA->CheckType(2, Type::STRING);
      LUA->CheckType(3, Type::FUNCTION);

      auto obj = LuaObject<TType, TChildObject>::Pop(state, 1);
      auto name = LuaValue::Pop(state, 2);

      LUA->Push(3);
      int fn_ref = LUA->ReferenceCreate();

      obj->addListener(state, name, fn_ref, false);
      return 0;
    }

    static int once(lua_State *state)
    {
      LUA->CheckType(2, Type::STRING);
      LUA->CheckType(3, Type::FUNCTION);

      auto obj = LuaObject<TType, TChildObject>::Pop(state, 1);
      auto name = LuaValue::Pop(state, 2);

      LUA->Push(3);
      int fn_ref = LUA->ReferenceCreate();

      obj->addListener(state, name, fn_ref, true);
      return 0;
    }

    static int add_listener(lua_State *state)
    {
      LUA->CheckType(2, Type::STRING);
      LUA->CheckType(3, Type::FUNCTION);

      auto obj = LuaObject<TType, TChildObject>::Pop(state, 1);
      auto name = LuaValue::Pop(state, 2);
      auto once = false;

      LUA->Push(3);
      int fn_ref = LUA->ReferenceCreate();

      if (LUA->IsType(4, Type::BOOL))
        once = LUA->GetBool(4);

      obj->addListener(state, name, fn_ref, once);
      return 0;
    }

    static int remove_listeners(lua_State *state)
    {
      LuaObject<TType, TChildObject>::Pop(state, 1)->removeListeners(state);
      return 0;
    }
  }; // LuaEventEmitter

  /**
   * @brief lt operator implementation for ILuaEventEmitter weak_ptr to allow weak_ptr in set
   * @param lhs - left hand side
   * @param rhs - right hand side
   * @return lhs < rhs
   */
  bool operator < (const std::weak_ptr<ILuaEventEmitter>& lhs, const std::weak_ptr<ILuaEventEmitter>& rhs)
  {
    auto lhs_shared = lhs.lock();
    auto rhs_shared = rhs.lock();

    if (!lhs_shared & !rhs_shared)
      return false;
    
    if (!lhs_shared) return false;
    if (!rhs_shared) return true;

    return lhs_shared < rhs_shared;
  }

}} // GarrysMod::Lua

#endif//_GLOO_LUA_EVENT_H_
