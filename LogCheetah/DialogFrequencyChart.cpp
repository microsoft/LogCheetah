// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "DialogFrequencyChart.h"
#include "Globals.h"
#include "WinMain.h"
#include "GuiStatusMonitor.h"
#include "resource.h"

#include <Windowsx.h>
#include <CommCtrl.h>
#include <thread>
#include <list>
#include <memory>
#include <map>
#include <iomanip>

namespace
{
    std::string TempGetItemString;

    struct FrequencyChart
    {
        HWND hwndWindow = 0;
        HWND hwndChart = 0;

        HWND hwndButtonCopy = 0;
        HWND hwndCopyName = 0;
        HWND hwndCopyCount = 0;
        HWND hwndCopyPercent = 0;

        std::string ColumnName;
        std::vector<std::pair<std::string, int>> Occurances;
        int TotalRowsRepresented = 0;
    };

    std::list<std::shared_ptr<FrequencyChart>> AllInstances;

    LRESULT CALLBACK FrequencyChartWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        FrequencyChart *fc = (FrequencyChart*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
            fc = (FrequencyChart*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)fc);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            //copy
            fc->hwndButtonCopy = CreateWindow(WC_BUTTON, "Copy To Clipboard", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 5, 2, 140, 25, hwnd, 0, hInstance, 0);

            fc->hwndCopyName = CreateWindow(WC_BUTTON, "Name", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 152, 4, 60, 22, hwnd, 0, hInstance, 0);
            Button_SetCheck(fc->hwndCopyName, true);
            fc->hwndCopyCount = CreateWindow(WC_BUTTON, "Count", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 212, 4, 60, 22, hwnd, 0, hInstance, 0);
            Button_SetCheck(fc->hwndCopyCount, true);
            fc->hwndCopyPercent = CreateWindow(WC_BUTTON, "Percent", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 272, 4, 70, 22, hwnd, 0, hInstance, 0);
            Button_SetCheck(fc->hwndCopyPercent, true);

            //data
            fc->hwndChart = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, "", WS_CHILD | LVS_REPORT | LVS_NOSORTHEADER | WS_VISIBLE | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0, 30, clientRect.right, clientRect.bottom - clientRect.top - 30, hwnd, 0, hInstance, 0);
            ListView_SetExtendedListViewStyle(fc->hwndChart, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT);

            LVCOLUMN lvc = { 0 };
            lvc.fmt = LVCFMT_LEFT;
            lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT;
            lvc.cx = 200;
            lvc.pszText = (LPSTR)fc->ColumnName.c_str();
            ListView_InsertColumn(fc->hwndChart, 0, &lvc);
            lvc.pszText = (char*)"Count";
            lvc.cx = 100;
            ListView_InsertColumn(fc->hwndChart, 1, &lvc);
            lvc.pszText = (char*)"Percent";
            lvc.cx = 80;
            ListView_InsertColumn(fc->hwndChart, 2, &lvc);

            ListView_SetItemCount(fc->hwndChart, fc->Occurances.size());

            //
            FixChildFonts(hwnd);

            return 0;
        }
        case WM_COMMAND:
        {
            if ((HWND)lParam == fc->hwndButtonCopy)
            {
                bool copyName = Button_GetCheck(fc->hwndCopyName) == BST_CHECKED;
                bool copyCount = Button_GetCheck(fc->hwndCopyCount) == BST_CHECKED;
                bool copyPercent = Button_GetCheck(fc->hwndCopyPercent) == BST_CHECKED;

                std::vector<std::string> headerNames;
                if (copyName)
                    headerNames.push_back(fc->ColumnName);
                if (copyCount)
                    headerNames.push_back("Count");
                if (copyPercent)
                    headerNames.push_back("Percent");

                std::stringstream ss;
                ss << StringJoin('|', headerNames.begin(), headerNames.end()) << "\r\n";

                std::vector<std::string> vals;
                vals.resize(headerNames.size());
                for (auto &o : fc->Occurances)
                {
                    int c = 0;
                    if (copyName)
                        vals[c++] = o.first;
                    if (copyCount)
                    {
                        std::stringstream count;
                        count << o.second;
                        vals[c++] = count.str();
                    }
                    if (copyPercent)
                    {
                        std::stringstream percent;
                        percent << std::fixed << std::setprecision(2) << 100.0*o.second / fc->TotalRowsRepresented;
                        vals[c++] = percent.str();
                    }

                    ss << StringJoin('|', vals.begin(), vals.end()) << "\r\n";
                }

                std::string str = ss.str();
                CopyTextToClipboard(str);
            }
            else if (HWND(lParam) == fc->hwndCopyName || HWND(lParam) == fc->hwndCopyCount || HWND(lParam) == fc->hwndCopyPercent)
            {
                Button_SetCheck(HWND(lParam), !Button_GetCheck(HWND(lParam)));

                bool copyName = Button_GetCheck(fc->hwndCopyName) == BST_CHECKED;
                bool copyCount = Button_GetCheck(fc->hwndCopyCount) == BST_CHECKED;
                bool copyPercent = Button_GetCheck(fc->hwndCopyPercent) == BST_CHECKED;

                EnableWindow(fc->hwndButtonCopy, copyName || copyCount || copyPercent);
            }
        }
        break;
        case WM_SIZE:
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            SetWindowPos(fc->hwndChart, 0, 0, 30, clientRect.right, clientRect.bottom - clientRect.top - 30, 0);
        }
        break;
        case WM_NOTIFY:
        {
            NMHDR *n = (NMHDR*)lParam;
            if (n->hwndFrom == fc->hwndChart)
            {
                switch (n->code)
                {
                case LVN_GETDISPINFO:
                {
                    NMLVDISPINFO *di = (NMLVDISPINFO*)lParam;
                    int row = di->item.iItem;
                    int col = di->item.iSubItem;

                    TempGetItemString.clear();
                    if (row >= 0 && row < fc->Occurances.size() && col >= 0 && col < 3)
                    {
                        if (col == 0)
                            TempGetItemString = fc->Occurances[row].first;
                        else if (col == 1)
                        {
                            std::stringstream ss;
                            ss << fc->Occurances[row].second;
                            TempGetItemString = ss.str();
                        }
                        else
                        {
                            std::stringstream ss;
                            ss << std::fixed << std::setprecision(2) << 100.0*fc->Occurances[row].second / fc->TotalRowsRepresented;
                            TempGetItemString = ss.str();
                        }
                    }

                    di->item.pszText = (char*)TempGetItemString.c_str();
                    return 0;
                }
                //prevent selection from being shown since we don't act on it
                case NM_CUSTOMDRAW:
                {
                    LPNMCUSTOMDRAW pncd = (LPNMCUSTOMDRAW)lParam;
                    switch (pncd->dwDrawStage)
                    {
                    case CDDS_PREPAINT:
                        return CDRF_NOTIFYITEMDRAW;
                    case CDDS_ITEMPREPAINT:
                    {
                        pncd->uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
                        return CDRF_DODEFAULT;
                    }
                    }
                }
                }
            }
        }
        break;
        case WM_DESTROY: case WM_QUIT: case WM_CLOSE:
            AllInstances.remove_if([&](std::shared_ptr<FrequencyChart> p) {return p.get() == fc; });
            break;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    bool didRegistorWindowClass = false;
    void RegisterWindowClass()
    {
        if (didRegistorWindowClass)
            return;

        WNDCLASS wc;
        wc.style = 0;
        wc.lpfnWndProc = (WNDPROC)FrequencyChartWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_CREATEDIBSECTION);
        wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "LogCheetahFrequencyChart";

        if (!RegisterClass(&wc))
            return;

        didRegistorWindowClass = true;
    }
}

