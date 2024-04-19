// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "GuiStatusMonitor.h"
#include "Globals.h"
#include "WinMain.h"
#include "DebugWindow.h"

#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <future>

INT_PTR CALLBACK AppStatusDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

namespace
{
    struct BusyDialogSectionData
    {
        HWND hwndLabel = 0;
        HWND hwndProgress = 0;
        HWND hwndSpeed = 0;

        bool cancellable = false;
        std::string description;

        uint64_t curProgress = 0;
        uint64_t maxProgress = 0;
        bool isMeasurableProgressStyle = false;
        bool hasInitializedProgressStyle = false;
        bool progressChanged = false;

        uint64_t lastRawProgressPoint = 0;
        std::string speedString;

        std::array<uint64_t, 4> speedSamples = { 0 };
    };

    struct BusyDialogInstanceData
    {
        HWND hwndCancel = 0;

        std::vector<BusyDialogSectionData> sectionData;

        bool isFirstUpdate = true;
        size_t lastWindowHeight = 0;

        std::chrono::high_resolution_clock::time_point lastSampleTime = std::chrono::high_resolution_clock::time_point::min();
    };

    std::future<void> busyCreatedFuture;
    std::promise<void> busyCreatedPromise;
    volatile HWND hwndBusyDialog = 0;
    BusyDialogInstanceData busyData;

    std::recursive_mutex activeSectionsLock;
    std::list<std::shared_ptr<GuiStatusSection>> activeSections;
    volatile bool isCancelling = false;

    //

    void RunBusyDialog()
    {
        if (hwndBusyDialog)
        {
            GlobalDebugOutput("BUG: RunBusyDialog called while already busy... bad things may happen...");
            return;
        }

        GlobalDebugOutput("Opening Busy Dialog");

        busyData = BusyDialogInstanceData();

        struct
        {
            DLGTEMPLATE dlg;
            DWORD trash0, trash1, trash2;
        } dlg = { 0 };

        dlg.dlg.style = DS_CENTER;
        dlg.dlg.dwExtendedStyle = 0;
        dlg.dlg.cx = 400;
        dlg.dlg.cy = 100;

        HWND prevActiveWindow = GetActiveWindow();
        DialogBoxIndirect(hInstance, &dlg.dlg, activeMainWindow, AppStatusDialogProc);
        PopLockoutInteraction();
        if (prevActiveWindow)
            SetActiveWindow(prevActiveWindow);

        GlobalDebugOutput("Busy Dialog Ended");
    }

    void CloseBusyDialog()
    {
        if (!hwndBusyDialog)
        {
            GlobalDebugOutput("BUG: CloseBusyDialog called but not dialog exists... bad things may happen...");
            return;
        }

        GlobalDebugOutput("Closing Busy Dialog");

        SendMessage(hwndBusyDialog, WM_DESTROY, 0, 0);
        while (hwndBusyDialog)
            Sleep(0);

        isCancelling = false;
    }

