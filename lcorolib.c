/*
** $Id: lcorolib.c,v 1.10.1.1 2017/04/19 17:20:42 roberto Exp $
** Coroutine Library
** See Copyright Notice in lua.h
*/

#define lcorolib_c
#define LUA_LIB

#include "lprefix.h"


#include <stdlib.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


/*
** 协程不是进程或线程,其执行过程更类似于子例程,或者说不带返回值的函数调用
** Lua没有独立的线程,所以每次执行脚本的时候,都是单线程执行
** 线程相对资源独立,有自己的上下文,由系统切换调用
** 协程也相对独立,有自己的上下文,但是其切换由自己控制,由当前协程切换到其他协程由当前协程来控制
** 协程的设计思路:
** Lua通过luaB_cocreate函数,去创建一个协程栈结构,这个协程栈结构是独立的数据栈结构,但是跟主线程栈共用了全局状态机global_State
** 当创建完新的协程栈,主线程上会将协程L和回调函数入栈(lua_pushvalue)
** 一个协程回调函数里面,可能还会嵌套和包含其他的协程,所以,协程是支持嵌套的
** 当我们调用luaB_coresume函数的时候,才会真正去执行协程的回调函数,该函数有两种状态需要处理:正常状态和yield挂起中断状态
**     正常状态处理:正常状态,最终会调用luaD_precall和luaV_execute,执行C闭包函数/内置API函数或者执行Lua字节码
**     中断状态处理:如果是中断状态,则在Lua的coroutine回调函数中,执行了luaB_yield方法,resume主要将中断的操作进行恢复
** 中断过程是通过抛出异常的方式完成的,当回调函数中调用luaB_yield中断处理的时候,会抛出一个LUA_YIELD的异常,
**     resume函数是通过luaD_rawrunprotected异常保护方法去执行的,所以代码会跳到LUAI_TRY点,
**     然后根据L->status状态判断是中断还是正常状态,执行不同的代码逻辑
*/


static lua_State *getco (lua_State *L) {
  lua_State *co = lua_tothread(L, 1);
  luaL_argcheck(L, co, 1, "thread expected");
  return co;
}


/*
** L - 原始线程栈
** co - 要启动的线程栈
** 如果返回值不是LUA_OK或LUA_YIELD,则表示错误,将co栈顶的错误对象转移到L栈顶
** 否则表示协程函数返回或中途有yield操作,将co栈中的返回值全部转移到L栈
*/
static int auxresume (lua_State *L, lua_State *co, int narg) {
  int status;
  if (!lua_checkstack(co, narg)) {
    lua_pushliteral(L, "too many arguments to resume");
    return -1;  /* error flag */
  }
  if (lua_status(co) == LUA_OK && lua_gettop(co) == 0) {
    lua_pushliteral(L, "cannot resume dead coroutine");
    return -1;  /* error flag */
  }
  lua_xmove(L, co, narg);  /* 将L上的栈数据拷贝到co上 */
  status = lua_resume(co, L, narg);
  if (status == LUA_OK || status == LUA_YIELD) {
    int nres = lua_gettop(co);
    if (!lua_checkstack(L, nres + 1)) {
      lua_pop(co, nres);  /* remove results anyway */
      lua_pushliteral(L, "too many results to resume");
      return -1;  /* error flag */
    }
    lua_xmove(co, L, nres);  /* move yielded values */
    return nres;
  }
  else {
    lua_xmove(co, L, 1);  /* move error message */
    return -1;  /* error flag */
  }
}


