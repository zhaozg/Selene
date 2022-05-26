#pragma once

#include "ExceptionHandler.h"
#include <iostream>
#include <memory>
#include <string>
#include "Registry.h"
#include "Selector.h"
#include <tuple>
#include "util.h"
#include <vector>

namespace sel {

#if LUA_VERSION_NUM == 501

#define lua_getfield(L, i, k) \
  (lua_getfield((L), (i), (k)), lua_type((L), -1))

inline int luaL_getsubtable (lua_State *L, int i, const char *name) {
  int abs_i = lua_absindex(L, i);
  luaL_checkstack(L, 3, "not enough stack slots");
  lua_pushstring(L, name);
  lua_gettable(L, abs_i);
  if (lua_istable(L, -1))
    return 1;
  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushstring(L, name);
  lua_pushvalue(L, -2);
  lua_settable(L, abs_i);
  return 0;
}
inline void luaL_requiref (lua_State *L, const char *modname,
                           lua_CFunction openf, int glb) {
  luaL_checkstack(L, 3, "not enough stack slots available");
  luaL_getsubtable(L, LUA_REGISTRYINDEX, "_LOADED");
  if (lua_getfield(L, -1, modname) == LUA_TNIL) {
    lua_pop(L, 1);
    lua_pushcfunction(L, openf);
    lua_pushstring(L, modname);
    lua_call(L, 1, 1);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, modname);
  }
  if (glb) {
    lua_pushvalue(L, -1);
    lua_setglobal(L, modname);
  }
  lua_replace(L, -2);
}
#endif

class State {
private:
    lua_State *_l;
    bool _l_owner;
    std::unique_ptr<Registry> _registry;
    std::unique_ptr<ExceptionHandler> _exception_handler;

public:
    State() : State(false) {}
    State(bool should_open_libs) : _l(nullptr), _l_owner(true), _exception_handler(new ExceptionHandler) {
        _l = luaL_newstate();
        if (_l == nullptr) throw 0;
        if (should_open_libs) luaL_openlibs(_l);
        _registry.reset(new Registry(_l));
        HandleExceptionsPrintingToStdOut();
    }
    State(lua_State *l) : _l(l), _l_owner(false), _exception_handler(new ExceptionHandler) {
        _registry.reset(new Registry(_l));
        HandleExceptionsPrintingToStdOut();
    }
    State(const State &other) = delete;
    State &operator=(const State &other) = delete;
    State(State &&other)
        : _l(other._l),
          _l_owner(other._l_owner),
          _registry(std::move(other._registry)) {
        other._l = nullptr;
    }
    State &operator=(State &&other) {
        if (&other == this) return *this;
        _l = other._l;
        _l_owner = other._l_owner;
        _registry = std::move(other._registry);
        other._l = nullptr;
        return *this;
    }
    ~State() {
        if (_l != nullptr && _l_owner) {
            ForceGC();
            lua_close(_l);
        }
        _l = nullptr;
    }

    int Size() const {
        return lua_gettop(_l);
    }

    bool Load(const std::string &file) {
        ResetStackOnScopeExit savedStack(_l);
        int status = luaL_loadfile(_l, file.c_str());
#if LUA_VERSION_NUM >= 502
        auto const lua_ok = LUA_OK;
#else
        auto const lua_ok = 0;
#endif
        if (status != lua_ok) {
            if (status == LUA_ERRSYNTAX) {
                const char *msg = lua_tostring(_l, -1);
                _exception_handler->Handle(status, msg ? msg : file + ": syntax error");
            } else if (status == LUA_ERRFILE) {
                const char *msg = lua_tostring(_l, -1);
                _exception_handler->Handle(status, msg ? msg : file + ": file error");
            }
            return false;
        }

        status = lua_pcall(_l, 0, LUA_MULTRET, 0);
        if(status == lua_ok) {
            return true;
        }

        const char *msg = lua_tostring(_l, -1);
        _exception_handler->Handle(status, msg ? msg : file + ": dofile failed");
        return false;
    }

    void OpenLib(const std::string& modname, lua_CFunction openf) {
        ResetStackOnScopeExit savedStack(_l);
        luaL_requiref(_l, modname.c_str(), openf, 1);
    }

    void HandleExceptionsPrintingToStdOut() {
        *_exception_handler = ExceptionHandler([](int, std::string msg, std::exception_ptr){_print(msg);});
    }

    void HandleExceptionsWith(ExceptionHandler::function handler) {
        *_exception_handler = ExceptionHandler(std::move(handler));
    }

public:
    Selector operator[](const char *name) const {
        return Selector(_l, *_registry, *_exception_handler, name);
    }

    bool operator()(const char *code) {
        ResetStackOnScopeExit savedStack(_l);
        int status = luaL_dostring(_l, code);
        if(status) {
            _exception_handler->Handle_top_of_stack(status, _l);
            return false;
        }
        return true;
    }
    void ForceGC() {
        lua_gc(_l, LUA_GCCOLLECT, 0);
    }

    void InteractiveDebug() {
        luaL_dostring(_l, "debug.debug()");
    }

    friend std::ostream &operator<<(std::ostream &os, const State &state);
};

inline std::ostream &operator<<(std::ostream &os, const State &state) {
    os << "sel::State - " << state._l;
    return os;
}
}
