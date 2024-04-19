// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <UxTheme.h>

#include "WinMain.h"
#include "JsonParser.h"
#include "DSVParser.h"
#include "WindowsDragDrop.h"
#include "DialogOpenLocal.h"
#include "DialogSetup.h"
#include "Preferences.h"
#include "GuiStatusMonitor.h"
#include "resource.h"
#include "GenericTextLogParseRouter.h"
#include "ObtainParseCoordinator.h"
#include "DebugWindow.h"
#include "Globals.h"
#include "MainLogView.h"
#include "CatWindow.h"

#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <memory>
#include <algorithm>
#include <thread>
#include <mutex>
#include <list>
#include <map>
#include <cctype>
#include <atomic>
#include <set>

INT_PTR CALLBACK AppStatusDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

namespace AutoGeneratedVersions
{
    extern const char *BuildVersion;
}

namespace
{
    HWND hwndMainLogView = 0;
    HWND hwndOpenLocal = 0;
    HWND hwndSetup = 0;
    HWND hwndDebug = 0;

    const UINT WM_NEW_STREAMING_LOGS = WM_USER + 1;
    const UINT WM_DRAGDROPPED_FILES = WM_USER + 2;
    const UINT WM_FIRSTRUN_PROMPTS = WM_USER + 3;
    const UINT WM_PROCESS_QUEUED_DEBUG_MESSAGES = WM_USER + 4; //keep in sync with DebugWindow.cpp

    std::vector<std::string> dragDroppedFiles;

    void ResetUI()
    {
        LogViewCloseAllWindows();
        hwndMainLogView = CreateWindowEx(WS_EX_CLIENTEDGE, "LogCheetahLogView", "LogCheetah - Log View", WS_VISIBLE | WS_CHILD, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndMain, (HMENU)0, hInstance, (LPVOID)0);
        SendMessage(hwndMain, WM_SIZE, 0, 0);
    }

    void UpdateTitleText()
    {
        std::string text = "LogCheetah";

        if (globalLogs.Lines.empty())
            text += " - On The Prowl";
        else
        {
            text += " - " + std::to_string(globalLogs.Lines.size()) + " Total Lines";

            std::string logViewText = LogViewGetWindowStatusText(hwndMainLogView);
            if (!logViewText.empty())
                text += " - " + logViewText;
        }

        //TODO: Put build version back here later
        //text = text + "      (Version " + AutoGeneratedVersions::BuildVersion + ")";

        SetWindowText(hwndMain, text.c_str());
    }

    std::vector<HWND> interactionOwnerStack;
}

HWND currentInteractionOwner = 0;
HWND activeMainWindow = 0;

//

void MoveAndLoadLogs(LogCollection &&logs, bool merge)
{
    if (!merge)
        ResetUI();

    if (merge)
    {
        GuiStatusManager::ShowBusyDialogAndRunMonitor("Merging Logs", true, [&](GuiStatusMonitor &monitor)
        {
            globalLogs.MoveAndMergeInLogs(monitor, std::move(logs), false, false);
        });
    }
    else
        globalLogs = std::move(logs);

    if (!globalLogs.Lines.empty())
    {
        GuiStatusManager::ShowBusyDialogAndRunMonitor("Sorting Logs", true, [&](GuiStatusMonitor &monitor)
        {
            auto timerStart = std::chrono::high_resolution_clock::now();
            globalLogs.SortRange(0, globalLogs.Lines.size());
            auto timerEnd = std::chrono::high_resolution_clock::now();
            monitor.AddDebugOutputTime("Sort Logs", std::chrono::duration_cast<std::chrono::microseconds>(timerEnd - timerStart).count() / 1000.0);
        });
    }

    LogViewNotifyDataChanged(0, globalLogs.Lines.size(), 0, globalLogs.Columns.size());
    UpdateTitleText();
}

void CopyTextToClipboard(const std::string &text)
{
    if (!text.empty())
    {
        if (OpenClipboard(hwndMain))
        {
            EmptyClipboard();

            HGLOBAL clipMemHandle = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            if (clipMemHandle)
            {
                char *clipMemory = (char*)GlobalLock(clipMemHandle);
                if (clipMemory)
                {
                    std::copy(text.begin(), text.end(), clipMemory);
                    clipMemory[text.size()] = 0;
                    GlobalUnlock(clipMemHandle);
                    SetClipboardData(CF_TEXT, clipMemHandle);
                }
            }

            CloseClipboard();
        }
    }
}