/*
** 当创建完一个协程后,就需要启动该协程
** 主要功能:启动 & 恢复 协程
** 首先调用getco方法从主线程栈上获取协程栈结构(如果多层嵌套就是上一层的栈)
** 然后调用auxresume方法,如果小于0表示出错,则栈顶是一个错误对象,此时在错误对象前面插入一个false
**     否则表示成功,栈顶是由协程函数或yield返回的参数,在这些参数之下插入一个true
** 线程相对资源独立,有自己的上下文,由系统切换调用
** 协程也相对独立,有自己的上下文,但是其切换由自己控制,由当前协程切换到其他协程由当前协程来控制
*/
static int luaB_coresume (lua_State *L) {
  lua_State *co = getco(L);  /* 获取协程栈 */
  int r;
  r = auxresume(L, co, lua_gettop(L) - 1);
  if (r < 0) {
    lua_pushboolean(L, 0);
    lua_insert(L, -2);
    return 2;  /* return false + error message */
  }
  else {
    lua_pushboolean(L, 1);
    lua_insert(L, -(r + 1));
    return r + 1;  /* return true + 'resume' returns */
  }
}


static int luaB_auxwrap (lua_State *L) {
  lua_State *co = lua_tothread(L, lua_upvalueindex(1));
  int r = auxresume(L, co, lua_gettop(L));
  if (r < 0) {
    if (lua_type(L, -1) == LUA_TSTRING) {  /* error object is a string? */
      luaL_where(L, 1);  /* add extra info */
      lua_insert(L, -2);
      lua_concat(L, 2);
    }
    return lua_error(L);  /* propagate error */
  }
  return r;
}


/*
** 协程创建函数,会创建一个独立的Lua栈结构
** 通过lua_newthread创建一个新的协程栈(lua_State),协程栈独立管理内部的栈资源
** 将CallInfo操作栈上的协程回调函数,移动到L->top数据栈顶部
** 拷贝回调函数到协程的数据栈上
*/
static int luaB_cocreate (lua_State *L) {
  lua_State *NL;
  luaL_checktype(L, 1, LUA_TFUNCTION);  /* 创建一个新协程 */
  NL = lua_newthread(L);
  lua_pushvalue(L, 1);  /* move function to top - 将CallInfo操作栈上的协程回调函数,移动到L->top数据栈顶部 */
  lua_xmove(L, NL, 1);  /* move function from L to NL - 拷贝回调函数到协程的数据栈上 */
  return 1;
}


static int luaB_cowrap (lua_State *L) {
  luaB_cocreate(L);
  lua_pushcclosure(L, luaB_auxwrap, 1);
  return 1;
}


/*
** 协程挂起函数
** L - 当前的协程栈
** 为何参数L是当前协程栈呢?因为挂起函数,一般都是在协程回调函数内部使用,
**     回调函数是被resume函数执行的,执行环境为当前协程栈环境
*/
static int luaB_yield (lua_State *L) {
  return lua_yield(L, lua_gettop(L));
}


static int luaB_costatus (lua_State *L) {
  lua_State *co = getco(L);
  if (L == co) lua_pushliteral(L, "running");
  else {
    switch (lua_status(co)) {
      case LUA_YIELD:
        lua_pushliteral(L, "suspended");
        break;
      case LUA_OK: {
        lua_Debug ar;
        if (lua_getstack(co, 0, &ar) > 0)  /* does it have frames? */
          lua_pushliteral(L, "normal");  /* it is running */
        else if (lua_gettop(co) == 0)
            lua_pushliteral(L, "dead");
        else
          lua_pushliteral(L, "suspended");  /* initial state */
        break;
      }
      default:  /* some error occurred */
        lua_pushliteral(L, "dead");
        break;
    }
  }
  return 1;
}


static int luaB_yieldable (lua_State *L) {
  lua_pushboolean(L, lua_isyieldable(L));
  return 1;
}


static int luaB_corunning (lua_State *L) {
  int ismain = lua_pushthread(L);
  lua_pushboolean(L, ismain);
  return 2;
}


static const luaL_Reg co_funcs[] = {
  {"create", luaB_cocreate},
  {"resume", luaB_coresume},
  {"running", luaB_corunning},
  {"status", luaB_costatus},
  {"wrap", luaB_cowrap},
  {"yield", luaB_yield},
  {"isyieldable", luaB_yieldable},
  {NULL, NULL}
};



LUAMOD_API int luaopen_coroutine (lua_State *L) {
  luaL_newlib(L, co_funcs);
  return 1;
}

