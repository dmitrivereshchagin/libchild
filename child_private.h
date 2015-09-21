/* child_private.h */

#ifndef _CHILD_PRIVATE_H
#define _CHILD_PRIVATE_H

#include <windows.h>

#include <tchar.h>

#define PIPE_BUFSIZ		1024
#define PIPE_TIMEOUT		1000
#define PIPE_PREFIX		_TEXT("\\\\.\\pipe\\")

#define LIBCHILD_UNTRUSTED	_TEXT("LIBCHILD_UNTRUSTED")
#define LIBCHILD_SILENT		_TEXT("LIBCHILD_SILENT")

#define ERROR_MODE_FLAGS	(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX)

typedef struct _CHILD {
	HANDLE hJob;
	HANDLE hPipe;
	int nReadCount;
	char *pchReadBuf;
	char *pchReadPos;
	int nWriteCount;
	char *pchWriteBuf;
	char *pchWritePos;
	int iFlags;
} CHILD;

#endif				/* _CHILD_PRIVATE_H */
