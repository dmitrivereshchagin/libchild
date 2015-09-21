/* io.c */

#include <windows.h>

#include <stdlib.h>

#include "include/child.h"
#include "child_private.h"

static inline void SetErrorFlag(DWORD dwError, HCHILD hcl)
{
	switch (dwError) {
	case ERROR_SUCCESS:
		break;
	case ERROR_BROKEN_PIPE:
	case ERROR_NO_DATA:
		hcl->iFlags |= CHILD_EPIPE;
		break;
	case ERROR_IO_INCOMPLETE:
		hcl->iFlags |= CHILD_ETIME;
		break;
	default:
		hcl->iFlags |= CHILD_EIO;
		break;
	}
}

static BOOL HandleOverlappedResult(BOOL bIoSuccess, DWORD dwIoError,
				   LPOVERLAPPED lpOverlapped,
				   LPDWORD lpNumberOfBytesTransferred,
				   HCHILD hcl)
{
	BOOL bSuccess;
	DWORD dwError;

	hcl->iFlags &= ~(CHILD_ETIME | CHILD_EIO);

	if (!bIoSuccess && dwIoError != ERROR_IO_PENDING) {
		SetErrorFlag(dwIoError, hcl);
		return FALSE;
	}

	WaitForSingleObject(lpOverlapped->hEvent, PIPE_TIMEOUT);

	bSuccess = GetOverlappedResult(hcl->hPipe, lpOverlapped,
				       lpNumberOfBytesTransferred, FALSE);
	dwError = GetLastError();
	if (!bSuccess) {
		if (dwError == ERROR_IO_INCOMPLETE)
			CancelIo(hcl->hPipe);
		SetErrorFlag(dwError, hcl);
		return FALSE;
	}

	return TRUE;
}

static BOOL DoReadPipe(HCHILD hcl)
{
	BOOL bSuccess;
	DWORD dwError;
	DWORD nRead;
	OVERLAPPED Overlapped = { 0 };

	Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	bSuccess = ReadFile(hcl->hPipe, hcl->pchReadBuf, PIPE_BUFSIZ,
			    NULL, &Overlapped);
	dwError = GetLastError();

	bSuccess = HandleOverlappedResult(bSuccess, dwError, &Overlapped,
					  &nRead, hcl);

	CloseHandle(Overlapped.hEvent);

	if (!bSuccess)
		return FALSE;

	hcl->nReadCount = nRead;
	if (--hcl->nReadCount < 0) {
		hcl->nReadCount = 0;
		return FALSE;
	}

	return TRUE;
}

static int FillBuffer(HCHILD hcl)
{
	if (hcl->pchReadBuf == NULL) {
		hcl->pchReadBuf = (char *) malloc(PIPE_BUFSIZ);
		if (hcl->pchReadBuf == NULL)
			return CHILD_EOF;
	}
	hcl->pchReadPos = hcl->pchReadBuf;
	if (!DoReadPipe(hcl))
		return CHILD_EOF;
	return (unsigned char) *hcl->pchReadPos++;
}

#define GetChar(hcl) (--(hcl)->nReadCount >= 0 \
		? *(hcl)->pchReadPos++ : FillBuffer(hcl))

_CHILDIMP char *__cdecl child_gets(char *sz, int n, HCHILD hcl)
{
	register int ch;
	register char *pch;

	pch = sz;
	while (--n > 0 && (ch = GetChar(hcl)) != CHILD_EOF) {
		if (ch != '\r')
			*pch++ = ch;
		else
			n++;
		if (ch == '\n')
			break;
	}
	*pch = '\0';
	return (ch == CHILD_EOF && pch == sz) ? NULL : sz;
}

static BOOL DoWritePipe(HCHILD hcl)
{
	BOOL bSuccess;
	DWORD dwError;
	DWORD nToWrite;
	DWORD nWritten;
	OVERLAPPED Overlapped = { 0 };

	Overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	nToWrite = hcl->pchWritePos - hcl->pchWriteBuf;
	bSuccess = WriteFile(hcl->hPipe, hcl->pchWriteBuf, nToWrite,
			     NULL, &Overlapped);
	dwError = GetLastError();

	bSuccess = HandleOverlappedResult(bSuccess, dwError,
					  &Overlapped, &nWritten, hcl);

	CloseHandle(Overlapped.hEvent);

	if (!bSuccess || nToWrite != nWritten)
		return FALSE;

	return TRUE;
}

static int FlushBuffer(int ch, HCHILD hcl)
{
	if (hcl->pchWriteBuf == NULL) {
		hcl->pchWriteBuf = (char *) malloc(PIPE_BUFSIZ);
		if (hcl->pchWriteBuf == NULL)
			return CHILD_EOF;
	} else if (!DoWritePipe(hcl)) {
		return CHILD_EOF;
	}
	hcl->nWriteCount = PIPE_BUFSIZ - 1;
	hcl->pchWritePos = hcl->pchWriteBuf;
	*hcl->pchWritePos++ = (char)ch;
	return ch;
}

static inline int Flush(HCHILD hcl)
{
	int ch;

	ch = FlushBuffer(0, hcl);
	hcl->nWriteCount = PIPE_BUFSIZ;
	hcl->pchWritePos = hcl->pchWriteBuf;
	return ch;
}

#define PutChar(ch, hcl) (--(hcl)->nWriteCount >= 0 \
		? *(hcl)->pchWritePos++ = (char) (ch) : FlushBuffer((ch), hcl))

_CHILDIMP int __cdecl child_puts(const char *csz, HCHILD hcl)
{
	int ch;

	while ((ch = *csz++)) {
		if (ch == '\n') {
			PutChar('\r', hcl);
			PutChar('\n', hcl);
			Flush(hcl);
		} else {
			PutChar(ch, hcl);
		}
	}
	return (hcl->iFlags & (CHILD_ETIME | CHILD_EPIPE | CHILD_EIO))
	    ? CHILD_EOF : 0;
}

_CHILDIMP int __cdecl child_flush(HCHILD hcl)
{
	return Flush(hcl);
}
