#include "DebugWindow.h"
#include "WinMain.h"
#include "resource.h"

#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <string>

const UINT WM_PROCESS_QUEUED_DEBUG_MESSAGES = WM_USER + 4;

namespace
{
    HWND hwndDebugOutput = 0;
    HWND hwndDebugText = 0;
    bool hasRegisteredClass = false;

    std::mutex debugOutputMut;
    std::string queuedDebugOutput;
}

LRESULT CALLBACK DebugWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        hwndDebugText = CreateWindow(WC_EDIT, "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_MULTILINE | WS_VSCROLL | WS_HSCROLL, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, hwnd, 0, hInstance, 0);

        return 0;
    }

    case WM_COMMAND:
    {
        if (hwndDebugText)
        {
            if ((HWND)lParam == 0) //weird case for context menus and accelerators
            {
                switch (LOWORD(wParam))
                {
                case ACCEL_COPY:
                {
                    std::string dataToCopy;
                    DWORD selStart = 0, selEnd = 0;
                    SendMessage(hwndDebugText, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
                    if (selStart != selEnd)
                    {
                        size_t len = GetWindowTextLength(hwndDebugText);
                        dataToCopy.resize(len + 1);
                        len = GetWindowText(hwndDebugText, (char*)dataToCopy.data(), (int)dataToCopy.size());
                        dataToCopy.resize(len);
                        while (!dataToCopy.empty() && dataToCopy.back() == 0)
                            dataToCopy.pop_back();

                        if (selEnd > dataToCopy.size())
                            selEnd = (DWORD)dataToCopy.size();

                        dataToCopy = std::string(dataToCopy.c_str() + selStart, dataToCopy.c_str() + selEnd);

                        CopyTextToClipboard(dataToCopy);
                    }
                    return 0;
                }
                case ACCEL_SELALL:
                {
                    SendMessage(hwndDebugText, EM_SETSEL, 0, -1);
                    return 0;
                }
                }
            }
        }
        break;
    }

    case WM_DESTROY: case WM_QUIT: case WM_CLOSE:
    {
        hwndDebugOutput = 0;
        hwndDebugText = 0;
        break;
    }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void GlobalDebugOutput(const std::string &s)
{
    if (hwndDebugText)
    {
        std::lock_guard<std::mutex> lock(debugOutputMut);
        queuedDebugOutput += s + "\r\n";
    }

    PostMessage(hwndMain, WM_PROCESS_QUEUED_DEBUG_MESSAGES, 0, 0);
}

void ShowDebugWindow()
{
    if (!hasRegisteredClass)
    {
        WNDCLASS wc;
        wc.style = 0;
        wc.lpfnWndProc = (WNDPROC)DebugWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_CREATEDIBSECTION);
        wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "LogCheetahDebugSpew";

        if (!RegisterClass(&wc))
            return;
        hasRegisteredClass = true;
    }

    if (!hwndDebugOutput)
    {
        hwndDebugOutput = CreateWindow("LogCheetahDebugSpew", "Debug", WS_OVERLAPPED | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 700, 500, 0, (HMENU)0, hInstance, 0);
    }
}

void ProcessAllQueuedDebugMessages()
{
    if (hwndDebugText)
    {
        std::string stringToAdd;
        {
            std::lock_guard<std::mutex> lock(debugOutputMut);
            stringToAdd = std::move(queuedDebugOutput);
        }

        int len = Edit_GetTextLength(hwndDebugText);
        Edit_SetSel(hwndDebugText, len, len);
        Edit_ReplaceSel(hwndDebugText, stringToAdd.c_str());
    }
}
