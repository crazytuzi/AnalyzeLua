/*
** $Id: lzio.h,v 1.31.1.1 2017/04/19 17:20:42 roberto Exp $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.h"

#include "lmem.h"


#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

/*
** 逐个读取字符
** 语法解析原理:通过逐个读取文件流数据,通过关键字Token,然后分割不同的代码语句statement
*/
#define zgetc(z)  (((z)->n--)>0 ?  cast_uchar(*(z)->p++) : luaZ_fill(z))


typedef struct Mbuffer {
  char *buffer;
  size_t n;
  size_t buffsize;
} Mbuffer;

#define luaZ_initbuffer(L, buff) ((buff)->buffer = NULL, (buff)->buffsize = 0)

#define luaZ_buffer(buff)	((buff)->buffer)
#define luaZ_sizebuffer(buff)	((buff)->buffsize)
#define luaZ_bufflen(buff)	((buff)->n)

#define luaZ_buffremove(buff,i)	((buff)->n -= (i))
#define luaZ_resetbuffer(buff) ((buff)->n = 0)


#define luaZ_resizebuffer(L, buff, size) \
	((buff)->buffer = luaM_reallocvchar(L, (buff)->buffer, \
				(buff)->buffsize, size), \
	(buff)->buffsize = size)

#define luaZ_freebuffer(L, buff)	luaZ_resizebuffer(L, buff, 0)


LUAI_FUNC void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader,
                                        void *data);
LUAI_FUNC size_t luaZ_read (ZIO* z, void *b, size_t n);	/* read next n bytes */



/* --------- Private Part ------------------ */

/*
** 存储文件流读取的状态信息
*/
struct Zio {
  size_t n;			/* bytes still unread - 未读取数量 */
  const char *p;		/* current position in buffer - 读取buf指针地址 */
  lua_Reader reader;		/* reader function - 文件读取方法 */
  void *data;			/* additional data - buf指针地址 */
  lua_State *L;			/* Lua state (for reader) - 当前线程栈地址 */
};


LUAI_FUNC int luaZ_fill (ZIO *z);

#endif
