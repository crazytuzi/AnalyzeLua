/*
** $Id: lstate.h,v 2.133.1.1 2017/04/19 17:39:34 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*

** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized;
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).
**
** Moreover, there is another set of lists that control gray objects.
** These lists are linked by fields 'gclist'. (All objects that
** can become gray have such a field. The field is not the same
** in all objects, but it always has this name.)  Any gray object
** must belong to one of these lists, and all objects in these lists
** must be gray:
**
** 'gray': regular gray objects, still waiting to be visited.
** 'grayagain': objects that must be revisited at the atomic phase.
**   That includes
**   - black objects got in a write barrier;
**   - all kinds of weak tables during propagation phase;
**   - all threads.
** 'weak': tables with weak values to be cleared;
** 'ephemeron': ephemeron tables with white->white entries;
** 'allweak': tables with weak keys and/or weak values to be cleared.
** The last three lists are used only during the atomic phase.

*/


struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */


typedef struct stringtable {
  TString **hash;
  int nuse;  /* number of elements */
  int size;
} stringtable;


/*
** Information about a call.
** When a thread yields, 'func' is adjusted to pretend that the
** top function has only the yielded values in its stack; in that
** case, the actual 'func' value is saved in field 'extra'.
** When a function calls another with a continuation, 'extra' keeps
** the function index so that, in case of errors, the continuation
** function can be called with the correct top.
** 正在调用的函数一定存在于数据栈上
** func - 指向正在调用操作的栈底位置,也可以认为指向调用函数的位置
** top - 指向调用栈的栈顶部分,默认是LUA_MINSTACK=20个,大部分情况下够用了,
**       如果调用栈过大,则会抛出栈溢出的错误
** previous和next是双向链表指针,用于连接各个调用栈,
**       当执行完一个函数后,通过previous回退到上一个调用栈
*/
typedef struct CallInfo {
  StkId func;  /* function index in the stack - 当前调用栈的调用指针处 */
  StkId	top;  /* top for this function - 调用栈的栈顶 */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Lua functions */
      StkId base;  /* base for this function */
      const Instruction *savedpc;
    } l;
    struct {  /* only for C functions */
      lua_KFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      lua_KContext ctx;  /* context info. in case of yields */
    } c;
  } u;
  ptrdiff_t extra;
  short nresults;  /* expected number of results from this function */
  unsigned short callstatus;
} CallInfo;


/*
** Bits in CallInfo status
*/
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_LUA	(1<<1)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<2)	/* call is running a debug hook */
#define CIST_FRESH	(1<<3)	/* call is running on a fresh invocation
                                   of luaV_execute */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_TAIL	(1<<5)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */
#define CIST_LEQ	(1<<7)  /* using __lt for __le */
#define CIST_FIN	(1<<8)  /* call is running a finalizer */

#define isLua(ci)	((ci)->callstatus & CIST_LUA)

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** Lua通过实现全局的注册表,来管理全局变量、CAPI扩展库的加载等信息
** 注册表主要通过Table的数据结构进行管理,所以注册表是一个多维数组的结构
** 一般情况下,全局注册表通过lua_setfield和lua_getfield两个函数跟Lua的主线程栈进行通信
** lua_setfield:将栈顶L->top的值,赋值到T[k]上,并将栈顶数据pop弹出(L->top--),
**    当我们向一个Table结构进行设置值的时候,将栈顶L->top-1的值,设置到指定Table的指定索引上,并将栈顶值弹出
** lua_getfield:从T[k]上取到一个值,并将值放置到L->top栈顶上,并调整栈顶(L->top++),
**    要从全局注册表中获取一个数据值的时候,需要将注册表上的值压入栈顶,该值才能被后续的Lua程序调用
*/


/*
** 'global state', shared by all threads of this state
** Lua 全局状态机
** 管理全局数据,全局字符串表,内存管理函数,GC把所有对象串联起来的信息,内存等
** 
*/
typedef struct global_State {
  lua_Alloc frealloc;  /* function to reallocate memory - Lua的全局内存分配器.用户可以替换成自己的 */
  void *ud;         /* auxiliary data to 'frealloc' - 分配器的userdata */
  l_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  lu_mem GCmemtrav;  /* memory traversed by the GC */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
  stringtable strt;  /* hash table for strings - 字符串表 Lua的字符串分短字符串和长字符串 */
  TValue l_registry;
  unsigned int seed;  /* randomized seed for hashes */
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcrunning;  /* true if GC is running */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  GCObject *tobefnz;  /* list of userdata to be GC */
  GCObject *fixedgc;  /* list of objects not to be collected */
  struct lua_State *twups;  /* list of threads with open upvalues - 闭包了当前线程变量的其他线程列表 */
  unsigned int gcfinnum;  /* number of finalizers to call in each GC step */
  int gcpause;  /* size of pause between successive GCs */
  int gcstepmul;  /* GC 'granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  struct lua_State *mainthread;  /* 主线程 */
  const lua_Number *version;  /* pointer to version number - 版本号 */
  TString *memerrmsg;  /* memory-error message */
  TString *tmname[TM_N];  /* array with tag-method names - 预定义方法名字数组 */
  struct Table *mt[LUA_NUMTAGS];  /* metatables for basic types - 每个基本类型一个metatable(整个Lua最重要的Hook机制) */
  /*
  ** 二维数组strcache的表索引,通过字符串hash值,并对桶做求余实现
  ** 桶的默认值是STRCACHE_N=53个
  ** 链表的默认长度是STRCACHE_M=2个
  ** 估计作者认为hash冲突的概率会非常小,同时每次都会将最早的元素淘汰出去
  */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API - 字符串缓存 */
  /*
  ** 版本号
  ** const lua_Number *version  版本号
  **
  ** 内存管理
  ** lua_Alloc frealloc  Lua的全局内存分配器.用户可以替换成自己的
  ** void *ud  分配器的userdata
  **
  ** 线程管理
  ** struct lua_State  主线程
  ** struct lua_State *twups  闭包了当前线程变量的其他线程列表
  **
  ** 字符串管理
  ** stringtable strt  链式存储(短字符串<40)
  ** TString *strcache[STRCACHE_N][STRCACHE_M]  Hash缓存
  **
  ** 虚函数表
  ** TString *tmname[TM_N]  预定义方法名字数组
  ** struct Table *mt[LUA_NUMTAGS]  每个基本类型一个metatable(整个Lua最重要的Hook机制)
  **
  ** 错误处理
  ** lua_CFunction panic
  ** TString *memerrmsg
  **
  ** GC管理
  ** unsigned int gcfinnum
  ** int gcpause
  ** int gcstepmul
  **
  */
} global_State;