    //sync activeSections to the data, and create the window elements if they don't exist
    void UnprotectedUpdateBusyData(HWND hwnd)
    {
        //if the number of sections changes, just wipe everything and start over
        if (busyData.sectionData.size() != activeSections.size() || busyData.isFirstUpdate)
        {
            busyData.isFirstUpdate = false;

            for (auto &sd : busyData.sectionData)
            {
                DestroyWindow(sd.hwndLabel);
                sd.hwndLabel = 0;
                DestroyWindow(sd.hwndProgress);
                sd.hwndProgress = 0;
                DestroyWindow(sd.hwndSpeed);
                sd.hwndSpeed = 0;
            }

            busyData.sectionData.resize(activeSections.size());

            size_t desiredWindowHeight = 100 + (int)activeSections.size() * 100;
            if (desiredWindowHeight > busyData.lastWindowHeight)
            {
                busyData.lastWindowHeight = desiredWindowHeight;
                SetWindowPos(hwnd, 0, 0, 0, 400, (int)desiredWindowHeight, SWP_NOMOVE);
            }

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            SetWindowPos(busyData.hwndCancel, 0, (clientRect.right - clientRect.left) / 2 + clientRect.left - 90 / 2, clientRect.bottom - 40, 0, 0, SWP_NOSIZE);

            auto asIter = activeSections.begin();
            for (size_t si = 0; si < busyData.sectionData.size(); ++si)
            {
                auto &sd = busyData.sectionData[si];
                const auto &as = **(asIter++);

                bool isActivityMultiLine = false;
                bool hitBreak = false;
                for (char c : as.Activity())
                {
                    if (hitBreak && !(c == '\r' || c == '\n'))
                    {
                        isActivityMultiLine = true;
                        break;
                    }
                    else if (c == '\r' || c == '\n')
                        hitBreak = true;
                }

                sd.hwndLabel = CreateWindow(WC_STATIC, "", WS_VISIBLE | WS_CHILD | SS_CENTER, 10, 5 + (int)si * 100 + (isActivityMultiLine ? 0 : 22), clientRect.right - clientRect.left - 20, 45 - (isActivityMultiLine ? 0 : 22), hwnd, 0, hInstance, 0);
                sd.hwndProgress = CreateWindow(PROGRESS_CLASS, "", PBS_MARQUEE | PBS_SMOOTH | WS_VISIBLE | WS_CHILD, 10, 51 + (int)si * 100, clientRect.right - clientRect.left - 20, 24, hwnd, 0, hInstance, 0);
                sd.hwndSpeed = CreateWindow(WC_STATIC, "", WS_VISIBLE | WS_CHILD | SS_CENTER, 10, 76 + (int)si * 100, clientRect.right - clientRect.left - 20, 23, hwnd, 0, hInstance, 0);
            }

            FixChildFonts(hwnd);
        }

        //update busyData from activeSections
        auto updateTime = std::chrono::high_resolution_clock::now();
        uint64_t usChanged = std::chrono::duration_cast<std::chrono::microseconds>(updateTime - busyData.lastSampleTime).count();

        auto asIter = activeSections.begin();
        for (size_t si = 0; si < busyData.sectionData.size(); ++si)
        {
            auto &sd = busyData.sectionData[si];
            const auto &as = **(asIter++);

            sd.cancellable = as.AggregateCancellable();
            sd.description = as.Activity() + "...";

            uint64_t nextCurProgress = as.BalancedProgressCurrent();
            uint64_t nextMaxProgress = as.BalancedProgressMax();
            if (nextCurProgress != sd.curProgress || nextMaxProgress != sd.maxProgress)
            {
                sd.progressChanged = true;
                sd.curProgress = nextCurProgress;
                sd.maxProgress = nextMaxProgress;
            }

            if (usChanged > 0)
            {
                uint64_t curProgressPoint = as.TotalProgress();
                uint64_t progressChange = curProgressPoint - sd.lastRawProgressPoint;
                if (progressChange > 100000000)
                    progressChange = progressChange;

                sd.lastRawProgressPoint = curProgressPoint;
                uint64_t unitsPerSecond = progressChange * 1000000 / as.SpeedDivider() / usChanged;

                std::string speedUnit = as.SpeedUnit();
                if (speedUnit.empty())
                    sd.speedString.clear();
                else
                {
                    for (size_t i = 1; i < sd.speedSamples.size(); ++i)
                        sd.speedSamples[i] = sd.speedSamples[i - 1];
                    sd.speedSamples[0] = unitsPerSecond;

                    uint64_t smoothedSpeed = 0;
                    for (auto s : sd.speedSamples)
                        smoothedSpeed += s;
                    uint64_t smoothedSpeedScaled = smoothedSpeed / sd.speedSamples.size();

                    std::string zeroSpeedMessage = as.ZeroSpeedMessage();

                    if (smoothedSpeed > 0 || zeroSpeedMessage.empty())
                        sd.speedString = std::to_string(smoothedSpeedScaled) + " " + speedUnit + "/s";
                    else
                        sd.speedString = std::move(zeroSpeedMessage);
                }
            }

            if (sd.curProgress == sd.maxProgress && sd.maxProgress != 0)
                sd.speedString.clear();
        }

        if (usChanged > 0)
            busyData.lastSampleTime = updateTime;
    }

