#pragma once

#include <Windows.h>

namespace CrashReporting
{
struct CrashCaptureRequest
{
	DWORD ThreadId;
	EXCEPTION_POINTERS* Exception;
};
}
