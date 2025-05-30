/**
 * WinPR: Windows Portable Runtime
 * Serial Communication API
 *
 * Copyright 2011 O.S. Systems Software Ltda.
 * Copyright 2011 Eduardo Fiss Beloni <beloni@ossystems.com.br>
 * Copyright 2014 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2014 Hewlett-Packard Development Company, L.P.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <winpr/config.h>

#include <winpr/assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#if defined(WINPR_HAVE_SYS_EVENTFD_H)
#include <sys/eventfd.h>
#endif
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <winpr/crt.h>
#include <winpr/comm.h>
#include <winpr/tchar.h>
#include <winpr/wlog.h>
#include <winpr/handle.h>

#include "comm_ioctl.h"

#include "../log.h"
#define TAG WINPR_TAG("comm")

/**
 * Communication Resources:
 * http://msdn.microsoft.com/en-us/library/windows/desktop/aa363196/
 */

#include "comm.h"

static wLog* sLog = NULL;

struct comm_device
{
	LPTSTR name;
	LPTSTR path;
};

typedef struct comm_device COMM_DEVICE;

/* FIXME: get a clever data structure, see also io.h functions */
/* _CommDevices is a NULL-terminated array with a maximum of COMM_DEVICE_MAX COMM_DEVICE */
#define COMM_DEVICE_MAX 128
static COMM_DEVICE** sCommDevices = NULL;
static CRITICAL_SECTION sCommDevicesLock = { 0 };

static pthread_once_t sCommInitialized = PTHREAD_ONCE_INIT;

static const _SERIAL_IOCTL_NAME S_SERIAL_IOCTL_NAMES[] = {
	{ IOCTL_SERIAL_SET_BAUD_RATE, "IOCTL_SERIAL_SET_BAUD_RATE" },
	{ IOCTL_SERIAL_GET_BAUD_RATE, "IOCTL_SERIAL_GET_BAUD_RATE" },
	{ IOCTL_SERIAL_SET_LINE_CONTROL, "IOCTL_SERIAL_SET_LINE_CONTROL" },
	{ IOCTL_SERIAL_GET_LINE_CONTROL, "IOCTL_SERIAL_GET_LINE_CONTROL" },
	{ IOCTL_SERIAL_SET_TIMEOUTS, "IOCTL_SERIAL_SET_TIMEOUTS" },
	{ IOCTL_SERIAL_GET_TIMEOUTS, "IOCTL_SERIAL_GET_TIMEOUTS" },
	{ IOCTL_SERIAL_GET_CHARS, "IOCTL_SERIAL_GET_CHARS" },
	{ IOCTL_SERIAL_SET_CHARS, "IOCTL_SERIAL_SET_CHARS" },
	{ IOCTL_SERIAL_SET_DTR, "IOCTL_SERIAL_SET_DTR" },
	{ IOCTL_SERIAL_CLR_DTR, "IOCTL_SERIAL_CLR_DTR" },
	{ IOCTL_SERIAL_RESET_DEVICE, "IOCTL_SERIAL_RESET_DEVICE" },
	{ IOCTL_SERIAL_SET_RTS, "IOCTL_SERIAL_SET_RTS" },
	{ IOCTL_SERIAL_CLR_RTS, "IOCTL_SERIAL_CLR_RTS" },
	{ IOCTL_SERIAL_SET_XOFF, "IOCTL_SERIAL_SET_XOFF" },
	{ IOCTL_SERIAL_SET_XON, "IOCTL_SERIAL_SET_XON" },
	{ IOCTL_SERIAL_SET_BREAK_ON, "IOCTL_SERIAL_SET_BREAK_ON" },
	{ IOCTL_SERIAL_SET_BREAK_OFF, "IOCTL_SERIAL_SET_BREAK_OFF" },
	{ IOCTL_SERIAL_SET_QUEUE_SIZE, "IOCTL_SERIAL_SET_QUEUE_SIZE" },
	{ IOCTL_SERIAL_GET_WAIT_MASK, "IOCTL_SERIAL_GET_WAIT_MASK" },
	{ IOCTL_SERIAL_SET_WAIT_MASK, "IOCTL_SERIAL_SET_WAIT_MASK" },
	{ IOCTL_SERIAL_WAIT_ON_MASK, "IOCTL_SERIAL_WAIT_ON_MASK" },
	{ IOCTL_SERIAL_IMMEDIATE_CHAR, "IOCTL_SERIAL_IMMEDIATE_CHAR" },
	{ IOCTL_SERIAL_PURGE, "IOCTL_SERIAL_PURGE" },
	{ IOCTL_SERIAL_GET_HANDFLOW, "IOCTL_SERIAL_GET_HANDFLOW" },
	{ IOCTL_SERIAL_SET_HANDFLOW, "IOCTL_SERIAL_SET_HANDFLOW" },
	{ IOCTL_SERIAL_GET_MODEMSTATUS, "IOCTL_SERIAL_GET_MODEMSTATUS" },
	{ IOCTL_SERIAL_GET_DTRRTS, "IOCTL_SERIAL_GET_DTRRTS" },
	{ IOCTL_SERIAL_GET_COMMSTATUS, "IOCTL_SERIAL_GET_COMMSTATUS" },
	{ IOCTL_SERIAL_GET_PROPERTIES, "IOCTL_SERIAL_GET_PROPERTIES" },
	// {IOCTL_SERIAL_XOFF_COUNTER,	"IOCTL_SERIAL_XOFF_COUNTER"},
	// {IOCTL_SERIAL_LSRMST_INSERT,	"IOCTL_SERIAL_LSRMST_INSERT"},
	{ IOCTL_SERIAL_CONFIG_SIZE, "IOCTL_SERIAL_CONFIG_SIZE" },
	// {IOCTL_SERIAL_GET_STATS,	"IOCTL_SERIAL_GET_STATS"},
	// {IOCTL_SERIAL_CLEAR_STATS,	"IOCTL_SERIAL_CLEAR_STATS"},
	// {IOCTL_SERIAL_GET_MODEM_CONTROL,"IOCTL_SERIAL_GET_MODEM_CONTROL"},
	// {IOCTL_SERIAL_SET_MODEM_CONTROL,"IOCTL_SERIAL_SET_MODEM_CONTROL"},
	// {IOCTL_SERIAL_SET_FIFO_CONTROL,	"IOCTL_SERIAL_SET_FIFO_CONTROL"},

	// {IOCTL_PAR_QUERY_INFORMATION,	"IOCTL_PAR_QUERY_INFORMATION"},
	// {IOCTL_PAR_SET_INFORMATION,	"IOCTL_PAR_SET_INFORMATION"},
	// {IOCTL_PAR_QUERY_DEVICE_ID,	"IOCTL_PAR_QUERY_DEVICE_ID"},
	// {IOCTL_PAR_QUERY_DEVICE_ID_SIZE,"IOCTL_PAR_QUERY_DEVICE_ID_SIZE"},
	// {IOCTL_IEEE1284_GET_MODE,	"IOCTL_IEEE1284_GET_MODE"},
	// {IOCTL_IEEE1284_NEGOTIATE,	"IOCTL_IEEE1284_NEGOTIATE"},
	// {IOCTL_PAR_SET_WRITE_ADDRESS,	"IOCTL_PAR_SET_WRITE_ADDRESS"},
	// {IOCTL_PAR_SET_READ_ADDRESS,	"IOCTL_PAR_SET_READ_ADDRESS"},
	// {IOCTL_PAR_GET_DEVICE_CAPS,	"IOCTL_PAR_GET_DEVICE_CAPS"},
	// {IOCTL_PAR_GET_DEFAULT_MODES,	"IOCTL_PAR_GET_DEFAULT_MODES"},
	// {IOCTL_PAR_QUERY_RAW_DEVICE_ID, "IOCTL_PAR_QUERY_RAW_DEVICE_ID"},
	// {IOCTL_PAR_IS_PORT_FREE,	"IOCTL_PAR_IS_PORT_FREE"},

	{ IOCTL_USBPRINT_GET_1284_ID, "IOCTL_USBPRINT_GET_1284_ID" }
};

