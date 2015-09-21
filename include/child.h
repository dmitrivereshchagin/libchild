/* child.h */

#ifndef _CHILD_H_
#define _CHILD_H_

#include <tchar.h>

#define CHILD_EOF	(-1)

enum {
	CHILD_EPIPE = 01,
	CHILD_ETIME = 02,
	CHILD_EIO = 04
};

typedef struct _CHILD *HCHILD;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _EXPORTING
#define _CHILDIMP	__declspec(dllexport)
#else
#define _CHILDIMP	__declspec(dllimport)
#endif

_CHILDIMP HCHILD __cdecl child_create(const _TCHAR * cszCommand);

_CHILDIMP void __cdecl child_terminate(HCHILD hcl);

_CHILDIMP char *__cdecl child_gets(char *sz, int n, HCHILD hcl);

_CHILDIMP int __cdecl child_puts(const char *csz, HCHILD hcl);

_CHILDIMP int __cdecl child_flush(HCHILD hcl);

_CHILDIMP int __cdecl child_flags(HCHILD hcl);

#ifdef __cplusplus
}
#endif

#define child_broken_pipe(hcl)	(child_flags(hcl) & CHILD_EPIPE)

#define child_timeout(hcl)	(child_flags(hcl) & CHILD_ETIME)

#define child_io_error(hcl)	(child_flags(hcl) & CHILD_EIO)

#endif				/* _CHILD_H_ */
