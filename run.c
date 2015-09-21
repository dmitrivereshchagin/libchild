/* run.c */

#define _WIN32_WINNT 0x0600

#define MINGW_HAS_SECURE_API

#include <windows.h>
#include <winsafer.h>
#include <rpc.h>

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>

#include "include/child.h"
#include "child_private.h"

#ifdef UNICODE
typedef RPC_WSTR RPC_TSTR;
#else
typedef RPC_CSTR RPC_TSTR;
#endif				/* UNICODE */

extern errno_t getenv_s(size_t *, char *, size_t, const char *);

static int CheckEnvironment(const _TCHAR *cszName)
{
	_TCHAR *szValue;
	int iResult = 0;
	size_t nSize;

	_tgetenv_s(&nSize, NULL, 0, cszName);
	if (nSize == 0)
		return 0;

	szValue = (_TCHAR *) malloc(nSize * sizeof(_TCHAR));
	if (szValue == NULL)
		return 0;

	_tgetenv_s(&nSize, szValue, nSize, cszName);
	if (!_tcsicmp(szValue, _TEXT("yes")))
		iResult = 1;

	free(szValue);
	return iResult;
}

static inline BOOL RestrictToken(PHANDLE phToken)
{
	BOOL bResult = TRUE;
	BOOL bSuccess;
	DWORD dwLevelId;
	SAFER_LEVEL_HANDLE LevelHandle;

	dwLevelId = CheckEnvironment(LIBCHILD_UNTRUSTED)
	    ? SAFER_LEVELID_UNTRUSTED : SAFER_LEVELID_CONSTRAINED;

	bSuccess = SaferCreateLevel(SAFER_SCOPEID_USER, dwLevelId,
				    SAFER_LEVEL_OPEN, &LevelHandle, NULL);
	if (!bSuccess)
		return FALSE;

	bSuccess = SaferComputeTokenFromLevel(LevelHandle, NULL, phToken,
					      0, NULL);
	if (!bSuccess)
		bResult = FALSE;

	SaferCloseLevel(LevelHandle);
	return bResult;
}

static inline void ChangeErrorMode(PUINT puOldMode)
{
	UINT uMode;

	uMode = GetErrorMode();
	*puOldMode = uMode;
	if (CheckEnvironment(LIBCHILD_SILENT))
		uMode |= ERROR_MODE_FLAGS;
	else
		uMode &= ~ERROR_MODE_FLAGS;
	SetErrorMode(uMode);
}

static _TCHAR *GeneratePipeName(void)
{
	UUID Uuid;
	RPC_TSTR StringUuid;
	_TCHAR *szName;
	int nNeeded;

	if (UuidCreate(&Uuid) != RPC_S_OK)
		return NULL;
	if (UuidToString(&Uuid, &StringUuid) != RPC_S_OK)
		return NULL;

	nNeeded = _sntprintf(NULL, 0, PIPE_PREFIX _TEXT("%s"), StringUuid);
	szName = (_TCHAR *) malloc(nNeeded * sizeof(_TCHAR));
	if (szName != NULL)
		_stprintf(szName, PIPE_PREFIX _TEXT("%s"), StringUuid);

	RpcStringFree(&StringUuid);
	return szName;
}

static BOOL CreateRedirectionPipe(PHANDLE phPipeLocal,
				  PHANDLE phPipeInheritable)
{
	BOOL bResult = TRUE;
	DWORD dwOpenMode;
	DWORD dwPipeMode;
	_TCHAR *szName;

	SECURITY_ATTRIBUTES SecurityAttributes = {
		.nLength = sizeof(SECURITY_ATTRIBUTES),
		.lpSecurityDescriptor = NULL,
		.bInheritHandle = TRUE
	};

	szName = GeneratePipeName();
	if (szName == NULL)
		return FALSE;

	dwOpenMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE |
	    FILE_FLAG_OVERLAPPED;

	dwPipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;

	*phPipeLocal = CreateNamedPipe(szName, dwOpenMode, dwPipeMode, 1,
				       PIPE_BUFSIZ, PIPE_BUFSIZ, 0, NULL);
	if (*phPipeLocal == INVALID_HANDLE_VALUE) {
		bResult = FALSE;
		goto envOut;
	}

	*phPipeInheritable = CreateFile(szName, GENERIC_READ | GENERIC_WRITE,
					0, &SecurityAttributes, OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL, NULL);
	free(szName);
	if (*phPipeInheritable == INVALID_HANDLE_VALUE)
		bResult = FALSE;

 envOut:
	free(szName);
	return bResult;
}