const char* _comm_serial_ioctl_name(ULONG number)
{
	for (size_t x = 0; x < ARRAYSIZE(S_SERIAL_IOCTL_NAMES); x++)
	{
		const _SERIAL_IOCTL_NAME* cur = &S_SERIAL_IOCTL_NAMES[x];
		if (cur->number == number)
			return cur->name;
	}

	return "(unknown ioctl name)";
}

static int CommGetFd(HANDLE handle)
{
	WINPR_COMM* comm = (WINPR_COMM*)handle;

	if (!CommIsHandled(handle))
		return -1;

	return comm->fd;
}

const HANDLE_CREATOR* GetCommHandleCreator(void)
{
#if defined(WINPR_HAVE_SERIAL_SUPPORT)
	static const HANDLE_CREATOR sCommHandleCreator = { .IsHandled = IsCommDevice,
		                                               .CreateFileA = CommCreateFileA };
	return &sCommHandleCreator;
#else
	return NULL;
#endif
}

static void CommInit(void)
{
	/* NB: error management to be done outside of this function */
	WINPR_ASSERT(sLog == NULL);
	WINPR_ASSERT(sCommDevices == NULL);
	sCommDevices = (COMM_DEVICE**)calloc(COMM_DEVICE_MAX + 1, sizeof(COMM_DEVICE*));

	if (!sCommDevices)
		return;

	if (!InitializeCriticalSectionEx(&sCommDevicesLock, 0, 0))
	{
		free((void*)sCommDevices);
		sCommDevices = NULL;
		return;
	}

	sLog = WLog_Get(TAG);
	WINPR_ASSERT(sLog != NULL);
}

/**
 * Returns TRUE when the comm module is correctly initialized, FALSE otherwise
 * with ERROR_DLL_INIT_FAILED set as the last error.
 */
static BOOL CommInitialized(void)
{
	if (pthread_once(&sCommInitialized, CommInit) != 0)
	{
		SetLastError(ERROR_DLL_INIT_FAILED);
		return FALSE;
	}

	return TRUE;
}

void CommLog_PrintEx(DWORD level, const char* file, size_t line, const char* fkt, ...)
{
	if (!CommInitialized())
		return;

	if (!WLog_IsLevelActive(sLog, level))
		return;
	va_list ap = { 0 };
	va_start(ap, fkt);
	WLog_PrintMessageVA(sLog, WLOG_MESSAGE_TEXT, level, line, file, fkt, ap);
	va_end(ap);
}

BOOL BuildCommDCBA(WINPR_ATTR_UNUSED LPCSTR lpDef, WINPR_ATTR_UNUSED LPDCB lpDCB)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL BuildCommDCBW(WINPR_ATTR_UNUSED LPCWSTR lpDef, WINPR_ATTR_UNUSED LPDCB lpDCB)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL BuildCommDCBAndTimeoutsA(WINPR_ATTR_UNUSED LPCSTR lpDef, WINPR_ATTR_UNUSED LPDCB lpDCB,
                              WINPR_ATTR_UNUSED LPCOMMTIMEOUTS lpCommTimeouts)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL BuildCommDCBAndTimeoutsW(WINPR_ATTR_UNUSED LPCWSTR lpDef, WINPR_ATTR_UNUSED LPDCB lpDCB,
                              WINPR_ATTR_UNUSED LPCOMMTIMEOUTS lpCommTimeouts)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL CommConfigDialogA(WINPR_ATTR_UNUSED LPCSTR lpszName, WINPR_ATTR_UNUSED HWND hWnd,
                       WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL CommConfigDialogW(WINPR_ATTR_UNUSED LPCWSTR lpszName, WINPR_ATTR_UNUSED HWND hWnd,
                       WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL GetCommConfig(HANDLE hCommDev, WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC,
                   WINPR_ATTR_UNUSED LPDWORD lpdwSize)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hCommDev;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL SetCommConfig(HANDLE hCommDev, WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC,
                   WINPR_ATTR_UNUSED DWORD dwSize)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hCommDev;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL GetCommMask(HANDLE hFile, WINPR_ATTR_UNUSED PDWORD lpEvtMask)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL SetCommMask(HANDLE hFile, WINPR_ATTR_UNUSED DWORD dwEvtMask)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL GetCommModemStatus(HANDLE hFile, WINPR_ATTR_UNUSED PDWORD lpModemStat)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

/**
 * ERRORS:
 *   ERROR_DLL_INIT_FAILED
 *   ERROR_INVALID_HANDLE
 */
BOOL GetCommProperties(HANDLE hFile, LPCOMMPROP lpCommProp)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;
	DWORD bytesReturned = 0;

	if (!CommIsHandleValid(hFile))
		return FALSE;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_GET_PROPERTIES, NULL, 0, lpCommProp,
	                         sizeof(COMMPROP), &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "GetCommProperties failure.");
		return FALSE;
	}

	return TRUE;
}

/**
 *
 *
 * ERRORS:
 *   ERROR_INVALID_HANDLE
 *   ERROR_INVALID_DATA
 *   ERROR_IO_DEVICE
 *   ERROR_OUTOFMEMORY
 */