void PromptAndParseDataFromClipboard()
{
    LogType logType = DoAskForLogType("Clipboard text", true);
    if (logType == LogType::None)
        return;

    bool merge = false;
    if (!globalLogs.Lines.empty())
        merge = MessageBox(hwndMain, "Merge new logs with existing logs?", "", MB_YESNO) == IDYES;

    if (!merge)
        MoveAndLoadLogs(LogCollection(), false); //clear old logs before we start to reduce memory use

    std::vector<ObtainerSource> obtainers;
    obtainers.emplace_back();
    obtainers.back().LogTypeIfKnown = logType;
    obtainers.back().Obtain = [](AppStatusMonitor &monitor)
    {
        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(0, "MB", 1000000);

        std::vector<char> allData;

        std::chrono::time_point<std::chrono::high_resolution_clock> timerStart = std::chrono::high_resolution_clock::now();
        if (OpenClipboard(hwndMain))
        {
            HANDLE clipDataHandle = GetClipboardData(CF_TEXT);
            if (clipDataHandle)
            {
                char *clipMemory = (char*)GlobalLock(clipDataHandle);
                if (clipMemory)
                {
                    char *cur = clipMemory;
                    while (*cur)
                    {
                        allData.emplace_back(*cur);
                        ++cur;

                        if (((size_t)&*cur) % 100000 == 99999)
                        {
                            monitor.AddProgress(100000);
                            if (monitor.IsCancelling())
                            {
                                allData.clear();
                                break;
                            }
                        }
                    }

                    GlobalUnlock(clipDataHandle);
                }
            }
            CloseClipboard();
        }
        std::chrono::time_point<std::chrono::high_resolution_clock> timerEnd = std::chrono::high_resolution_clock::now();
        monitor.AddDebugOutputTime("Read data from clipboard", std::chrono::duration_cast<std::chrono::microseconds>(timerEnd - timerStart).count() / 1000.0f);

        return std::move(allData);
    };

    auto newLogs = ObtainRawDataAndParse("Reading Data from Clipboard", obtainers, 1, ParserFilter());
    MoveAndLoadLogs(std::move(newLogs), merge);
}

//windows calls change the current directory for these, which breaks a lot of stuff.  so we wrap them and undo that dumb side-effect.
bool RunLoadSaveDialog(bool isSave, OPENFILENAME &ofn, std::string &chosenPath)
{
    char curDir[MAX_PATH + 1] = { 0 };
    if (!GetCurrentDirectory(MAX_PATH, curDir))
        return false;
    chosenPath = curDir;

    PushLockoutInteraction();
    bool ret;
    if (isSave)
        ret = 0 != GetSaveFileName(&ofn);
    else
        ret = 0 != GetOpenFileName(&ofn);
    PopLockoutInteraction();

    SetCurrentDirectory(curDir);

    return ret;
}

