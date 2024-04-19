// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "DialogSaveLocal.h"
#include "GuiStatusMonitor.h"
#include "LogFormatter.h"

#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include "Globals.h"
#include "WinMain.h"
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>

namespace
{
    const uint32_t LOGROWFILTER_ALL = 0;
    const uint32_t LOGROWFILTER_FILTERED = 1;
    const uint32_t LOGROWFILTER_SELECTED = 2;

    const uint32_t LOGCOLFILTER_ALL = 0;
    const uint32_t LOGCOLFILTER_FILTERED = 1;

    HWND hwndSaveLocalOk = 0;
    HWND hwndSaveLocalCancel = 0;
    HWND hwndSaveLocalLogFormat = 0;
    HWND hwndSaveLocalRowFilter = 0;
    HWND hwndSaveLocalColFilter = 0;
    HWND hwndSaveLocalFilename = 0;
    HWND hwndSaveLocalFileBrowse = 0;

    //user dialog data
    std::string DialogExtraDescriptionData;
    std::string SaveFilename;
    uint32_t SaveLogFormat;
    uint32_t SaveLogRowFilter;
    uint32_t SaveLogColFilter;

    //
    void UpdateUIState()
    {
        EnableWindow(hwndSaveLocalOk, SaveFilename.empty() ? false : true);

        if (SaveLogFormat == LOGFORMAT_RAW)
        {
            EnableWindow(hwndSaveLocalColFilter, false);
            SaveLogColFilter = LOGCOLFILTER_ALL;
            ComboBox_SetCurSel(hwndSaveLocalColFilter, SaveLogColFilter);
        }
        else
            EnableWindow(hwndSaveLocalColFilter, true);
    }
}

INT_PTR CALLBACK OpenSaveLocalDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

void SaveLogsLocalWorker(const std::vector<uint32_t> &filteredRows, const std::vector<uint32_t> &selectedRows, std::vector<uint32_t> &filteredColumns)
{
    if (SaveFilename.empty())
        return;

    //write logs
    GuiStatusManager::ShowBusyDialogAndRunMonitor("Saving Logs", false, [&](GuiStatusMonitor &monitor)
    {
        std::chrono::time_point<std::chrono::high_resolution_clock> timerStart = std::chrono::high_resolution_clock::now();

        {
            std::ofstream file(SaveFilename.c_str(), std::ios::out | std::ios::binary);
            if (!file.is_open())
                MessageBox(hwndMain, "Failed to open file for writing.", "Ruh Roh", MB_OK);

            std::vector<uint32_t> rows;
            if (SaveLogRowFilter == LOGROWFILTER_FILTERED)
                rows = filteredRows;
            else if (SaveLogRowFilter == LOGROWFILTER_SELECTED)
                rows = selectedRows;
            else
            {
                for (size_t r = 0; r < globalLogs.Lines.size(); ++r)
                    rows.emplace_back((uint32_t)r);
            }

            std::vector<uint32_t> cols;
            if (SaveLogColFilter == LOGCOLFILTER_FILTERED)
                cols = filteredColumns;
            else
            {
                for (size_t c = 0; c < globalLogs.Columns.size(); ++c)
                    cols.emplace_back((uint32_t)c);
            }

            FormatLogData(SaveLogFormat, rows, cols, file);
        }

        std::chrono::time_point<std::chrono::high_resolution_clock> timerEnd = std::chrono::high_resolution_clock::now();
        monitor.AddDebugOutputTime("SaveLogs", std::chrono::duration_cast<std::chrono::microseconds>(timerEnd - timerStart).count() / 1000.0);
    });

    //write schemas
    if (globalLogs.Parser)
    {
        GuiStatusManager::ShowBusyDialogAndRunMonitor("Saving Schemas", false, [&](GuiStatusMonitor &monitor)
        {
            std::chrono::time_point<std::chrono::high_resolution_clock> timerStart = std::chrono::high_resolution_clock::now();

            std::string schemaData = globalLogs.Parser->SaveSchemaData();
            if (!schemaData.empty())
            {
                size_t extensionLength = std::filesystem::path(SaveFilename).extension().string().size();
                std::string fileWithoutExtension = SaveFilename;
                if (extensionLength > 0 && fileWithoutExtension.size() > extensionLength)
                    fileWithoutExtension.resize(fileWithoutExtension.size() - extensionLength);
                std::string fileToWrite = fileWithoutExtension + ".schema";

                std::ofstream file(fileToWrite, std::ios::binary | std::ios::out);
                if (file.is_open())
                {
                    file.write(schemaData.data(), schemaData.size());
                    file.close();
                }
                else
                    monitor.AddDebugOutput("Failed to write to file " + fileToWrite);

                std::chrono::time_point<std::chrono::high_resolution_clock> timerEnd = std::chrono::high_resolution_clock::now();
                monitor.AddDebugOutputTime("SaveSchemas", std::chrono::duration_cast<std::chrono::microseconds>(timerEnd - timerStart).count() / 1000.0);
            }
        });
    }
}