BOOL GetCommState(HANDLE hFile, LPDCB lpDCB)
{
	DCB* lpLocalDcb = NULL;
	struct termios currentState;
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;
	DWORD bytesReturned = 0;

	if (!CommIsHandleValid(hFile))
		return FALSE;

	if (!lpDCB)
	{
		SetLastError(ERROR_INVALID_DATA);
		return FALSE;
	}

	if (lpDCB->DCBlength < sizeof(DCB))
	{
		SetLastError(ERROR_INVALID_DATA);
		return FALSE;
	}

	if (tcgetattr(pComm->fd, &currentState) < 0)
	{
		SetLastError(ERROR_IO_DEVICE);
		return FALSE;
	}

	lpLocalDcb = (DCB*)calloc(1, lpDCB->DCBlength);

	if (lpLocalDcb == NULL)
	{
		SetLastError(ERROR_OUTOFMEMORY);
		return FALSE;
	}

	/* error_handle */
	lpLocalDcb->DCBlength = lpDCB->DCBlength;
	SERIAL_BAUD_RATE baudRate;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_GET_BAUD_RATE, NULL, 0, &baudRate,
	                         sizeof(SERIAL_BAUD_RATE), &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "GetCommState failure: could not get the baud rate.");
		goto error_handle;
	}

	lpLocalDcb->BaudRate = baudRate.BaudRate;
	lpLocalDcb->fBinary = (currentState.c_cflag & ICANON) == 0;

	if (!lpLocalDcb->fBinary)
	{
		CommLog_Print(WLOG_WARN, "Unexpected nonbinary mode, consider to unset the ICANON flag.");
	}

	lpLocalDcb->fParity = (currentState.c_iflag & INPCK) != 0;
	SERIAL_HANDFLOW handflow;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_GET_HANDFLOW, NULL, 0, &handflow,
	                         sizeof(SERIAL_HANDFLOW), &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "GetCommState failure: could not get the handflow settings.");
		goto error_handle;
	}

	lpLocalDcb->fOutxCtsFlow = (handflow.ControlHandShake & SERIAL_CTS_HANDSHAKE) != 0;
	lpLocalDcb->fOutxDsrFlow = (handflow.ControlHandShake & SERIAL_DSR_HANDSHAKE) != 0;

	if (handflow.ControlHandShake & SERIAL_DTR_HANDSHAKE)
	{
		lpLocalDcb->fDtrControl = DTR_CONTROL_HANDSHAKE;
	}
	else if (handflow.ControlHandShake & SERIAL_DTR_CONTROL)
	{
		lpLocalDcb->fDtrControl = DTR_CONTROL_ENABLE;
	}
	else
	{
		lpLocalDcb->fDtrControl = DTR_CONTROL_DISABLE;
	}

	lpLocalDcb->fDsrSensitivity = (handflow.ControlHandShake & SERIAL_DSR_SENSITIVITY) != 0;
	lpLocalDcb->fTXContinueOnXoff = (handflow.FlowReplace & SERIAL_XOFF_CONTINUE) != 0;
	lpLocalDcb->fOutX = (handflow.FlowReplace & SERIAL_AUTO_TRANSMIT) != 0;
	lpLocalDcb->fInX = (handflow.FlowReplace & SERIAL_AUTO_RECEIVE) != 0;
	lpLocalDcb->fErrorChar = (handflow.FlowReplace & SERIAL_ERROR_CHAR) != 0;
	lpLocalDcb->fNull = (handflow.FlowReplace & SERIAL_NULL_STRIPPING) != 0;

	if (handflow.FlowReplace & SERIAL_RTS_HANDSHAKE)
	{
		lpLocalDcb->fRtsControl = RTS_CONTROL_HANDSHAKE;
	}
	else if (handflow.FlowReplace & SERIAL_RTS_CONTROL)
	{
		lpLocalDcb->fRtsControl = RTS_CONTROL_ENABLE;
	}
	else
	{
		lpLocalDcb->fRtsControl = RTS_CONTROL_DISABLE;
	}

	// FIXME: how to get the RTS_CONTROL_TOGGLE state? Does it match the UART 16750's Autoflow
	// Control Enabled bit in its Modem Control Register (MCR)
	lpLocalDcb->fAbortOnError = (handflow.ControlHandShake & SERIAL_ERROR_ABORT) != 0;
	/* lpLocalDcb->fDummy2 not used */
	lpLocalDcb->wReserved = 0; /* must be zero */
	lpLocalDcb->XonLim = WINPR_ASSERTING_INT_CAST(WORD, handflow.XonLimit);
	lpLocalDcb->XoffLim = WINPR_ASSERTING_INT_CAST(WORD, handflow.XoffLimit);
	SERIAL_LINE_CONTROL lineControl = { 0 };

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_GET_LINE_CONTROL, NULL, 0, &lineControl,
	                         sizeof(SERIAL_LINE_CONTROL), &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "GetCommState failure: could not get the control settings.");
		goto error_handle;
	}

	lpLocalDcb->ByteSize = lineControl.WordLength;
	lpLocalDcb->Parity = lineControl.Parity;
	lpLocalDcb->StopBits = lineControl.StopBits;
	SERIAL_CHARS serialChars;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_GET_CHARS, NULL, 0, &serialChars,
	                         sizeof(SERIAL_CHARS), &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "GetCommState failure: could not get the serial chars.");
		goto error_handle;
	}

	lpLocalDcb->XonChar = serialChars.XonChar;
	lpLocalDcb->XoffChar = serialChars.XoffChar;
	lpLocalDcb->ErrorChar = serialChars.ErrorChar;
	lpLocalDcb->EofChar = serialChars.EofChar;
	lpLocalDcb->EvtChar = serialChars.EventChar;
	memcpy(lpDCB, lpLocalDcb, lpDCB->DCBlength);
	free(lpLocalDcb);
	return TRUE;
error_handle:
	free(lpLocalDcb);
	return FALSE;
}

/**
 * @return TRUE on success, FALSE otherwise.
 *
 * As of today, SetCommState() can fail half-way with some settings
 * applied and some others not. SetCommState() returns on the first
 * failure met. FIXME: or is it correct?
 *
 * ERRORS:
 *   ERROR_INVALID_HANDLE
 *   ERROR_IO_DEVICE
 */
