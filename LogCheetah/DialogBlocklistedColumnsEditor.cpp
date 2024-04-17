#include "DialogBlocklistedColumnsEditor.h"

#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <set>

#include "Preferences.h"
#include "Globals.h"
#include "WinMain.h"
#include "Win32Helpers.h"

INT_PTR CALLBACK PickBlocklistedDefaultColumnsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

namespace
{
    HWND hwndBlocklistDefaultColumnsOk = 0;
    HWND hwndBlocklistDefaultColumnsCancel = 0;
    HWND hwndBlocklistPossibleColumns = 0;
    HWND hwndBlocklistCurrentColumns = 0;
    HWND hwndBlocklistAdd = 0;
    HWND hwndBlocklistRemove = 0;

    std::set<std::string> currentColumnBlocklist;
}

void ShowBlocklistedDefaultColumnsEditor()
{
    //initialize from preferences
    currentColumnBlocklist.clear();
    currentColumnBlocklist.insert(Preferences::BlockListedDefaultColumns.begin(), Preferences::BlockListedDefaultColumns.end());

    //show box
    struct
    {
        DLGTEMPLATE dlg;
        DWORD trash0, trash1, trash2;
    } dlg = { 0 };

    dlg.dlg.style = DS_CENTER;
    dlg.dlg.dwExtendedStyle = 0;
    dlg.dlg.cx = 710;
    dlg.dlg.cy = 470;

    INT_PTR dbiRet = DialogBoxIndirect(hInstance, &dlg.dlg, activeMainWindow, PickBlocklistedDefaultColumnsProc);
    PopLockoutInteraction();
    if (dbiRet <= 0)
        return;

    //store in preferences
    Preferences::BlockListedDefaultColumns.clear();
    Preferences::BlockListedDefaultColumns.insert(Preferences::BlockListedDefaultColumns.begin(), currentColumnBlocklist.begin(), currentColumnBlocklist.end());
}