void DoSaveLogsDialog(const std::string &sourceDescription, const std::vector<uint32_t> &filteredRows, const std::vector<uint32_t> &selectedRows, std::vector<uint32_t> &filteredColumns)
{
    DialogExtraDescriptionData = sourceDescription;
    SaveFilename.clear();
    if (globalLogs.IsRawRepresentationValid)
        SaveLogFormat = LOGFORMAT_RAW;
    else
        SaveLogFormat = LOGFORMAT_FIELDS_PSV;
    SaveLogRowFilter = LOGROWFILTER_ALL;
    SaveLogColFilter = LOGCOLFILTER_ALL;

    //show box
    struct
    {
        DLGTEMPLATE dlg;
        DWORD trash0, trash1, trash2;
    } dlg = { 0 };

    dlg.dlg.style = DS_CENTER;
    dlg.dlg.dwExtendedStyle = 0;
    dlg.dlg.cx = 615;
    dlg.dlg.cy = 200;

    PushLockoutInteraction();
    INT_PTR dbiRet = DialogBoxIndirect(hInstance, &dlg.dlg, activeMainWindow, OpenSaveLocalDialogProc);
    PopLockoutInteraction();
    if (dbiRet <= 0)
        return;

    SaveLogsLocalWorker(filteredRows, selectedRows, filteredColumns);
}

