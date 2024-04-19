// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "MainLogView.h"

#include <Windowsx.h>
#include <CommCtrl.h>

#include "SharedGlobals.h"
#include "WindowsDragDrop.h"
#include "Win32Helpers.h"
#include "DialogFrequencyChart.h"
#include "DialogHistogramChart.h"
#include "DialogBlocklistedColumnsEditor.h"
#include "DialogSaveLocal.h"
#include "DialogQosVisualizer.h"
#include "LogFormatter.h"
#include "Preferences.h"
#include "GuiStatusMonitor.h"
#include "resource.h"
#include "GenericTextLogParseRouter.h"
#include "ObtainParseCoordinator.h"
#include "DebugWindow.h"
#include "Globals.h"
#include "WinMain.h"

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

LRESULT CALLBACK DetailedViewSplitterProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MainLogViewWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

namespace
{
    const DWORD CONTEXTMENU_COPYCELL = 12001;
    const DWORD CONTEXTMENU_COPYRAW = 12002;
    const DWORD CONTEXTMENU_COPYFIELDS_ALL = 12003;
    const DWORD CONTEXTMENU_COPYFIELDS_FILTERED = 12004;
    const DWORD CONTEXTMENU_PASTE = 12005;
    const DWORD CONTEXTMENU_FILTERNONEMPTYCOLUMNS = 12006;
    const DWORD CONTEXTMENU_FILTERNONEMPTYNONZEROCOLUMNS = 12007;
    const DWORD CONTEXTMENU_FILTERMATCHINGROWS = 12008;
    const DWORD CONTEXTMENU_FILTERMATCHINGROWS_PREFIX = 12009;
    const DWORD CONTEXTMENU_FILTERMATCHINGROWS_NOTPREFIX = 12010;
    const DWORD CONTEXTMENU_FILTERMATCHINGROWS_NOT = 12011;
    const DWORD CONTEXTMENU_VIEWMATCHINGROWS = 12012;
    const DWORD CONTEXTMENU_VIEWMATCHINGROWS_PREFIX = 12013;
    const DWORD CONTEXTMENU_VIEWMATCHINGROWS_NOTPREFIX = 12014;
    const DWORD CONTEXTMENU_VIEWMATCHINGROWS_NOT = 12015;
    const DWORD CONTEXTMENU_HIGHLIGHTROWS = 12016;
    const DWORD CONTEXTMENU_UNHIGHLIGHTROWS = 12017;

    const DWORD CONTEXTMENU_HIDECOLUMN = 12101;
    const DWORD CONTEXTMENU_SORTCOLUMNS_DEFAULT = 12102;
    const DWORD CONTEXTMENU_SORTCOLUMNS_ALPHA = 12103;
    const DWORD CONTEXTMENU_SORTROWS_ASCENDING = 12104;
    const DWORD CONTEXTMENU_SORTROWS_DESCENDING = 12105;
    const DWORD CONTEXTMENU_MULTICOLUMNCHOICE_ADD = 12106;
    const DWORD CONTEXTMENU_MULTICOLUMNCHOICE_CLEAR = 12107;
    const DWORD CONTEXTMENU_SHOWFREQUENCY = 12108;
    const DWORD CONTEXTMENU_SHOWHISTOGRAM = 12109;

    const DWORD CONTEXTMENU_DNSLOOKUP = 12111;

    const std::vector<std::string> IGNORABLE_STRINGS { "0", "\\0", "00000000-0000-0000-0000-000000000000" };

    UINT_PTR CustomEditId_Normal = 0;
    UINT_PTR CustomEditId_ReadOnly = 1;

    enum class DetailViewFormat
    {
        Default,
        Raw,
        Fields,
        JsonOrig,
        JsonSynth
    };

    size_t nextViewNumber = 0;

    bool didRegistorWindowClass = false;
    void RegisterLogViewWindowClasses()
    {
        if (didRegistorWindowClass)
            return;

        WNDCLASS wc;
        wc.style = 0;
        wc.lpfnWndProc = (WNDPROC)DetailedViewSplitterProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = hInstance;
        wc.hIcon = 0;
        wc.hCursor = LoadCursor(NULL, IDC_SIZENS);
        wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = "TLHDetailedViewSplitter";

        if (!RegisterClass(&wc))
            return;

        didRegistorWindowClass = true;
    }

    class MainLogView
    {
    public:
        size_t viewNumber = 0;

        HWND hwndWindow = 0;
        HWND hwndLogs = 0;
        HWND hwndSaveLocal = 0;
        HWND hwndDuplicateView = 0;
        HWND hwndLogsHeader = 0;
        HWND hwndLogsColumnsTooltip = 0;
        HWND hwndColumnList = 0;
        HWND hwndColumnListAll = 0;
        HWND hwndColumnListNone = 0;
        HWND hwndColumnListEditBlockList = 0;
        HWND hwndFilterListColumns = 0;
        HWND hwndFilterListValue = 0;
        HWND hwndFilterListCase = 0;
        HWND hwndFilterListNot = 0;
        HWND hwndFilterListSubstring = 0;
        HWND hwndFilterListAdd = 0;
        HWND hwndFilterListRemove = 0;
        HWND hwndFilterList = 0;
        HWND hwndExpandLogDetailView = 0;
        HWND hwndLogDetailView = 0;
        HWND hwndLogDetailViewSplitter = 0;
        HWND hwndLogDetailViewFormatRaw = 0;
        HWND hwndLogDetailViewFormatFields = 0;
        HWND hwndLogDetailViewFormatJsonOrig = 0;
        HWND hwndLogDetailViewFormatJsonSynth = 0;
        HWND hwndOpenQosVisualizer = 0;
        HWND hwndSearchString = 0;
        HWND hwndSearchNext = 0;
        HWND hwndSearchPrev = 0;

        std::vector<uint32_t> columnVisibilityMap;
        std::vector<uint32_t> rowVisibilityMap;
        std::vector<LogFilterEntry> rowFilters;

        bool logHeaderMouseTrackingActive = false;
        TOOLINFO logHeaderTooltipInfo = { 0 };
        int logHeaderTooltipLastX = -1;
        int logHeaderTooltipLastY = -1;
        bool logHeaderTooltipManuallyTruncateText = false;
        std::string logHeaderTooltipTruncatedString;

        bool detailViewOpened = false;
        int detailViewHeight = 0;
        bool detailViewIsResizing = false;
        DetailViewFormat detailViewFormat = DetailViewFormat::Default;

        LogFilterEntry showApplicableRowsFilter;
        std::string cellValueToCopy;

        int columnContextDataCol = 0;
        std::vector<uint32_t> columnOperationStack;

        std::map<std::string, int> previousColumnWidths;

        std::string searchString;

        MainLogView()
        {
            viewNumber = nextViewNumber++;
        }

        ~MainLogView()
        {
            if (hwndWindow)
                DestroyWindow(hwndWindow);
        }

        void ResizeChildren()
        {
            RECT mainClientRect;
            GetClientRect(hwndWindow, &mainClientRect);

            SetWindowPos(hwndLogs, 0, 200, 25, mainClientRect.right - 200, mainClientRect.bottom - mainClientRect.top - detailViewHeight - 30 + (detailViewOpened ? 0 : 5), 0);

            SetWindowPos(hwndColumnList, 0, 10, 400, 180, mainClientRect.bottom - 400 - 35, 0);
            SetWindowPos(hwndColumnListAll, 0, 15, mainClientRect.bottom - 35, 78, 24, 0);
            SetWindowPos(hwndColumnListNone, 0, 105, mainClientRect.bottom - 35, 78, 24, 0);

            SetWindowPos(hwndColumnListEditBlockList, 0, 93, mainClientRect.bottom - 30, 12, 15, 0);

            SetWindowPos(hwndLogDetailViewSplitter, 0, 200, mainClientRect.bottom - detailViewHeight - 6, mainClientRect.right - 200, 5, 0);

            SetWindowPos(hwndLogDetailView, 0, 200, mainClientRect.bottom - detailViewHeight, mainClientRect.right - 200, detailViewHeight - 18, 0);
            SetWindowPos(hwndLogDetailViewFormatRaw, 0, 200, mainClientRect.bottom - 18, 0, 0, SWP_NOSIZE);
            SetWindowPos(hwndLogDetailViewFormatFields, 0, 230, mainClientRect.bottom - 18, 0, 0, SWP_NOSIZE);
            SetWindowPos(hwndLogDetailViewFormatJsonOrig, 0, 268, mainClientRect.bottom - 18, 0, 0, SWP_NOSIZE);
            SetWindowPos(hwndLogDetailViewFormatJsonSynth, 0, 300, mainClientRect.bottom - 18, 0, 0, SWP_NOSIZE);
        }

        void ResetColumnSortIndicator()
        {
            HWND hwndLogsHeaderSort = ListView_GetHeader(hwndLogs);
            int headerItemCount = Header_GetItemCount(hwndLogsHeaderSort);
            if (headerItemCount > 0)
            {
                for (int i = 0; i < headerItemCount; ++i)
                {
                    if (i < columnVisibilityMap.size())
                    {
                        HDITEM item = { 0 };
                        item.mask = HDI_FORMAT;
                        if (Header_GetItem(hwndLogsHeaderSort, i, &item))
                        {
                            item.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
                            if (columnVisibilityMap[i] == globalLogs.SortColumn)
                                item.fmt |= (globalLogs.SortAscending ? HDF_SORTDOWN : HDF_SORTUP);
                            Header_SetItem(hwndLogsHeaderSort, i, &item);
                        }
                    }
                }
            }
        }

        void RebuildColumnChooser()
        {
            for (int i = 0; i < (int)globalLogs.Columns.size(); ++i)
            {
                std::string lbString = Win32ListBoxGetText(hwndColumnList, i);
                int indToFind = (int)(std::find_if(globalLogs.Columns.begin(), globalLogs.Columns.end(), [&](const ColumnInformation &ci) {return ci.UniqueName == lbString; }) - globalLogs.Columns.begin());
                if (std::find(columnVisibilityMap.begin(), columnVisibilityMap.end(), (uint32_t)indToFind) == columnVisibilityMap.end())
                    ListBox_SetSel(hwndColumnList, FALSE, i);
                else
                    ListBox_SetSel(hwndColumnList, TRUE, i);
            }
        }

