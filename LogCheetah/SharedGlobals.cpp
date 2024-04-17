#include "SharedGlobals.h"
#include <Windows.h>
#include <Psapi.h>
#include <thread>

HINSTANCE hInstance = 0;
HWND hwndMain = 0;
HFONT lessDerpyFont = 0;
HFONT lessDerpyFontItalic = 0;
HFONT lessDerpyFontBold = 0;

int cpuCountGeneral = 1;
int cpuCountParse = 1;
int cpuCountSort = 1;
int cpuCountFilter = 1;

bool allowMemoryUseChecks = true;
bool isAppStatusCanceling = false;

void OverrideCpuCount(int &val, int targetVal)
{
    if (targetVal >= 1)
        val = targetVal;
    else
    {
        val = std::thread::hardware_concurrency();
        if (val < 1)
            val = 1;
    }
}

void OverrideCpuCount(int general, int parse, int sort, int filter)
{
    OverrideCpuCount(cpuCountGeneral, general);
    OverrideCpuCount(cpuCountParse, parse);
    OverrideCpuCount(cpuCountSort, sort);
    OverrideCpuCount(cpuCountFilter, filter);
}

void SharedGlobalInit()
{
    OverrideCpuCount(0, 0, 0, 0);
}

bool VerifyMemoryUse(VMUState &vmu)
{
    if (!allowMemoryUseChecks)
        return true;

    std::lock_guard<std::mutex> guard(vmu.lock);

    if (vmu.SkipCur++%vmu.SkipMax != 0 || (vmu.HasAskedUserCurrentFull && vmu.HasAskedUserTooMuchForSystem) || vmu.UserResponse == false)
        return vmu.UserResponse;

    PROCESS_MEMORY_COUNTERS_EX pmc = { 0 };
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
    {
        MEMORYSTATUSEX sysmem = { 0 };
        sysmem.dwLength = sizeof(sysmem);
        if (GlobalMemoryStatusEx(&sysmem))
        {
            uint64_t mbTotal = sysmem.ullTotalPhys / 1024 / 1024;
            uint64_t mbAvailable = sysmem.ullAvailPhys / 1024 / 1024;
            uint64_t mbUsedByUs = pmc.PrivateUsage / 1024 / 1024;

            if (!vmu.HasAskedUserCurrentFull)
            {
                if (100 * mbAvailable / mbTotal < 20)
                {
                    vmu.HasAskedUserCurrentFull = true;
                    if (MessageBox(hwndMain, "The system has less than 20% free physical memory remaining.  Continue?\r\n(Performance may temporarily degrade)", "", MB_YESNO) != IDYES)
                        vmu.UserResponse = false;
                }
            }
            else if (!vmu.HasAskedUserTooMuchForSystem)
            {
                if (100 * mbUsedByUs / mbTotal > 70)
                {
                    vmu.HasAskedUserTooMuchForSystem = true;
                    if (MessageBox(hwndMain, "This process is now using over 70% of the system's physical memory.  Continue?\r\n(This may drastically impair system performance)", "", MB_YESNO) != IDYES)
                        vmu.UserResponse = false;
                }
            }
        }
    }

    return vmu.UserResponse;
}

AppStatusMonitor AppStatusMonitor::Instance;

void AppStatusMonitor::SetControlFeatures(bool isCancellable)
{
}

void AppStatusMonitor::SetProgressFeatures(uint64_t maxProgress, const std::string speedUnitIfVisible, uint64_t speedDivisor)
{
}

void AppStatusMonitor::AddProgress(uint64_t amount)
{
}

void AppStatusMonitor::Complete()
{
}

bool AppStatusMonitor::IsCancelling() const
{
    return false;
}

void AppStatusMonitor::AddDebugOutput(const std::string &str)
{
}