BOOL SetCommState(HANDLE hFile, LPDCB lpDCB)
{
	struct termios upcomingTermios = { 0 };
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;
	DWORD bytesReturned = 0;

	/* FIXME: validate changes according GetCommProperties? */

	if (!CommIsHandleValid(hFile))
		return FALSE;

	if (!lpDCB)
	{
		SetLastError(ERROR_INVALID_DATA);
		return FALSE;
	}

	/* NB: did the choice to call ioctls first when available and
	   then to setup upcomingTermios. Don't mix both stages. */
	/** ioctl calls stage **/
	SERIAL_BAUD_RATE baudRate;
	baudRate.BaudRate = lpDCB->BaudRate;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_SET_BAUD_RATE, &baudRate, sizeof(SERIAL_BAUD_RATE),
	                         NULL, 0, &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "SetCommState failure: could not set the baud rate.");
		return FALSE;
	}

	SERIAL_CHARS serialChars;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_GET_CHARS, NULL, 0, &serialChars,
	                         sizeof(SERIAL_CHARS), &bytesReturned,
	                         NULL)) /* as of today, required for BreakChar */
	{
		CommLog_Print(WLOG_WARN, "SetCommState failure: could not get the initial serial chars.");
		return FALSE;
	}

	serialChars.XonChar = lpDCB->XonChar;
	serialChars.XoffChar = lpDCB->XoffChar;
	serialChars.ErrorChar = lpDCB->ErrorChar;
	serialChars.EofChar = lpDCB->EofChar;
	serialChars.EventChar = lpDCB->EvtChar;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_SET_CHARS, &serialChars, sizeof(SERIAL_CHARS),
	                         NULL, 0, &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "SetCommState failure: could not set the serial chars.");
		return FALSE;
	}

	SERIAL_LINE_CONTROL lineControl;
	lineControl.StopBits = lpDCB->StopBits;
	lineControl.Parity = lpDCB->Parity;
	lineControl.WordLength = lpDCB->ByteSize;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_SET_LINE_CONTROL, &lineControl,
	                         sizeof(SERIAL_LINE_CONTROL), NULL, 0, &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "SetCommState failure: could not set the control settings.");
		return FALSE;
	}

	SERIAL_HANDFLOW handflow = { 0 };

	if (lpDCB->fOutxCtsFlow)
	{
		handflow.ControlHandShake |= SERIAL_CTS_HANDSHAKE;
	}

	if (lpDCB->fOutxDsrFlow)
	{
		handflow.ControlHandShake |= SERIAL_DSR_HANDSHAKE;
	}

	switch (lpDCB->fDtrControl)
	{
		case SERIAL_DTR_HANDSHAKE:
			handflow.ControlHandShake |= DTR_CONTROL_HANDSHAKE;
			break;

		case SERIAL_DTR_CONTROL:
			handflow.ControlHandShake |= DTR_CONTROL_ENABLE;
			break;

		case DTR_CONTROL_DISABLE:
			/* do nothing since handflow is init-zeroed */
			break;

		default:
			CommLog_Print(WLOG_WARN, "Unexpected fDtrControl value: %" PRIu32 "\n",
			              lpDCB->fDtrControl);
			return FALSE;
	}

	if (lpDCB->fDsrSensitivity)
	{
		handflow.ControlHandShake |= SERIAL_DSR_SENSITIVITY;
	}

	if (lpDCB->fTXContinueOnXoff)
	{
		handflow.FlowReplace |= SERIAL_XOFF_CONTINUE;
	}

	if (lpDCB->fOutX)
	{
		handflow.FlowReplace |= SERIAL_AUTO_TRANSMIT;
	}

	if (lpDCB->fInX)
	{
		handflow.FlowReplace |= SERIAL_AUTO_RECEIVE;
	}

	if (lpDCB->fErrorChar)
	{
		handflow.FlowReplace |= SERIAL_ERROR_CHAR;
	}

	if (lpDCB->fNull)
	{
		handflow.FlowReplace |= SERIAL_NULL_STRIPPING;
	}

	switch (lpDCB->fRtsControl)
	{
		case RTS_CONTROL_TOGGLE:
			CommLog_Print(WLOG_WARN, "Unsupported RTS_CONTROL_TOGGLE feature");
			// FIXME: see also GetCommState()
			return FALSE;

		case RTS_CONTROL_HANDSHAKE:
			handflow.FlowReplace |= SERIAL_RTS_HANDSHAKE;
			break;

		case RTS_CONTROL_ENABLE:
			handflow.FlowReplace |= SERIAL_RTS_CONTROL;
			break;

		case RTS_CONTROL_DISABLE:
			/* do nothing since handflow is init-zeroed */
			break;

		default:
			CommLog_Print(WLOG_WARN, "Unexpected fRtsControl value: %" PRIu32 "\n",
			              lpDCB->fRtsControl);
			return FALSE;
	}

	if (lpDCB->fAbortOnError)
	{
		handflow.ControlHandShake |= SERIAL_ERROR_ABORT;
	}

	/* lpDCB->fDummy2 not used */
	/* lpLocalDcb->wReserved  ignored */
	handflow.XonLimit = lpDCB->XonLim;
	handflow.XoffLimit = lpDCB->XoffLim;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_SET_HANDFLOW, &handflow, sizeof(SERIAL_HANDFLOW),
	                         NULL, 0, &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "SetCommState failure: could not set the handflow settings.");
		return FALSE;
	}

	/** upcomingTermios stage **/

	if (tcgetattr(pComm->fd, &upcomingTermios) <
	    0) /* NB: preserves current settings not directly handled by the Communication Functions */
	{
		SetLastError(ERROR_IO_DEVICE);
		return FALSE;
	}

	if (lpDCB->fBinary)
	{
		upcomingTermios.c_lflag &= (tcflag_t)~ICANON;
	}
	else
	{
		upcomingTermios.c_lflag |= ICANON;
		CommLog_Print(WLOG_WARN, "Unexpected nonbinary mode, consider to unset the ICANON flag.");
	}

	if (lpDCB->fParity)
	{
		upcomingTermios.c_iflag |= INPCK;
	}
	else
	{
		upcomingTermios.c_iflag &= (tcflag_t)~INPCK;
	}

	/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa363423%28v=vs.85%29.aspx
	 *
	 * The SetCommState function reconfigures the communications
	 * resource, but it does not affect the internal output and
	 * input buffers of the specified driver. The buffers are not
	 * flushed, and pending read and write operations are not
	 * terminated prematurely.
	 *
	 * TCSANOW matches the best this definition
	 */

	if (comm_ioctl_tcsetattr(pComm->fd, TCSANOW, &upcomingTermios) < 0)
	{
		SetLastError(ERROR_IO_DEVICE);
		return FALSE;
	}

	return TRUE;
}

/**
 * ERRORS:
 *   ERROR_INVALID_HANDLE
 */
BOOL GetCommTimeouts(HANDLE hFile, LPCOMMTIMEOUTS lpCommTimeouts)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;
	DWORD bytesReturned = 0;

	if (!CommIsHandleValid(hFile))
		return FALSE;

	/* as of today, SERIAL_TIMEOUTS and COMMTIMEOUTS structures are identical */

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_GET_TIMEOUTS, NULL, 0, lpCommTimeouts,
	                         sizeof(COMMTIMEOUTS), &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "GetCommTimeouts failure.");
		return FALSE;
	}

	return TRUE;
}

/**
 * ERRORS:
 *   ERROR_INVALID_HANDLE
 */
BOOL SetCommTimeouts(HANDLE hFile, LPCOMMTIMEOUTS lpCommTimeouts)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;
	DWORD bytesReturned = 0;

	if (!CommIsHandleValid(hFile))
		return FALSE;

	/* as of today, SERIAL_TIMEOUTS and COMMTIMEOUTS structures are identical */

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_SET_TIMEOUTS, lpCommTimeouts, sizeof(COMMTIMEOUTS),
	                         NULL, 0, &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "SetCommTimeouts failure.");
		return FALSE;
	}

	return TRUE;
}

BOOL GetDefaultCommConfigA(WINPR_ATTR_UNUSED LPCSTR lpszName, WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC,
                           WINPR_ATTR_UNUSED LPDWORD lpdwSize)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL GetDefaultCommConfigW(WINPR_ATTR_UNUSED LPCWSTR lpszName, WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC,
                           WINPR_ATTR_UNUSED LPDWORD lpdwSize)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL SetDefaultCommConfigA(WINPR_ATTR_UNUSED LPCSTR lpszName, WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC,
                           WINPR_ATTR_UNUSED DWORD dwSize)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL SetDefaultCommConfigW(WINPR_ATTR_UNUSED LPCWSTR lpszName, WINPR_ATTR_UNUSED LPCOMMCONFIG lpCC,
                           WINPR_ATTR_UNUSED DWORD dwSize)
{
	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */
	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL SetCommBreak(HANDLE hFile)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL ClearCommBreak(HANDLE hFile)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL ClearCommError(HANDLE hFile, WINPR_ATTR_UNUSED PDWORD lpErrors,
                    WINPR_ATTR_UNUSED LPCOMSTAT lpStat)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL PurgeComm(HANDLE hFile, DWORD dwFlags)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;
	DWORD bytesReturned = 0;

	if (!CommIsHandleValid(hFile))
		return FALSE;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_PURGE, &dwFlags, sizeof(DWORD), NULL, 0,
	                         &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "PurgeComm failure.");
		return FALSE;
	}

	return TRUE;
}

BOOL SetupComm(HANDLE hFile, DWORD dwInQueue, DWORD dwOutQueue)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;
	SERIAL_QUEUE_SIZE queueSize;
	DWORD bytesReturned = 0;

	if (!CommIsHandleValid(hFile))
		return FALSE;

	queueSize.InSize = dwInQueue;
	queueSize.OutSize = dwOutQueue;

	if (!CommDeviceIoControl(pComm, IOCTL_SERIAL_SET_QUEUE_SIZE, &queueSize,
	                         sizeof(SERIAL_QUEUE_SIZE), NULL, 0, &bytesReturned, NULL))
	{
		CommLog_Print(WLOG_WARN, "SetCommTimeouts failure.");
		return FALSE;
	}

	return TRUE;
}

BOOL EscapeCommFunction(HANDLE hFile, WINPR_ATTR_UNUSED DWORD dwFunc)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL TransmitCommChar(HANDLE hFile, WINPR_ATTR_UNUSED char cChar)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