std::shared_ptr<FrequencyChart> ComputeFrequencyChart(const std::vector<uint32_t> &dataColumns, const std::vector<uint32_t> &rowsToUse)
{
    AllInstances.emplace_back(std::make_shared<FrequencyChart>());
    std::shared_ptr<FrequencyChart> fc = AllInstances.back();

    GuiStatusManager::ShowBusyDialogAndRunMonitor("Crunching data", false, [&](GuiStatusMonitor &monitor)
    {
        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(rowsToUse.size(), "kiloline", 1000);

        fc->ColumnName = StringJoin(" + ", dataColumns.begin(), dataColumns.end(), [&](int c) {return globalLogs.Columns[c].UniqueName; });

        std::map<std::string, int> occurances;

        for (int row = 0; row < (int)rowsToUse.size(); ++row)
        {
            if (monitor.IsCancelling())
            {
                fc = nullptr;
                break;
            }

            ++fc->TotalRowsRepresented;

            LogEntry &le = globalLogs.Lines[rowsToUse[row]];
            const std::string val = StringJoin(" + ", dataColumns.begin(), dataColumns.end(), [&](int c) {return le.GetColumnNumberValue((uint16_t)c).str(); });

            auto iter = occurances.find(val);
            if (iter == occurances.end())
                iter = occurances.emplace(std::make_pair(val, 0)).first;

            ++iter->second;

            monitor.AddProgress(1);
        }

        if (fc)
        {
            fc->Occurances.assign(occurances.begin(), occurances.end());
            std::sort(fc->Occurances.begin(), fc->Occurances.end(), [](std::pair<std::string, int> &a, std::pair<std::string, int> &b) {return a.second > b.second; });
        }

        monitor.Complete();
    });
    return fc;
}

void ShowFrequencyChart(const std::vector<uint32_t> &dataColumns, const std::vector<uint32_t> &rowsToUse)
{
    std::shared_ptr<FrequencyChart> fc = ComputeFrequencyChart(dataColumns, rowsToUse);

    if (fc)
    {
        RegisterWindowClass();
        std::stringstream windowText;
        windowText << fc->ColumnName << " - " << fc->TotalRowsRepresented << " Data Points - " << fc->Occurances.size() << " Unique Values";
        fc->hwndWindow = CreateWindow("LogCheetahFrequencyChart", windowText.str().c_str(), WS_OVERLAPPED | WS_SYSMENU | WS_SIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 450, 600, 0, (HMENU)0, hInstance, fc.get()); //this call has to happen from the main thread
    }
}
