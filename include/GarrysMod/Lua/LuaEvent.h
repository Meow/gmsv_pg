#ifndef _GLOO_LUA_EVENT_H_
#define _GLOO_LUA_EVENT_H_

#include <map>
#include <deque>
#include <mutex>
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <stdexcept>
#include "LuaValue.h"
#include "LuaObject.h"
#include "GarrysMod/Lua/Interface.h"

namespace GarrysMod {
namespace Lua {

  /**
   * @brief delivers events to the listeners of event emitters
   *
   * Events can be emitted from any thread. Listeners are called from the main
   * thread, on the Think hook.
   *
   * Listeners are stored in the metatable of the emitter's userdata rather
   * than referenced from C++, so that a listener that refers to its emitter
   * does not keep it from being garbage collected.
   */
  class LuaEventEmitterManager
  {
  private:
    struct Event
    {
      int object;
      std::string name;
      std::vector<LuaValue> args;
    };

    std::deque<Event> _events;
    std::mutex _events_mtx;
    bool _hooked = false;
    bool _closed = false;

    std::string _hook_name()
    {
      std::ostringstream ss;
      ss << "LuaEventEmitterManager_" << reinterpret_cast<const void*>(this);

      return ss.str();
    }
  public:
    /**
     * @brief keeps the emitter at the supplied stack position from being
     *  garbage collected until an event was delivered to it. Main thread only.
     * @param LUA      - Lua interface
     * @param position - lua stack position of the emitter
     * @return reference to the emitter, to be passed to Emit
     */
    int Hold(ILuaBase *LUA, int position)
    {
      LUA->Push(position);
      return LUA->ReferenceCreate();
    }

    /**
     * @brief enqueue event with supplied arguments, releasing the emitter
     *  once the event was delivered. Can be called from any thread.
     * @param object - emitter reference returned by Hold
     * @param name   - event name
     * @param args   - event args
     */
    void Emit(int object, std::string name, std::vector<LuaValue> args = {})
    {
      std::lock_guard<std::mutex> lock(_events_mtx);

      // Nobody is left to deliver the event to
      if (_closed)
        return;

      _events.push_back(Event{ object, std::move(name), std::move(args) });
    }

    /**
     * @brief called every tick
     * @param LUA - Lua interface
     */
    void Think(ILuaBase *LUA)
    {
      std::deque<Event> events;

      // Listeners are free to emit more events, don't keep the queue locked
      {
        std::lock_guard<std::mutex> lock(_events_mtx);
        events.swap(_events);
      }

      for (const auto &event : events)
        dispatch(LUA, event);
    }
  private:
    void hookThink(ILuaBase *LUA)
    {
      if (_hooked)
        return;

      LUA->PushSpecial(SPECIAL_GLOB);
      LUA->GetField(-1, "hook");

      if (!LUA->IsType(-1, Type::Table))
      {
        LUA->Pop(2);
        throw std::runtime_error("unable to listen for events, the hook library is not loaded");
      }

      LUA->GetField(-1, "Add");
        LUA->PushString("Think");
        LUA->PushString(_hook_name().c_str());
        LUA->PushCFunction(think);

      if (LUA->PCall(3, 0, 0) != 0)
      {
        LUA->Pop(3);
        throw std::runtime_error("unable to listen for events, hook.Add failed");
      }

      LUA->Pop(2);

      _hooked = true;
    }

    /**
     * @brief pushes every function of metatable[field][name] to the stack
     * @param LUA       - Lua interface
     * @param metatable - lua stack position of the emitter's metatable
     * @return number of functions pushed
     */
    static int pushListeners(ILuaBase *LUA, int metatable, const char *field, const char *name)
    {
      int count = 0;

      LUA->GetField(metatable, field);

      if (!LUA->IsType(-1, Type::Table))
      {
        LUA->Pop();
        return 0;
      }

      LUA->GetField(-1, name);

      if (!LUA->IsType(-1, Type::Table))
      {
        LUA->Pop(2);
        return 0;
      }

      int list = LUA->Top();

      for (int i = 1, length = LUA->ObjLen(list); i <= length; i++)
      {
        LUA->PushNumber(i);
        LUA->RawGet(list);
        count++;
      }

      // Leave nothing but the functions
      LUA->Remove(list);
      LUA->Remove(list - 1);

      return count;
    }

    void dispatch(ILuaBase *LUA, const Event &event)
    {
      int top = LUA->Top();

      LUA->ReferencePush(event.object);
      LUA->ReferenceFree(event.object);

      if (LUA->GetMetaTable(-1))
      {
        int metatable = LUA->Top();
        int count = pushListeners(LUA, metatable, "listeners", event.name.c_str());
        int count_once = pushListeners(LUA, metatable, "listeners_once", event.name.c_str());

        // Forget the listeners that only wanted to be called once
        if (count_once > 0)
        {
          LUA->GetField(metatable, "listeners_once");
            LUA->PushNil();
            LUA->SetField(-2, event.name.c_str());
          LUA->Pop();
        }

        // The listeners were copied to the stack before the first one is
        // called, they can add and remove listeners without affecting this
        for (int i = 1; i <= count + count_once; i++)
        {
          LUA->Push(metatable + i);

          for (const auto &arg : event.args)
            arg.Push(LUA);

          // Errors in a listener must not get in the way of the others
          if (LUA->PCall((int)event.args.size(), 0, 0) != 0)
            reportError(LUA);
        }
      }

      LUA->Pop(LUA->Top() - top);
    }