BOOL WaitCommEvent(HANDLE hFile, WINPR_ATTR_UNUSED PDWORD lpEvtMask,
                   WINPR_ATTR_UNUSED LPOVERLAPPED lpOverlapped)
{
	WINPR_COMM* pComm = (WINPR_COMM*)hFile;

	if (!CommInitialized())
		return FALSE;

	/* TODO: not implemented */

	if (!pComm)
		return FALSE;

	CommLog_Print(WLOG_ERROR, "Not implemented");
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return FALSE;
}

/**
 * Returns TRUE on success, FALSE otherwise. To get extended error
 * information, call GetLastError.
 *
 * ERRORS:
 *   ERROR_DLL_INIT_FAILED
 *   ERROR_OUTOFMEMORY was not possible to get mappings.
 *   ERROR_INVALID_DATA was not possible to add the device.
 */
BOOL DefineCommDevice(/* DWORD dwFlags,*/ LPCTSTR lpDeviceName, LPCTSTR lpTargetPath)
{
	LPTSTR storedDeviceName = NULL;
	LPTSTR storedTargetPath = NULL;

	if (!CommInitialized())
		return FALSE;

	EnterCriticalSection(&sCommDevicesLock);

	if (sCommDevices == NULL)
	{
		SetLastError(ERROR_DLL_INIT_FAILED);
		goto error_handle;
	}

	storedDeviceName = _tcsdup(lpDeviceName);

	if (storedDeviceName == NULL)
	{
		SetLastError(ERROR_OUTOFMEMORY);
		goto error_handle;
	}

	storedTargetPath = _tcsdup(lpTargetPath);

	if (storedTargetPath == NULL)
	{
		SetLastError(ERROR_OUTOFMEMORY);
		goto error_handle;
	}

	int i = 0;
	for (; i < COMM_DEVICE_MAX; i++)
	{
		if (sCommDevices[i] != NULL)
		{
			if (_tcscmp(sCommDevices[i]->name, storedDeviceName) == 0)
			{
				/* take over the emplacement */
				free(sCommDevices[i]->name);
				free(sCommDevices[i]->path);
				sCommDevices[i]->name = storedDeviceName;
				sCommDevices[i]->path = storedTargetPath;
				break;
			}
		}
		else
		{
			/* new emplacement */
			sCommDevices[i] = (COMM_DEVICE*)calloc(1, sizeof(COMM_DEVICE));

			if (sCommDevices[i] == NULL)
			{
				SetLastError(ERROR_OUTOFMEMORY);
				goto error_handle;
			}

			sCommDevices[i]->name = storedDeviceName;
			sCommDevices[i]->path = storedTargetPath;
			break;
		}
	}

	if (i == COMM_DEVICE_MAX)
	{
		SetLastError(ERROR_OUTOFMEMORY);
		goto error_handle;
	}

	LeaveCriticalSection(&sCommDevicesLock);
	return TRUE;
error_handle:
	free(storedDeviceName);
	free(storedTargetPath);
	LeaveCriticalSection(&sCommDevicesLock);
	return FALSE;
}

/**
 * Returns the number of target paths in the buffer pointed to by
 * lpTargetPath.
 *
 * The current implementation returns in any case 0 and 1 target
 * path. A NULL lpDeviceName is not supported yet to get all the
 * paths.
 *
 * ERRORS:
 *   ERROR_SUCCESS
 *   ERROR_DLL_INIT_FAILED
 *   ERROR_OUTOFMEMORY was not possible to get mappings.
 *   ERROR_NOT_SUPPORTED equivalent QueryDosDevice feature not supported.
 *   ERROR_INVALID_DATA was not possible to retrieve any device information.
 *   ERROR_INSUFFICIENT_BUFFER too small lpTargetPath
 */
DWORD QueryCommDevice(LPCTSTR lpDeviceName, LPTSTR lpTargetPath, DWORD ucchMax)
{
	LPTSTR storedTargetPath = NULL;
	SetLastError(ERROR_SUCCESS);

	if (!CommInitialized())
		return 0;

	if (sCommDevices == NULL)
	{
		SetLastError(ERROR_DLL_INIT_FAILED);
		return 0;
	}

	if (lpDeviceName == NULL || lpTargetPath == NULL)
	{
		SetLastError(ERROR_NOT_SUPPORTED);
		return 0;
	}

	EnterCriticalSection(&sCommDevicesLock);
	storedTargetPath = NULL;

	for (int i = 0; i < COMM_DEVICE_MAX; i++)
	{
		if (sCommDevices[i] != NULL)
		{
			if (_tcscmp(sCommDevices[i]->name, lpDeviceName) == 0)
			{
				storedTargetPath = sCommDevices[i]->path;
				break;
			}

			continue;
		}

		break;
	}

	LeaveCriticalSection(&sCommDevicesLock);

	if (storedTargetPath == NULL)
	{
		SetLastError(ERROR_INVALID_DATA);
		return 0;
	}

	const size_t size = _tcsnlen(storedTargetPath, ucchMax);
	if (size + 2 > ucchMax)
	{
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}

	_tcsncpy(lpTargetPath, storedTargetPath, size + 1);
	lpTargetPath[size + 2] = '\0'; /* 2nd final '\0' */
	return (DWORD)size + 2UL;
}

/**
 * Checks whether lpDeviceName is a valid and registered Communication device.
 */
BOOL IsCommDevice(LPCTSTR lpDeviceName)
{
	TCHAR lpTargetPath[MAX_PATH];

	if (!CommInitialized())
		return FALSE;

	if (QueryCommDevice(lpDeviceName, lpTargetPath, MAX_PATH) > 0)
	{
		return TRUE;
	}

	return FALSE;
}

/**
 * Sets
 */
void _comm_setServerSerialDriver(HANDLE hComm, SERIAL_DRIVER_ID driverId)
{
	ULONG Type = 0;
	WINPR_HANDLE* Object = NULL;
	WINPR_COMM* pComm = NULL;

	if (!CommInitialized())
		return;

	if (!winpr_Handle_GetInfo(hComm, &Type, &Object))
	{
		CommLog_Print(WLOG_WARN, "_comm_setServerSerialDriver failure");
		return;
	}

	pComm = (WINPR_COMM*)Object;
	pComm->serverSerialDriverId = driverId;
}

static HANDLE_OPS ops = { CommIsHandled, CommCloseHandle,
	                      CommGetFd,     NULL, /* CleanupHandle */
	                      NULL,          NULL,
	                      NULL,          NULL,
	                      NULL,          NULL,
	                      NULL,          NULL,
	                      NULL,          NULL,
	                      NULL,          NULL,
	                      NULL,          NULL,
	                      NULL,          NULL,
	                      NULL };

/**
 * http://msdn.microsoft.com/en-us/library/windows/desktop/aa363198%28v=vs.85%29.aspx
 *
 * @param lpDeviceName e.g. COM1, ...
 *
 * @param dwDesiredAccess expects GENERIC_READ | GENERIC_WRITE, a
 * warning message is printed otherwise. TODO: better support.
 *
 * @param dwShareMode must be zero, INVALID_HANDLE_VALUE is returned
 * otherwise and GetLastError() should return ERROR_SHARING_VIOLATION.
 *
 * @param lpSecurityAttributes NULL expected, a warning message is printed
 * otherwise. TODO: better support.
 *
 * @param dwCreationDisposition must be OPEN_EXISTING. If the
 * communication device doesn't exist INVALID_HANDLE_VALUE is returned
 * and GetLastError() returns ERROR_FILE_NOT_FOUND.
 *
 * @param dwFlagsAndAttributes zero expected, a warning message is
 * printed otherwise.
 *
 * @param hTemplateFile must be NULL.
 *
 * @return INVALID_HANDLE_VALUE on error.
 */