    bool TryUpdateBusyData(HWND hwnd)
    {
        if (activeSectionsLock.try_lock())
        {
            UnprotectedUpdateBusyData(hwnd);

            activeSectionsLock.unlock();
            return true;
        }

        return false;
    }

    //sync the window to the data
    void UpdateBusyDialog(HWND hwnd)
    {
        bool anyCancellable = false;
        std::vector<std::string> activities;

        for (auto &sd : busyData.sectionData)
        {
            if (sd.cancellable)
                anyCancellable = true;

            if (!sd.description.empty())
                activities.emplace_back(sd.description);

            SetWindowText(sd.hwndLabel, sd.description.c_str());
            SetWindowText(sd.hwndSpeed, sd.speedString.c_str());

            if (sd.maxProgress == 0)
            {
                if (sd.isMeasurableProgressStyle || !sd.hasInitializedProgressStyle)
                {
                    sd.hasInitializedProgressStyle = true;
                    sd.isMeasurableProgressStyle = false;
                    SetWindowLong(sd.hwndProgress, GWL_STYLE, GetWindowLong(sd.hwndProgress, GWL_STYLE) | PBS_MARQUEE);
                    SendMessage(sd.hwndProgress, PBM_SETMARQUEE, 1, 0);
                }
            }
            else
            {
                if (!sd.isMeasurableProgressStyle || !sd.hasInitializedProgressStyle)
                {
                    sd.hasInitializedProgressStyle = true;
                    sd.isMeasurableProgressStyle = true;
                    SendMessage(sd.hwndProgress, PBM_SETMARQUEE, 0, 0);
                    SendMessage(sd.hwndProgress, PBM_SETSTEP, 1, 0);
                    SetWindowLong(sd.hwndProgress, GWL_STYLE, GetWindowLong(sd.hwndProgress, GWL_STYLE)&~PBS_MARQUEE);
                }

                if (sd.progressChanged)
                {
                    size_t dividerBecauseWindowsCantTakeItThatBig = 1;
                    if (sd.maxProgress > 100000000)
                        dividerBecauseWindowsCantTakeItThatBig = 10000;

                    SendMessage(sd.hwndProgress, PBM_SETRANGE32, 0, (LPARAM)sd.maxProgress / dividerBecauseWindowsCantTakeItThatBig);
                    SendMessage(sd.hwndProgress, PBM_SETPOS, sd.curProgress / dividerBecauseWindowsCantTakeItThatBig + 1, 0); //hack to force it to update to the target position rather than animating to it, which lags behind
                    SendMessage(sd.hwndProgress, PBM_SETPOS, sd.curProgress / dividerBecauseWindowsCantTakeItThatBig, 0);
                }
            }
        }

        EnableWindow(busyData.hwndCancel, anyCancellable);

        std::string newWindowStatus = StringJoin(" + ", activities.begin(), activities.end());
    }
}

//

INT_PTR CALLBACK AppStatusDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //NOTE: In here, always use activeSectionsLock.try_lock, and just skip the update if it fails, to avoid potential Create/Close deadlocks

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        PushLockoutInteraction(hwnd);

        busyData.hwndCancel = CreateWindow(WC_BUTTON, "Cancel", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 0, 0, 90, 25, hwnd, 0, hInstance, 0);

        UnprotectedUpdateBusyData(hwnd); //technically we don't own the mutex here, but this is safe because the caller of CreateBusyDialog, which is blocking until hwndBusyDialog is set, owns it
        UpdateBusyDialog(hwnd);

        FixChildFonts(hwnd);
        SetTimer(hwnd, 0, 100, nullptr); //10 fps
        hwndBusyDialog = hwnd;
        busyCreatedPromise.set_value();

        return TRUE;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 0);

        UnprotectedUpdateBusyData(hwnd); //technically we don't own the mutex here, but this is safe because the caller of CloseBusyDialog, which is blocking until hwndBusyDialog is clear, owns it
        UpdateBusyDialog(hwnd);

        hwndBusyDialog = 0;
        EndDialog(hwnd, 0);
        return TRUE;
    case WM_COMMAND:
        if ((HWND)lParam == busyData.hwndCancel)
        {
            isCancelling = true;
            SetWindowText(busyData.hwndCancel, "Cancelling...");
            EnableWindow(busyData.hwndCancel, false);
        }
    case WM_TIMER:
    {
        if (hwndBusyDialog && TryUpdateBusyData(hwndBusyDialog))
            UpdateBusyDialog(hwndBusyDialog);
    }
    return TRUE;
    }

    return FALSE;
}

