// RegListener.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#define BUFSIZE 8192

WCHAR MYROOT[BUFSIZE];
WCHAR MYKEY[BUFSIZE];
WCHAR MYVALUE[BUFSIZE];
WCHAR MYFILE[BUFSIZE];

SERVICE_STATUS        g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE                g_ServiceStopEvent = INVALID_HANDLE_VALUE;

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv);
VOID WINAPI ServiceCtrlHandler(DWORD);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);

#define SERVICE_NAME  _T("UU Registry Listener")    

void logText(const WCHAR *fmt, ...) {
	static WCHAR tmpbuf[BUFSIZE];
	HANDLE log = RegisterEventSourceW(NULL, TEXT("UU Registry Listener"));
	WCHAR *bufs = { tmpbuf };
	va_list myargs;
	va_start(myargs, fmt);
	vswprintf_s(tmpbuf, BUFSIZE, fmt, myargs);
	va_end(myargs);
	ReportEventW(log, EVENTLOG_ERROR_TYPE, 1, 61337, NULL, 0, 2*wcslen(tmpbuf), NULL, tmpbuf);
	DeregisterEventSource(log);
}

void DisplayError(LPTSTR lpszFunction)
// Routine Description:
// Retrieve and output the system error message for the last-error code
{
	LPVOID lpMsgBuf;
	LPVOID lpDisplayBuf;
	DWORD dw = GetLastError();

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		dw,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf,
		0,
		NULL);

	lpDisplayBuf =
		(LPVOID)LocalAlloc(LMEM_ZEROINIT,
		(lstrlen((LPCTSTR)lpMsgBuf)
			+ lstrlen((LPCTSTR)lpszFunction)
			+ 40) // account for format string
			* sizeof(TCHAR));

	if (FAILED(StringCchPrintf((LPTSTR)lpDisplayBuf,
		LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		TEXT("%s failed with error code %d as follows:\n%s"),
		lpszFunction,
		dw,
		lpMsgBuf)))
	{
		printf("FATAL ERROR: Unable to output error code.\n");
	}

	logText(TEXT("ERROR: %s\n"), (LPCTSTR)lpDisplayBuf);

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
}

int doInit(TCHAR *root, TCHAR *key, TCHAR *value, TCHAR *file, HANDLE *hFile, HKEY *hKey, HANDLE *hEvent, DWORD *curtype, char *curvalbuf, DWORD *curvalbufsize)
{
	HKEY   hMainKey;
	LONG   lErrorCode;
	
	// Convert parameters to appropriate handles.
	if (_tcscmp(TEXT("HKLM"), root) == 0) hMainKey = HKEY_LOCAL_MACHINE;
	else if (_tcscmp(TEXT("HKU"), root) == 0) hMainKey = HKEY_USERS;
	else if (_tcscmp(TEXT("HKCU"), root) == 0) hMainKey = HKEY_CURRENT_USER;
	else if (_tcscmp(TEXT("HKCR"), root) == 0) hMainKey = HKEY_CLASSES_ROOT;
	else if (_tcscmp(TEXT("HCC"), root) == 0) hMainKey = HKEY_CURRENT_CONFIG;
	else
	{
		logText(TEXT("Usage: notify [HKLM|HKU|HKCU|HKCR|HCC] [<subkey>]\n"));
		return 0;
	}

	*hFile = CreateFile(file,
		FILE_APPEND_DATA,
		FILE_SHARE_READ,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
		NULL);

	if (*hFile == INVALID_HANDLE_VALUE)
	{
		DisplayError(TEXT("CreateFile"));
		logText(TEXT("Terminal failure: Unable to open file \"%s\" for write.\n"), file);
		return 0;
	}

	// Open a key.
	lErrorCode = RegOpenKeyEx(hMainKey, key, 0, KEY_NOTIFY | KEY_QUERY_VALUE, hKey);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegOpenKeyEx (%s) (%d).\n"), key, lErrorCode);
		return 0;
	}

	lErrorCode = RegQueryValueEx(*hKey, value, NULL, curtype, (LPBYTE)curvalbuf, curvalbufsize);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegQueryValueEx (%s) (%d).\n"), value, lErrorCode);
		return 0;
	}

	// Create an event.
	*hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (*hEvent == NULL)
	{
		logText(TEXT("Error in CreateEvent (%d).\n"), GetLastError());
		return 0;
	}
	return 1;
}