    /**
     * @brief prints the error message at the top of the stack and pops it
     */
    static void reportError(ILuaBase *LUA)
    {
      LUA->PushSpecial(SPECIAL_GLOB);
      LUA->GetField(-1, "ErrorNoHalt");

      if (!LUA->IsType(-1, Type::Function))
      {
        LUA->Pop(3);
        return;
      }

      LUA->Push(-3);
      LUA->PushString("\n");

      if (LUA->PCall(2, 0, 0) != 0)
        LUA->Pop();

      LUA->Pop(2);
    }
  private:
    typedef std::map<ILuaBase*, std::shared_ptr<LuaEventEmitterManager>> managers_t;

    static managers_t &managers()
    {
      static managers_t _managers;
      return _managers;
    }

    GLOO_METHOD(think)
    {
      auto manager = managers().find(LUA);

      if (manager != managers().end())
        manager->second->Think(LUA);

      return 0;
    }
  public:
    /**
     * @brief get the manager of the supplied Lua interface, which is hooked
     *  to Think if it is not already. Main thread only.
     * @param LUA - Lua interface
     */
    static std::shared_ptr<LuaEventEmitterManager> Current(ILuaBase *LUA)
    {
      auto &manager = managers()[LUA];

      if (!manager)
        manager = std::make_shared<LuaEventEmitterManager>();

      manager->hookThink(LUA);
      return manager;
    }

    /**
     * @brief drops the manager of the supplied Lua interface together with
     *  the events it did not get to deliver. To be called when the module is
     *  closed.
     * @param LUA - Lua interface
     */
    static void Close(ILuaBase *LUA)
    {
      auto manager = managers().find(LUA);

      if (manager == managers().end())
        return;

      {
        std::lock_guard<std::mutex> lock(manager->second->_events_mtx);
        manager->second->_closed = true;
        manager->second->_events.clear();
      }

      managers().erase(manager);
    }
  }; // LuaEventEmitterManager

  template<class TChildObject>
  class LuaEventEmitter :
    public LuaObject<TChildObject>
  {
  public:
    LuaEventEmitter() :
      LuaObject<TChildObject>()
    {
      LuaObject<TChildObject>::AddMethod("on", on);
      LuaObject<TChildObject>::AddMethod("once", once);
      LuaObject<TChildObject>::AddMethod("add_listener", add_listener);
      LuaObject<TChildObject>::AddMethod("remove_listeners", remove_listeners);
    }
  private:
    /**
     * @brief appends the function at stack position 3 to the listeners of
     *  the event named at stack position 2, for the emitter at position 1
     */
    static void addListener(ILuaBase *LUA, bool once)
    {
      // Only here to check that this is called on an emitter
      LuaObject<TChildObject>::Pop(LUA, 1);

      if (!LUA->IsType(2, Type::String))
        throw std::invalid_argument("bad argument #2 (string expected)");
      if (!LUA->IsType(3, Type::Function))
        throw std::invalid_argument("bad argument #3 (function expected)");

      const char *field = once ? "listeners_once" : "listeners";

      LUA->GetMetaTable(1);
      LUA->GetField(-1, field);

      if (!LUA->IsType(-1, Type::Table))
      {
        LUA->Pop();
        LUA->CreateTable();
        LUA->Push(-1);
        LUA->SetField(-3, field);
      }

      LUA->GetField(-1, LUA->GetString(2));

      if (!LUA->IsType(-1, Type::Table))
      {
        LUA->Pop();
        LUA->CreateTable();
        LUA->Push(-1);
        LUA->SetField(-3, LUA->GetString(2));
      }

      LUA->PushNumber(LUA->ObjLen(-1) + 1);
      LUA->Push(3);
      LUA->SetTable(-3);
      LUA->Pop(3);
    }
  private:
    GLOO_METHOD(on)
    {
      addListener(LUA, false);
      return 0;
    }

    GLOO_METHOD(once)
    {
      addListener(LUA, true);
      return 0;
    }

    GLOO_METHOD(add_listener)
    {
      addListener(LUA, LUA->IsType(4, Type::Bool) && LUA->GetBool(4));
      return 0;
    }

    GLOO_METHOD(remove_listeners)
    {
      LuaObject<TChildObject>::Pop(LUA, 1);

      LUA->GetMetaTable(1);
        LUA->PushNil();
        LUA->SetField(-2, "listeners");
        LUA->PushNil();
        LUA->SetField(-2, "listeners_once");
      LUA->Pop();

      return 0;
    }
  }; // LuaEventEmitter

}} // GarrysMod::Lua

#endif//_GLOO_LUA_EVENT_H_