/*
** 'per thread' state
** Lua 主线程 栈 数据结构
** 管理整个栈和当前函数使用的栈的情况,最主要的功能就是函数调用以及和C的通信
** 
*/
struct lua_State {
  CommonHeader;
  unsigned short nci;  /* number of items in 'ci' list - 存储一共多少个CallInfo */
  lu_byte status;  /* 用于记录中间状态 */
  /* StkId为TValue数据 */
  StkId top;  /* first free slot in the stack - 线程栈的栈顶指针 */
  global_State *l_G;  /* 全局状态机 */
  /* CallInfo为双向链表结构 */
  CallInfo *ci;  /* call info for current function - 当前运行函数信息 */
  const Instruction *oldpc;  /* last pc traced - 在当前thread的解释执行指令的过程中,指向最后一次执行的指令的指针 */
  StkId stack_last;  /* last free slot in the stack - 线程栈的最后一个位置 */
  StkId stack;  /* stack base - 栈的指针,当前执行的位置 */
  UpVal *openupval;  /* list of open upvalues in this stack - 上值列表 */
  GCObject *gclist;  /* GC列表 */
  struct lua_State *twups;  /* list of threads with open upvalues */
  struct lua_longjmp *errorJmp;  /* current error recover point */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) - 调用栈的头部指针 */
  volatile lua_Hook hook;
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  int stacksize;
  int basehookcount;
  int hookcount;
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
  l_signalT hookmask;
  lu_byte allowhook;
  /*
  ** 调用栈: 调用栈信息管理
  ** unsigned short nci  存储一共多少个CallInfo
  ** CallInfo base_ci  调用栈的头部指针
  ** CallInfo *ci  当前运行函数信息
  ** 主要由一个CallInfo的结构组成,CallInfo是一个双向链表结构,用来管理每一个Lua的函数调用栈信息
  ** Lua一共有三种类型的函数:C语言闭包函数(如pmain)、Lua的C函数库(如str字符串函数)和Lua函数
  ** 每一个函数的调用,都会产生一个新的CallInfo的调用栈结构,用于管理函数调用的栈指针信息
  **    当一个函数调用结束后,会返回CallInfo链表的前一个调用栈,直到所有的调用栈结束回到base_ci
  ** 调用栈最终都会指向数据栈上,通过一个个调用栈,用于管理不同的函数调用
  ** 每次调用栈调用函数完成后,都会将函数返回的结果在栈上移动位置,将结果逐个从ci->func位置开始填充,
  **    并调整top栈顶指针,这样的好处是调用完一个函数后,数据栈上只需要存储最终函数返回的结果集,
  **    不会因为复杂的函数嵌套而导致整个栈体结构迅速扩大
  **
  ** 数据栈: 栈指针地址管理
  ** StkId top  线程栈的栈顶指针
  ** StkId stack_last  线程栈的最后一个位置
  ** StkId stack  栈的指针,当前执行的位置
  ** 主要由一个StkId结构的数组组成,StkId结构为TValue,支持字符串、函数、数字等数据结构
  ** 所有的数据都通过lapi.c文件中的lua_push*函数将不同类型的值压栈
  ** stack指向栈底部,每次将一个数据压栈,栈指针top都会指向下一个结构
  ** stacksize为栈大小,在栈结构初始化的时候,会分配一个BASIC_STACK_SIZE=40大小的栈结构
  ** stack_last指向栈头部,会留空EXTRA_STACK=5个buf,用于元表调用或错误处理的栈操作
  ** top为栈顶指针,压入数据,都通过移动栈顶指针来实现
  ** 
  ** Hook相关管理 - 服务于debug模块
  ** volatile lua_Hook hook
  ** ptrdiff_t errfunc
  ** int stacksize
  ** int basehookcount
  ** int hookcount
  ** l_signalT hookmask
  ** lu_byte allowhook
  ** 
  ** 跟C语言通信 管理
  ** unsigned short nCcalls
  ** unsigned short nny
  **
  */
};


#define G(L)	(L->l_G)


/*
** Union of all collectable objects (only for conversions)
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  /* thread */
};


#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))
#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))
#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))
#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))
#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))
#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))
#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))


/* macro to convert a Lua object into a GCObject */
#define obj2gco(v) \
	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);


#endif