// -- GuiStatusMonitor

void GuiStatusMonitor::SetControlFeatures(bool isCancellable)
{
    canCancel = isCancellable;
}

void GuiStatusMonitor::SetProgressFeatures(uint64_t maxProgress, const std::string speedUnitIfVisible, uint64_t speedDivisor)
{
    std::lock_guard<std::mutex> lock(mut);

    progressMax = maxProgress;
    speedUnit = speedUnitIfVisible;
    if (speedDivisor > 0)
        speedDivider = speedDivisor;
}

void GuiStatusMonitor::AddProgress(uint64_t amount)
{
    if (forcedComplete)
        return;

    progressCur.fetch_add(amount, std::memory_order_relaxed);
}

void GuiStatusMonitor::Complete()
{
    forcedComplete = true;
}

bool GuiStatusMonitor::IsCancelling() const
{
    return isCancelling;
}

void GuiStatusMonitor::AddDebugOutput(const std::string &str)
{
    GlobalDebugOutput(str);
}

uint64_t GuiStatusMonitor::PollProgress(bool returnMaxIfComplete) const
{
    if (forcedComplete && returnMaxIfComplete)
    {
        return progressMax > 0 ? progressMax : 1;
    }

    return progressCur.load(std::memory_order_seq_cst);
}

uint64_t GuiStatusMonitor::MaxProgress() const
{
    if (forcedComplete && progressMax == 0)
        return 1;

    return progressMax;
}

uint64_t GuiStatusMonitor::SpeedDivider() const
{
    return speedDivider;
}

std::string GuiStatusMonitor::SpeedUnit() const
{
    std::string ret;

    {
        std::lock_guard<std::mutex> lock(mut);
        ret = speedUnit;
    }

    return std::move(ret);
}

bool GuiStatusMonitor::CanCancel() const
{
    return canCancel;
}

// -- DebugStatusOnlyMonitor

DebugStatusOnlyMonitor DebugStatusOnlyMonitor::Instance;

bool DebugStatusOnlyMonitor::IsCancelling() const
{
    return isCancelling;
}

void DebugStatusOnlyMonitor::AddDebugOutput(const std::string &str)
{
    GlobalDebugOutput(str);
}

// -- GuiStatusSection

GuiStatusSection::GuiStatusSection(const std::string &activity, size_t partCount)
{
    sectionActivity = activity;
    for (size_t i = 0; i < partCount; ++i)
        parts.emplace_back(std::make_unique<GuiStatusMonitor>());
}

void GuiStatusSection::ChangeName(const std::string &newName)
{
    std::lock_guard<std::mutex> guard(mut);
    sectionActivity = newName;
}

void GuiStatusSection::SetZeroSpeedMessage(const std::string &messageForZeroSpeed)
{
    std::lock_guard<std::mutex> guard(mut);
    zeroSpeedMessage = messageForZeroSpeed;
}

void GuiStatusSection::SetAsWriteOperation(bool isWriteOperation)
{
    writeOperation = isWriteOperation;
}

size_t GuiStatusSection::PartCount() const
{
    return parts.size();
}

GuiStatusMonitor& GuiStatusSection::PartIndex(size_t i)
{
    return *parts[i];
}

std::string GuiStatusSection::Activity() const
{
    std::string ret;
    {
        std::lock_guard<std::mutex> guard(mut);
        ret = sectionActivity;
    }

    if (!ret.empty())
        return std::move(ret);

    return "???";
}

bool GuiStatusSection::IsWriteOperation() const
{
    return writeOperation;
}

uint64_t GuiStatusSection::TotalProgress() const
{
    uint64_t total = 0;
    for (const auto &p : parts)
        total += p->PollProgress(false);
    return total;
}