INT_PTR CALLBACK PickBlocklistedDefaultColumnsProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        PushLockoutInteraction(hwnd);
        SetWindowText(hwnd, "Default Columns To Ignore");

        //oddly it creates us at a size different than we specified.. so fix it
        SetWindowPos(hwnd, 0, 0, 0, 500, 500, SWP_NOMOVE);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        //dialog buttons
        hwndBlocklistDefaultColumnsOk = CreateWindow(WC_BUTTON, "I Like It", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, clientRect.right - 140, clientRect.bottom - 30, 135, 25, hwnd, 0, hInstance, 0);
        hwndBlocklistDefaultColumnsCancel = CreateWindow(WC_BUTTON, "I Changed My Mind", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 5, clientRect.bottom - 30, 135, 25, hwnd, 0, hInstance, 0);

        //lists
        CreateWindow(WC_STATIC, "The Possibilities Are Endless:", WS_VISIBLE | WS_CHILD, 45, 5, 200, 20, hwnd, 0, hInstance, 0);
        hwndBlocklistPossibleColumns = CreateWindow(WC_LISTBOX, "", WS_VISIBLE | WS_CHILD | LBS_SORT | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 5, 25, (clientRect.right - clientRect.left) / 2 - 10, clientRect.bottom - 30 - 40 - 5, hwnd, 0, hInstance, 0);
        CreateWindow(WC_STATIC, "Current Ignore List:", WS_VISIBLE | WS_CHILD, 305, 5, 200, 20, hwnd, 0, hInstance, 0);
        hwndBlocklistCurrentColumns = CreateWindow(WC_LISTBOX, "", WS_VISIBLE | WS_CHILD | LBS_SORT | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 5 + (clientRect.right - clientRect.left) / 2, 25, (clientRect.right - clientRect.left) / 2 - 10, clientRect.bottom - 30 - 40 - 5, hwnd, 0, hInstance, 0);

        std::set<std::string> everythingPossible;
        for (auto &c : globalLogs.Columns)
            everythingPossible.emplace(c.UniqueName);
        for (auto &s : currentColumnBlocklist)
            everythingPossible.emplace(s);

        for (const auto &s : everythingPossible)
        {
            if (std::find(currentColumnBlocklist.begin(), currentColumnBlocklist.end(), s) == currentColumnBlocklist.end())
                ListBox_AddString(hwndBlocklistPossibleColumns, s.c_str());
        }

        for (const auto &s : currentColumnBlocklist)
            ListBox_AddString(hwndBlocklistCurrentColumns, s.c_str());

        //add and remove buttons
        hwndBlocklistAdd = CreateWindow(WC_BUTTON, "Ignore", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, (clientRect.right - clientRect.left) / 2 - 15 - 70, clientRect.bottom - 30 - 20 - 5, 70, 23, hwnd, 0, hInstance, 0);
        hwndBlocklistRemove = CreateWindow(WC_BUTTON, "Remove", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, (clientRect.right - clientRect.left) / 2 + 15, clientRect.bottom - 30 - 20 - 5, 70, 23, hwnd, 0, hInstance, 0);
        EnableWindow(hwndBlocklistAdd, false);
        EnableWindow(hwndBlocklistRemove, false);

        FixChildFonts(hwnd);

        return TRUE;
    }
    case WM_COMMAND:
    {
        if ((HWND)lParam == hwndBlocklistDefaultColumnsOk)
            EndDialog(hwnd, 1);
        else if ((HWND)lParam == hwndBlocklistDefaultColumnsCancel)
            EndDialog(hwnd, 0);
        else if ((HWND)lParam == hwndBlocklistAdd)
        {
            int curSel = ListBox_GetCurSel(hwndBlocklistPossibleColumns);
            if (curSel != LB_ERR)
            {
                std::string lbString = Win32ListBoxGetText(hwndBlocklistPossibleColumns, curSel);
                if (currentColumnBlocklist.find(lbString) == currentColumnBlocklist.end())
                {
                    int existing = ListBox_FindString(hwndBlocklistPossibleColumns, -1, lbString.c_str());
                    if (existing != LB_ERR)
                    {
                        ListBox_DeleteString(hwndBlocklistPossibleColumns, existing);
                        ListBox_AddString(hwndBlocklistCurrentColumns, lbString.c_str());
                        currentColumnBlocklist.emplace(lbString);
                        ListBox_SetSel(hwndBlocklistPossibleColumns, false, 0);
                        EnableWindow(hwndBlocklistAdd, false);
                    }
                }
            }
        }
        else if ((HWND)lParam == hwndBlocklistRemove)
        {
            int curSel = ListBox_GetCurSel(hwndBlocklistCurrentColumns);
            if (curSel != LB_ERR)
            {
                std::string lbString = Win32ListBoxGetText(hwndBlocklistCurrentColumns, curSel);
                int existing = ListBox_FindString(hwndBlocklistCurrentColumns, -1, lbString.c_str());
                if (existing != LB_ERR)
                {
                    ListBox_DeleteString(hwndBlocklistCurrentColumns, existing);
                    ListBox_AddString(hwndBlocklistPossibleColumns, lbString.c_str());
                    currentColumnBlocklist.erase(lbString);
                    ListBox_SetSel(hwndBlocklistCurrentColumns, false, 0);
                    EnableWindow(hwndBlocklistRemove, false);
                }
            }
        }
        else if ((HWND)lParam == hwndBlocklistPossibleColumns)
        {
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                int curSel = ListBox_GetCurSel(hwndBlocklistPossibleColumns);
                EnableWindow(hwndBlocklistAdd, (curSel != LB_ERR));
            }
        }
        else if ((HWND)lParam == hwndBlocklistCurrentColumns)
        {
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                int curSel = ListBox_GetCurSel(hwndBlocklistCurrentColumns);
                EnableWindow(hwndBlocklistRemove, (curSel != LB_ERR));
            }
        }
    }
    };

    return FALSE;
}