void AddDnsLookupColumnForIpColumn(int dataCol)
{
    bool columnAdded = false;
    GuiStatusManager::ShowBusyDialogAndRunMonitor("Doing DNS Lookups", true, [&](GuiStatusMonitor &monitor)
    {
        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(globalLogs.Lines.size(), "kilolines", 1000);

        if (dataCol < 0 || dataCol >= globalLogs.Columns.size())
            return;

        //since this is cancellable (and it's slow so they might actually cancel it), we need to do all of the lookups before we actually touch the original data
        std::vector<std::string*> dnsColText;
        dnsColText.resize(globalLogs.Lines.size(), nullptr);

        std::map<unsigned long, std::string> cachedLookups;

        std::atomic<size_t> sharedRow = 0;
        std::mutex mut;
        std::vector<std::thread> threads;

        for (int t = 0; t < 10; ++t) //do up to 10 in parallel since they're rather slow to resolve
        {
            threads.emplace_back([&]
            {
                while (true)
                {
                    size_t index = sharedRow++;
                    if (index >= globalLogs.Lines.size())
                        break;

                    if (monitor.IsCancelling())
                        break;

                    monitor.AddProgress(1);

                    auto &log = globalLogs.Lines[index];
                    const std::string ipString = log.GetColumnNumberValue((uint16_t)dataCol).str();
                    if (ipString.empty())
                        continue;

                    in_addr ipAddr = { 0 };
                    unsigned long numericIp = INADDR_NONE;
                    if (inet_pton(AF_INET, ipString.c_str(), &ipAddr) == 1)
                        numericIp = ipAddr.S_un.S_addr;

                    if (numericIp == INADDR_NONE)
                        continue;

                    {
                        std::lock_guard<std::mutex> guard(mut);

                        auto existing = cachedLookups.find(numericIp);
                        if (existing != cachedLookups.end())
                        {
                            dnsColText[index] = &existing->second;
                            continue;
                        }
                    }

                    sockaddr_in sa;
                    sa.sin_family = AF_INET;
                    sa.sin_addr.s_addr = numericIp;
                    sa.sin_port = 0;

                    char hostname[NI_MAXHOST];
                    hostname[0] = 0;
                    if (getnameinfo((sockaddr*)&sa, sizeof(sa), hostname, NI_MAXHOST, nullptr, 0, 0) || hostname[0] == 0) //failed, just put in IP
                    {
                        if (ipString.size() < NI_MAXHOST)
                            strcpy(hostname, ipString.c_str());
                    }

                    {
                        std::lock_guard<std::mutex> guard(mut);

                        auto iter = cachedLookups.emplace(numericIp, hostname);
                        dnsColText[index] = &iter.first->second;
                    }
                }
            });
        }

        for (auto &t : threads)
            t.join();

        //alter every logline to contain this new data
        if (!monitor.IsCancelling())
        {
            //add a column for this new data
            ColumnInformation newColumn = globalLogs.Columns[dataCol];
            newColumn.UniqueName += "_DNS";
            newColumn.Description = "IP-to-DNS lookup";
            if (!newColumn.DisplayNameOverride.empty())
                newColumn.DisplayNameOverride += "_DNS";
            for (int c = 0; c < (int)globalLogs.Columns.size(); ++c)
            {
                if (globalLogs.Columns[c].UniqueName == newColumn.UniqueName)
                    return; //we've already done the geo ip lookups for this column
            }

            globalLogs.Columns.emplace_back(newColumn);
            int dnsCol = (int)globalLogs.Columns.size() - 1;
            columnAdded = true;

            //add the data
            for (size_t row = 0; row < globalLogs.Lines.size(); ++row)
            {
                if (dnsColText[row] && !dnsColText[row]->empty())
                {
                    auto &log = globalLogs.Lines[row];
                    log.AppendExtra(*dnsColText[row], std::vector<LogEntryColumn>{LogEntryColumn((uint16_t)dnsCol, 0, (uint16_t)dnsColText[row]->size())});
                }
            }
        }

        monitor.Complete();
    });

    if (columnAdded)
        LogViewNotifyDataChanged(0, 0, globalLogs.Columns.size() - 1, globalLogs.Columns.size());
}

BOOL CALLBACK FixFont(HWND hwnd, LPARAM italic)
{
    if (!italic)
        SendMessage(hwnd, WM_SETFONT, (WPARAM)lessDerpyFont, true);
    else
        SendMessage(hwnd, WM_SETFONT, (WPARAM)lessDerpyFontItalic, true);
    return true;
}

void FixChildFonts(HWND hwnd)
{
    EnumChildWindows(hwnd, FixFont, 0);
}

void HandleDragDropFileResults(const std::vector<std::string> &files)
{
    dragDroppedFiles = files;
    PostMessage(hwndMain, WM_DRAGDROPPED_FILES, 0, 0);
}

bool HandleDragDropFileBusyCheck()
{
    return GuiStatusManager::IsBusyAnything() || currentInteractionOwner != 0;
}

void PushLockoutInteraction(HWND interactionOwner)
{
    if (interactionOwner)
    {
        interactionOwnerStack.emplace_back(interactionOwner);
        currentInteractionOwner = interactionOwner;

        EnableWindow(hwndMain, false);
        LogViewWindowLockoutInteraction(false);
    }
}

