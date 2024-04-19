// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <Windows.h>

//F#$%ing windows.h macros
#undef min
#undef max

#include <mutex>
#include <string>
#include <sstream>

//must be called at startup
void SharedGlobalInit();

//these need set by the app's WinMain
extern HINSTANCE hInstance;
extern HWND hwndMain;
extern HFONT lessDerpyFont;
extern HFONT lessDerpyFontItalic;
extern HFONT lessDerpyFontBold;

//these are set by SharedGlobalInit
extern int cpuCountGeneral;
extern int cpuCountParse;
extern int cpuCountSort;
extern int cpuCountFilter;
void OverrideCpuCount(int general, int parse, int sort, int filter);


//
extern bool allowMemoryUseChecks; //default is true
struct VMUState
{
    inline VMUState(int skipCallsCount = 10000) : HasAskedUserCurrentFull(false), HasAskedUserTooMuchForSystem(false), UserResponse(true), SkipCur(0), SkipMax(skipCallsCount)
    {
        if (SkipMax < 1)
            SkipMax = 1;
    }

    bool HasAskedUserCurrentFull;
    bool HasAskedUserTooMuchForSystem;
    bool UserResponse;
    int SkipCur;
    int SkipMax;
    std::mutex lock;
};
bool VerifyMemoryUse(VMUState &vmu);

//this default implementation does nothing
class AppStatusMonitor
{
public:
    static AppStatusMonitor Instance;

    //progress reporting and control
    virtual void SetControlFeatures(bool isCancellable);
    virtual void SetProgressFeatures(uint64_t maxProgress, const std::string speedUnitIfVisible = std::string(), uint64_t speedDivisor = 1);
    virtual void AddProgress(uint64_t amount);
    virtual void Complete();

    //if true, the activity should be aborted
    virtual bool IsCancelling() const;

    //debug spew
    virtual void AddDebugOutput(const std::string &str);

    inline void AddDebugOutputTime(const std::string &item, double timeInMs)
    {
        AddDebugOutput(item + " - " + std::to_string(timeInMs) + "ms");
    }
};
