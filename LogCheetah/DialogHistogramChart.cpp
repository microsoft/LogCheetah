// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "DialogHistogramChart.h"
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

    HFONT graphFont = 0;

    void SetupFontIfNeeded()
    {
        if (graphFont)
            return;

        //load a font that doesn't look silly bold everywhere
        NONCLIENTMETRICS nonClientMetrics = { 0 };
        nonClientMetrics.cbSize = sizeof(NONCLIENTMETRICS);
        if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, 0, &nonClientMetrics, 0))
        {
            nonClientMetrics.lfMessageFont.lfWeight = 200;
            if (nonClientMetrics.lfMessageFont.lfHeight > 0)
                nonClientMetrics.lfMessageFont.lfHeight -= 1;
            else
                nonClientMetrics.lfMessageFont.lfHeight += 1;
            graphFont = CreateFontIndirect(&nonClientMetrics.lfMessageFont);
        }
    }

    struct HistogramChart
    {
        ~HistogramChart()
        {
            if (GraphDib)
                DeleteObject(GraphDib);
            GraphDib = 0;

            if (GraphDC)
                DeleteDC(GraphDC);
            GraphDC = 0;
        }

        HWND hwndWindow = 0;
        HWND hwndChart = 0;
        HWND hwndGraph = 0;

        HWND hwndModeGraph = 0;
        HWND hwndModeChart = 0;

        HWND hwndButtonCopy = 0;
        HWND hwndCopyName = 0;
        HWND hwndCopyCount = 0;
        HWND hwndCopyPercent = 0;

        HWND hwndBucketCount = 0;
        HWND hwndValueMin = 0;
        HWND hwndValueMax = 0;
        bool badValueMin = false;
        bool badValueMax = false;
        HWND hwndValueReset = 0;

        bool GraphDirty = true;
        HBITMAP GraphDib = 0;
        HDC GraphDC = 0;

        std::string ColumnName;

        std::vector<double> RawDataValues;
        double ValuesPerBucket = 0;
        double MinDataValue = 0;
        double MaxDataValue = 0;
        //int MinBucketCount=0;
        int MaxBucketCount = 0; //currently only written to, can't be controlled
        std::vector<int> BucketCounts;
        int RawDataCounted = 0; //sum of values in BucketCounts

        void RefreshWindowStuff()
        {
            GraphDirty = true;
            if (hwndGraph)
                InvalidateRect(hwndGraph, nullptr, true);
            if (hwndChart)
            {
                ListView_SetItemCount(hwndChart, BucketCounts.size());
                InvalidateRect(hwndChart, nullptr, true);
            }
        }

        void ResetValueRange()
        {
            badValueMin = false;
            badValueMax = false;
            MinDataValue = 0;
            MaxDataValue = 1;

            if (!RawDataValues.empty())
            {
                //recompute data values
                MinDataValue = std::numeric_limits<decltype(MinDataValue)>::max();
                MaxDataValue = std::numeric_limits<decltype(MaxDataValue)>::min();
                for (const double &d : RawDataValues)
                {
                    if (d < MinDataValue)
                        MinDataValue = d;
                    if (d > MaxDataValue)
                        MaxDataValue = d;
                }

                if (MaxDataValue == MinDataValue)
                    MaxDataValue = MinDataValue + 1;
            }

            double minVal = MinDataValue;
            double maxVal = MaxDataValue;

            if (hwndValueMin)
            {
                std::stringstream ss;
                ss << minVal;
                SetWindowText(hwndValueMin, ss.str().c_str());
            }

            if (hwndValueMax)
            {
                std::stringstream ss;
                ss << maxVal;
                SetWindowText(hwndValueMax, ss.str().c_str());
            }
        }

        void RecomputeBuckets()
        {
            BucketCounts.clear();

            //recompute buckets
            int numberOfBuckets = 0;
            if (hwndBucketCount)
            {
                char temp[100] = { 0 };
                if (GetWindowText(hwndBucketCount, temp, 100))
                    numberOfBuckets = strtol(temp, nullptr, 10);
            }
            else
                numberOfBuckets = 50;

            if (numberOfBuckets <= 0 || numberOfBuckets > 1000)
            {
                RefreshWindowStuff();
                return;
            }

            ValuesPerBucket = (MaxDataValue - MinDataValue) / numberOfBuckets;
            RawDataCounted = 0;
            BucketCounts.resize(numberOfBuckets);

            for (const double &d : RawDataValues)
            {
                int bucket = (int)((d - MinDataValue) / ValuesPerBucket);
                if (bucket < 0)
                    continue;
                else if (bucket >= BucketCounts.size())
                {
                    if (d <= MaxDataValue)
                        bucket = (int)BucketCounts.size() - 1;
                    else
                        continue;
                }

                ++BucketCounts[bucket];
                ++RawDataCounted;
            }

            MaxBucketCount = std::numeric_limits<decltype(MaxBucketCount)>::min();
            for (int &i : BucketCounts)
            {
                if (i > MaxBucketCount)
                    MaxBucketCount = i;
            }

            RefreshWindowStuff();
        }

        std::string GetChartString(int row, int col)
        {
            std::stringstream ss;

            if (col == 0)
            {
                double startRange = MinDataValue + row * ValuesPerBucket;
                ss << "[" << std::fixed << std::setprecision(6) << startRange << ", ";

                if (row == BucketCounts.size() - 1)
                {
                    ss << std::fixed << std::setprecision(6) << (MaxDataValue) << "]";
                }
                else
                {
                    ss << std::fixed << std::setprecision(6) << (startRange + ValuesPerBucket) << ")";
                }
            }
            else if (col == 1)
            {
                ss << BucketCounts[row];
            }
            else
            {
                if (RawDataCounted > 0)
                    ss << std::fixed << std::setprecision(2) << 100.0*BucketCounts[row] / RawDataCounted;
            }

            return ss.str();
        }

        void ShowChart()
        {
            Button_SetCheck(hwndModeGraph, false);
            Button_SetCheck(hwndModeChart, true);

            ShowWindow(hwndChart, SW_SHOW);
            ShowWindow(hwndGraph, SW_HIDE);

            ShowWindow(hwndButtonCopy, SW_SHOW);
            ShowWindow(hwndCopyName, SW_SHOW);
            ShowWindow(hwndCopyCount, SW_SHOW);
            ShowWindow(hwndCopyPercent, SW_SHOW);

            ListView_SetItemCount(hwndChart, BucketCounts.size());
        }

        void ShowGraph()
        {
            Button_SetCheck(hwndModeGraph, true);
            Button_SetCheck(hwndModeChart, false);

            ShowWindow(hwndChart, SW_HIDE);
            ShowWindow(hwndGraph, SW_SHOW);

            ShowWindow(hwndButtonCopy, SW_HIDE);
            ShowWindow(hwndCopyName, SW_HIDE);
            ShowWindow(hwndCopyCount, SW_HIDE);
            ShowWindow(hwndCopyPercent, SW_HIDE);
        }

        void RenderGraph(const RECT &destRect, HDC &dc)
        {
            //set up dib
            if (GraphDib && !GraphDirty)
                return;

            if (GraphDib)
                DeleteObject(GraphDib);
            GraphDib = 0;

            if (destRect.right - destRect.left == 0 || destRect.bottom - destRect.top == 0)
                return;

            const int wid = destRect.right - destRect.left;
            const int hei = destRect.bottom - destRect.top;

            BITMAPINFO bmi = { 0 };
            BITMAPINFOHEADER &bmih = bmi.bmiHeader;
            bmih.biSize = sizeof(BITMAPINFOHEADER);
            bmih.biWidth = wid;
            bmih.biHeight = hei;
            bmih.biPlanes = 1;
            bmih.biBitCount = 32;
            bmih.biCompression = BI_RGB;

            uint32_t *bits = nullptr;
            GraphDib = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, (void**)&bits, 0, 0);
            if (!bits || !GraphDib)
            {
                if (GraphDib)
                    DeleteObject(GraphDib);
                GraphDib = 0;
                return;
            }

            //dc for graph
            if (!GraphDC)
                GraphDC = CreateCompatibleDC(dc);

            SelectObject(GraphDC, GraphDib);
            SelectObject(GraphDC, graphFont);

            //draw graphs
            memset(bits, 0xff, wid*hei * 4);

            const int leftSpace = 50;
            const int rightSpace = 14;
            const int topSpace = 10;
            const int botSpace = 33;
            if (!BucketCounts.empty() && hei > topSpace + botSpace && wid > leftSpace + rightSpace)
            {
                const double barWid = (double)(wid - leftSpace - rightSpace) / (double)(BucketCounts.size() * 2);
                int graphYSpace = hei - topSpace - botSpace;
                if (barWid > 0)
                {
                    for (int b = 0; b < (int)BucketCounts.size(); ++b)
                    {
                        double barX = leftSpace + b * 2 * barWid;
                        int barHeight = (int)(graphYSpace*(double)BucketCounts[b] / (double)MaxBucketCount);
                        if (barHeight <= 0)
                            barHeight = 1;

                        int xStart = (int)barX;
                        int xEnd = (int)(xStart + barWid);
                        if (xEnd - xStart < 1)
                            ++xEnd;

                        int yStart = botSpace;
                        int yEnd = yStart + barHeight;

                        for (int y = yStart; y < yEnd; ++y)
                        {
                            uint32_t mod1 = 60 - 60 * (y - yStart) / graphYSpace;
                            uint32_t mod2 = 60 * (y - yStart) / graphYSpace;
                            uint32_t color = 0xff000000;
                            color |= 0 | (mod1 << 8) | (mod2 << 16);

                            for (int x = xStart; x < xEnd; ++x)
                            {
                                bits[y*wid + x] = color;
                            }
                        }

                        //bottom tick marks and labels for first, last, and middle bucket
                        if (b == 0 || b == BucketCounts.size() / 2 || b == BucketCounts.size() - 1)
                        {
                            bits[(yStart - 3)*wid + xStart + (int)barWid / 2] = 0xff000000;
                            bits[(yStart - 4)*wid + xStart + (int)barWid / 2] = 0xff000000;
                            bits[(yStart - 5)*wid + xStart + (int)barWid / 2] = 0xff000000;

                            int dtY = hei - yStart + 5;
                            RECT dtRect;
                            dtRect.top = dtY;
                            dtRect.bottom = hei;
                            dtRect.left = xStart + (int)barWid / 2 - 100;
                            dtRect.right = xStart + (int)barWid / 2 + 100;

                            std::stringstream ss;
                            ss << MinDataValue + b * ValuesPerBucket << "\r\n" << MinDataValue + ((b + 1)*ValuesPerBucket);
                            std::string s = ss.str();

                            DrawText(GraphDC, s.c_str(), (int)s.size(), &dtRect, DT_CENTER | DT_NOPREFIX);
                        }
                    }

                    //left ticks and labels for min, middle, and max
                    for (int i = 0; i < 3; ++i)
                    {
                        int ySpot = botSpace + (int)(i / 2.0*graphYSpace);

                        bits[ySpot*wid + leftSpace - 5] = 0xff000000;
                        bits[ySpot*wid + leftSpace - 4] = 0xff000000;
                        bits[ySpot*wid + leftSpace - 3] = 0xff000000;

                        int dtY = hei - ySpot - 8;
                        RECT dtRect;
                        dtRect.top = dtY;
                        dtRect.bottom = dtY + 18;
                        dtRect.left = 0;
                        dtRect.right = leftSpace - 7;

                        std::stringstream ss;
                        ss << i / 2.0*MaxBucketCount;
                        std::string s = ss.str();

                        DrawText(GraphDC, s.c_str(), (int)s.size(), &dtRect, DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
                    }
                }
            }

            //
            GraphDirty = false;
        }
    };

    std::list<std::shared_ptr<HistogramChart>> AllInstances;

    LRESULT CALLBACK HistogramChartWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        HistogramChart *hc = (HistogramChart*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
            hc = (HistogramChart*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)hc);

            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            //mode
            hc->hwndModeGraph = CreateWindow(WC_BUTTON, "Graph", WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON, 5, 3, 50, 22, hwnd, 0, hInstance, 0);
            hc->hwndModeChart = CreateWindow(WC_BUTTON, "Chart", WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON, 60, 3, 50, 22, hwnd, 0, hInstance, 0);

            //bucket and value controls
            CreateWindow(WC_STATIC, "Buckets:", WS_VISIBLE | WS_CHILD, 130, 6, 100, 23, hwnd, 0, hInstance, 0);
            hc->hwndBucketCount = CreateWindow(WC_EDIT, "50", WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 180, 4, 40, 19, hwnd, 0, hInstance, 0);

            CreateWindow(WC_STATIC, "Value Range:", WS_VISIBLE | WS_CHILD, 250, 6, 70, 23, hwnd, 0, hInstance, 0);
            hc->hwndValueMin = CreateWindow(WC_EDIT, "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 322, 4, 50, 19, hwnd, 0, hInstance, 0);
            SendMessage(hc->hwndValueMin, EM_SETLIMITTEXT, 40, 0);
            CreateWindow(WC_STATIC, "-", WS_VISIBLE | WS_CHILD, 373, 6, 100, 23, hwnd, 0, hInstance, 0);
            hc->hwndValueMax = CreateWindow(WC_EDIT, "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 379, 4, 50, 19, hwnd, 0, hInstance, 0);
            SendMessage(hc->hwndValueMax, EM_SETLIMITTEXT, 40, 0);
            hc->ResetValueRange(); //just to get the correct SetWindowText calls to these

            hc->hwndValueReset = CreateWindow(WC_BUTTON, "Reset", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 434, 3, 50, 21, hwnd, 0, hInstance, 0);

            //copy
            hc->hwndButtonCopy = CreateWindow(WC_BUTTON, "Copy To Clipboard", WS_CHILD | BS_PUSHBUTTON, 5, 32, 140, 25, hwnd, 0, hInstance, 0);

            hc->hwndCopyName = CreateWindow(WC_BUTTON, "Name", WS_CHILD | BS_CHECKBOX, 152, 34, 60, 22, hwnd, 0, hInstance, 0);
            Button_SetCheck(hc->hwndCopyName, true);
            hc->hwndCopyCount = CreateWindow(WC_BUTTON, "Count", WS_CHILD | BS_CHECKBOX, 212, 34, 60, 22, hwnd, 0, hInstance, 0);
            Button_SetCheck(hc->hwndCopyCount, true);
            hc->hwndCopyPercent = CreateWindow(WC_BUTTON, "Percent", WS_CHILD | BS_CHECKBOX, 272, 34, 70, 22, hwnd, 0, hInstance, 0);
            Button_SetCheck(hc->hwndCopyPercent, true);

            //chart
            hc->hwndChart = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, "", WS_CHILD | LVS_REPORT | LVS_NOSORTHEADER | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0, 30, clientRect.right, clientRect.bottom - clientRect.top - 30, hwnd, 0, hInstance, 0);
            ListView_SetExtendedListViewStyle(hc->hwndChart, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_BORDERSELECT);

            LVCOLUMN lvc = { 0 };
            lvc.fmt = LVCFMT_LEFT;
            lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT;
            lvc.cx = 200;
            lvc.pszText = (char*)"Value Range";
            ListView_InsertColumn(hc->hwndChart, 0, &lvc);
            lvc.pszText = (char*)"Count";
            lvc.cx = 100;
            ListView_InsertColumn(hc->hwndChart, 1, &lvc);
            lvc.pszText = (char*)"Percent";
            lvc.cx = 80;
            ListView_InsertColumn(hc->hwndChart, 2, &lvc);

            //graph
            hc->hwndGraph = CreateWindowEx(WS_EX_CLIENTEDGE, "LogCheetahHistogramChartGraph", "", WS_CHILD, 0, 30, clientRect.right, clientRect.bottom - clientRect.top - 30, hwnd, 0, hInstance, hc);

            //
            FixChildFonts(hwnd);

            return 0;
        }
        case WM_SIZE:
        {
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);

            SetWindowPos(hc->hwndChart, 0, 0, 60, clientRect.right, clientRect.bottom - clientRect.top - 60, 0);
            SetWindowPos(hc->hwndGraph, 0, 0, 30, clientRect.right, clientRect.bottom - clientRect.top - 30, 0);
        }
        break;
        case WM_COMMAND:
        {
            if ((HWND)lParam == hc->hwndModeChart)
                hc->ShowChart();
            else if ((HWND)lParam == hc->hwndModeGraph)
                hc->ShowGraph();
            else if ((HWND)lParam == hc->hwndBucketCount)
            {
                if (HIWORD(wParam) == EN_CHANGE)
                {
                    char temp[100] = { 0 };
                    if (GetWindowText(hc->hwndBucketCount, temp, 100))
                    {
                        int numberOfBuckets = strtol(temp, nullptr, 10);
                        if (numberOfBuckets < 0)
                            SetWindowText(hc->hwndBucketCount, "0");
                        if (numberOfBuckets > 1000)
                            SetWindowText(hc->hwndBucketCount, "1000");
                    }

                    hc->RecomputeBuckets();
                }
            }
            else if ((HWND)lParam == hc->hwndValueMin || (HWND)lParam == hc->hwndValueMax)
            {
                if (HIWORD(wParam) == EN_CHANGE)
                {
                    bool changed = false;

                    double minVal = 0.0, maxVal = 0.0;

                    {
                        char temp[100] = { 0 };
                        if (GetWindowText(hc->hwndValueMin, temp, 100))
                        {
                            char *temp2 = nullptr;
                            double val = strtod(temp, &temp2);
                            if (temp2 && temp2 != temp)
                            {
                                minVal = val;
                                changed = true;
                                hc->badValueMin = false;
                            }
                            else //bad value
                            {
                                hc->badValueMin = true;
                            }
                        }
                    }

                    {
                        char temp[100] = { 0 };
                        if (GetWindowText(hc->hwndValueMax, temp, 100))
                        {
                            char *temp2 = nullptr;
                            double val = strtod(temp, &temp2);
                            if (temp2 && temp2 != temp)
                            {
                                maxVal = val;
                                changed = true;
                                hc->badValueMax = false;
                            }
                            else //bad value
                            {
                                hc->badValueMax = true;
                            }
                        }
                    }

                    if (maxVal <= minVal)
                    {
                        hc->badValueMin = true;
                        hc->badValueMax = true;
                    }

                    if (changed && !hc->badValueMin && !hc->badValueMax)
                    {
                        hc->MinDataValue = minVal;
                        hc->MaxDataValue = maxVal;
                        hc->RecomputeBuckets();
                    }

                    InvalidateRect(hc->hwndValueMin, nullptr, true);
                    InvalidateRect(hc->hwndValueMax, nullptr, true);
                }
            }
            else if ((HWND)lParam == hc->hwndValueReset)
            {
                hc->ResetValueRange();
                hc->RecomputeBuckets();
            }
            else if ((HWND)lParam == hc->hwndButtonCopy)
            {
                bool copyName = Button_GetCheck(hc->hwndCopyName) == BST_CHECKED;
                bool copyCount = Button_GetCheck(hc->hwndCopyCount) == BST_CHECKED;
                bool copyPercent = Button_GetCheck(hc->hwndCopyPercent) == BST_CHECKED;

                std::vector<std::string> headerNames;
                if (copyName)
                    headerNames.push_back("Value Range");
                if (copyCount)
                    headerNames.push_back("Count");
                if (copyPercent)
                    headerNames.push_back("Percent");

                std::stringstream ss;
                ss << StringJoin('|', headerNames.begin(), headerNames.end()) << "\r\n";

                std::vector<std::string> vals;
                for (int r = 0; r < (int)hc->BucketCounts.size(); ++r)
                {
                    vals.clear();
                    for (int c = 0; c < (int)headerNames.size(); ++c)
                        vals.emplace_back(hc->GetChartString(r, c));

                    ss << StringJoin('|', vals.begin(), vals.end()) << "\r\n";
                }

                std::string str = ss.str();
                CopyTextToClipboard(str);
            }
            else if (HWND(lParam) == hc->hwndCopyName || HWND(lParam) == hc->hwndCopyCount || HWND(lParam) == hc->hwndCopyPercent)
            {
                Button_SetCheck(HWND(lParam), !Button_GetCheck(HWND(lParam)));

                bool copyName = Button_GetCheck(hc->hwndCopyName) == BST_CHECKED;
                bool copyCount = Button_GetCheck(hc->hwndCopyCount) == BST_CHECKED;
                bool copyPercent = Button_GetCheck(hc->hwndCopyPercent) == BST_CHECKED;

                EnableWindow(hc->hwndButtonCopy, copyName || copyCount || copyPercent);
            }
        }
        break;
        case WM_NOTIFY:
        {
            NMHDR *n = (NMHDR*)lParam;
            if (n->hwndFrom == hc->hwndChart)
            {
                switch (n->code)
                {
                case LVN_GETDISPINFO:
                {
                    NMLVDISPINFO *di = (NMLVDISPINFO*)lParam;
                    int row = di->item.iItem;
                    int col = di->item.iSubItem;

                    TempGetItemString.clear();
                    if (row >= 0 && row < hc->BucketCounts.size() && col >= 0 && col < 3)
                        TempGetItemString = hc->GetChartString(row, col);

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
        case WM_CTLCOLOREDIT:
        {
            if ((HWND)lParam == hc->hwndValueMin)
            {
                HBRUSH hbr = (HBRUSH)DefWindowProc(hwnd, uMsg, wParam, lParam);
                if (hc->badValueMin)
                    SetBkColor((HDC)wParam, RGB(255, 255, 0));
                else
                    SetBkColor((HDC)wParam, RGB(255, 255, 255));
                return (LRESULT)hbr;
            }
            else if ((HWND)lParam == hc->hwndValueMax)
            {
                HBRUSH hbr = (HBRUSH)DefWindowProc(hwnd, uMsg, wParam, lParam);
                if (hc->badValueMax)
                    SetBkColor((HDC)wParam, RGB(255, 255, 0));
                else
                    SetBkColor((HDC)wParam, RGB(255, 255, 255));
                return (LRESULT)hbr;
            }
        }
        break;
        case WM_DESTROY: case WM_QUIT: case WM_CLOSE:
            AllInstances.remove_if([&](std::shared_ptr<HistogramChart> p) {return p.get() == hc; });
            break;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    LRESULT CALLBACK HistogramChartGraphWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        HistogramChart *hc = (HistogramChart*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
            hc = (HistogramChart*)cs->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)hc);
        }
        break;
        case WM_SIZE:
        {
            hc->GraphDirty = true;
        }
        break;
        case WM_PAINT:
        {
            bool didPaint = false;

            PAINTSTRUCT ps = { 0 };
            HDC hDC = BeginPaint(hwnd, &ps);
            if (hDC)
            {
                if (hc->GraphDirty)
                    hc->RenderGraph(ps.rcPaint, ps.hdc);

                if (hc->GraphDib && hc->GraphDC)
                {
                    BitBlt(ps.hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, hc->GraphDC, 0, 0, SRCCOPY);
                    didPaint = true;
                }
            }

            EndPaint(hwnd, &ps);

            if (didPaint)
                return 0;
        }
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
        wc.lpfnWndProc = (WNDPROC)HistogramChartWindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_CREATEDIBSECTION);
        wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "LogCheetahHistogramChart";

        if (!RegisterClass(&wc))
            return;

        wc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW;
        wc.lpfnWndProc = (WNDPROC)HistogramChartGraphWindowProc;
        wc.lpszClassName = "LogCheetahHistogramChartGraph";
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);

        if (!RegisterClass(&wc))
            return;

        didRegistorWindowClass = true;
    }
}