        void RecreateColumns()
        {
            SendMessage(hwndLogs, WM_SETREDRAW, FALSE, 0);

            //remember previous column widths
            {
                int col = 0;
                std::array<char, 1024> tempBuffer;
                LVCOLUMN lvc = { 0 };
                lvc.mask = LVCF_WIDTH | LVCF_TEXT;
                lvc.pszText = tempBuffer.data();
                lvc.cchTextMax = (int)tempBuffer.size() - 1;
                while (ListView_GetColumn(hwndLogs, col, &lvc))
                {
                    previousColumnWidths[lvc.pszText] = lvc.cx;
                    ++col;
                }
            }

            //recreate the columns to match the new set
            LVCOLUMN lvc = { 0 };
            std::array<char, 2048> colNameBuff = { 0 };
            lvc.fmt = LVCFMT_LEFT;
            lvc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_FMT;

            int dataCol = 0;
            for (; dataCol < columnVisibilityMap.size(); ++dataCol)
            {
                bool needCreate = true;
                lvc.pszText = colNameBuff.data();
                lvc.cchTextMax = (int)colNameBuff.size();
                if (ListView_GetColumn(hwndLogs, dataCol, &lvc))
                    needCreate = false;

                const std::string &colName = globalLogs.Columns[columnVisibilityMap[dataCol]].GetDisplayName();
                int colWidth = 100;
                auto prevWidthIter = previousColumnWidths.find(colName);
                if (prevWidthIter != previousColumnWidths.end())
                    colWidth = prevWidthIter->second;
                else //special case certain known ones to look better by default
                {
                    if (colName == "Level")
                        colWidth = 40;
                    else if (colName == "Date (UTC)" || colName == "Date" || colName == "Sortable Date")
                        colWidth = 140;
                    else if (colName == "EventName")
                        colWidth = 150;
                    else if (colName == "time")
                        colWidth = 170;
                    else if (colName == "name")
                        colWidth = 170;
                    else if (colName == "ver")
                        colWidth = 35;
                }

                lvc.pszText = (char*)colName.c_str();
                lvc.cx = colWidth;

                if (needCreate)
                    ListView_InsertColumn(hwndLogs, dataCol, &lvc);
                else
                    ListView_SetColumn(hwndLogs, dataCol, &lvc);
            }

            while (ListView_DeleteColumn(hwndLogs, dataCol)); //remove extras leftover at the end

            columnOperationStack.clear();

            ResetColumnSortIndicator();
            SendMessage(hwndLogs, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(hwndLogs, nullptr, 0, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

            UpdateLogDetailText();
        }

        int GetDataColForColHeaderPosition(int x)
        {
            POINT pointClient = { 0 };
            pointClient.x = x; //this is the position within the scrollable window, not visible window, so we need to fix that
            pointClient.y = 10;

            RECT sir = { 0 };
            if (ListView_GetSubItemRect(hwndLogs, 0, 0, LVIR_BOUNDS, &sir))
                pointClient.x += sir.left;

            LVHITTESTINFO hti = { 0 };
            hti.pt = pointClient;
            int htiret = ListView_SubItemHitTest(hwndLogs, &hti);
            if (htiret != -1)
            {
                if (hti.iSubItem >= 0 && hti.iSubItem < (int)columnVisibilityMap.size())
                {
                    int dataCol = columnVisibilityMap[hti.iSubItem];
                    return dataCol;
                }
            }

            return -1;
        }

        void SyncFilterChoicesToWindow()
        {
            while (ComboBox_GetCount(hwndFilterListColumns) > 0)
                ComboBox_DeleteString(hwndFilterListColumns, 0);
            ComboBox_AddString(hwndFilterListColumns, "<raw logline>");

            for (auto &c : globalLogs.Columns)
                ComboBox_AddString(hwndFilterListColumns, c.UniqueName.c_str());

            ComboBox_SetCurSel(hwndFilterListColumns, 0);
        }

        void SyncFilterListToWindow()
        {
            while (ListBox_GetCount(hwndFilterList) > 0)
                ListBox_DeleteString(hwndFilterList, 0);

            for (auto &f : rowFilters)
            {
                std::string s = "<raw log>";
                if (f.Column >= 0 && f.Column < globalLogs.Columns.size())
                    s = globalLogs.Columns[f.Column].UniqueName;

                s += " ";
                if (f.Not)
                    s += "!";
                if (f.MatchCase)
                    s += "=";
                else
                    s += "~";
                if (!f.MatchSubstring)
                    s += "=";
                s += " ";

                s += f.Value;

                ListBox_AddString(hwndFilterList, s.c_str());
            }

            EnableWindow(hwndFilterListRemove, false);
        }

        void SyncVisibleRowsToWindow()
        {
            int prevCount = ListView_GetItemCount(hwndLogs);
            ListView_SetItemCount(hwndLogs, rowVisibilityMap.size());

            if (prevCount == (int)rowVisibilityMap.size()) //force redraw
                RedrawWindow(hwndLogs, 0, 0, RDW_INVALIDATE);

            UpdateWindowStatusText();
        }

        void SyncVisibleColumnsToWindow()
        {
            RecreateColumns();

            while (ListBox_GetCount(hwndColumnList) > 0)
                ListBox_DeleteString(hwndColumnList, 0);

            for (int i = 0; i < globalLogs.Columns.size(); ++i)
            {
                int ind = ListBox_AddString(hwndColumnList, globalLogs.Columns[i].UniqueName.c_str());
                if (ind != LB_ERR)
                {
                    if (std::find(columnVisibilityMap.begin(), columnVisibilityMap.end(), (uint32_t)i) != columnVisibilityMap.end())
                        ListBox_SetSel(hwndColumnList, TRUE, ind);
                    else
                        ListBox_SetSel(hwndColumnList, false, ind);
                }
            }
        }

        void SyncEverythingToWindow()
        {
            SyncFilterChoicesToWindow();
            SyncFilterListToWindow();
            SyncVisibleColumnsToWindow();
            SyncVisibleRowsToWindow();
        }

        void ApplyFilterWithStatusDialog(const std::vector<LogFilterEntry> &filters)
        {
            GuiStatusManager::ShowBusyDialogAndRunMonitor("Applying filters", true, [&](GuiStatusMonitor &monitor)
            {
                std::chrono::time_point<std::chrono::high_resolution_clock> timerStart = std::chrono::high_resolution_clock::now();
                rowFilters = filters;

                rowVisibilityMap.clear();

                if (rowFilters.empty())
                {
                    monitor.SetProgressFeatures(1);
                    for (int i = 0; i < (int)globalLogs.Lines.size(); ++i)
                        rowVisibilityMap.push_back(i);

                    rowVisibilityMap.shrink_to_fit();
                }
                else
                {
                    monitor.SetProgressFeatures(globalLogs.Lines.size(), "kiloline", 1000);

                    //break task into chunks and run in parallel
                    std::vector<std::thread> allThreads;
                    std::vector<std::vector<int>> separateRowPools;
                    separateRowPools.resize(cpuCountFilter);
                    for (int cpu = 0; cpu < cpuCountFilter; ++cpu)
                        separateRowPools[cpu].reserve(globalLogs.Lines.size() / 4);
                    for (int cpu = 0; cpu < cpuCountFilter; ++cpu)
                    {
                        allThreads.emplace_back([&](int threadIndex)
                        {
                            size_t chunkSize = globalLogs.Lines.size() / cpuCountFilter;
                            if (!chunkSize)
                                chunkSize = 1;

                            size_t rowStartIndex = chunkSize * threadIndex;
                            size_t rowEndIndex = chunkSize * (threadIndex + 1);
                            if (rowEndIndex > globalLogs.Lines.size() || threadIndex == cpuCountFilter - 1)
                                rowEndIndex = globalLogs.Lines.size();

                            for (size_t row = rowStartIndex; row < rowEndIndex; ++row)
                            {
                                monitor.AddProgress(1);

                                const LogEntry &entry = globalLogs.Lines[row];
                                bool match = DoesLogEntryPassFilters(entry, rowFilters);
                                if (match)
                                    separateRowPools[threadIndex].push_back((int)row);
                            }
                        }, cpu);
                    }

                    for (auto &t : allThreads)
                        t.join();

                    //merge chunks back into one list
                    for (int cpu = 0; cpu < cpuCountFilter; ++cpu)
                        rowVisibilityMap.insert(rowVisibilityMap.end(), separateRowPools[cpu].begin(), separateRowPools[cpu].end());
                }

                std::chrono::time_point<std::chrono::high_resolution_clock> timerEnd = std::chrono::high_resolution_clock::now();
                monitor.AddDebugOutputTime("ChangeFilters", std::chrono::duration_cast<std::chrono::microseconds>(timerEnd - timerStart).count() / 1000.0f);

                monitor.Complete();
            });

            SyncFilterListToWindow();
            SyncVisibleRowsToWindow();
            UpdateLogDetailText();
        }

        void UpdateLogDetailText()
        {
            std::stringstream ss;

            if (detailViewOpened)
            {
                //get the list of rows and columns based on the selection
                std::vector<uint32_t> selectedRows = GetSelectedRows();
                bool isTruncated = false;
                if (selectedRows.size() > 100)
                {
                    selectedRows.resize(100);
                    isTruncated = true;
                }

                std::set<uint32_t> selectedColumnsSet;
                for (uint32_t rowIndex : selectedRows)
                {
                    LogEntry &le = globalLogs.Lines[rowIndex];
                    for (auto cvi = le.ColumnDataBegin(); cvi != le.ColumnDataEnd(); ++cvi)
                    {
                        if (std::find(columnVisibilityMap.begin(), columnVisibilityMap.end(), cvi->ColumnNumber) != columnVisibilityMap.end())
                            selectedColumnsSet.emplace(cvi->ColumnNumber);
                    }
                }

                std::vector<uint32_t> selectedColumns;
                selectedColumns.insert(selectedColumns.end(), selectedColumnsSet.begin(), selectedColumnsSet.end());
                std::sort(selectedColumns.begin(), selectedColumns.end());
                std::sort(selectedRows.begin(), selectedRows.end());

                //format as desired
                if (detailViewFormat == DetailViewFormat::Raw)
                {
                    if (globalLogs.IsRawRepresentationValid)
                    {
                        for (auto row : selectedRows)
                        {
                            const auto &log = globalLogs.Lines[row];
                            ss << ExternalSubstring<const char>(log.OriginalLogBegin(), log.OriginalLogEnd()).str();
                            ss << "\r\n\r\n";
                        }
                    }
                    else
                    {
                        ss << "(Raw representation is not valid)\r\n";
                    }
                }
                else if (detailViewFormat == DetailViewFormat::Fields)
                {
                    for (auto col : selectedColumns)
                    {
                        ss << globalLogs.Columns[col].UniqueName << ": ";
                        bool first = true;

                        for (auto row : selectedRows)
                        {
                            if (!first)
                                ss << " · ";
                            first = false;

                            const auto &log = globalLogs.Lines[row];
                            for (auto logColIter = log.ColumnDataBegin(); logColIter != log.ColumnDataEnd(); ++logColIter)
                            {
                                if (col == logColIter->ColumnNumber)
                                {
                                    ss << log.GetColumnNumberValue((uint16_t)col).str();
                                    break;
                                }
                            }
                        }

                        if (isTruncated)
                            ss << " · ...";

                        ss << "\r\n\r\n";
                    }
                }
                else if (detailViewFormat == DetailViewFormat::JsonOrig)
                {
                    DetailViewJsonOrig(ss, selectedRows);
                }
                else if (detailViewFormat == DetailViewFormat::JsonSynth)
                {
                    DetailViewJsonSynth(ss, selectedRows);
                }
            }

            SetWindowText(hwndLogDetailView, ss.str().c_str());

            //indicator for which format is currently chosen
            SendMessage(hwndLogDetailViewFormatRaw, WM_SETFONT, detailViewFormat == DetailViewFormat::Raw ? (WPARAM)lessDerpyFontBold : (WPARAM)lessDerpyFont, true);
            SendMessage(hwndLogDetailViewFormatFields, WM_SETFONT, detailViewFormat == DetailViewFormat::Fields? (WPARAM)lessDerpyFontBold : (WPARAM)lessDerpyFont, true);
            SendMessage(hwndLogDetailViewFormatJsonOrig, WM_SETFONT, detailViewFormat == DetailViewFormat::JsonOrig ? (WPARAM)lessDerpyFontBold : (WPARAM)lessDerpyFont, true);
            SendMessage(hwndLogDetailViewFormatJsonSynth, WM_SETFONT, detailViewFormat == DetailViewFormat::JsonSynth ? (WPARAM)lessDerpyFontBold : (WPARAM)lessDerpyFont, true);
        }

        size_t GetLastIndexOfDot(std::string dimensionName)
        {
            size_t lastIndexOfDot = dimensionName.find_last_of('.');

            if (lastIndexOfDot == std::string::npos)
                return 0;

            return lastIndexOfDot + 1;
        }

        void PrettyFormatJsonString(std::stringstream& ss, const char* srcBegin, const char* srcEnd)
        {
            int depth = 0;
            bool inString = false;
            bool inEscape = false;
            for (auto cp = srcBegin; cp != srcEnd; ++cp)
            {
                const char c = *cp;
                if (inString)
                {
                    if (inEscape)
                    {
                        inEscape = false;
                    }
                    else
                    {
                        if (c == '\\')
                            inEscape = true;
                        else if (c == '"')
                            inString = false;
                    }

                    ss << c;
                }
                else
                {
                    if (c == '{' || c == '[')
                    {
                        ss << "\r\n";
                        for (int i = 0; i < depth; ++i)
                            ss << "  ";
                        ss << c;

                        ++depth;
                        ss << "\r\n";
                        for (int i = 0; i < depth; ++i)
                            ss << "  ";
                    }
                    else if (c == '}' || c == ']')
                    {
                        --depth;
                        ss << "\r\n";
                        for (int i = 0; i < depth; ++i)
                            ss << "  ";
                        ss << c;
                    }
                    else if (c == '"')
                    {
                        inString = true;
                        ss << c;
                    }
                    else if (c == ',')
                    {
                        ss << c;

                        ss << "\r\n";
                        for (int i = 0; i < depth; ++i)
                            ss << "  ";
                    }
                    else
                        ss << c;
                }
            }
        }

        // assume standard json and walk the original string
        void DetailViewJsonOrig(std::stringstream &ss, std::vector<uint32_t> selectedRows)
        {
            if (globalLogs.IsRawRepresentationValid)
            {
                for (auto row : selectedRows)
                {
                    const auto& log = globalLogs.Lines[row];
                    PrettyFormatJsonString(ss, log.OriginalLogBegin(), log.OriginalLogEnd());
                    ss << "\r\n\r\n";
                }
            }
            else
            {
                ss << "(Raw representation is not valid)\r\n";
            }
        }

        // construct json from field data
        void DetailViewJsonSynth(std::stringstream &ss, std::vector<uint32_t> selectedRows)
        {
            // arrange the column data into a tree structure
            struct Node
            {
                std::string Name;
                std::string Value; // only used if Children is empty
                std::vector<std::unique_ptr<Node>> Children;
            };

            Node dummyTopNode;

            std::function<Node& (const std::string &name, Node &treeNode, int depth)> locateNodeForName = [&](const std::string &name, Node &treeNode, int depth) -> Node&
            {
                if (name.empty() || depth > 20)
                    return treeNode;

                size_t nextDot = name.find_first_of('.');
                if (nextDot == std::string::npos)
                {
                    for (std::unique_ptr<Node> &n : treeNode.Children)
                    {
                        if (n->Name == name)
                            return *n;
                    }

                    treeNode.Children.emplace_back(std::make_unique<Node>());
                    treeNode.Children.back()->Name = name;
                    return *treeNode.Children.back();
                }
                else
                {
                    std::string branchName = std::string(name.begin(), name.begin() + nextDot);
                    std::string leafName = std::string(name.begin() + nextDot + 1, name.end());

                    for (std::unique_ptr<Node>& n : treeNode.Children)
                    {
                        if (n->Name == branchName)
                        {
                            return locateNodeForName(leafName, *n, depth + 1);
                        }
                    }

                    treeNode.Children.emplace_back(std::make_unique<Node>());
                    treeNode.Children.back()->Name = branchName;
                    return locateNodeForName(leafName, *treeNode.Children.back(), depth + 1);
                }
            };

            for (auto row : selectedRows)
            {
                const auto &log = globalLogs.Lines[row];

                for (const LogEntryColumn *colInfo = log.ColumnDataBegin(); colInfo != log.ColumnDataEnd(); ++colInfo)
                {
                    const std::string& colName = globalLogs.Columns[colInfo->ColumnNumber].UniqueName;
                    Node& node = locateNodeForName(colName, dummyTopNode, 0);
                    node.Value = log.GetColumnNumberValue(colInfo->ColumnNumber).str();
                }
            }

            // synthesize unformatted json - note that everything is treated as a string
            std::stringstream synth;
            std::function<void(Node &treeNode)> walkNodeTree = [&](Node &treeNode)
            {
                if (&treeNode != &dummyTopNode)
                    synth << "\"" << treeNode.Name << "\":";

                if (treeNode.Children.empty())
                {
                    std::string escapedString;
                    escapedString.reserve(treeNode.Value.length());
                    for (char c : treeNode.Value)
                    {
                        if (c == '\\' || c == '"')
                        {
                            escapedString.push_back('\\');
                            escapedString.push_back(c);
                        }
                        else if (c == '\r')
                        {
                            escapedString.push_back('\\');
                            escapedString.push_back('r');
                        }
                        else if (c == '\n')
                        {
                            escapedString.push_back('\\');
                            escapedString.push_back('n');
                        }
                        else if (c == '\t')
                        {
                            escapedString.push_back('\\');
                            escapedString.push_back('t');
                        }
                        else
                        {
                            escapedString.push_back(c);
                        }
                    }

                    synth << "\"" << escapedString << "\"";
                }
                else
                {
                    synth << "{";

                    // reorganize children, such that we walk ones without grandchildren first, to bring sanity to the field ordering
                    std::vector<Node*> childrenToWalk;
                    std::vector<Node*> childrenToDelay;
                    for (std::unique_ptr<Node>& child : treeNode.Children)
                    {
                        if (child->Children.empty())
                            childrenToWalk.emplace_back(&*child);
                        else
                            childrenToDelay.emplace_back(&*child);
                    }

                    childrenToWalk.insert(childrenToWalk.end(), childrenToDelay.begin(), childrenToDelay.end());

                    for (Node *child : childrenToWalk)
                    {
                        walkNodeTree(*child);

                        if (child != childrenToWalk.back())
                            synth << ",";
                    }

                    synth << "}";
                }
            };

            walkNodeTree(dummyTopNode);

            // reformat it to be pretty
            std::string rawSynthJson = synth.str();
            if (!rawSynthJson.empty())
                PrettyFormatJsonString(ss, rawSynthJson.data(), rawSynthJson.data() + rawSynthJson.size());
        }

        std::vector<std::string> GetVisibleColumnNames() const
        {
            std::vector<std::string> visibleColumns;
            for (int i = 0; i < (int)globalLogs.Columns.size(); ++i)
            {
                if (std::find(columnVisibilityMap.begin(), columnVisibilityMap.end(), (uint32_t)i) != columnVisibilityMap.end())
                    visibleColumns.emplace_back(globalLogs.Columns[i].UniqueName);
            }
            return std::move(visibleColumns);
        }

        std::string ExtractVisibleCell(int visibleRow, int visibleColumn) const
        {
            if (visibleRow < 0 || visibleRow >= rowVisibilityMap.size() || visibleColumn < 0 || visibleColumn >= columnVisibilityMap.size())
                return std::string();

            int dataRow = rowVisibilityMap[visibleRow];
            const LogEntry &entry = globalLogs.Lines[dataRow];

            int dataColumn = columnVisibilityMap[visibleColumn];
            return entry.GetColumnNumberValue((uint16_t)dataColumn).str();
        }

        //gets the selected rows, as indexes into globalLogs.Lines
        std::vector<uint32_t> GetSelectedRows()
        {
            std::vector<uint32_t> rows;

            LVITEMINDEX ii = { 0 };
            ii.iItem = -1;
            while (ListView_GetNextItemIndex(hwndLogs, &ii, LVNI_SELECTED))
            {
                if (ii.iItem >= 0 && ii.iItem < rowVisibilityMap.size())
                {
                    uint32_t rowIndex = rowVisibilityMap[ii.iItem];
                    rows.emplace_back(rowIndex);
                }
            }

            return std::move(rows);
        }

        std::string GetWindowStatusText()
        {
            return std::to_string(rowVisibilityMap.size()) + " Lines Visible";
        }

        void UpdateWindowStatusText()
        {
            std::string text = "LogCheetah (LogView #" + std::to_string(viewNumber) + ") - " + GetWindowStatusText();
            SetWindowText(hwndWindow, text.c_str());
        }

        void KeepScrolledToEndIfPreviously(size_t previousEnd)
        {
            if (rowVisibilityMap.empty())
                return;

            bool wasScrolledToEnd = false;
            if (previousEnd < rowVisibilityMap.size())
                wasScrolledToEnd = (0 != ListView_IsItemVisible(hwndLogs, previousEnd));

            if (wasScrolledToEnd)
            {
                ListView_EnsureVisible(hwndLogs, (int)rowVisibilityMap.size() - 1, false);
            }
        }
    };

    //

    std::list<MainLogView> logViews;

    std::string TempGetItemString;

    //

    bool TooltipExistsForDataCol(int dataCol)
    {
        if (dataCol < 0 || dataCol >= globalLogs.Columns.size())
            return false;

        if (globalLogs.Columns[dataCol].Description.empty())
            return false;

        return true;
    }

    std::vector<std::string> GetPreferredColumnNames()
    {
        std::vector<std::string> columnNames;
        for (const auto &c : globalLogs.Columns)
        {
            if (std::find(Preferences::BlockListedDefaultColumns.begin(), Preferences::BlockListedDefaultColumns.end(), c.UniqueName) == Preferences::BlockListedDefaultColumns.end())
                columnNames.emplace_back(c.UniqueName);
        }
        return std::move(columnNames);
    }

    void RedrawAllWindows()
    {
        for (auto &lv : logViews)
        {
            RedrawWindow(lv.hwndWindow, nullptr, 0, RDW_INVALIDATE);
        }
    }

    MainLogView* DuplicateView(MainLogView &src)
    {
        HWND newViewWindow = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, "LogCheetahLogView", "LogCheetah (New Log View)", WS_VISIBLE | WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, (HMENU)0, hInstance, (LPVOID)0);

        for (auto &dest : logViews)
        {
            if (dest.hwndWindow == newViewWindow)
            {
                dest.columnVisibilityMap = src.columnVisibilityMap;

                dest.rowVisibilityMap = src.rowVisibilityMap;
                dest.rowFilters = src.rowFilters;

                SendMessage(newViewWindow, WM_SIZE, 0, 0);

                dest.SyncEverythingToWindow();

                return &dest;
            }
        }

        GlobalDebugOutput("DuplicateView - Could not find destination window to duplicate state to");
        return nullptr;
    }
}

//

void LogViewPreprocessMessage(const MSG &msg)
{
    //special cases that we need to handle that Translate unfortunately obliterates
    for (auto &lv : logViews)
    {
        if (msg.hwnd == lv.hwndLogsHeader)
        {
            if (msg.message == WM_MOUSEMOVE)
            {
                if (!lv.logHeaderMouseTrackingActive)
                {
                    TRACKMOUSEEVENT tme = { 0 };
                    tme.cbSize = sizeof(tme);
                    tme.hwndTrack = lv.hwndLogsHeader;
                    tme.dwFlags = TME_HOVER | TME_LEAVE;
                    tme.dwHoverTime = 25;
                    lv.logHeaderMouseTrackingActive = TrackMouseEvent(&tme) != 0;
                }

                int newX = GET_X_LPARAM(msg.lParam);
                int newY = GET_Y_LPARAM(msg.lParam);

                if (lv.logHeaderTooltipLastX != newX || lv.logHeaderTooltipLastY != newY)
                {
                    lv.logHeaderTooltipLastX = newX;
                    lv.logHeaderTooltipLastY = newY;

                    POINT pt = { newX, newY };
                    ClientToScreen(msg.hwnd, &pt);
                    SendMessage(lv.hwndLogsColumnsTooltip, TTM_TRACKPOSITION, 0, (LPARAM)MAKELONG(pt.x, pt.y));

                    //if too close to the right side of the screen, windows has a bug where it clips it (despite documentation claiming it will won't), so we'll hack around that..
                    lv.logHeaderTooltipManuallyTruncateText = false;
                    RECT screenRect = { 0 };
                    if (GetWindowRect(GetDesktopWindow(), &screenRect))
                    {
                        if (screenRect.right - pt.x < 320)
                        {
                            if (screenRect.right - pt.x < 100)
                            {
                                SendMessage(lv.hwndLogsColumnsTooltip, TTM_SETMAXTIPWIDTH, 0, 100);
                                lv.logHeaderTooltipManuallyTruncateText = true;
                            }
                            if (screenRect.right - pt.x < 150)
                                SendMessage(lv.hwndLogsColumnsTooltip, TTM_SETMAXTIPWIDTH, 0, 200);
                            else
                                SendMessage(lv.hwndLogsColumnsTooltip, TTM_SETMAXTIPWIDTH, 0, 300);

                            lv.logHeaderTooltipInfo.uFlags |= TTF_CENTERTIP;
                        }
                        else
                        {
                            SendMessage(lv.hwndLogsColumnsTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
                            lv.logHeaderTooltipInfo.uFlags &= ~TTF_CENTERTIP;
                        }

                        SendMessage(lv.hwndLogsColumnsTooltip, TTM_SETTOOLINFO, 0, (LPARAM)&lv.logHeaderTooltipInfo);
                    }
                }
            }
            else if (msg.message == WM_MOUSELEAVE)
            {
                lv.logHeaderMouseTrackingActive = false;

                SendMessage(lv.hwndLogsColumnsTooltip, TTM_TRACKACTIVATE, (WPARAM)FALSE, (LPARAM)&lv.logHeaderTooltipInfo);
                SendMessage(lv.hwndLogsColumnsTooltip, TTM_ACTIVATE, (WPARAM)FALSE, 0);
                ShowWindow(lv.hwndLogsColumnsTooltip, SW_HIDE);
            }
            else if (msg.message == WM_MOUSEHOVER)
            {
                lv.logHeaderMouseTrackingActive = false;

                if (TooltipExistsForDataCol(lv.GetDataColForColHeaderPosition(lv.logHeaderTooltipLastX)))
                {
                    POINT pt = { lv.logHeaderTooltipLastX, lv.logHeaderTooltipLastY };

                    //show and position it
                    SendMessage(lv.hwndLogsColumnsTooltip, TTM_TRACKACTIVATE, (WPARAM)TRUE, (LPARAM)&lv.logHeaderTooltipInfo);
                    SendMessage(lv.hwndLogsColumnsTooltip, TTM_ACTIVATE, (WPARAM)TRUE, 0);


                    ClientToScreen(msg.hwnd, &pt);
                    SendMessage(lv.hwndLogsColumnsTooltip, TTM_TRACKPOSITION, 0, (LPARAM)MAKELONG(pt.x, pt.y));

                    if (GetActiveWindow() == lv.hwndWindow)
                        ShowWindow(lv.hwndLogsColumnsTooltip, SW_SHOWNA); //shouldn't need this... but without it it sometimes just doesn't show up until we move...
                }
                else
                {
                    SendMessage(lv.hwndLogsColumnsTooltip, TTM_TRACKACTIVATE, (WPARAM)FALSE, (LPARAM)&lv.logHeaderTooltipInfo);
                    SendMessage(lv.hwndLogsColumnsTooltip, TTM_ACTIVATE, (WPARAM)FALSE, 0);
                    ShowWindow(lv.hwndLogsColumnsTooltip, SW_HIDE);
                }
            }
        }
    }
}

bool LogViewRequiresSpecificWindowTranslated(HWND hwnd)
{
    //our edit controls have to be specifically targetted by translate for accelerators to work on them
    for (auto &lv : logViews)
    {
        if (hwnd == lv.hwndFilterListValue)
            return true;
        else if (hwnd == lv.hwndLogDetailView && lv.detailViewOpened)
            return true;
    }

    return false;
}

bool LogViewInitialize()
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)MainLogViewWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON2), IMAGE_ICON, 0, 0, LR_CREATEDIBSECTION);
    wc.hCursor = LoadCursor((HINSTANCE)0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(1 + COLOR_3DFACE);
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = "LogCheetahLogView";

    if (!RegisterClass(&wc))
        return false;
    return true;
}