int doClose(HKEY *hKey, HANDLE *hEvent, HANDLE *hFile) {
	LONG lErrorCode = RegCloseKey(*hKey);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegCloseKey (%d).\n"), GetLastError());
		return 0;
	}

	// Close the handle.
	if (!CloseHandle(*hEvent))
	{
		logText(TEXT("Error in CloseHandle.\n"));
		return 0;
	}

	CloseHandle(*hFile);
	return 1;
}

int doListen(TCHAR *value, HKEY *hKey, HANDLE *hEvent, HANDLE *hFile, DWORD *curtype, char *curvalbuf, DWORD *curvalbufsize) {
	DWORD  dwFilter = REG_NOTIFY_CHANGE_LAST_SET;
	char newvalbuf[BUFSIZE];
	DWORD newvalbufsize;
	DWORD newtype;
	char valtmpbuf[BUFSIZE];
	char tmpbuf[BUFSIZE * 3];
	// Watch the registry key for a change of value.
	LONG lErrorCode = RegNotifyChangeKeyValue(*hKey,
		FALSE,
		dwFilter,
		*hEvent,
		TRUE);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegNotifyChangeKeyValue (%d).\n"), lErrorCode);
		return 0;
	}

	// Wait for an event to occur.
	if (WaitForSingleObject(*hEvent, INFINITE) == WAIT_FAILED)
	{
		logText(TEXT("Error in WaitForSingleObject (%d).\n"), GetLastError());
		return 0;
	}
	else {
		newvalbufsize = BUFSIZE;
		lErrorCode = RegQueryValueEx(*hKey, value, NULL, &newtype, (LPBYTE)newvalbuf, &newvalbufsize);
		if (lErrorCode != ERROR_SUCCESS)
		{
			logText(TEXT("Error in RegQueryValueEx (%d).\n"), lErrorCode);
			return 0;
		}
		if (newtype != *curtype || newvalbufsize != *curvalbufsize || memcmp(newvalbuf, curvalbuf, newvalbufsize)) {
			*curtype = newtype;
			*curvalbufsize = newvalbufsize;
			memcpy(curvalbuf, newvalbuf, newvalbufsize);
			if (*curtype == REG_SZ) {
				WideCharToMultiByte(CP_UTF8, 0, (LPCWCH)newvalbuf, newvalbufsize / 2, valtmpbuf, sizeof(valtmpbuf), NULL, NULL);
				valtmpbuf[newvalbufsize / 2] = '\0';
			}
			else if (*curtype == REG_DWORD) {
				DWORD *foo = (DWORD *)curvalbuf;
				sprintf_s(valtmpbuf, sizeof(valtmpbuf), "%d", *foo);
			}
			else {	
				sprintf_s(valtmpbuf, sizeof(valtmpbuf), "Unhandled value type %d", *curtype);
			}
			SYSTEMTIME st;
			char curdate[64];
			GetLocalTime(&st);
			sprintf_s(curdate, sizeof(curdate), "%04d-%02d-%02d %02d:%02d:%02d.%d",
				st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
			sprintf_s(tmpbuf, sizeof(tmpbuf), "%s\t%s\n", curdate, valtmpbuf);
			WriteFile(*hFile, tmpbuf, strlen(tmpbuf), NULL, NULL);
		}
	}
	return 1;
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
	char curvalbuf[BUFSIZE];
	DWORD curvalbufsize = BUFSIZE;
	DWORD curtype;
	HANDLE hFile;
	HKEY hKey;
	HANDLE hEvent;
	int isOk=doInit(MYROOT, MYKEY, MYVALUE, MYFILE, &hFile, &hKey, &hEvent, &curtype, curvalbuf, &curvalbufsize);

	//  Periodically check if the service has been requested to stop
	while (isOk && WaitForSingleObject(g_ServiceStopEvent, 0) != WAIT_OBJECT_0)
	{

		isOk=doListen(MYVALUE, &hKey, &hEvent, &hFile, &curtype, curvalbuf, &curvalbufsize);
	}

	doClose(&hKey, &hEvent, &hFile);
	return isOk?ERROR_SUCCESS:-255;
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
	switch (CtrlCode)
	{
	case SERVICE_CONTROL_STOP:

		if (g_ServiceStatus.dwCurrentState != SERVICE_RUNNING)
			break;

		/*
		* Perform tasks necessary to stop the service here
		*/

		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
		g_ServiceStatus.dwWin32ExitCode = 0;
		g_ServiceStatus.dwCheckPoint = 4;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(_T(
				"UU Registry Listener: ServiceCtrlHandler: SetServiceStatus returned error"));
		}

		// This will signal the worker thread to start shutting down
		SetEvent(g_ServiceStopEvent);

		break;

	default:
		break;
	}
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv)
{
	DWORD Status = E_FAIL;

	// Register our service control handler with the SCM
	g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);

	if (g_StatusHandle == NULL)
	{
		goto EXIT;
	}

	// Tell the service controller we are starting
	ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
	g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwServiceSpecificExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T(
			"UU Registry Listener: ServiceMain: SetServiceStatus returned error"));
	}

	/*
	* Perform tasks necessary to start the service here
	*/

	// Create a service stop event to wait on later
	g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (g_ServiceStopEvent == NULL)
	{
		// Error creating event
		// Tell service controller we are stopped and exit
		g_ServiceStatus.dwControlsAccepted = 0;
		g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
		g_ServiceStatus.dwWin32ExitCode = GetLastError();
		g_ServiceStatus.dwCheckPoint = 1;

		if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
		{
			OutputDebugString(_T(
				"UU Registry Listener: ServiceMain: SetServiceStatus returned error"));
		}
		goto EXIT;
	}

	// Tell the service controller we are started
	g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 0;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T(
			"UU Registry Listener: ServiceMain: SetServiceStatus returned error"));
	}

	// Start a thread that will perform the main task of the service
	HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

	// Wait until our worker thread exits signaling that the service needs to stop
	WaitForSingleObject(hThread, INFINITE);


	/*
	* Perform any cleanup tasks
	*/

	CloseHandle(g_ServiceStopEvent);

	// Tell the service controller we are stopped
	g_ServiceStatus.dwControlsAccepted = 0;
	g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
	g_ServiceStatus.dwWin32ExitCode = 0;
	g_ServiceStatus.dwCheckPoint = 3;

	if (SetServiceStatus(g_StatusHandle, &g_ServiceStatus) == FALSE)
	{
		OutputDebugString(_T(
			"UU Registry Listener: ServiceMain: SetServiceStatus returned error"));
	}

