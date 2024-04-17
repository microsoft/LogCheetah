#include "DialogPickLogFormat.h"

#include "WinMain.h"
#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <filesystem>
#include <cctype>
#include <algorithm>

namespace
{
    HWND hwndAuto = 0;
    HWND hwndCSV = 0;
    HWND hwndPSV = 0;
    HWND hwndTSV = 0;
    HWND hwndSSV = 0;
    HWND hwndJson = 0;
    HWND hwndNestedJson = 0;
    HWND hwndTRX = 0;
    HWND hwndCancel = 0;

    //user dialog data
    LogType chosenLogType;
    std::string description;
    bool allowCancel;
}

INT_PTR CALLBACK OpenPickLogFormatDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

LogType DoAskForLogType(const std::string  &description, bool allowCancel)
{
    chosenLogType = LogType::None;
    ::description = description;
    ::allowCancel = allowCancel;

    //show box
    struct
    {
        DLGTEMPLATE dlg;
        DWORD trash0, trash1, trash2;
    } dlg = { 0 };

    dlg.dlg.style = DS_CENTER;
    dlg.dlg.dwExtendedStyle = 0;
    dlg.dlg.cx = 500;
    dlg.dlg.cy = 230;

    INT_PTR dbiRet = DialogBoxIndirect(hInstance, &dlg.dlg, activeMainWindow, OpenPickLogFormatDialogProc);
    PopLockoutInteraction();
    if (dbiRet <= 0)
        return LogType::None;

    return chosenLogType;
}

LogType IdentifyLogTypeFromFileExtension(const std::string &filename)
{
    std::string fileExtension = std::filesystem::path(filename).extension().string();
    TransformStringToLower(fileExtension);

    if (fileExtension == ".csv")
        return LogType::CSV;
    else if (fileExtension == ".psv")
        return LogType::PSV;
    else if (fileExtension == ".tsv")
        return LogType::TSV;
    else if (fileExtension == ".ssv")
        return LogType::SSV;
    else if (fileExtension == ".json")
        return LogType::Json;

    return LogType::Unknown;
}

INT_PTR CALLBACK OpenPickLogFormatDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        PushLockoutInteraction(hwnd);
        SetWindowText(hwnd, "Choose Log Format");

        //oddly it creates us at a size different than we specified.. so fix it
        int hei = 230;
        if (!allowCancel)
            hei -= 40;
        SetWindowPos(hwnd, 0, 0, 0, 500, hei, SWP_NOMOVE);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        //description
        CreateWindow(WC_STATIC, description.c_str(), WS_VISIBLE | WS_CHILD | SS_CENTER | SS_PATHELLIPSIS, 45, 5, 410, 20, hwnd, 0, hInstance, 0);

        //text based options
        int y = 40;
        hwndAuto = CreateWindow(WC_BUTTON, "Auto", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 220, y, 60, 27, hwnd, 0, hInstance, 0);
        y += 50;
        hwndCSV = CreateWindow(WC_BUTTON, "CSV", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 0 * 400 / 4, y, 60, 22, hwnd, 0, hInstance, 0);
        hwndTSV = CreateWindow(WC_BUTTON, "TSV", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 1 * 400 / 4, y, 60, 22, hwnd, 0, hInstance, 0);
        hwndTRX = CreateWindow(WC_BUTTON, "TRX", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 2 * 400 / 4, y, 60, 22, hwnd, 0, hInstance, 0);
        hwndSSV = CreateWindow(WC_BUTTON, "SSV", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 3 * 400 / 4, y, 60, 22, hwnd, 0, hInstance, 0);
        hwndPSV = CreateWindow(WC_BUTTON, "PSV", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 4 * 400 / 4, y, 60, 22, hwnd, 0, hInstance, 0);
        y += 30;
        hwndJson = CreateWindow(WC_BUTTON, "Json", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,             20 + 1 * 400 / 4, y, 60, 22, hwnd, 0, hInstance, 0);
        hwndNestedJson = CreateWindow(WC_BUTTON, "NestedJson", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 20 + 2 * 400 / 4, y, 70, 22, hwnd, 0, hInstance, 0);

        //cancel
        if (allowCancel)
        {
            hwndCancel = CreateWindow(WC_BUTTON, "I Changed My Mind", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, clientRect.right - 140, clientRect.bottom - 30, 135, 25, hwnd, 0, hInstance, 0);
        }

        FixChildFonts(hwnd);

        return TRUE;
    }
    case WM_COMMAND:
    {
        if ((HWND)lParam == hwndAuto)
        {
            chosenLogType = LogType::Unknown;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndCSV)
        {
            chosenLogType = LogType::CSV;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndTSV)
        {
            chosenLogType = LogType::TSV;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndSSV)
        {
            chosenLogType = LogType::SSV;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndPSV)
        {
            chosenLogType = LogType::PSV;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndJson)
        {
            chosenLogType = LogType::Json;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndNestedJson)
        {
            chosenLogType = LogType::NestedJson;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndTRX)
        {
            chosenLogType = LogType::TRX;
            EndDialog(hwnd, 1);
        }
        else if ((HWND)lParam == hwndCancel)
            EndDialog(hwnd, 0);
    }
    };

    return FALSE;
}