INT_PTR CALLBACK OpenSaveLocalDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        std::string windowTitleText = "Save - From " + DialogExtraDescriptionData;
        SetWindowText(hwnd, windowTitleText.c_str());

        //oddly it creates us at a size different than we specified.. so fix it
        SetWindowPos(hwnd, 0, 0, 0, 615, 200, SWP_NOMOVE);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        //buttons
        hwndSaveLocalOk = CreateWindow(WC_BUTTON, "Save Logs", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, clientRect.right - 140, clientRect.bottom - 30, 135, 25, hwnd, 0, hInstance, 0);
        hwndSaveLocalCancel = CreateWindow(WC_BUTTON, "I Changed My Mind", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, clientRect.right - 280, clientRect.bottom - 30, 135, 25, hwnd, 0, hInstance, 0);

        //format options
        HWND label;
        label = CreateWindow(WC_STATIC, "Log Format", WS_VISIBLE | WS_CHILD, 60, 3, 85, 20, hwnd, 0, hInstance, 0);
        hwndSaveLocalLogFormat = CreateWindow(WC_COMBOBOX, "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 5, 20, 190, 250, hwnd, 0, hInstance, 0);
        ComboBox_AddString(hwndSaveLocalLogFormat, globalLogs.IsRawRepresentationValid ? "Raw Data Lines" : "Raw Data Lines (not valid)");
        ComboBox_AddString(hwndSaveLocalLogFormat, "Pipe-Separated Values");
        ComboBox_AddString(hwndSaveLocalLogFormat, "Comma-Separated Values");
        ComboBox_AddString(hwndSaveLocalLogFormat, "Tab-Separated Values");
        ComboBox_SetCurSel(hwndSaveLocalLogFormat, SaveLogFormat);

        label = CreateWindow(WC_STATIC, "Rows", WS_VISIBLE | WS_CHILD, 280, 3, 85, 20, hwnd, 0, hInstance, 0);
        hwndSaveLocalRowFilter = CreateWindow(WC_COMBOBOX, "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 205, 20, 190, 250, hwnd, 0, hInstance, 0);
        ComboBox_AddString(hwndSaveLocalRowFilter, "All");
        ComboBox_AddString(hwndSaveLocalRowFilter, "Filtered");
        ComboBox_AddString(hwndSaveLocalRowFilter, "Selected");
        ComboBox_SetCurSel(hwndSaveLocalRowFilter, SaveLogRowFilter);

        label = CreateWindow(WC_STATIC, "Columns", WS_VISIBLE | WS_CHILD, 470, 3, 85, 20, hwnd, 0, hInstance, 0);
        hwndSaveLocalColFilter = CreateWindow(WC_COMBOBOX, "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 405, 20, 190, 250, hwnd, 0, hInstance, 0);
        ComboBox_AddString(hwndSaveLocalColFilter, "All");
        ComboBox_AddString(hwndSaveLocalColFilter, "Chosen");
        ComboBox_SetCurSel(hwndSaveLocalColFilter, SaveLogColFilter);

        //filename
        label = CreateWindow(WC_STATIC, "Filename", WS_VISIBLE | WS_CHILD, 280, 65, 85, 20, hwnd, 0, hInstance, 0);
        hwndSaveLocalFilename = CreateWindow(WC_EDIT, SaveFilename.c_str(), WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 5, 82, clientRect.right - 75, 20, hwnd, 0, hInstance, 0);
        EnableWindow(hwndSaveLocalFilename, false);
        hwndSaveLocalFileBrowse = CreateWindow(WC_BUTTON, "Browse...", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, clientRect.right - 65, 80, 60, 22, hwnd, 0, hInstance, 0);

        //
        FixChildFonts(hwnd);

        UpdateUIState();
        return TRUE;
    }
    case WM_COMMAND:
    {
        if ((HWND)lParam == hwndSaveLocalOk)
            EndDialog(hwnd, 1);
        else if ((HWND)lParam == hwndSaveLocalCancel || wParam == IDCANCEL)
            EndDialog(hwnd, 0);
        else if ((HWND)lParam == hwndSaveLocalLogFormat)
        {
            if (HIWORD(wParam) == CBN_SELENDOK)
            {
                int sel = ComboBox_GetCurSel(hwndSaveLocalLogFormat);
                if (sel != CB_ERR)
                    SaveLogFormat = sel;
            }
        }
        else if ((HWND)lParam == hwndSaveLocalRowFilter)
        {
            if (HIWORD(wParam) == CBN_SELENDOK)
            {
                int sel = ComboBox_GetCurSel(hwndSaveLocalRowFilter);
                if (sel != CB_ERR)
                    SaveLogRowFilter = sel;
            }
        }
        else if ((HWND)lParam == hwndSaveLocalColFilter)
        {
            if (HIWORD(wParam) == CBN_SELENDOK)
            {
                int sel = ComboBox_GetCurSel(hwndSaveLocalColFilter);
                if (sel != CB_ERR)
                    SaveLogColFilter = sel;
            }
        }
        else if ((HWND)lParam == hwndSaveLocalFileBrowse)
        {
            std::stringstream filterStream;

            filterStream << "Raw Log Files";
            filterStream.write("", 1);
            filterStream << "*.log";
            filterStream.write("", 1);

            filterStream << "Pipe-seperated Values";
            filterStream.write("", 1);
            filterStream << "*.psv";
            filterStream.write("", 1);

            filterStream << "Comma-seperated Values";
            filterStream.write("", 1);
            filterStream << "*.csv";
            filterStream.write("", 1);

            filterStream << "Tab-seperated Values";
            filterStream.write("", 1);
            filterStream << "*.tsv";
            filterStream.write("", 1);

            filterStream << "All Files";
            filterStream.write("", 1);
            filterStream << "*.*";
            filterStream.write("", 1);

            std::string filterString = filterStream.str();

            uint32_t saveType = LOGFORMAT_FIELDS_PSV;
            int sel = ComboBox_GetCurSel(hwndSaveLocalLogFormat); //not really future proof if format list changes...
            if (sel != CB_ERR)
                saveType = sel;

            char ofnBuffer[MAX_PATH] = { 0 };
            OPENFILENAME ofn = { 0 };
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = activeMainWindow;
            ofn.lpstrFilter = filterString.c_str();
            ofn.Flags = 0;
            ofn.lpstrFile = ofnBuffer;
            ofn.nMaxFile = sizeof(ofnBuffer);
            ofn.nFilterIndex = saveType + 1;

            std::string savePath;
            if (RunLoadSaveDialog(true, ofn, savePath))
            {
                SaveFilename = ofnBuffer;

                std::string extension = std::filesystem::path(SaveFilename).extension().string();
                if (extension.empty())
                {
                    if (saveType == LOGFORMAT_FIELDS_PSV)
                        SaveFilename += ".psv";
                    else if (saveType == LOGFORMAT_FIELDS_CSV)
                        SaveFilename += ".csv";
                    else if (saveType == LOGFORMAT_FIELDS_TSV)
                        SaveFilename += ".tsv";
                    else
                        SaveFilename += ".log";
                }

                SetWindowText(hwndSaveLocalFilename, SaveFilename.c_str());
            }
        }

        UpdateUIState();
    }
    };

    return FALSE;
}