HANDLE CommCreateFileA(LPCSTR lpDeviceName, DWORD dwDesiredAccess, DWORD dwShareMode,
                       LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                       DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
	CHAR devicePath[MAX_PATH] = { 0 };
	struct stat deviceStat = { 0 };
	WINPR_COMM* pComm = NULL;
	struct termios upcomingTermios = { 0 };

	if (!CommInitialized())
		return INVALID_HANDLE_VALUE;

	if (dwDesiredAccess != (GENERIC_READ | GENERIC_WRITE))
	{
		CommLog_Print(WLOG_WARN, "unexpected access to the device: 0x%08" PRIX32 "",
		              dwDesiredAccess);
	}

	if (dwShareMode != 0)
	{
		SetLastError(ERROR_SHARING_VIOLATION);
		return INVALID_HANDLE_VALUE;
	}

	/* TODO: Prevents other processes from opening a file or
	 * device if they request delete, read, or write access. */

	if (lpSecurityAttributes != NULL)
	{
		CommLog_Print(WLOG_WARN, "unexpected security attributes, nLength=%" PRIu32 "",
		              lpSecurityAttributes->nLength);
	}

	if (dwCreationDisposition != OPEN_EXISTING)
	{
		SetLastError(ERROR_FILE_NOT_FOUND); /* FIXME: ERROR_NOT_SUPPORTED better? */
		return INVALID_HANDLE_VALUE;
	}

	if (QueryCommDevice(lpDeviceName, devicePath, MAX_PATH) <= 0)
	{
		/* SetLastError(GetLastError()); */
		return INVALID_HANDLE_VALUE;
	}

	if (stat(devicePath, &deviceStat) < 0)
	{
		CommLog_Print(WLOG_WARN, "device not found %s", devicePath);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	if (!S_ISCHR(deviceStat.st_mode))
	{
		CommLog_Print(WLOG_WARN, "bad device %s", devicePath);
		SetLastError(ERROR_BAD_DEVICE);
		return INVALID_HANDLE_VALUE;
	}

	if (dwFlagsAndAttributes != 0)
	{
		CommLog_Print(WLOG_WARN, "unexpected flags and attributes: 0x%08" PRIX32 "",
		              dwFlagsAndAttributes);
	}

	if (hTemplateFile != NULL)
	{
		SetLastError(ERROR_NOT_SUPPORTED); /* FIXME: other proper error? */
		return INVALID_HANDLE_VALUE;
	}

	pComm = (WINPR_COMM*)calloc(1, sizeof(WINPR_COMM));

	if (pComm == NULL)
	{
		SetLastError(ERROR_OUTOFMEMORY);
		return INVALID_HANDLE_VALUE;
	}

	WINPR_HANDLE_SET_TYPE_AND_MODE(pComm, HANDLE_TYPE_COMM, WINPR_FD_READ);
	pComm->common.ops = &ops;
	/* error_handle */
	pComm->fd = open(devicePath, O_RDWR | O_NOCTTY | O_NONBLOCK);

	if (pComm->fd < 0)
	{
		CommLog_Print(WLOG_WARN, "failed to open device %s", devicePath);
		SetLastError(ERROR_BAD_DEVICE);
		goto error_handle;
	}

	pComm->fd_read = open(devicePath, O_RDONLY | O_NOCTTY | O_NONBLOCK);

	if (pComm->fd_read < 0)
	{
		CommLog_Print(WLOG_WARN, "failed to open fd_read, device: %s", devicePath);
		SetLastError(ERROR_BAD_DEVICE);
		goto error_handle;
	}

#if defined(WINPR_HAVE_SYS_EVENTFD_H)
	pComm->fd_read_event = eventfd(
	    0, EFD_NONBLOCK); /* EFD_NONBLOCK required because a read() is not always expected */
#endif

	if (pComm->fd_read_event < 0)
	{
		CommLog_Print(WLOG_WARN, "failed to open fd_read_event, device: %s", devicePath);
		SetLastError(ERROR_BAD_DEVICE);
		goto error_handle;
	}

	InitializeCriticalSection(&pComm->ReadLock);
	pComm->fd_write = open(devicePath, O_WRONLY | O_NOCTTY | O_NONBLOCK);

	if (pComm->fd_write < 0)
	{
		CommLog_Print(WLOG_WARN, "failed to open fd_write, device: %s", devicePath);
		SetLastError(ERROR_BAD_DEVICE);
		goto error_handle;
	}

#if defined(WINPR_HAVE_SYS_EVENTFD_H)
	pComm->fd_write_event = eventfd(
	    0, EFD_NONBLOCK); /* EFD_NONBLOCK required because a read() is not always expected */
#endif

	if (pComm->fd_write_event < 0)
	{
		CommLog_Print(WLOG_WARN, "failed to open fd_write_event, device: %s", devicePath);
		SetLastError(ERROR_BAD_DEVICE);
		goto error_handle;
	}

	InitializeCriticalSection(&pComm->WriteLock);
	/* can also be setup later on with _comm_setServerSerialDriver() */
	pComm->serverSerialDriverId = SerialDriverUnknown;
	InitializeCriticalSection(&pComm->EventsLock);

	(void)CommUpdateIOCount(pComm, TRUE);

	/* The binary/raw mode is required for the redirection but
	 * only flags that are not handle somewhere-else, except
	 * ICANON, are forced here. */
	ZeroMemory(&upcomingTermios, sizeof(struct termios));

	if (tcgetattr(pComm->fd, &upcomingTermios) < 0)
	{
		SetLastError(ERROR_IO_DEVICE);
		goto error_handle;
	}

	upcomingTermios.c_iflag &=
	    (tcflag_t) ~(/*IGNBRK |*/ BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL /*| IXON*/);
	upcomingTermios.c_oflag = 0; /* <=> &= ~OPOST */
	upcomingTermios.c_lflag = 0; /* <=> &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN); */
	/* upcomingTermios.c_cflag &= ~(CSIZE | PARENB); */
	/* upcomingTermios.c_cflag |= CS8; */
	/* About missing flags recommended by termios(3):
	 *
	 *   IGNBRK and IXON, see: IOCTL_SERIAL_SET_HANDFLOW
	 *   CSIZE, PARENB and CS8, see: IOCTL_SERIAL_SET_LINE_CONTROL
	 */
	/* a few more settings required for the redirection */
	upcomingTermios.c_cflag |= CLOCAL | CREAD;

	if (comm_ioctl_tcsetattr(pComm->fd, TCSANOW, &upcomingTermios) < 0)
	{
		SetLastError(ERROR_IO_DEVICE);
		goto error_handle;
	}

	return (HANDLE)pComm;
error_handle:
	WINPR_PRAGMA_DIAG_PUSH
	WINPR_PRAGMA_DIAG_IGNORED_MISMATCHED_DEALLOC(void) CloseHandle(pComm);
	WINPR_PRAGMA_DIAG_POP
	return INVALID_HANDLE_VALUE;
}

BOOL CommIsHandled(HANDLE handle)
{
	if (!CommInitialized())
		return FALSE;

	return WINPR_HANDLE_IS_HANDLED(handle, HANDLE_TYPE_COMM, TRUE);
}

BOOL CommIsHandleValid(HANDLE handle)
{
	WINPR_COMM* pComm = (WINPR_COMM*)handle;
	if (!CommIsHandled(handle))
		return FALSE;
	if (pComm->fd <= 0)
	{
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	return TRUE;
}

BOOL CommCloseHandle(HANDLE handle)
{
	WINPR_COMM* pComm = (WINPR_COMM*)handle;

	if (!CommIsHandled(handle))
		return FALSE;

	DeleteCriticalSection(&pComm->ReadLock);
	DeleteCriticalSection(&pComm->WriteLock);
	DeleteCriticalSection(&pComm->EventsLock);

	if (pComm->fd > 0)
		close(pComm->fd);

	if (pComm->fd_write > 0)
		close(pComm->fd_write);

	if (pComm->fd_write_event > 0)
		close(pComm->fd_write_event);

	if (pComm->fd_read > 0)
		close(pComm->fd_read);

	if (pComm->fd_read_event > 0)
		close(pComm->fd_read_event);

	free(pComm);
	return TRUE;
}

#if defined(WINPR_HAVE_SYS_EVENTFD_H)
#ifndef WITH_EVENTFD_READ_WRITE
int eventfd_read(int fd, eventfd_t* value)
{
	return (read(fd, value, sizeof(*value)) == sizeof(*value)) ? 0 : -1;
}

int eventfd_write(int fd, eventfd_t value)
{
	return (write(fd, &value, sizeof(value)) == sizeof(value)) ? 0 : -1;
}
#endif
#endif

static const char* CommIoCtlToStr(unsigned long int io)
{
	switch (io)
	{
#if defined(WINPR_HAVE_SERIAL_SUPPORT)
#if defined(TCGETS)
		case TCGETS:
			return "TCGETS";
#endif
#if defined(TCSETS)
		case TCSETS:
			return "TCSETS";
#endif
#if defined(TCSETSW)
		case TCSETSW:
			return "TCSETSW";
#endif
#if defined(TCSETSF)
		case TCSETSF:
			return "TCSETSF";
#endif
#if defined(TCGETA)
		case TCGETA:
			return "TCGETA";
#endif
#if defined(TCSETA)
		case TCSETA:
			return "TCSETA";
#endif
#if defined(TCSETAW)
		case TCSETAW:
			return "TCSETAW";
#endif
#if defined(TCSETAF)
		case TCSETAF:
			return "TCSETAF";
#endif
#if defined(TCSBRK)
		case TCSBRK:
			return "TCSBRK";
#endif
#if defined(TCXONC)
		case TCXONC:
			return "TCXONC";
#endif
#if defined(TCFLSH)
		case TCFLSH:
			return "TCFLSH";
#endif
#if defined(TIOCEXCL)
		case TIOCEXCL:
			return "TIOCEXCL";
#endif
#if defined(TIOCNXCL)
		case TIOCNXCL:
			return "TIOCNXCL";
#endif
#if defined(TIOCSCTTY)
		case TIOCSCTTY:
			return "TIOCSCTTY";
#endif
#if defined(TIOCGPGRP)
		case TIOCGPGRP:
			return "TIOCGPGRP";
#endif
#if defined(TIOCSPGRP)
		case TIOCSPGRP:
			return "TIOCSPGRP";
#endif
#if defined(TIOCOUTQ)
		case TIOCOUTQ:
			return "TIOCOUTQ";
#endif
#if defined(TIOCSTI)
		case TIOCSTI:
			return "TIOCSTI";
#endif
#if defined(TIOCGWINSZ)
		case TIOCGWINSZ:
			return "TIOCGWINSZ";
#endif
#if defined(TIOCSWINSZ)
		case TIOCSWINSZ:
			return "TIOCSWINSZ";
#endif
#if defined(TIOCMGET)
		case TIOCMGET:
			return "TIOCMGET";
#endif
#if defined(TIOCMBIS)
		case TIOCMBIS:
			return "TIOCMBIS";
#endif
#if defined(TIOCMBIC)
		case TIOCMBIC:
			return "TIOCMBIC";
#endif
#if defined(TIOCMSET)
		case TIOCMSET:
			return "TIOCMSET";
#endif
#if defined(TIOCGSOFTCAR)
		case TIOCGSOFTCAR:
			return "TIOCGSOFTCAR";
#endif
#if defined(TIOCSSOFTCAR)
		case TIOCSSOFTCAR:
			return "TIOCSSOFTCAR";
#endif
#if defined(FIONREAD)
		case FIONREAD:
			return "FIONREAD/TIOCINQ";
#endif
#if defined(TIOCLINUX)
		case TIOCLINUX:
			return "TIOCLINUX";
#endif
#if defined(TIOCCONS)
		case TIOCCONS:
			return "TIOCCONS";
#endif
#if defined(TIOCGSERIAL)
		case TIOCGSERIAL:
			return "TIOCGSERIAL";
#endif
#if defined(TIOCSSERIAL)
		case TIOCSSERIAL:
			return "TIOCSSERIAL";
#endif
#if defined(TIOCPKT)
		case TIOCPKT:
			return "TIOCPKT";
#endif
#if defined(FIONBIO)
		case FIONBIO:
			return "FIONBIO";
#endif
#if defined(TIOCNOTTY)
		case TIOCNOTTY:
			return "TIOCNOTTY";
#endif
#if defined(TIOCSETD)
		case TIOCSETD:
			return "TIOCSETD";
#endif
#if defined(TIOCGETD)
		case TIOCGETD:
			return "TIOCGETD";
#endif
#if defined(TCSBRKP)
		case TCSBRKP:
			return "TCSBRKP";
#endif
#if defined(TIOCSBRK)
		case TIOCSBRK:
			return "TIOCSBRK";
#endif
#if defined(TIOCCBRK)
		case TIOCCBRK:
			return "TIOCCBRK";
#endif
#if defined(TIOCGSID)
		case TIOCGSID:
			return "TIOCGSID";
#endif
#if defined(TIOCGRS485)
		case TIOCGRS485:
			return "TIOCGRS485";
#endif
#if defined(TIOCSRS485)
		case TIOCSRS485:
			return "TIOCSRS485";
#endif
#if defined(TIOCSPTLCK)
		case TIOCSPTLCK:
			return "TIOCSPTLCK";
#endif
#if defined(TCGETX)
		case TCGETX:
			return "TCGETX";
#endif
#if defined(TCSETX)
		case TCSETX:
			return "TCSETX";
#endif
#if defined(TCSETXF)
		case TCSETXF:
			return "TCSETXF";
#endif
#if defined(TCSETXW)
		case TCSETXW:
			return "TCSETXW";
#endif
#if defined(TIOCSIG)
		case TIOCSIG:
			return "TIOCSIG";
#endif
#if defined(TIOCVHANGUP)
		case TIOCVHANGUP:
			return "TIOCVHANGUP";
#endif
#if defined(TIOCGPTPEER)
		case TIOCGPTPEER:
			return "TIOCGPTPEER";
#endif
#if defined(FIONCLEX)
		case FIONCLEX:
			return "FIONCLEX";
#endif
#if defined(FIOCLEX)
		case FIOCLEX:
			return "FIOCLEX";
#endif
#if defined(FIOASYNC)
		case FIOASYNC:
			return "FIOASYNC";
#endif
#if defined(TIOCSERCONFIG)
		case TIOCSERCONFIG:
			return "TIOCSERCONFIG";
#endif
#if defined(TIOCSERGWILD)
		case TIOCSERGWILD:
			return "TIOCSERGWILD";
#endif
#if defined(TIOCSERSWILD)
		case TIOCSERSWILD:
			return "TIOCSERSWILD";
#endif
#if defined(TIOCGLCKTRMIOS)
		case TIOCGLCKTRMIOS:
			return "TIOCGLCKTRMIOS";
#endif
#if defined(TIOCSLCKTRMIOS)
		case TIOCSLCKTRMIOS:
			return "TIOCSLCKTRMIOS";
#endif
#if defined(TIOCSERGSTRUCT)
		case TIOCSERGSTRUCT:
			return "TIOCSERGSTRUCT";
#endif
#if defined(TIOCSERGETLSR)
		case TIOCSERGETLSR:
			return "TIOCSERGETLSR";
#endif
#if defined(TIOCSERGETMULTI)
		case TIOCSERGETMULTI:
			return "TIOCSERGETMULTI";
#endif
#if defined(TIOCSERSETMULTI)
		case TIOCSERSETMULTI:
			return "TIOCSERSETMULTI";
#endif
#if defined(TIOCMIWAIT)
		case TIOCMIWAIT:
			return "TIOCMIWAIT";
#endif
#if defined(TIOCGICOUNT)
		case TIOCGICOUNT:
			return "TIOCGICOUNT";
#endif
#if defined(FIOQSIZE)
		case FIOQSIZE:
			return "FIOQSIZE";
#endif
#if defined(TIOCPKT_DATA)
		case TIOCPKT_DATA:
			return "TIOCPKT_DATA";
#endif
#if defined(TIOCPKT_FLUSHWRITE)
		case TIOCPKT_FLUSHWRITE:
			return "TIOCPKT_FLUSHWRITE";
#endif
#if defined(TIOCPKT_STOP)
		case TIOCPKT_STOP:
			return "TIOCPKT_STOP";
#endif
#if defined(TIOCPKT_START)
		case TIOCPKT_START:
			return "TIOCPKT_START";
#endif
#if defined(TIOCPKT_NOSTOP)
		case TIOCPKT_NOSTOP:
			return "TIOCPKT_NOSTOP";
#endif
#if defined(TIOCPKT_DOSTOP)
		case TIOCPKT_DOSTOP:
			return "TIOCPKT_DOSTOP";
#endif
#if defined(TIOCPKT_IOCTL)
		case TIOCPKT_IOCTL:
			return "TIOCPKT_IOCTL";
#endif
#endif
		default:
			return "UNKNOWN";
	}
}

static BOOL CommStatusErrorEx(WINPR_COMM* pComm, unsigned long int ctl, const char* file,
                              const char* fkt, size_t line)
{
	WINPR_ASSERT(pComm);
	BOOL rc = pComm->permissive ? TRUE : FALSE;
	const DWORD level = rc ? WLOG_DEBUG : WLOG_WARN;
	char ebuffer[256] = { 0 };
	const char* str = CommIoCtlToStr(ctl);

	if (CommInitialized())
	{
		if (WLog_IsLevelActive(sLog, level))
		{
			WLog_PrintMessage(sLog, WLOG_MESSAGE_TEXT, level, line, file, fkt,
			                  "%s [0x%08" PRIx32 "] ioctl failed, errno=[%d] %s.", str, ctl, errno,
			                  winpr_strerror(errno, ebuffer, sizeof(ebuffer)));
		}
	}

	if (!rc)
		SetLastError(ERROR_IO_DEVICE);

	return rc;
}

BOOL CommIoCtl_int(WINPR_COMM* pComm, unsigned long int ctl, void* data, const char* file,
                   const char* fkt, size_t line)
{
	if (ioctl(pComm->fd, ctl, data) < 0)
	{
		if (!CommStatusErrorEx(pComm, ctl, file, fkt, line))
			return FALSE;
	}
	return TRUE;
}

BOOL CommUpdateIOCount(WINPR_ATTR_UNUSED HANDLE handle, WINPR_ATTR_UNUSED BOOL checkSupportStatus)
{
	WINPR_COMM* pComm = (WINPR_COMM*)handle;
	WINPR_ASSERT(pComm);

#if defined(WINPR_HAVE_COMM_COUNTERS)
	ZeroMemory(&(pComm->counters), sizeof(struct serial_icounter_struct));
	if (pComm->TIOCGICOUNTSupported || checkSupportStatus)
	{
		const int rc = ioctl(pComm->fd, TIOCGICOUNT, &(pComm->counters));
		if (checkSupportStatus)
			pComm->TIOCGICOUNTSupported = rc >= 0;
		else if (rc < 0)
		{
			if (!CommStatusErrorEx(pComm, TIOCGICOUNT, __FILE__, __func__, __LINE__))
				return FALSE;
		}
	}
#endif
	return TRUE;
}

static const char* CommSerialEvFlagString(ULONG flag)
{
	switch (flag)
	{
		case SERIAL_EV_RXCHAR:
			return "SERIAL_EV_RXCHAR";
		case SERIAL_EV_RXFLAG:
			return "SERIAL_EV_RXFLAG";
		case SERIAL_EV_TXEMPTY:
			return "SERIAL_EV_TXEMPTY";
		case SERIAL_EV_CTS:
			return "SERIAL_EV_CTS ";
		case SERIAL_EV_DSR:
			return "SERIAL_EV_DSR ";
		case SERIAL_EV_RLSD:
			return "SERIAL_EV_RLSD";
		case SERIAL_EV_BREAK:
			return "SERIAL_EV_BREAK";
		case SERIAL_EV_ERR:
			return "SERIAL_EV_ERR ";
		case SERIAL_EV_RING:
			return "SERIAL_EV_RING";
		case SERIAL_EV_PERR:
			return "SERIAL_EV_PERR";
		case SERIAL_EV_RX80FULL:
			return "SERIAL_EV_RX80FULL";
		case SERIAL_EV_EVENT1:
			return "SERIAL_EV_EVENT1";
		case SERIAL_EV_EVENT2:
			return "SERIAL_EV_EVENT2";
		case SERIAL_EV_WINPR_WAITING:
			return "SERIAL_EV_WINPR_WAITING";
		case SERIAL_EV_WINPR_STOP:
			return "SERIAL_EV_WINPR_STOP";
		default:
			return "SERIAL_EV_UNKNOWN";
	}
}

const char* CommSerialEvString(ULONG status, char* buffer, size_t size)
{
	const ULONG flags[] = { SERIAL_EV_RXCHAR, SERIAL_EV_RXFLAG,        SERIAL_EV_TXEMPTY,
		                    SERIAL_EV_CTS,    SERIAL_EV_DSR,           SERIAL_EV_RLSD,
		                    SERIAL_EV_BREAK,  SERIAL_EV_ERR,           SERIAL_EV_RING,
		                    SERIAL_EV_PERR,   SERIAL_EV_RX80FULL,      SERIAL_EV_EVENT1,
		                    SERIAL_EV_EVENT2, SERIAL_EV_WINPR_WAITING, SERIAL_EV_WINPR_STOP };

	winpr_str_append("{", buffer, size, "");

	const char* sep = "";
	for (size_t x = 0; x < ARRAYSIZE(flags); x++)
	{
		const ULONG flag = flags[x];
		if (status & flag)
		{
			winpr_str_append(CommSerialEvFlagString(flag), buffer, size, sep);
			sep = "|";
		}
	}

	char number[32] = { 0 };
	(void)_snprintf(number, sizeof(number), "}[0x%08" PRIx32 "]", status);
	winpr_str_append(number, buffer, size, "");
	return buffer;
}
