/*
** $Id: linit.c,v 1.39.1.1 2017/04/19 17:20:42 roberto Exp $
** Initialization of libraries for lua.c and other clients
** 内嵌库的初始化
** See Copyright Notice in lua.h
*/


#define linit_c
#define LUA_LIB

/*
** If you embed Lua in your program and need to open the standard
** libraries, call luaL_openlibs in your program. If you need a
** different set of libraries, copy this file to your project and edit
** it to suit your needs.
**
** You can also *preload* libraries, so that a later 'require' can
** open the library, which is already linked to the application.
** For that, do the following code:
**
**  luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
**  lua_pushcfunction(L, luaopen_modname);
**  lua_setfield(L, -2, modname);
**  lua_pop(L, 1);  // remove PRELOAD table
*/

#include "lprefix.h"


#include <stddef.h>

#include "lua.h"

#include "lualib.h"
#include "lauxlib.h"

/*
** 扩展库实现重点需要分三个部分:
** 函数配置数组:主要定义了函数名称和对应的C语言函数,
**    数组最后一个对象为{NULL,NULL},因为数组遍历的时候会根据NULL做判断进行跳出
** 扩展库加载方法:入参为L,对当前的线程栈,方法内容必须实现luaL_newlib,
**    该函数主要将配置数组中的方法,遍历注册到一个module[funcname] = func 数组上,
**    该mudule数组,作为加载方法openf的结果放入栈上
** 函数实现:每个函数的入参都为lua_State* L,为当前线程栈  
**
** 为啥每个函数入参都为L,那么在Lua中调用的时候如何进行多个参数入参?
** 实际上,Lua语言还会解析语法本身,对于多个参数入参,也都会通过lua_push*方法,放入栈上,
** 所以当调用函数的时候,往栈上取参即可
*/


/*
** these libs are loaded by lua.c and are readily available to any Lua
** program
** Lua标准库常量
** 定义标准库名称和启动方法
*/
static const luaL_Reg loadedlibs[] = {
  {"_G", luaopen_base},  /* 全局基础方法聚合,常见的loadfile等函数都在这上面配置,在lua语言中,直接使用函数即可,无需使用库名字.方法名称方式 */
  {LUA_LOADLIBNAME, luaopen_package},
  {LUA_COLIBNAME, luaopen_coroutine},
  {LUA_TABLIBNAME, luaopen_table},
  {LUA_IOLIBNAME, luaopen_io},
  {LUA_OSLIBNAME, luaopen_os},
  {LUA_STRLIBNAME, luaopen_string},
  {LUA_MATHLIBNAME, luaopen_math},
  {LUA_UTF8LIBNAME, luaopen_utf8},
  {LUA_DBLIBNAME, luaopen_debug},
#if defined(LUA_COMPAT_BITLIB)
  {LUA_BITLIBNAME, luaopen_bit32},
#endif
  {NULL, NULL}  /* 主要用于在循环的时候遍历退出 */
};


/*
** 主要遍历一个loadedlibs数组,然后循环调用luaL_requiref方法,进行扩展库的逐个加载
** loadedlibs数组包含两个元素:扩展库名称和启动方法
*/
LUALIB_API void luaL_openlibs (lua_State *L) {
  const luaL_Reg *lib;
  /* "require" functions from 'loadedlibs' and set results to global table */
  for (lib = loadedlibs; lib->func; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1);  /* remove lib */
  }
}