void LogViewNotifyDataChanged(size_t beginRow, size_t endRow, size_t beginColumn, size_t endColumn)
{
    for (auto &lv : logViews)
    {
        //update rows
        if (beginRow != endRow)
        {
            size_t prevRowMapSize = lv.rowVisibilityMap.size();

            while (!lv.rowVisibilityMap.empty() && lv.rowVisibilityMap.back() >= beginRow)
                lv.rowVisibilityMap.pop_back();

            for (size_t r = beginRow; r < endRow; ++r)
            {
                if (DoesLogEntryPassFilters(globalLogs.Lines[r], lv.rowFilters))
                    lv.rowVisibilityMap.emplace_back((uint32_t)r);
            }

            lv.SyncVisibleRowsToWindow();
            lv.KeepScrolledToEndIfPreviously(prevRowMapSize > 0 ? prevRowMapSize - 1 : 0);
        }

        //update columns
        if (beginColumn != endColumn)
        {
            while (!lv.columnVisibilityMap.empty() && lv.columnVisibilityMap.back() >= beginColumn)
                lv.columnVisibilityMap.pop_back();

            for (size_t c = beginColumn; c < endColumn; ++c)
            {
                bool isBlocklisted = (std::find(Preferences::BlockListedDefaultColumns.begin(), Preferences::BlockListedDefaultColumns.end(), globalLogs.Columns[c].UniqueName) != Preferences::BlockListedDefaultColumns.end());
                if (!isBlocklisted)
                    lv.columnVisibilityMap.emplace_back((uint32_t)c);
            }

            lv.SyncVisibleColumnsToWindow();
            lv.SyncFilterChoicesToWindow();
        }

        RedrawWindow(lv.hwndWindow, nullptr, 0, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
}

void LogViewCloseAllWindows()
{
    while (!logViews.empty())
    {
        logViews.pop_back();
    }
}

void LogViewWindowLockoutInteraction(bool interactionEnabled)
{
    for (auto &lv : logViews)
    {
        EnableWindow(lv.hwndWindow, interactionEnabled);
    }
}

std::string LogViewGetWindowStatusText(HWND window)
{
    for (auto &lv : logViews)
    {
        if (lv.hwndWindow == window)
        {
            return lv.GetWindowStatusText();
        }
    }

    return std::string();
}

void OpenNewFilteredMainLogView(std::vector<LogFilterEntry> filters)
{
    MainLogView *newView = DuplicateView(logViews.front());
    newView->ApplyFilterWithStatusDialog(filters);
}

LRESULT CALLBACK CustomizedEditBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (uMsg == WM_COMMAND)
    {
        if ((HWND)lParam == 0) //weird case for context menus and accelerators
        {
            switch (LOWORD(wParam))
            {
            case ACCEL_COPY:
            {
                SendMessage(hwnd, WM_COPY, 0, 0);
            }
            return 0;
            case ACCEL_CUT:
            {
                if (uIdSubclass == CustomEditId_Normal)
                    SendMessage(hwnd, WM_CUT, 0, 0);
            }
            return 0;
            case ACCEL_PASTE:
            {
                if (uIdSubclass == CustomEditId_Normal)
                    SendMessage(hwnd, WM_PASTE, 0, 0);
            }
            return 0;
            case ACCEL_SELALL:
            {
                SendMessage(hwnd, EM_SETSEL, 0, GetWindowTextLength(hwnd));
            }
            return 0;
            }
        }
    }

    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK DetailedViewSplitterProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    MainLogView* lvp = (MainLogView*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (uMsg == WM_LBUTTONDOWN)
    {
        lvp->detailViewIsResizing = true;
        SetCapture(hwnd);
    }
    else if (uMsg == WM_LBUTTONUP)
    {
        lvp->detailViewIsResizing = false;
        ReleaseCapture();
    }
    else if (uMsg == WM_MOUSEMOVE)
    {
        if (lvp->detailViewIsResizing)
        {
            POINT mousePos;
            mousePos.x = GET_X_LPARAM(lParam);
            mousePos.y = GET_Y_LPARAM(lParam);
            ClientToScreen(hwnd, &mousePos);

            RECT mainClientRect;
            GetClientRect(lvp->hwndWindow, &mainClientRect);
            POINT mainWindowBotPos;
            mainWindowBotPos.x = 0;
            mainWindowBotPos.y = mainClientRect.bottom;
            ClientToScreen(lvp->hwndWindow, &mainWindowBotPos);

            int lastViewHeight = lvp->detailViewHeight;
            lvp->detailViewHeight = mainWindowBotPos.y - mousePos.y;
            if (lvp->detailViewHeight < 40)
                lvp->detailViewHeight = 40;
            if (lvp->detailViewHeight > mainClientRect.bottom - mainClientRect.top - 75)
                lvp->detailViewHeight = mainClientRect.bottom - mainClientRect.top - 75;

            if (lastViewHeight != lvp->detailViewHeight)
                lvp->ResizeChildren();
        }
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK MainLogViewWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    //creation
    MainLogView *lvp = (MainLogView*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (uMsg == WM_CREATE)
    {
        if (!lvp)
        {
            logViews.emplace_back();
            lvp = &logViews.back();
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lvp);

            lvp->hwndWindow = hwnd;
        }

        RegisterLogViewWindowClasses();

        //logs area
        lvp->hwndLogs = CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, "", WS_CHILD | LVS_REPORT | LVS_NOSORTHEADER | WS_VISIBLE | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0, 0, 0, 0, hwnd, 0, hInstance, 0);
        ListView_SetExtendedListViewStyle(lvp->hwndLogs, LVS_EX_GRIDLINES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);

        lvp->hwndLogsHeader = ListView_GetHeader(lvp->hwndLogs);

        lvp->hwndLogsColumnsTooltip = CreateWindow(TOOLTIPS_CLASS, "", WS_POPUP | TTS_NOPREFIX | TTS_BALLOON | TTS_NOFADE | TTS_NOANIMATE, CW_DEFAULT, CW_DEFAULT, CW_DEFAULT, CW_DEFAULT, hwnd, 0, hInstance, 0);
        lvp->logHeaderTooltipInfo = { 0 };
        lvp->logHeaderTooltipInfo.cbSize = sizeof(lvp->logHeaderTooltipInfo);
        lvp->logHeaderTooltipInfo.uFlags = TTF_TRANSPARENT | TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
        lvp->logHeaderTooltipInfo.hwnd = hwnd;
        lvp->logHeaderTooltipInfo.uId = (UINT_PTR)lvp->hwndLogsHeader;
        lvp->logHeaderTooltipInfo.hinst = hInstance;
        lvp->logHeaderTooltipInfo.lpszText = LPSTR_TEXTCALLBACK;
        SendMessage(lvp->hwndLogsColumnsTooltip, TTM_ADDTOOL, 0, (LPARAM)&lvp->logHeaderTooltipInfo);

        //top buttons
        lvp->hwndSaveLocal = CreateWindow(WC_BUTTON, "Save To File", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 5, 10, 90, 25, hwnd, 0, hInstance, 0);
        lvp->hwndDuplicateView = CreateWindow(WC_BUTTON, "New View", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 105, 10, 90, 25, hwnd, 0, hInstance, 0);

        //filter
        CreateWindow(WC_STATIC, "Row Filter", WS_VISIBLE | WS_CHILD, 70, 57, 100, 18, hwnd, 0, hInstance, 0);
        lvp->hwndFilterListColumns = CreateWindow(WC_COMBOBOX, "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | CBS_SORT, 10, 75, 180, 200, hwnd, 0, hInstance, 0);
        lvp->hwndFilterListValue = CreateWindow(WC_EDIT, "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 10, 102, 180, 23, hwnd, 0, hInstance, 0);
        SetWindowSubclass(lvp->hwndFilterListValue, CustomizedEditBoxProc, CustomEditId_Normal, 0);
        CreateWindow(WC_STATIC, "Match:", WS_VISIBLE | WS_CHILD, 6, 127, 50, 20, hwnd, 0, hInstance, 0);
        lvp->hwndFilterListCase = CreateWindow(WC_BUTTON, "Case", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 45, 125, 43, 22, hwnd, 0, hInstance, 0);
        lvp->hwndFilterListNot = CreateWindow(WC_BUTTON, "Not", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 159, 125, 37, 22, hwnd, 0, hInstance, 0);
        lvp->hwndFilterListSubstring = CreateWindow(WC_BUTTON, "Substring", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 90, 125, 66, 22, hwnd, 0, hInstance, 0);
        Button_SetCheck(lvp->hwndFilterListSubstring, true);
        lvp->hwndFilterListAdd = CreateWindow(WC_BUTTON, "Add", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 10, 147, 85, 25, hwnd, 0, hInstance, 0);
        lvp->hwndFilterListRemove = CreateWindow(WC_BUTTON, "Remove", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 105, 147, 85, 25, hwnd, 0, hInstance, 0);
        EnableWindow(lvp->hwndFilterListRemove, false);
        lvp->hwndFilterList = CreateWindow(WC_LISTBOX, "", WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL, 10, 175, 180, 205, hwnd, 0, hInstance, 0);

        lvp->SyncFilterChoicesToWindow();

        //column chooser
        CreateWindow(WC_STATIC, "Column Chooser", WS_VISIBLE | WS_CHILD, 50, 382, 140, 18, hwnd, 0, hInstance, 0);
        lvp->hwndColumnList = CreateWindow(WC_LISTBOX, "", WS_VISIBLE | WS_CHILD | LBS_MULTIPLESEL | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_SORT, 0, 0, 0, 0, hwnd, 0, hInstance, 0);
        lvp->hwndColumnListAll = CreateWindow(WC_BUTTON, "Select All*", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, 0, hInstance, 0);
        lvp->hwndColumnListNone = CreateWindow(WC_BUTTON, "Select None", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, 0, hInstance, 0);
        lvp->hwndColumnListEditBlockList = CreateWindow(WC_BUTTON, "*", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, 0, hInstance, 0);

        //lower optional details pane
        lvp->hwndExpandLogDetailView = CreateWindow(WC_BUTTON, "Toggle Detail View", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 250, 2, 125, 22, hwnd, 0, hInstance, 0);
        lvp->hwndLogDetailView = CreateWindowEx(WS_EX_STATICEDGE, WC_EDIT, "", WS_CHILD | ES_MULTILINE | ES_NOHIDESEL | ES_READONLY | WS_BORDER | WS_VSCROLL, 0, 0, 0, 0, hwnd, 0, hInstance, 0);
        SetWindowSubclass(lvp->hwndLogDetailView, CustomizedEditBoxProc, CustomEditId_ReadOnly, 0);
        lvp->hwndLogDetailViewSplitter = CreateWindow("TLHDetailedViewSplitter", "", WS_CHILD, 0, 0, 0, 0, hwnd, 0, hInstance, 0);
        SetWindowLongPtr(lvp->hwndLogDetailViewSplitter, GWLP_USERDATA, (LONG_PTR)lvp);
        lvp->hwndLogDetailViewFormatRaw = CreateWindowEx(0, WC_BUTTON, "Raw", WS_CHILD | BS_PUSHBUTTON, 0, 0, 31, 19, hwnd, 0, hInstance, 0);
        lvp->hwndLogDetailViewFormatFields = CreateWindowEx(0, WC_BUTTON, "Fields", WS_CHILD | BS_PUSHBUTTON, 0, 0, 39, 19, hwnd, 0, hInstance, 0);
        lvp->hwndLogDetailViewFormatJsonOrig = CreateWindowEx(0, WC_BUTTON, "Json", WS_CHILD | BS_PUSHBUTTON, 0, 0, 33, 19, hwnd, 0, hInstance, 0);
        lvp->hwndLogDetailViewFormatJsonSynth = CreateWindowEx(0, WC_BUTTON, "Json(synth)", WS_CHILD | BS_PUSHBUTTON, 0, 0, 69, 19, hwnd, 0, hInstance, 0);

        //qos visualizer
        lvp->hwndOpenQosVisualizer = CreateWindow(WC_BUTTON, "QoS Request Visualizer", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 1050, 2, 160, 22, hwnd, 0, hInstance, 0);

        //search
        CreateWindow(WC_STATIC, "Find:", WS_VISIBLE | WS_CHILD, 420, 4, 29, 21, hwnd, 0, hInstance, 0);
        lvp->hwndSearchString = CreateWindow(WC_EDIT, "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 450, 2, 250, 21, hwnd, 0, hInstance, 0);
        lvp->hwndSearchNext = CreateWindow(WC_BUTTON, "Next", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 705, 2, 60, 22, hwnd, 0, hInstance, 0);
        lvp->hwndSearchPrev = CreateWindow(WC_BUTTON, "Previous", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 765, 2, 60, 22, hwnd, 0, hInstance, 0);

        //
        lvp->ResizeChildren();
        FixChildFonts(hwnd);

        lvp->SyncEverythingToWindow();

        //set us up the drag and drop
        SetupDragDropForWindow(hwnd, HandleDragDropFileResults, HandleDragDropFileBusyCheck);

        return 0;
    }

    //everything else
    MainLogView &lv = *lvp;

    switch (uMsg)
    {
    case WM_SIZE:
    {
        lv.ResizeChildren();
        break;
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
    case WM_COMMAND:
    {
        if ((HWND)lParam == lv.hwndDuplicateView)
        {
            DuplicateView(lv);
            return 0;
        }
        else if ((HWND)lParam == lv.hwndSaveLocal)
        {
            std::string desc;
            if (GetParent(lv.hwndWindow) == hwndMain)
                desc = "Main Window";
            else
                desc = "LogView #" + std::to_string(lv.viewNumber);
            DoSaveLogsDialog(desc, lv.rowVisibilityMap, lv.GetSelectedRows(), lv.columnVisibilityMap);
            return 0;
        }
        else if ((HWND)lParam == lv.hwndOpenQosVisualizer)
        {
            ShowQosVisualizer(lv.rowVisibilityMap);
        }
        else if ((HWND)lParam == lv.hwndColumnList) //visible column list window
        {
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                lv.columnVisibilityMap.clear();

                std::vector<int> allSels;
                allSels.resize(ListBox_GetSelCount(lv.hwndColumnList));
                int ret = ListBox_GetSelItems(lv.hwndColumnList, allSels.size(), allSels.data());
                if (ret != LB_ERR)
                {
                    allSels.resize(ret);
                    for (auto sel : allSels)
                    {
                        std::string lbString = Win32ListBoxGetText(lv.hwndColumnList, sel);
                        auto iter = std::find_if(globalLogs.Columns.begin(), globalLogs.Columns.end(), [&](const ColumnInformation &ci) {return ci.UniqueName == lbString; });
                        if (iter != globalLogs.Columns.end())
                        {
                            lv.columnVisibilityMap.push_back((int)(iter - globalLogs.Columns.begin()));
                        }
                    }
                }

                std::sort(lv.columnVisibilityMap.begin(), lv.columnVisibilityMap.end());

                lv.RecreateColumns();
                return 0;
            }
        }
        else if ((HWND)lParam == lv.hwndColumnListAll)
        {
            //if the preferred set is already selected, instead select all.  otherwise select the preferred set
            std::vector<std::string> preferredColumns = GetPreferredColumnNames();
            std::sort(preferredColumns.begin(), preferredColumns.end());
            std::vector<std::string> currentSelectedColumns = lv.GetVisibleColumnNames();
            std::sort(currentSelectedColumns.begin(), currentSelectedColumns.end());

            std::vector<std::string> difference;
            std::set_symmetric_difference(preferredColumns.begin(), preferredColumns.end(), currentSelectedColumns.begin(), currentSelectedColumns.end(), std::back_inserter(difference));
            if (difference.empty())
            {
                lv.columnVisibilityMap.clear();
                for (int i = 0; i < (int)globalLogs.Columns.size(); ++i)
                    lv.columnVisibilityMap.push_back(i);
            }
            else
            {
                lv.columnVisibilityMap.clear();
                for (int i = 0; i < (int)globalLogs.Columns.size(); ++i)
                {
                    if (std::find(preferredColumns.begin(), preferredColumns.end(), globalLogs.Columns[i].UniqueName) != preferredColumns.end())
                        lv.columnVisibilityMap.push_back(i);
                }
            }

            lv.SyncVisibleColumnsToWindow();
            return 0;
        }
        else if ((HWND)lParam == lv.hwndColumnListNone)
        {
            lv.columnVisibilityMap.clear();
            lv.SyncVisibleColumnsToWindow();
            return 0;
        }
        else if ((HWND)lParam == lv.hwndColumnListEditBlockList)
        {
            ShowBlocklistedDefaultColumnsEditor();
            return 0;
        }
        else if ((HWND)lParam == lv.hwndFilterListCase || (HWND)lParam == lv.hwndFilterListNot || (HWND)lParam == lv.hwndFilterListSubstring)
        {
            Button_SetCheck((HWND)lParam, !Button_GetCheck((HWND)lParam));
            return 0;
        }
        else if ((HWND)lParam == lv.hwndFilterListAdd)
        {
            std::vector<char> columnNameBuffer;
            columnNameBuffer.resize(ComboBox_GetTextLength(lv.hwndFilterListColumns) + 1);
            ComboBox_GetText(lv.hwndFilterListColumns, columnNameBuffer.data(), (int)columnNameBuffer.size());
            int column = -1;
            for (int col = 0; col < globalLogs.Columns.size(); ++col)
            {
                if (globalLogs.Columns[col].UniqueName == columnNameBuffer.data())
                {
                    column = col;
                    break;
                }
            }

            std::vector<LogFilterEntry> currentFilters = lv.rowFilters;
            std::vector<char> valueBuffer;
            valueBuffer.resize(ComboBox_GetTextLength(lv.hwndFilterListValue) + 1);
            ComboBox_GetText(lv.hwndFilterListValue, valueBuffer.data(), (int)valueBuffer.size());
            if (!valueBuffer.empty())
            {
                currentFilters.emplace_back();
                currentFilters.back().Column = column;
                currentFilters.back().Value = valueBuffer.data();
                currentFilters.back().MatchCase = (0 != Button_GetCheck(lv.hwndFilterListCase));
                currentFilters.back().Not = (0 != Button_GetCheck(lv.hwndFilterListNot));
                currentFilters.back().MatchSubstring = (0 != Button_GetCheck(lv.hwndFilterListSubstring));
            }

            Edit_SetText(lv.hwndFilterListValue, "");
            lv.ApplyFilterWithStatusDialog(currentFilters);

            return 0;
        }
        else if ((HWND)lParam == lv.hwndFilterList)
        {
            if (HIWORD(wParam) == LBN_SELCHANGE)
            {
                int sel = ListBox_GetCurSel(lv.hwndFilterList);
                if (sel != LB_ERR)
                    EnableWindow(lv.hwndFilterListRemove, true);
                else
                    EnableWindow(lv.hwndFilterListRemove, false);

                return 0;
            }
        }
        else if ((HWND)lParam == lv.hwndFilterListRemove)
        {
            int sel = ListBox_GetCurSel(lv.hwndFilterList);
            if (sel != LB_ERR)
            {
                std::vector<LogFilterEntry> currentFilters = lv.rowFilters;
                currentFilters.erase(currentFilters.begin() + sel);

                lv.ApplyFilterWithStatusDialog(currentFilters);

                return 0;
            }
        }
        else if ((HWND)lParam == lv.hwndSearchString)
        {
            if (HIWORD(wParam) == EN_CHANGE)
            {
                lv.searchString.resize((size_t)GetWindowTextLength(lv.hwndSearchString) + 1);
                GetWindowText(lv.hwndSearchString, lv.searchString.data(), (int)lv.searchString.size());

                while (!lv.searchString.empty() && lv.searchString.back() == '\0')
                    lv.searchString.pop_back();

                RedrawWindow(lv.hwndLogs, nullptr, 0, RDW_INVALIDATE);
            }
        }
        else if ((HWND)lParam == lv.hwndSearchNext || (HWND)lParam == lv.hwndSearchPrev)
        {
            if (lv.searchString.empty())
            {
                SetFocus(lv.hwndSearchString);
                return 0;
            }

            if (lv.rowVisibilityMap.empty())
                return 0;

            std::vector<LogFilterEntry> matchFilter;
            matchFilter.emplace_back();
            matchFilter.back().Column = -1;
            matchFilter.back().MatchCase = false;
            matchFilter.back().MatchSubstring = true;
            matchFilter.back().Value = lv.searchString;

            int direction = ((HWND)lParam == lv.hwndSearchNext) ? 1 : -1;

            int64_t initialSel = ListView_GetNextItem(lv.hwndLogs, -1, LVNI_SELECTED);
            if (initialSel == LB_ERR)
                initialSel = 0;
            else
                initialSel += direction;

            auto timerStart = std::chrono::high_resolution_clock::now();
            auto[found, where] = FindNextLogline(initialSel, direction, matchFilter, globalLogs, lv.rowVisibilityMap);
            auto timeToRun = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - timerStart);
            GlobalDebugOutput("Search time: " + std::to_string(timeToRun.count()) + "us");
            if (found)
            {
                ListView_SetItemState(lv.hwndLogs, -1, 0, LVIS_SELECTED);
                ListView_SetItemState(lv.hwndLogs, where, LVIS_SELECTED, LVIS_SELECTED);
                ListView_EnsureVisible(lv.hwndLogs, where, false);
                return 0;
            }

            //not found
            Beep(4000, 10);
            return 0;
        }
        else if ((HWND)lParam == 0) //weird case for context menus and accelerators
        {
            switch (LOWORD(wParam))
            {
            case CONTEXTMENU_COPYCELL:
                CopyTextToClipboard(lv.cellValueToCopy);
                return 0;
            case CONTEXTMENU_COPYRAW: case CONTEXTMENU_COPYFIELDS_ALL: case CONTEXTMENU_COPYFIELDS_FILTERED:
            {
                std::stringstream dataToCopy;

                if (LOWORD(wParam) == CONTEXTMENU_COPYRAW)
                {
                    FormatLogData(LOGFORMAT_RAW, lv.GetSelectedRows(), std::vector<uint32_t>(), dataToCopy);
                }
                else if (LOWORD(wParam) == CONTEXTMENU_COPYFIELDS_ALL)
                {
                    std::vector<uint32_t> allColumns;
                    for (size_t c = 0; c < globalLogs.Columns.size(); ++c)
                        allColumns.emplace_back((uint32_t)c);

                    FormatLogData(LOGFORMAT_FIELDS_PSV, lv.GetSelectedRows(), allColumns, dataToCopy);
                }
                else if (LOWORD(wParam) == CONTEXTMENU_COPYFIELDS_FILTERED)
                {
                    FormatLogData(LOGFORMAT_FIELDS_PSV, lv.GetSelectedRows(), lv.columnVisibilityMap, dataToCopy);
                }

                CopyTextToClipboard(dataToCopy.str());
            }
            return 0;
            case CONTEXTMENU_HIGHLIGHTROWS: case CONTEXTMENU_UNHIGHLIGHTROWS:
            {
                for (uint32_t rowIndex : lv.GetSelectedRows())
                {
                    LogEntry &le = globalLogs.Lines[rowIndex];
                    le.Tagged = (LOWORD(wParam) == CONTEXTMENU_HIGHLIGHTROWS);
                }
                RedrawAllWindows();
            }
            return 0;
            case CONTEXTMENU_PASTE:
                PromptAndParseDataFromClipboard();
                return 0;
            case CONTEXTMENU_FILTERNONEMPTYCOLUMNS:
            case CONTEXTMENU_FILTERNONEMPTYNONZEROCOLUMNS:
            {
                lv.columnVisibilityMap.clear();

                //enable all columns that apply to the selected rows
                for (uint32_t rowIndex : lv.GetSelectedRows())
                {
                    LogEntry &le = globalLogs.Lines[rowIndex];
                    for (auto cvi = le.ColumnDataBegin(); cvi != le.ColumnDataEnd(); ++cvi)
                    {
                        if (cvi->IndexDataBegin != cvi->IndexDataEnd)
                        {
                            // ensure the column has not already been marked as visible
                            if (std::find(lv.columnVisibilityMap.begin(), lv.columnVisibilityMap.end(), cvi->ColumnNumber) == lv.columnVisibilityMap.end())
                            {
                                //exclude columns that only contain whitespace
                                const auto &colValueView = le.GetColumnNumberValue(cvi->ColumnNumber);
                                for (auto c : colValueView)
                                {
                                    if (!(c == ' ' || c == '\t'))
                                    {
                                        if (LOWORD(wParam) == CONTEXTMENU_FILTERNONEMPTYNONZEROCOLUMNS)
                                        {
                                            // Check whether or not the value is on the list of 'ignoreable' strings.  
                                            // The column will get added if we have another row selected with a non 'default' value.

                                            // TODO: Once we have string_view we can use this and replace IGNORABLE_STRINGS with a std::set instead of std::vector
                                            /*
                                            if (IGNORABLE_STRINGS.find(colValueView) == IGNORABLE_STRINGS.end())
                                            {
                                                break;
                                            }
                                            */

                                            if (std::find(IGNORABLE_STRINGS.begin(), IGNORABLE_STRINGS.end(), colValueView) != IGNORABLE_STRINGS.end())
                                            {
                                                break;
                                            }
                                        }

                                        lv.columnVisibilityMap.push_back(cvi->ColumnNumber);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }

                //remove blocklisted ones
                for (size_t i = 0; i < lv.columnVisibilityMap.size(); )
                {
                    bool isBlocklisted = (std::find(Preferences::BlockListedDefaultColumns.begin(), Preferences::BlockListedDefaultColumns.end(), globalLogs.Columns[lv.columnVisibilityMap[i]].UniqueName) != Preferences::BlockListedDefaultColumns.end());
                    if (isBlocklisted)
                    {
                        lv.columnVisibilityMap.erase(lv.columnVisibilityMap.begin() + i);
                    }
                    else
                        ++i;
                }

                std::sort(lv.columnVisibilityMap.begin(), lv.columnVisibilityMap.end());

                lv.RebuildColumnChooser();
                lv.RecreateColumns();
            }
            return 0;
            case CONTEXTMENU_FILTERMATCHINGROWS_NOTPREFIX:
                lv.showApplicableRowsFilter.Not = true;
            case CONTEXTMENU_FILTERMATCHINGROWS_PREFIX:
            {
                lv.showApplicableRowsFilter.MatchSubstring = true;
                lv.showApplicableRowsFilter.Value = StringSplit('.', lv.showApplicableRowsFilter.Value)[0];

                std::vector<LogFilterEntry> currentFilters = lv.rowFilters;
                currentFilters.emplace_back(lv.showApplicableRowsFilter);

                lv.ApplyFilterWithStatusDialog(currentFilters);
            }
            return 0;
            case CONTEXTMENU_FILTERMATCHINGROWS_NOT:
                lv.showApplicableRowsFilter.Not = true;
            case CONTEXTMENU_FILTERMATCHINGROWS:
            {
                std::vector<LogFilterEntry> currentFilters = lv.rowFilters;
                currentFilters.emplace_back(lv.showApplicableRowsFilter);

                lv.ApplyFilterWithStatusDialog(currentFilters);
            }
            return 0;
            case CONTEXTMENU_VIEWMATCHINGROWS_NOTPREFIX:
                lv.showApplicableRowsFilter.Not = true;
            case CONTEXTMENU_VIEWMATCHINGROWS_PREFIX:
            {
                lv.showApplicableRowsFilter.MatchSubstring = true;
                lv.showApplicableRowsFilter.Value = StringSplit('.', lv.showApplicableRowsFilter.Value)[0];

                std::vector<LogFilterEntry> newFilters;
                newFilters.emplace_back(lv.showApplicableRowsFilter);

                MainLogView *newView = DuplicateView(lv);
                newView->ApplyFilterWithStatusDialog(newFilters);
            }
            return 0;
            case CONTEXTMENU_VIEWMATCHINGROWS_NOT:
                lv.showApplicableRowsFilter.Not = true;
            case CONTEXTMENU_VIEWMATCHINGROWS:
            {
                std::vector<LogFilterEntry> newFilters;
                newFilters.emplace_back(lv.showApplicableRowsFilter);

                MainLogView *newView = DuplicateView(lv);
                newView->ApplyFilterWithStatusDialog(newFilters);
            }
            return 0;
            case CONTEXTMENU_HIDECOLUMN:
            {
                auto newEnd = std::remove(lv.columnVisibilityMap.begin(), lv.columnVisibilityMap.end(), (uint32_t)lv.columnContextDataCol);
                lv.columnVisibilityMap.erase(newEnd, lv.columnVisibilityMap.end());
                lv.SyncVisibleColumnsToWindow();
                lv.RecreateColumns();
            }
            return 0;
            case CONTEXTMENU_SORTCOLUMNS_DEFAULT:
            case CONTEXTMENU_SORTCOLUMNS_ALPHA:
            {
                //count how many columns exist
                LVCOLUMN lvc = { 0 };
                lvc.mask = LVCF_WIDTH;
                int totalCols = 0;
                while (ListView_GetColumn(lv.hwndLogs, totalCols, &lvc))
                    ++totalCols;

                //grab current order, sort, replace order
                std::vector<int> visualColOrder;
                visualColOrder.resize(totalCols);
                ListView_GetColumnOrderArray(lv.hwndLogs, (int)visualColOrder.size(), visualColOrder.data());

                if (LOWORD(wParam) == CONTEXTMENU_SORTCOLUMNS_ALPHA) //alphabetical
                {
                    std::sort(visualColOrder.begin(), visualColOrder.end(), [&](int a, int b)
                    {
                        std::string stringA = globalLogs.Columns[lv.columnVisibilityMap[a]].GetDisplayName();
                        TransformStringToLower(stringA);
                        std::string stringB = globalLogs.Columns[lv.columnVisibilityMap[b]].GetDisplayName();
                        TransformStringToLower(stringB);
                        return stringA < stringB;
                    });
                }
                else //default schema order
                {
                    for (int i = 0; i < (int)visualColOrder.size(); ++i)
                        visualColOrder[i] = i;
                }

                ListView_SetColumnOrderArray(lv.hwndLogs, (int)visualColOrder.size(), visualColOrder.data());
                RedrawWindow(lv.hwndLogs, nullptr, 0, RDW_INVALIDATE); //workaround a weird bug where it won't redraw what's already shown after the order change
            }
            return 0;
            case CONTEXTMENU_SORTROWS_ASCENDING:
            case CONTEXTMENU_SORTROWS_DESCENDING:
            {
                bool ascending = (LOWORD(wParam) == CONTEXTMENU_SORTROWS_ASCENDING);

                //sort
                GuiStatusManager::ShowBusyDialogAndRunMonitor("Sorting Logs", true, [&](GuiStatusMonitor &monitor)
                {
                    globalLogs.SortColumn = (uint16_t)lv.columnContextDataCol;
                    globalLogs.SortAscending = ascending;

                    std::chrono::time_point<std::chrono::high_resolution_clock> timerStart = std::chrono::high_resolution_clock::now();
                    globalLogs.SortRange(0, globalLogs.Lines.size());
                    std::chrono::time_point<std::chrono::high_resolution_clock> timerEnd = std::chrono::high_resolution_clock::now();
                    monitor.AddDebugOutputTime("Sort Logs", std::chrono::duration_cast<std::chrono::microseconds>(timerEnd - timerStart).count() / 1000.0f);
                });

                for (auto &lvs : logViews)
                {
                    lvs.ResetColumnSortIndicator();
                }

                LogViewNotifyDataChanged(0, globalLogs.Lines.size(), 0, 0);
            }
            return 0;
            case CONTEXTMENU_MULTICOLUMNCHOICE_ADD:
            {
                lv.columnOperationStack.emplace_back(lv.columnContextDataCol);
            }
            return 0;
            case CONTEXTMENU_MULTICOLUMNCHOICE_CLEAR:
            {
                lv.columnOperationStack.clear();
            }
            return 0;
            case CONTEXTMENU_SHOWFREQUENCY:
            {
                lv.columnOperationStack.emplace_back(lv.columnContextDataCol);
                ShowFrequencyChart(lv.columnOperationStack, lv.rowVisibilityMap);
                lv.columnOperationStack.clear();
            }
            return 0;
            case CONTEXTMENU_SHOWHISTOGRAM:
            {
                ShowHistogramChart(lv.columnContextDataCol, lv.rowVisibilityMap);
            }
            return 0;
            case CONTEXTMENU_DNSLOOKUP:
            {
                AddDnsLookupColumnForIpColumn(lv.columnContextDataCol);
            }
            return 0;
            case ACCEL_COPY:
            {
                std::stringstream dataToCopy;
                FormatLogData(LOGFORMAT_FIELDS_PSV, lv.GetSelectedRows(), lv.columnVisibilityMap, dataToCopy);
                CopyTextToClipboard(dataToCopy.str());
            }
            return 0;
            case ACCEL_CUT:
            {
                //not supported here
            }
            return 0;
            case ACCEL_PASTE:
                PromptAndParseDataFromClipboard();
                return 0;
            case ACCEL_SELALL:
            {
                ListView_SetItemState(lv.hwndLogs, -1, LVIS_SELECTED, LVIS_SELECTED);
            }
            return 0;
            case ACCEL_FINDNEXT:
                SendMessage(hwnd, WM_COMMAND, 0, (LPARAM)lv.hwndSearchNext);
                return 0;
            case ACCEL_FINDPREV:
                SendMessage(hwnd, WM_COMMAND, 0, (LPARAM)lv.hwndSearchPrev);
                return 0;
            }
        }
        else if ((HWND)lParam == lv.hwndExpandLogDetailView)
        {
            lv.detailViewOpened = !lv.detailViewOpened;

            RECT mainClientRect;
            GetClientRect(lv.hwndWindow, &mainClientRect);
            if (lv.detailViewOpened)
                lv.detailViewHeight = (mainClientRect.bottom - mainClientRect.top) / 2;
            else
                lv.detailViewHeight = 0;

            //initialize certain things based on current data
            if (lv.detailViewFormat == DetailViewFormat::Default)
            {
                if (globalLogs.Parser && globalLogs.Parser->IsJsonParser)
                {
                    if (globalLogs.Parser->ProducesFakeJson)
                        lv.detailViewFormat = DetailViewFormat::JsonSynth;
                    else
                        lv.detailViewFormat = DetailViewFormat::JsonOrig;
                }
                else
                    lv.detailViewFormat = DetailViewFormat::Fields;
            }

            lv.UpdateLogDetailText();
            ShowWindow(lv.hwndLogDetailView, lv.detailViewOpened ? SW_SHOWNOACTIVATE : SW_HIDE);
            ShowWindow(lv.hwndLogDetailViewSplitter, lv.detailViewOpened ? SW_SHOWNOACTIVATE : SW_HIDE);
            ShowWindow(lv.hwndLogDetailViewFormatRaw, lv.detailViewOpened ? SW_SHOWNOACTIVATE : SW_HIDE);
            ShowWindow(lv.hwndLogDetailViewFormatFields, lv.detailViewOpened ? SW_SHOWNOACTIVATE : SW_HIDE);
            ShowWindow(lv.hwndLogDetailViewFormatJsonOrig, lv.detailViewOpened ? SW_SHOWNOACTIVATE : SW_HIDE);
            ShowWindow(lv.hwndLogDetailViewFormatJsonSynth, lv.detailViewOpened ? SW_SHOWNOACTIVATE : SW_HIDE);

            lv.ResizeChildren();
            return 0;
        }
        else if ((HWND)lParam == lv.hwndLogDetailViewFormatRaw)
        {
            lv.detailViewFormat = DetailViewFormat::Raw;
            lv.UpdateLogDetailText();

            return 0;
        }
        else if ((HWND)lParam == lv.hwndLogDetailViewFormatFields)
        {
            lv.detailViewFormat = DetailViewFormat::Fields;
            lv.UpdateLogDetailText();

            return 0;
        }
        else if ((HWND)lParam == lv.hwndLogDetailViewFormatJsonOrig)
        {
            lv.detailViewFormat = DetailViewFormat::JsonOrig;
            lv.UpdateLogDetailText();

            return 0;
        }
        else if ((HWND)lParam == lv.hwndLogDetailViewFormatJsonSynth)
        {
            lv.detailViewFormat = DetailViewFormat::JsonSynth;
            lv.UpdateLogDetailText();

            return 0;
        }
    }
    break;
    case WM_NOTIFY:
    {
        NMHDR *n = (NMHDR*)lParam;
        if (n->hwndFrom == lv.hwndLogs)
        {
            switch (n->code)
            {
            case LVN_GETDISPINFO:
            {
                NMLVDISPINFO *di = (NMLVDISPINFO*)lParam;
                TempGetItemString = lv.ExtractVisibleCell(di->item.iItem, di->item.iSubItem);
                di->item.pszText = (char*)TempGetItemString.c_str();

                return 0;
            }
            case LVN_ODFINDITEM:
                return -1;
            case NM_RCLICK:
            {
                NMITEMACTIVATE *nia = (NMITEMACTIVATE*)lParam;

                DWORD gmp = GetMessagePos();
                POINTS points = MAKEPOINTS(gmp);

                HMENU menu = CreatePopupMenu();
                MENUINFO menuinfo = { 0 };
                menuinfo.cbSize = sizeof(menuinfo);
                GetMenuInfo(menu, &menuinfo);
                menuinfo.fMask = MIM_STYLE;
                menuinfo.dwStyle |= MNS_NOCHECK;
                SetMenuInfo(menu, &menuinfo);

                LVHITTESTINFO hti = { 0 };
                hti.pt = nia->ptAction;
                int htiret = ListView_SubItemHitTest(lv.hwndLogs, &hti);
                std::string cellValue;
                if (htiret != -1)
                {
                    if (hti.iItem >= 0 && hti.iItem < lv.rowVisibilityMap.size() && hti.iSubItem >= 0 && hti.iSubItem < lv.columnVisibilityMap.size())
                    {
                        int dataCol = lv.columnVisibilityMap[hti.iSubItem];
                        int dataRow = lv.rowVisibilityMap[hti.iItem];
                        const std::string &colName = globalLogs.Columns[dataCol].UniqueName;
                        cellValue = globalLogs.Lines[dataRow].GetColumnNumberValue((uint16_t)dataCol).str();
                        lv.showApplicableRowsFilter = LogFilterEntry();
                        lv.showApplicableRowsFilter.Column = dataCol;
                        lv.showApplicableRowsFilter.Value = cellValue;
                        lv.showApplicableRowsFilter.MatchCase = true;
                        lv.showApplicableRowsFilter.Not = false;
                        lv.showApplicableRowsFilter.MatchSubstring = false;
                        {
                            std::string ss = "Filter To Rows Where " + colName + " = " + cellValue;
                            InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_FILTERMATCHINGROWS, ss.c_str());
                        }
                        {
                            std::vector<std::string> parts = StringSplit('.', cellValue);
                            if (parts.size() > 1)
                            {
                                std::string ss = "Filter To Rows Where " + colName + " = " + parts[0];
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_FILTERMATCHINGROWS_PREFIX, ss.c_str());
                            }
                        }
                        {
                            std::string ss = "Filter To Rows Where " + colName + " != " + cellValue;
                            InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_FILTERMATCHINGROWS_NOT, ss.c_str());
                        }
                        {
                            std::vector<std::string> parts = StringSplit('.', cellValue);
                            if (parts.size() > 1)
                            {
                                std::string ss = "Filter To Rows Where " + colName + " != " + parts[0];
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_FILTERMATCHINGROWS_NOTPREFIX, ss.c_str());
                            }
                        }
                        InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");
                        {
                            std::string ss = "View Rows Where " + colName + " = " + cellValue;
                            InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_VIEWMATCHINGROWS, ss.c_str());
                        }
                        {
                            std::vector<std::string> parts = StringSplit('.', cellValue);
                            if (parts.size() > 1)
                            {
                                std::string ss = "View Rows Where " + colName + " = " + parts[0];
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_VIEWMATCHINGROWS_PREFIX, ss.c_str());
                            }
                        }
                        {
                            std::string ss = "View Rows Where " + colName + " != " + cellValue;
                            InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_VIEWMATCHINGROWS_NOT, ss.c_str());
                        }
                        {
                            std::vector<std::string> parts = StringSplit('.', cellValue);
                            if (parts.size() > 1)
                            {
                                std::string ss = "View Rows Where " + colName + " != " + parts[0];
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_VIEWMATCHINGROWS_NOTPREFIX, ss.c_str());
                            }
                        }
                        InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");
                    }
                }

                int selCount = ListView_GetSelectedCount(lv.hwndLogs);
                if (selCount > 0)
                {
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_FILTERNONEMPTYCOLUMNS, "Filter To Non-Empty Columns");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_FILTERNONEMPTYNONZEROCOLUMNS, "Filter To Non-Empty Non-Zero Columns");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");

                    lv.cellValueToCopy = cellValue;
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_COPYCELL, "Copy This Cell");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_COPYRAW, "Copy Raw Loglines");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_COPYFIELDS_ALL, "Copy Pipe-Separated Loglines (All Columns)");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_COPYFIELDS_FILTERED, "Copy Pipe-Separated Loglines (Chosen Columns)");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");

                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_HIGHLIGHTROWS, "Highlight Rows");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_UNHIGHLIGHTROWS, "Unhighlight Rows");
                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");
                }

                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_PASTE, "Paste Raw Loglines");
                TrackPopupMenu(menu, 0, points.x, points.y, 0, hwnd, nullptr);

                return 1;
            }
            case NM_CUSTOMDRAW:
            {
                //don't draw if we're doing work that could be changing the log data
                if (GuiStatusManager::IsBusyWithWrites())
                    return CDRF_SKIPDEFAULT;

                //highlight rows that failed to process correctly or are highlighted by the user
                NMLVCUSTOMDRAW *lvDraw = (NMLVCUSTOMDRAW*)lParam;
                if (lvDraw->nmcd.dwDrawStage == CDDS_PREPAINT)
                {
                    return CDRF_NOTIFYITEMDRAW;
                }
                else if (lvDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    return CDRF_NOTIFYSUBITEMDRAW;
                }
                else if (lvDraw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM))
                {
                    RECT bounds = { 0 };
                    int ret = ListView_GetSubItemRect(lv.hwndLogs, lvDraw->nmcd.dwItemSpec, lvDraw->iSubItem, LVIR_LABEL, &bounds);
                    if (!ret)
                        return CDRF_DODEFAULT; //hrm

                    if (lvDraw->iSubItem == 0 && lv.columnVisibilityMap.size() > 1)
                    {
                        //subitem 0 has some screwyness where it's leaving a gap on the left of subitem 0.. not sure the cause, but we can hack around it...
                        bounds.left -= 4;
                        bounds.bottom -= 1;
                    }

                    if (lvDraw->nmcd.dwItemSpec >= 0 && lvDraw->nmcd.dwItemSpec < lv.rowVisibilityMap.size())
                    {
                        if (lvDraw->iSubItem >= 0 && lvDraw->iSubItem < lv.columnVisibilityMap.size())
                        {
                            const LogEntry &le = globalLogs.Lines[lv.rowVisibilityMap[lvDraw->nmcd.dwItemSpec]];
                            const ExternalSubstring<const char> str = le.GetColumnNumberValue((uint16_t)lv.columnVisibilityMap[lvDraw->iSubItem]);

                            //normal things that would affect background color
                            COLORREF bgColor1 = RGB(255, 255, 255);
                            if (le.ParseFailed && le.Tagged)
                                bgColor1 = RGB(255, 159, 255);
                            else if (le.ParseFailed)
                                bgColor1 = RGB(255, 223, 63);
                            else if (le.Tagged)
                                bgColor1 = RGB(95, 255, 223);

                            bool selected = (0 != (ListView_GetItemState(lv.hwndLogs, lvDraw->nmcd.dwItemSpec, LVIS_SELECTED)&LVIS_SELECTED));
                            if (selected)
                                bgColor1 = GetSysColor(COLOR_HIGHLIGHT);

                            //if a search is active, we'll have 2 background colors if there is a match and another
                            COLORREF bgColor2 = bgColor1;
                            if (!lv.searchString.empty())
                            {
                                //ideally we would search and highlight the whole row, but that's more expensive.  for now we'll just do it per-column since that's how windows draws us.
                                if (str.find_insensitive(lv.searchString) != std::string::npos)
                                    bgColor2 = RGB(255, 210, 190);

                                //if nothing else was set to be highlighted for the first color (or the row is selected), the whole thing should be the search color
                                if (bgColor1 == RGB(255, 255, 255) || selected)
                                {
                                    selected = false;
                                    bgColor1 = bgColor2;
                                }
                            }

                            //if both bg colors are the same, only draw once, else draw a split background
                            if (bounds.right - bounds.left <= 5)
                                return CDRF_SKIPDEFAULT;

                            RECT paddedBounds = bounds;
                            paddedBounds.left += 3;
                            paddedBounds.right -= 2;

                            COLORREF prevBgColor = GetBkColor(lvDraw->nmcd.hdc);
                            char nothing = 0;
                            if (bgColor1 == bgColor2)
                            {
                                SetBkColor(lvDraw->nmcd.hdc, bgColor1);
                                ExtTextOut(lvDraw->nmcd.hdc, 0, 0, ETO_OPAQUE, &bounds, &nothing, 0, 0); //using this trick to draw a solid rect with our bg color
                            }
                            else
                            {
                                int mid = (bounds.bottom - bounds.top) / 2 + bounds.top;
                                RECT boundsTopHalf = bounds;
                                RECT boundsBotHalf = bounds;
                                boundsTopHalf.bottom = mid;
                                boundsBotHalf.top = mid;

                                SetBkColor(lvDraw->nmcd.hdc, bgColor1);
                                ExtTextOut(lvDraw->nmcd.hdc, 0, 0, ETO_OPAQUE, &boundsTopHalf, &nothing, 0, 0); //using this trick to draw a solid rect with our bg color

                                SetBkColor(lvDraw->nmcd.hdc, bgColor2);
                                ExtTextOut(lvDraw->nmcd.hdc, 0, 0, ETO_OPAQUE, &boundsBotHalf, &nothing, 0, 0); //using this trick to draw a solid rect with our bg color
                            }

                            //Draw text.  Huge text really bogs down windows even if it's all truncated for some reason.. so we'll limit how much we tell it to draw
                            COLORREF prevFgColor = { 0 };
                            if (selected)
                            {
                                prevFgColor = GetTextColor(lvDraw->nmcd.hdc);
                                SetTextColor(lvDraw->nmcd.hdc, RGB(255, 255, 255));
                            }

                            int charsToDraw = str.size() > 1000 ? 1000 : (int)str.size();
                            DrawText(lvDraw->nmcd.hdc, str.begin(), charsToDraw, &paddedBounds, DT_END_ELLIPSIS | DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                            if (selected)
                                SetTextColor(lvDraw->nmcd.hdc, prevFgColor);
                            SetBkColor(lvDraw->nmcd.hdc, prevBgColor);

                            return CDRF_SKIPDEFAULT;
                        }
                    }
                }

                return CDRF_DODEFAULT;
            }
            case LVN_ITEMCHANGED:
            {
                NMLISTVIEW *nmlv = (NMLISTVIEW*)lParam;
                if (nmlv->uNewState&LVIS_SELECTED || nmlv->uOldState&LVIS_SELECTED)
                    lv.UpdateLogDetailText();
                break;
            }
            }
        }
        else if (n->hwndFrom == lv.hwndLogsColumnsTooltip)
        {
            if (n->code == TTN_GETDISPINFO)
            {
                NMTTDISPINFO *ttdi = (NMTTDISPINFO*)lParam;
                ttdi->hinst = 0;
                ttdi->szText[0] = 0;

                int dataCol = lv.GetDataColForColHeaderPosition(lv.logHeaderTooltipLastX);
                if (dataCol < 0 || dataCol >= globalLogs.Columns.size())
                {
                    ttdi->lpszText = (char*)"";
                }
                else
                {
                    if (lv.logHeaderTooltipManuallyTruncateText)
                    {
                        lv.logHeaderTooltipTruncatedString.clear();
                        int lineCount = 0;
                        for (auto i = globalLogs.Columns[dataCol].Description.begin(); i != globalLogs.Columns[dataCol].Description.end(); ++i)
                        {
                            lv.logHeaderTooltipTruncatedString.push_back(*i);
                            ++lineCount;
                            if (lineCount >= 15)
                            {
                                lineCount = 0;

                                if (i + 1 < globalLogs.Columns[dataCol].Description.end() && *(i + 1) != ' ')
                                    lv.logHeaderTooltipTruncatedString.push_back('-');

                                lv.logHeaderTooltipTruncatedString.push_back('\n');
                            }
                        }

                        ttdi->lpszText = (char*)lv.logHeaderTooltipTruncatedString.c_str();
                    }
                    else
                        ttdi->lpszText = (char*)globalLogs.Columns[dataCol].Description.c_str();
                }
            }
        }
        else if (n->hwndFrom == lv.hwndFilterListColumns)
        {
            return 0;
        }
        else
        {
            int actualCode = ((LPNMHDR)lParam)->code;
            POINTS points = { 0 };
            POINT pointClient = { 0 }, pointScreen = { 0 };
            POINT pointHeaderClient = { 0 };

            if (actualCode == NM_RCLICK)
            {
                DWORD gmp = GetMessagePos();
                points = MAKEPOINTS(gmp);
                pointScreen.x = points.x;
                pointScreen.y = points.y;

                pointClient = pointScreen;
                ScreenToClient(lv.hwndLogs, &pointClient);

                pointHeaderClient = pointScreen;
                ScreenToClient(lv.hwndLogsHeader, &pointHeaderClient);
            }

            if (lv.hwndLogsHeader)
            {
                //check for right click on log column headers
                if (actualCode == NM_RCLICK)
                {
                    LVHITTESTINFO hti = { 0 };
                    hti.pt = pointClient;
                    int htiret = ListView_SubItemHitTest(lv.hwndLogs, &hti);
                    if (htiret != -1)
                    {
                        if (hti.iSubItem >= 0 && hti.iSubItem < (int)lv.columnVisibilityMap.size())
                        {
                            int dataCol = lv.columnVisibilityMap[hti.iSubItem];

                            HMENU menu = CreatePopupMenu();
                            MENUINFO menuinfo = { 0 };
                            menuinfo.cbSize = sizeof(menuinfo);
                            GetMenuInfo(menu, &menuinfo);
                            menuinfo.fMask = MIM_STYLE;
                            menuinfo.dwStyle |= MNS_NOCHECK;
                            SetMenuInfo(menu, &menuinfo);

                            lv.columnContextDataCol = dataCol;

                            {
                                std::stringstream ss;
                                ss << "Sort Rows Ascending by " << globalLogs.Columns[dataCol].UniqueName;
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_SORTROWS_ASCENDING, ss.str().c_str());
                            }
                            {
                                std::stringstream ss;
                                ss << "Sort Rows Descending by " << globalLogs.Columns[dataCol].UniqueName;
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_SORTROWS_DESCENDING, ss.str().c_str());
                            }
                            {
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");
                                {
                                    std::stringstream ss;
                                    ss << "Hide Column " << globalLogs.Columns[dataCol].UniqueName;
                                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_HIDECOLUMN, ss.str().c_str());
                                }
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_SORTCOLUMNS_DEFAULT, "Sort Columns Using Schema Order");
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_SORTCOLUMNS_ALPHA, "Sort Columns Alphabetically");
                            }
                            {
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");

                                bool columnAlreadyIncluded = false;
                                for (auto c : lv.columnOperationStack)
                                {
                                    if ((int)c == dataCol)
                                        columnAlreadyIncluded = true;
                                }

                                if (!columnAlreadyIncluded)
                                {
                                    auto nextColumnStack = lv.columnOperationStack;
                                    nextColumnStack.emplace_back(dataCol);

                                    std::stringstream ss;
                                    ss << "Use " << StringJoin(" + ", nextColumnStack.begin(), nextColumnStack.end(), [&](int c) {return globalLogs.Columns[c].UniqueName; }) << " and ...";
                                    InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_MULTICOLUMNCHOICE_ADD, ss.str().c_str());
                                }
                            }
                            if (!lv.columnOperationStack.empty())
                            {
                                std::stringstream ss;
                                ss << "Don't Use " << StringJoin(" + ", lv.columnOperationStack.begin(), lv.columnOperationStack.end(), [&](int c) {return globalLogs.Columns[c].UniqueName; });
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_MULTICOLUMNCHOICE_CLEAR, ss.str().c_str());
                            }
                            {
                                auto nextColumnStack = lv.columnOperationStack;
                                nextColumnStack.emplace_back(dataCol);

                                std::stringstream ss;
                                ss << "Count Frequency of Values for " << StringJoin(" + ", nextColumnStack.begin(), nextColumnStack.end(), [&](int c) {return globalLogs.Columns[c].UniqueName; });
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_SHOWFREQUENCY, ss.str().c_str());
                            }
                            {
                                std::stringstream ss;
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, "");
                                ss << "Render Histogram of Values for " << globalLogs.Columns[dataCol].UniqueName;
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_SHOWHISTOGRAM, ss.str().c_str());
                            }
                            {
                                std::stringstream ss;
                                ss << "DNS lookup using " << globalLogs.Columns[dataCol].UniqueName << " for IP";
                                InsertMenu(menu, (UINT)-1, MF_BYPOSITION | MF_STRING, CONTEXTMENU_DNSLOOKUP, ss.str().c_str());
                            }

                            TrackPopupMenu(menu, 0, points.x, points.y, 0, hwnd, nullptr);
                        }
                    }
                }
            }
        }
    }
    break;
    case WM_DESTROY: case WM_QUIT: case WM_CLOSE:
        logViews.remove_if([&](auto &p) {return &p == lvp; });
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
