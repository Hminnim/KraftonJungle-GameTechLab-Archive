#pragma once

namespace CrashReporting
{
void Initialize();
void ReserveExceptionStack();
[[noreturn]] void CauseCrash();
}