void PopLockoutInteraction()
{
    if (!interactionOwnerStack.empty())
    {
        interactionOwnerStack.pop_back();

        if (!interactionOwnerStack.empty())
            currentInteractionOwner = interactionOwnerStack.back();
        else
        {
            currentInteractionOwner = 0;
            EnableWindow(hwndMain, true);
            LogViewWindowLockoutInteraction(true);
        }
    }
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        //buttons
        hwndOpenLocal = CreateWindow(WC_BUTTON, "Open Local", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 7, 1, 90, 22, hwnd, 0, hInstance, 0);
        hwndSetup = CreateWindow(WC_BUTTON, "Setup", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 505, 1, 50, 22, hwnd, 0, hInstance, 0);
        hwndDebug = CreateWindow(WC_BUTTON, "Debug", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 445, 1, 50, 22, hwnd, 0, hInstance, 0);

        //
        FixChildFonts(hwnd);

        //set us up the drag and drop
        SetupDragDropForWindow(hwnd, HandleDragDropFileResults, HandleDragDropFileBusyCheck);

        //
        MoveAndLoadLogs(LogCollection(), false);
    }
    //INTENTIONAL FALL THROUGH
    case WM_SIZE:
    {
        RECT mainClientRect;
        GetClientRect(hwnd, &mainClientRect);

        SetWindowPos(hwndMainLogView, 0, 0, 24, mainClientRect.right, mainClientRect.bottom - 24, 0);
        SetWindowPos(hwndDebug, 0, mainClientRect.right - 110, 1, 0, 0, SWP_NOSIZE);
        SetWindowPos(hwndSetup, 0, mainClientRect.right - 50, 1, 0, 0, SWP_NOSIZE);
        return 0;
    }
    case WM_ACTIVATE:
    {
        if (wParam == WA_ACTIVE || wParam == WA_CLICKACTIVE)
        {
            if (currentInteractionOwner)
            {
                SetActiveWindow(currentInteractionOwner);
                return 0;
            }
            else
                activeMainWindow = hwnd;
        }
    }
    break;
    case WM_FIRSTRUN_PROMPTS:
    {
        if (!Preferences::HasTestedParallelism)
        {
            if (IDYES == MessageBox(hwnd, "LogCheetah has never tested the ideal parallelism on this machine.\r\n\r\nTest it now?\r\n(You can adjust this later under the Setup dialog)", "Performance Optimization", MB_YESNO))
            {
                Preferences::HasTestedParallelism = true;
                RunTestParallelismDialog();
            }
        }
        break;
    }
    case WM_PROCESS_QUEUED_DEBUG_MESSAGES:
    {
        ProcessAllQueuedDebugMessages();
        break;
    }
    case WM_COMMAND:
    {
        if ((HWND)lParam == hwndOpenLocal)
        {
            DoLoadLogsFromFileDialog();
            return 0;
        }
        else if ((HWND)lParam == hwndSetup)
        {
            ShowSetupDialog();
            return 0;
        }
        else if ((HWND)lParam == hwndDebug)
        {
            ShowDebugWindow();
            return 0;
        }
        else if ((HWND)lParam == 0) //weird case for context menus and accelerators
        {
            switch (LOWORD(wParam))
            {
            case ACCEL_DEBUGWINDOW:
            {
                ShowDebugWindow();
                return 0;
            }
            }

            //forward them onto the log window child of the main window if we didn't handle it
            SendMessage(hwndMainLogView, uMsg, wParam, lParam);
        }
    }
    break;
    case WM_DRAGDROPPED_FILES:
    {
        DoLoadLogsFromFileBatch(dragDroppedFiles);

        dragDroppedFiles.clear();
        return 0;
    }
    case WM_DESTROY:
        LogViewCloseAllWindows();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    //let's play nice, for when people want to do other stuff at the same time
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    // if the app was "opened via right clicking it on the taskbar, the current directory won't be where the .exe lives.. this breaks a lot of stuff
    std::string processDirectory;
    processDirectory.resize(2000);
    int moduleFileNameSize = GetModuleFileName(0, processDirectory.data(), (int)processDirectory.size());
    processDirectory.resize(moduleFileNameSize);

    if (processDirectory.size() >= 2 && processDirectory[1] == ':') // only try for normal local paths
    {
        size_t firstSep = processDirectory.find_first_of('\\');
        size_t lastSep = processDirectory.find_last_of('\\');
        if (firstSep != lastSep && firstSep != std::string::npos && lastSep != std::string::npos) // if we got a weird root path, don't even try
        {
            processDirectory.resize(lastSep);
            SetCurrentDirectory(processDirectory.c_str());
        }
    }

    //
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SharedGlobalInit();
    Preferences::Load();
    OverrideCpuCount(Preferences::ParallelismOverrideGeneral, Preferences::ParallelismOverrideParse, Preferences::ParallelismOverrideSort, Preferences::ParallelismOverrideFilter);

    //common control stuff
    ::hInstance = hInstance;

    INITCOMMONCONTROLSEX icex;
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    //main window class
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)MainWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 0, 0, LR_CREATEDIBSECTION);
    wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = "LogCheetah";

    if (!RegisterClass(&wc))
        return -1;

    //load a font that doesn't look silly bold everywhere
    NONCLIENTMETRICS nonClientMetrics = { 0 };
    nonClientMetrics.cbSize = sizeof(NONCLIENTMETRICS);
    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &nonClientMetrics, 0))
    {
        lessDerpyFont = CreateFontIndirect(&nonClientMetrics.lfMessageFont);

        LOGFONT lfItalic = nonClientMetrics.lfMessageFont;
        lfItalic.lfItalic = true;
        lessDerpyFontItalic = CreateFontIndirect(&lfItalic);

        LOGFONT lfBold = nonClientMetrics.lfMessageFont;
        lfBold.lfWeight *= 2;
        lessDerpyFontBold = CreateFontIndirect(&lfBold);
    }

    LogViewInitialize();

    //main window
    hwndMain = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, "LogCheetah", "LogCheetah - On The Prowl", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, (HWND)0, (HMENU)0, hInstance, (LPVOID)0);
    activeMainWindow = hwndMain;
    if (!hwndMain)
        return -1;

    //
    ShowWindow(hwndMain, nCmdShow);
    UpdateWindow(hwndMain);

    ResetUI();
    UpdateTitleText();

    //setup accelerator keys
    std::array<ACCEL, 7> accelKeys = { 0 };
    accelKeys[0].fVirt = FALT | FVIRTKEY;
    accelKeys[0].key = 'D';
    accelKeys[0].cmd = ACCEL_DEBUGWINDOW;
    accelKeys[1].fVirt = FCONTROL | FVIRTKEY;
    accelKeys[1].key = 'C';
    accelKeys[1].cmd = ACCEL_COPY;
    accelKeys[2].fVirt = FCONTROL | FVIRTKEY;
    accelKeys[2].key = 'X';
    accelKeys[2].cmd = ACCEL_CUT;
    accelKeys[3].fVirt = FCONTROL | FVIRTKEY;
    accelKeys[3].key = 'V';
    accelKeys[3].cmd = ACCEL_PASTE;
    accelKeys[4].fVirt = FCONTROL | FVIRTKEY;
    accelKeys[4].key = 'A';
    accelKeys[4].cmd = ACCEL_SELALL;
    accelKeys[5].fVirt = FVIRTKEY;
    accelKeys[5].key = VK_F3;
    accelKeys[5].cmd = ACCEL_FINDNEXT;
    accelKeys[6].fVirt = FSHIFT | FVIRTKEY;
    accelKeys[6].key = VK_F3;
    accelKeys[6].cmd = ACCEL_FINDPREV;
    HACCEL accelTable = CreateAcceleratorTable(accelKeys.data(), (int)accelKeys.size());

    //meow
    InitCatWindow(hwndMain);

    //Kick off first run stuff
    PostMessage(hwndMain, WM_FIRSTRUN_PROMPTS, 0, 0);

    //loop!
    MSG msg;
    while (GetMessage(&msg, 0, 0, 0) > 0)
    {
        LogViewPreprocessMessage(msg);

        HWND translateHwnd = (LogViewRequiresSpecificWindowTranslated(msg.hwnd) ? msg.hwnd : GetActiveWindow());
        if (!TranslateAccelerator(translateHwnd, accelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    ShutdownCatWindow();

    Preferences::Save();
    return 0;
}
