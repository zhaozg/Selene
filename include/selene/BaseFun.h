#pragma once

#include "function.h"
#include <exception>
#include "ExceptionHandler.h"
#include <functional>
#include "primitives.h"
#include <tuple>
#include "util.h"

namespace sel {
struct BaseFun {
    virtual ~BaseFun() {}
    virtual int Apply(lua_State *state) = 0;
};

#if LUA_VERSION_NUM == 501
inline
const char *lua51_pushlstring (lua_State *L, const char *s, size_t len) {
  lua_pushlstring(L, len > 0 ? s : "", len);
  return lua_tostring(L, -1);
}
#endif

namespace detail {

inline int _lua_dispatcher(lua_State *l) {
    BaseFun *fun = (BaseFun *)lua_touserdata(l, lua_upvalueindex(1));
    _lua_check_get raiseParameterConversionError = nullptr;
    const char * wrong_meta_table = nullptr;
    int erroneousParameterIndex = 0;
    try {
        return fun->Apply(l);
    } catch (GetParameterFromLuaTypeError & e) {
        raiseParameterConversionError = e.checked_get;
        erroneousParameterIndex = e.index;
    } catch (GetUserdataParameterFromLuaTypeError & e) {
#if LUA_VERSION_NUM == 501
        wrong_meta_table = lua51_pushlstring(
            l, e.metatable_name.c_str(), e.metatable_name.length());
#else
        wrong_meta_table = lua_pushlstring(
            l, e.metatable_name.c_str(), e.metatable_name.length());
#endif
        erroneousParameterIndex = e.index;
    } catch (std::exception & e) {
        lua_pushstring(l, e.what());
        Traceback(l);
        store_current_exception(l, lua_tostring(l, -1));
    } catch (...) {
        lua_pushliteral(l, "<Unknown exception>");
        Traceback(l);
        store_current_exception(l, lua_tostring(l, -1));
    }

    if(raiseParameterConversionError) {
        raiseParameterConversionError(l, erroneousParameterIndex);
    }
    else if(wrong_meta_table) {
        luaL_checkudata(l, erroneousParameterIndex, wrong_meta_table);
    }

    return lua_error(l);
}

template <typename Ret, typename... Args, std::size_t... N>
inline Ret _lift(std::function<Ret(Args...)> fun,
                 std::tuple<Args...> args,
                 _indices<N...>) {
    return fun(std::get<N>(args)...);
}

template <typename Ret, typename... Args>
inline Ret _lift(std::function<Ret(Args...)> fun,
                 std::tuple<Args...> args) {
    return _lift(fun, args, typename _indices_builder<sizeof...(Args)>::type());
}


template <typename... T, std::size_t... N>
inline std::tuple<T...> _get_args(lua_State *state, _indices<N...>) {
    return std::tuple<T...>{_check_get(_id<T>{}, state, N + 1)...};
}

template <typename... T>
inline std::tuple<T...> _get_args(lua_State *state) {
    constexpr std::size_t num_args = sizeof...(T);
    return _get_args<T...>(state, typename _indices_builder<num_args>::type());
}
}
}