std::shared_ptr<HistogramChart> ComputeHistogramChart(int dataColumn, const std::vector<uint32_t> &rowsToUse)
{
    AllInstances.emplace_back(std::make_shared<HistogramChart>());
    std::shared_ptr<HistogramChart> hc = AllInstances.back();

    GuiStatusManager::ShowBusyDialogAndRunMonitor("Parsing raw values", false, [&](GuiStatusMonitor &monitor)
    {
        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(rowsToUse.size(), "kiloline", 1000);

        hc->ColumnName = globalLogs.Columns[dataColumn].UniqueName;

        for (int row = 0; row < (int)rowsToUse.size(); ++row)
        {
            if (monitor.IsCancelling())
            {
                hc = nullptr;
                break;
            }

            LogEntry &le = globalLogs.Lines[rowsToUse[row]];
            const std::string val = le.GetColumnNumberValue((uint16_t)dataColumn).str();

            const char * const valBegin = val.c_str();
            const char *valEnd = val.c_str();
            double dval = strtod(valBegin, (char**)&valEnd);
            if (valEnd == valBegin)
                continue;

            hc->RawDataValues.emplace_back(dval);

            monitor.AddProgress(1);
        }

        if (hc)
        {
            hc->ResetValueRange();
            hc->RecomputeBuckets();
        }

        monitor.Complete();
    });
    return hc;
}

void ShowHistogramChart(int dataColumn, const std::vector<uint32_t> &rowsToUse)
{
    SetupFontIfNeeded();

    std::shared_ptr<HistogramChart> hc = ComputeHistogramChart(dataColumn, rowsToUse);

    if (hc)
    {
        RegisterWindowClass();
        std::stringstream windowText;
        windowText << hc->ColumnName << " - Histogram - " << hc->RawDataValues.size() << " Values - Range [" << hc->MinDataValue << ", " << hc->MaxDataValue << "]";
        hc->hwndWindow = CreateWindow("LogCheetahHistogramChart", windowText.str().c_str(), WS_OVERLAPPED | WS_SYSMENU | WS_SIZEBOX | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 800, 500, 0, (HMENU)0, hInstance, hc.get()); //this call has to happen from the main thread

        hc->ShowGraph();
    }
}