static HANDLE CreateAndAdjustJobObject(void)
{
	BOOL bSuccess;
	HANDLE hJob;

	JOBOBJECT_EXTENDED_LIMIT_INFORMATION JobObjectInfo = {
		.BasicLimitInformation.LimitFlags =
		    JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
		    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
		.BasicLimitInformation.ActiveProcessLimit = 1
	};

	hJob = CreateJobObject(NULL, NULL);
	if (hJob == NULL)
		return NULL;

	bSuccess = SetInformationJobObject(hJob,
					   JobObjectExtendedLimitInformation,
					   &JobObjectInfo,
					   sizeof(JobObjectInfo));
	if (!bSuccess) {
		CloseHandle(hJob);
		return NULL;
	}

	return hJob;
}

static BOOL StartChildProcess(const _TCHAR *cszCommand, HANDLE hJob,
			      LPSTARTUPINFO lpStartupInfo)
{
	BOOL bResult = TRUE;
	BOOL bSuccess;
	HANDLE hToken;
	PROCESS_INFORMATION ProcessInformation;
	UINT uOldMode;
	_TCHAR *szCommand;

	szCommand = _tcsdup(cszCommand);
	if (szCommand == NULL)
		return FALSE;

	bSuccess = RestrictToken(&hToken);
	if (!bSuccess) {
		bResult = FALSE;
		goto envOut;
	}

	ChangeErrorMode(&uOldMode);

	bSuccess = CreateProcessAsUser(hToken, NULL, szCommand, NULL, NULL,
				       TRUE, CREATE_SUSPENDED, NULL, NULL,
				       lpStartupInfo, &ProcessInformation);

	SetErrorMode(uOldMode);

	if (bSuccess) {
		AssignProcessToJobObject(hJob, ProcessInformation.hProcess);
		ResumeThread(ProcessInformation.hThread);

		CloseHandle(ProcessInformation.hProcess);
		CloseHandle(ProcessInformation.hThread);
	} else {
		bResult = FALSE;
	}

	CloseHandle(hToken);

 envOut:
	free(szCommand);
	return bResult;
}

_CHILDIMP HCHILD __cdecl child_create(const _TCHAR *cszCommand)
{
	BOOL bSuccess;
	HANDLE hPipeInheritable;
	HANDLE hPipeLocal;
	HANDLE hJob;
	HCHILD hcl;
	STARTUPINFO StartupInfo = { 0 };

	hcl = (HCHILD) malloc(sizeof(CHILD));
	if (hcl == NULL)
		return NULL;

	hJob = CreateAndAdjustJobObject();
	if (hJob == NULL) {
		free(hcl);
		return NULL;
	}

	bSuccess = CreateRedirectionPipe(&hPipeLocal, &hPipeInheritable);
	if (!bSuccess) {
		free(hcl);
		CloseHandle(hJob);
		return NULL;
	}

	StartupInfo.cb = sizeof(STARTUPINFO);
	StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	StartupInfo.hStdInput = hPipeInheritable;
	StartupInfo.hStdOutput = hPipeInheritable;
	StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	bSuccess = StartChildProcess(cszCommand, hJob, &StartupInfo);

	CloseHandle(hPipeInheritable);

	if (!bSuccess) {
		free(hcl);
		CloseHandle(hJob);
		CloseHandle(hPipeLocal);
		return NULL;
	}

	hcl->hJob = hJob;
	hcl->hPipe = hPipeLocal;
	hcl->pchReadBuf = NULL;
	hcl->nReadCount = 0;
	hcl->pchWriteBuf = NULL;
	hcl->nWriteCount = 0;
	hcl->iFlags = 0;

	return hcl;
}

_CHILDIMP void __cdecl child_terminate(HCHILD hcl)
{
	CloseHandle(hcl->hJob);
	CloseHandle(hcl->hPipe);

	free(hcl->pchReadBuf);
	free(hcl->pchWriteBuf);
	free(hcl);
}