uint64_t GuiStatusSection::BalancedProgressCurrent() const
{
    if (parts.empty())
        return 0;

    //if we have 1 item, use it's exact measures
    if (parts.size() == 1)
        return parts.front()->PollProgress(true);

    //if we have multiple items, each item gets an equal measure
    uint64_t total = 0;
    for (const auto &p : parts)
    {
        uint64_t partMax = p->MaxProgress();
        if (partMax > 0)
        {
            uint64_t partProgress = p->PollProgress(true);
            partProgress *= 100;
            partProgress /= partMax;
            total += partProgress;
        }
    }
    return total;
}

uint64_t GuiStatusSection::BalancedProgressMax() const
{
    if (parts.empty())
        return 0;

    //if we have 1 item, use it's exact measures
    if (parts.size() == 1)
        return parts.front()->MaxProgress();

    //if we have multiple items, each item gets an equal measure
    return parts.size() * 100;
}

uint64_t GuiStatusSection::SpeedDivider() const
{
    for (const auto &p : parts)
    {
        uint64_t d = p->SpeedDivider();
        if (d != 1)
            return d;
    }

    return 1;
}

std::string GuiStatusSection::SpeedUnit() const
{
    for (const auto &p : parts)
    {
        std::string u = p->SpeedUnit();
        if (!u.empty())
            return std::move(u);
    }

    return std::string();
}

std::string GuiStatusSection::ZeroSpeedMessage() const
{
    std::string ret;
    {
        std::lock_guard<std::mutex> guard(mut);
        ret = zeroSpeedMessage;
    }
    return std::move(ret);
}

bool GuiStatusSection::AggregateCancellable() const
{
    for (const auto &p : parts)
    {
        if (p->CanCancel())
            return true;
    }

    return false;
}

// -- GuiStatusManager

void GuiStatusManager::ShowBusyDialogAndRunText(const std::string &simpleBusyName, bool isWriteOperation, std::function<void(void)> work)
{
    ShowBusyDialogAndRunManager([&](GuiStatusManager &man)
    {
        GuiStatusManager::AutoSection sec(man, isWriteOperation, simpleBusyName, 1);
        work();
    });
}

void GuiStatusManager::ShowBusyDialogAndRunMonitor(const std::string &simpleBusyName, bool isWriteOperation, std::function<void(GuiStatusMonitor&)> work)
{
    ShowBusyDialogAndRunManager([&](GuiStatusManager &man)
    {
        GuiStatusManager::AutoSection sec(man, isWriteOperation, simpleBusyName, 1);
        work(sec.Section().PartIndex(0));
    });
}

void GuiStatusManager::ShowBusyDialogAndRunManager(std::function<void(GuiStatusManager&)> work)
{
    busyCreatedPromise = std::promise<void>();
    busyCreatedFuture = busyCreatedPromise.get_future();

    std::thread t([&]()
    {
        busyCreatedFuture.get(); //block if the box hasn't even been created yet
        work(global);
        CloseBusyDialog();
    });

    RunBusyDialog();
    t.join();
}

std::shared_ptr<GuiStatusSection> GuiStatusManager::AddSection(const std::string &activity, size_t partCount)
{
    std::shared_ptr<GuiStatusSection> added;

    {
        std::lock_guard<std::recursive_mutex> guard(activeSectionsLock);

        activeSections.emplace_back(std::make_shared<GuiStatusSection>(activity, partCount));
        added = activeSections.back();
    }

    return added;
}

void GuiStatusManager::RemoveSection(std::shared_ptr<GuiStatusSection> &section)
{
    std::lock_guard<std::recursive_mutex> guard(activeSectionsLock);

    activeSections.remove(section);
}

bool GuiStatusManager::IsBusyWithWrites()
{
    bool busy = false;
    {
        std::lock_guard<std::recursive_mutex> guard(activeSectionsLock);
        for (auto &a : activeSections)
            busy = busy || a->IsWriteOperation();
    }
    return busy;
}

bool GuiStatusManager::IsBusyAnything()
{
    bool busy;
    {
        std::lock_guard<std::recursive_mutex> guard(activeSectionsLock);
        busy = !activeSections.empty();
    }
    return busy;
}

bool GuiStatusManager::IsCancelling() const
{
    return isCancelling;
}

GuiStatusManager GuiStatusManager::global;