EXIT:
	return;
}

int _tmain(int argc, TCHAR *argv[])
{
	// Open a key.
	HKEY tmpKey;
	DWORD type;
	DWORD bufsz;
	LONG lErrorCode = RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\UU\\Registry Listener"), 0, KEY_QUERY_VALUE, &tmpKey);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in (config) RegOpenKeyEx (%d).\n"), lErrorCode);
		return 0;
	}
	bufsz = BUFSIZE;
	lErrorCode = RegQueryValueEx(tmpKey, TEXT("Root"), NULL, &type, (LPBYTE)MYROOT, &bufsz);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegQueryValueEx (%d).\n"), lErrorCode);
		return 0;
	}
	bufsz = BUFSIZE;
	lErrorCode = RegQueryValueEx(tmpKey, TEXT("Key"), NULL, &type, (LPBYTE)MYKEY, &bufsz);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegQueryValueEx (%d).\n"), lErrorCode);
		return 0;
	}
	bufsz = BUFSIZE;
	lErrorCode = RegQueryValueEx(tmpKey, TEXT("Value"), NULL, &type, (LPBYTE)MYVALUE, &bufsz);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegQueryValueEx (%d).\n"), lErrorCode);
		return 0;
	}
	bufsz = BUFSIZE;
	lErrorCode = RegQueryValueEx(tmpKey, TEXT("File"), NULL, &type, (LPBYTE)MYFILE, &bufsz);
	if (lErrorCode != ERROR_SUCCESS)
	{
		logText(TEXT("Error in RegQueryValueEx (%d).\n"), lErrorCode);
		return 0;
	}

	SERVICE_TABLE_ENTRY ServiceTable[] =
	{
		{ SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
		{ NULL, NULL }
	};

	if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
	{
		return GetLastError();
	}

	return 0;
}