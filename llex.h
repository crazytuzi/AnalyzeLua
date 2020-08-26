/*
** $Id: llex.h,v 1.79.1.1 2017/04/19 17:20:42 roberto Exp $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include "lobject.h"
#include "lzio.h"


/*
** Token定义:Lua会对脚本语言逐个分割出最小单位Token
** Lua通过luaX_next逐个读取字符串流字符,直到分割出一个完整的Token(每次分割一个)
** Token包含计算机语言的基础保留符号(;和{}等)、Lua保留字(nil和if等)和其他标记Token关键值
** Token包含各种保留字和基础符号等,同时针对字符串/数字等类型,Token结构提供了SemInfo来保存语法信息
*/


#define FIRST_RESERVED	257


#if !defined(LUA_ENV)
#define LUA_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
* Token的类型是用int来存储的,枚举RESERVED中包含了两部分类型:Lua系统关键字和其他标记Token关键值
* FIRST_RESERVED = 257,相当于将系统基础符号的类型给留空
* 基础保留符号类型,则直接返回单个字符(每个符号对应是一个int类型编号)
*/
enum RESERVED {
  /* terminal symbols denoted by reserved words - 系统默认关键字 */
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR, TK_REPEAT,
  TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols - 其他关键字 */
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};

/* number of reserved words */
#define NUM_RESERVED	(cast(int, TK_WHILE-FIRST_RESERVED+1))


/*
** 语法辅助信息
*/
typedef union {
  lua_Number r;
  lua_Integer i;
  TString *ts;
} SemInfo;  /* semantics information - 语义信息 */


/*
** 语义分割最小单位
*/
typedef struct Token {
  int token;  /* Token类型 */
  SemInfo seminfo;  /* 语义信息 */
} Token;


/* state of the lexer plus state of the parser when shared by all
   functions */
/*
** 语法分析上下文状态,该结构贯穿编译过程的始终,主要存储解析和编译过程中的语法树状态
*/
typedef struct LexState {
  int current;  /* current character (charint) - 解析字符指针 */
  int linenumber;  /* input line counter - 行数计数器 */
  int lastline;  /* line of last token 'consumed' - 最后一行 */
  Token t;  /* current token - 当前Token */
  Token lookahead;  /* look ahead token - 头部Token */
  struct FuncState *fs;  /* current function (parser) - 当前解析的方法 */
  struct lua_State *L;  /* Lua栈 */
  ZIO *z;  /* input stream - io输入流 */
  Mbuffer *buff;  /* buffer for tokens */
  Table *h;  /* to avoid collection/reuse strings */
  struct Dyndata *dyd;  /* dynamic structures used by the parser */
  TString *source;  /* current source name - 当前源名称 */
  TString *envn;  /* environment variable name - 环境变量 */
} LexState;


LUAI_FUNC void luaX_init (lua_State *L);
LUAI_FUNC void luaX_setinput (lua_State *L, LexState *ls, ZIO *z,
                              TString *source, int firstchar);
LUAI_FUNC TString *luaX_newstring (LexState *ls, const char *str, size_t l);
LUAI_FUNC void luaX_next (LexState *ls);
LUAI_FUNC int luaX_lookahead (LexState *ls);
LUAI_FUNC l_noret luaX_syntaxerror (LexState *ls, const char *s);
LUAI_FUNC const char *luaX_token2str (LexState *ls, int token);


#endif
