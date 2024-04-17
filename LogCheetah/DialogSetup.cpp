#include "DialogSetup.h"

#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <Shlobj.h>
#include <thread>
#include <filesystem>

#include "Preferences.h"
#include "WinMain.h"
#include "GuiStatusMonitor.h"
#include "JsonParser.h"
#include "CatWindow.h"

INT_PTR CALLBACK SetupDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

namespace
{
    HWND hwndDone = 0;
    HWND hwndCpuCountGeneral = 0;
    HWND hwndCpuCountParse = 0;
    HWND hwndCpuCountSort = 0;
    HWND hwndCpuCountFilter = 0;
    HWND hwndTestParallelism = 0;

    HWND hwndAllowCats = 0;
    HWND hwndForceCats = 0;

    HWND hwndPromptForMemoryUse = 0;

    void TestParallelismCase(uint64_t &outParseTime, uint64_t &outSortTime, uint64_t &outFilterTime)
    {
        std::vector<std::string> logs;

        int iters = 15000;
#ifdef _DEBUG
        iters = 1000;
#endif

        logs.reserve(iters);
        for (int i = 0; i < iters; ++i)
        {
            logs.emplace_back("{\"ver\":\"2.1\",\"name\":\"xHttpLiteModuleRequestFilter.MaxQueryStringLengthExceeded\",\"time\":\"2016-05-25T12:19:06.4781499Z\",\"epoch\":\"14500\",\"seqNum\":73838,\"os\":\"Win32NT\",\"osVer\":\"6.2.9200.0\",\"appId\":\"S:XTitle.exe\",\"appVer\":\"1.0.1605.23002\",\"cV\":\"T6KuV2f5y0+YliO7Pdk11g.1\",\"ext\":{\"cloud\":{\"name\":\"SLSXTitle\",\"role\":\"XTitle\",\"roleInstance\":\"BLUAPVM007424\",\"location\":\"ExampleLab\",\"roleVer\":\"XTitle_Main_Publish_1605_23002\",\"environment\":\"Dev\"},\"sll\":{\"libVer\":\"4.1.16127.1\",\"level\":6},\"xhttplite\":{\"ClientIP\":\"1.2.3.4\"},\"ap\":{\"env\":\"SLSXTitle-DEVNET-ExampleLab\"}},\"data\":{\"baseType\":\"\",\"queryStringLength\":7,\"maxQueryStringLength\":0}}");
            logs.emplace_back("{\"ver\":\"2.1\",\"name\":\"xHttpLite.RequestComplete\",\"time\":\"2016-05-25T12:19:06.4781499Z\",\"epoch\":\"14500\",\"seqNum\":73839,\"os\":\"Win32NT\",\"osVer\":\"6.2.9200.0\",\"appId\":\"S:XTitle.exe\",\"appVer\":\"1.0.1605.23002\",\"cV\":\"T6KuV2f5y0+YliO7Pdk11g.1\",\"ext\":{\"cloud\":{\"name\":\"SLSXTitle\",\"role\":\"XTitle\",\"roleInstance\":\"BLUAPVM007424\",\"location\":\"ExampleLab\",\"roleVer\":\"XTitle_Main_Publish_1605_23002\",\"environment\":\"Dev\"},\"sll\":{\"libVer\":\"4.1.16127.1\",\"level\":3},\"xhttplite\":{\"ClientIP\":\"1.2.3.4\"},\"ap\":{\"env\":\"SLSXTitle-DEVNET-ExampleLab\"}},\"data\":{\"baseType\":\"Ms.Qos.IncomingServiceRequest\",\"baseData\":{\"operationName\":\"XTitle.NsalHandler.GET\",\"targetUri\":\"http://example.xboxlive.com/titles/83872463/endpoints?type=1\",\"latencyMs\":1,\"succeeded\":true,\"requestMethod\":\"Get\",\"protocol\":\"HTTP\",\"protocolStatusCode\":\"414\",\"callerName\":\"Unknown\",\"requestStatus\":4},\"responseSizeBytes\":0,\"stackMS\":1,\"handlerMS\":0}}");
        }

        uint64_t val0, val1, val2, val3;
        QueryPerformanceCounter((LARGE_INTEGER*)&val0);
        LogCollection logCollection = JSON::ParseLogs(DebugStatusOnlyMonitor::Instance, std::move(logs), false);
        QueryPerformanceCounter((LARGE_INTEGER*)&val1);
        outParseTime = val1 - val0;
        logCollection.SortRange(0, logCollection.Lines.size());
        QueryPerformanceCounter((LARGE_INTEGER*)&val2);
        outSortTime = val2 - val1;
        LogFilterEntry lfe;
        lfe.Column = -1;
        lfe.Value = "xHttpLite.RequestComplete";
        for (auto &l : logCollection.Lines)
            DoesLogEntryPassFilters(l, { lfe });
        QueryPerformanceCounter((LARGE_INTEGER*)&val3);
        outFilterTime = val3 - val2;
    }

    size_t DetermineBestParallelism(const std::vector<uint64_t> &samples)
    {
        size_t firstDropBest;
        for (firstDropBest = 1; firstDropBest < samples.size(); ++firstDropBest)
        {
            if (samples[firstDropBest] >= samples[firstDropBest - 1])
                break;
        }
        --firstDropBest;

        size_t overallBest = samples.size() - 1;
        for (size_t p = 0; p < samples.size(); ++p)
        {
            if (samples[p] < samples[overallBest])
                overallBest = p;
        }

        double percentDiffHardwareBest = fabs((double)samples[std::thread::hardware_concurrency() - 1] - (double)samples[overallBest]) / std::max((double)samples[std::thread::hardware_concurrency() - 1], (double)samples[overallBest]);
        double percentDiffDropBest = fabs((double)samples[firstDropBest] - (double)samples[overallBest]) / std::max((double)samples[firstDropBest], (double)samples[overallBest]);
        if (firstDropBest + 1 == std::thread::hardware_concurrency() && percentDiffDropBest < 0.2)
            return std::thread::hardware_concurrency();
        else if (overallBest + 1 == std::thread::hardware_concurrency())
            return std::thread::hardware_concurrency();
        else
        {
            if (percentDiffHardwareBest < 0.1)
                return std::thread::hardware_concurrency();
            else if (percentDiffDropBest >= 0.2)
            {
                if (samples[firstDropBest] < samples[overallBest])
                    return firstDropBest + 1;
            }
        }

        return overallBest + 1;
    }

    std::string GetWindowsSpecialPath(GUID guid)
    {
        PWSTR comAppPathString = nullptr;
        if (SHGetKnownFolderPath(guid, 0, 0, &comAppPathString) != S_OK)
            return std::string();
        std::string appPathString = TruncateWideString(std::wstring(comAppPathString, comAppPathString + wcslen(comAppPathString)));
        CoTaskMemFree(comAppPathString);
        comAppPathString = nullptr;
        return std::move(appPathString);
    }
}

void ShowSetupDialog()
{
    //show box
    struct
    {
        DLGTEMPLATE dlg;
        DWORD trash0, trash1, trash2;
    } dlg = { 0 };

    dlg.dlg.style = DS_CENTER;
    dlg.dlg.dwExtendedStyle = 0;
    dlg.dlg.cx = 800;
    dlg.dlg.cy = 175;

    INT_PTR dbiRet = DialogBoxIndirect(hInstance, &dlg.dlg, activeMainWindow, SetupDialogProc);
    PopLockoutInteraction();
    if (dbiRet <= 0)
        return;
}

void RunTestParallelismDialog()
{
    GuiStatusManager::ShowBusyDialogAndRunMonitor("Testing Parallelism", false, [&](GuiStatusMonitor &monitor)
    {
        OverrideCpuCount(0, 0, 0, 0);

        int maxParallelism = std::thread::hardware_concurrency();
        if (!maxParallelism)
            maxParallelism = 16;

        int newCpuCountGeneral = 0;
        int newCpuCountParse = 0;
        int newCpuCountSort = 0;
        int newCpuCountFilter = 0;

        uint64_t freq = 0;
        QueryPerformanceFrequency((LARGE_INTEGER*)&freq);

        monitor.AddDebugOutput("Running Parallelism Tests...");
        monitor.SetProgressFeatures(maxParallelism);

        Sleep(100); //give ui stuff a chance to calm down, so it doesn't interfere

        if (maxParallelism > 1 && freq)
        {
            uint64_t parseTime, sortTime, filterTime;

            //kick one run off at max concurrency first, just to warm any caches
            OverrideCpuCount(maxParallelism, maxParallelism, maxParallelism, maxParallelism);
            TestParallelismCase(parseTime, sortTime, filterTime);

            //test them all
            std::vector<uint64_t> samplesParse;
            std::vector<uint64_t> samplesSort;
            std::vector<uint64_t> samplesFilter;
            std::vector<uint64_t> samplesTotal;
            for (int p = 0; p < maxParallelism; ++p)
            {
                monitor.AddProgress(1);

                OverrideCpuCount(p + 1, p + 1, p + 1, p + 1);
                TestParallelismCase(parseTime, sortTime, filterTime);
                samplesParse.emplace_back(parseTime);
                samplesSort.emplace_back(sortTime);
                samplesFilter.emplace_back(filterTime);
                samplesTotal.emplace_back(parseTime + sortTime + filterTime);
            }

            newCpuCountParse = (int)DetermineBestParallelism(samplesParse);
            newCpuCountSort = (int)DetermineBestParallelism(samplesSort);
            newCpuCountFilter = (int)DetermineBestParallelism(samplesFilter);
            int newCpuCountTotal = (int)DetermineBestParallelism(samplesTotal);
            newCpuCountGeneral = (std::max({ newCpuCountParse, newCpuCountSort, newCpuCountFilter }) + newCpuCountTotal) / 2;

            //diagnostics
            std::stringstream ss;
            ss << "Performance samples: \r\n";
            for (int p = 0; p < maxParallelism; ++p)
                ss << (p + 1) << ": parse " << samplesParse[p] << " (" << ((double)samplesParse[p] / freq)*1000.0 << "ms)" << " + sort " << samplesSort[p] << " (" << ((double)samplesSort[p] / freq)*1000.0 << "ms)" << " + filter " << samplesFilter[p] << " (" << ((double)samplesFilter[p] / freq)*1000.0 << "ms)" << " = " << samplesTotal[p] << " (" << ((double)samplesTotal[p] / freq)*1000.0 << "ms)" << "\r\n";
            ss << "\r\nPicked: General=" << newCpuCountGeneral << " Parse=" << newCpuCountParse << " Sort=" << newCpuCountSort << " Filter=" << newCpuCountFilter;
            monitor.AddDebugOutput(ss.str().c_str());
        }

        OverrideCpuCount(newCpuCountGeneral, newCpuCountParse, newCpuCountSort, newCpuCountFilter);
        Preferences::ParallelismOverrideGeneral = newCpuCountGeneral;
        Preferences::ParallelismOverrideParse = newCpuCountParse;
        Preferences::ParallelismOverrideSort = newCpuCountSort;
        Preferences::ParallelismOverrideFilter = newCpuCountFilter;
        monitor.Complete();
    });
}

INT_PTR CALLBACK SetupDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        PushLockoutInteraction(hwnd);
        SetWindowText(hwnd, "LogCheetah Setup");

        //oddly it creates us at a size different than we specified.. so fix it
        SetWindowPos(hwnd, 0, 0, 0, 650, 350, SWP_NOMOVE);

        RECT clientRect;
        GetClientRect(hwnd, &clientRect);

        hwndDone = CreateWindow(WC_BUTTON, "Make It So", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, clientRect.right - 110, clientRect.bottom - 30, 105, 25, hwnd, 0, hInstance, 0);

        //paralellism control
        std::stringstream parallelismLabel;
        parallelismLabel << "(Default for current CPU: " << std::thread::hardware_concurrency() << ")";
        CreateWindow(WC_STATIC, "Max Parallelism: (Leave blank for default)", WS_VISIBLE | WS_CHILD, 5, 5, 400, 19, hwnd, 0, hInstance, 0);
        CreateWindow(WC_STATIC, parallelismLabel.str().c_str(), WS_VISIBLE | WS_CHILD, 95, 25, 200, 19, hwnd, 0, hInstance, 0);

        CreateWindow(WC_STATIC, "General:", WS_VISIBLE | WS_CHILD, 5, 28, 50, 19, hwnd, 0, hInstance, 0);
        std::stringstream parallelismValueGeneral;
        if (Preferences::ParallelismOverrideGeneral)
            parallelismValueGeneral << Preferences::ParallelismOverrideGeneral;
        hwndCpuCountGeneral = CreateWindow(WC_EDIT, parallelismValueGeneral.str().c_str(), WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 55, 26, 30, 20, hwnd, 0, hInstance, 0);

        CreateWindow(WC_STATIC, "Parsing:", WS_VISIBLE | WS_CHILD, 5, 53, 50, 19, hwnd, 0, hInstance, 0);
        std::stringstream parallelismValueParse;
        if (Preferences::ParallelismOverrideParse)
            parallelismValueParse << Preferences::ParallelismOverrideParse;
        hwndCpuCountParse = CreateWindow(WC_EDIT, parallelismValueParse.str().c_str(), WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 55, 51, 30, 20, hwnd, 0, hInstance, 0);

        CreateWindow(WC_STATIC, "Sorting:", WS_VISIBLE | WS_CHILD, 5, 78, 50, 19, hwnd, 0, hInstance, 0);
        std::stringstream parallelismValueSort;
        if (Preferences::ParallelismOverrideSort)
            parallelismValueSort << Preferences::ParallelismOverrideSort;
        hwndCpuCountSort = CreateWindow(WC_EDIT, parallelismValueSort.str().c_str(), WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 55, 76, 30, 20, hwnd, 0, hInstance, 0);

        CreateWindow(WC_STATIC, "Filtering:", WS_VISIBLE | WS_CHILD, 5, 103, 50, 19, hwnd, 0, hInstance, 0);
        std::stringstream parallelismValueFilter;
        if (Preferences::ParallelismOverrideFilter)
            parallelismValueFilter << Preferences::ParallelismOverrideFilter;
        hwndCpuCountFilter = CreateWindow(WC_EDIT, parallelismValueFilter.str().c_str(), WS_VISIBLE | WS_CHILD | ES_NUMBER | WS_BORDER, 55, 101, 30, 20, hwnd, 0, hInstance, 0);

        hwndTestParallelism = CreateWindow(WC_BUTTON, "<-- Run Performance Tests", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 95, 64, 155, 20, hwnd, 0, hInstance, 0);

        //Misc general options
        CreateWindow(WC_STATIC, "General Options:", WS_VISIBLE | WS_CHILD, 275, 260, 400, 19, hwnd, 0, hInstance, 0);
        hwndPromptForMemoryUse = CreateWindow(WC_BUTTON, "Prompt For Memory Full", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 275, 280, 200, 22, hwnd, 0, hInstance, 0);
        Button_SetCheck(hwndPromptForMemoryUse, allowMemoryUseChecks);

        //Cats
        CreateWindow(WC_STATIC, "Cats:", WS_VISIBLE | WS_CHILD, 475, 170, 400, 19, hwnd, 0, hInstance, 0);
        hwndAllowCats = CreateWindow(WC_BUTTON, "Allow", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 475, 190, 170, 22, hwnd, 0, hInstance, 0);
        Button_SetCheck(hwndAllowCats, Preferences::AllowCats);
        hwndForceCats = CreateWindow(WC_BUTTON, "Force", WS_VISIBLE | WS_CHILD | BS_CHECKBOX, 475, 215, 170, 22, hwnd, 0, hInstance, 0);
        Button_SetCheck(hwndForceCats, Preferences::ForceCats && Preferences::AllowCats);
        EnableWindow(hwndForceCats, Preferences::AllowCats);

        //
        FixChildFonts(hwnd);

        return TRUE;
    }
    case WM_COMMAND:
    {
        if ((HWND)lParam == hwndDone || wParam == IDCANCEL)
            EndDialog(hwnd, 1);
        else if ((HWND)lParam == hwndCpuCountGeneral || (HWND)lParam == hwndCpuCountParse || (HWND)lParam == hwndCpuCountSort || (HWND)lParam == hwndCpuCountFilter)
        {
            if (HIWORD(wParam) == EN_CHANGE)
            {
                std::vector<char> buff;
                buff.resize(Edit_GetTextLength((HWND)lParam) + 1);
                Edit_GetText((HWND)lParam, buff.data(), (int)buff.size());
                std::string buffStr = buff.data();

                int newParallelism = 0;
                if (!buffStr.empty())
                    std::stringstream(buffStr) >> newParallelism;

                if (newParallelism > 256)
                {
                    if (IDNO == MessageBox(hwnd, "Extreme values for parallelism may degrade system performance.  Are you sure?", "Woah there...", MB_YESNO))
                    {
                        Edit_SetText((HWND)lParam, "");
                        break;
                    }
                }

                if (newParallelism <= 0)
                    newParallelism = 0;

                if ((HWND)lParam == hwndCpuCountGeneral)
                    Preferences::ParallelismOverrideGeneral = newParallelism;
                else if ((HWND)lParam == hwndCpuCountParse)
                    Preferences::ParallelismOverrideParse = newParallelism;
                else if ((HWND)lParam == hwndCpuCountSort)
                    Preferences::ParallelismOverrideSort = newParallelism;
                else if ((HWND)lParam == hwndCpuCountFilter)
                    Preferences::ParallelismOverrideFilter = newParallelism;

                OverrideCpuCount(Preferences::ParallelismOverrideGeneral, Preferences::ParallelismOverrideParse, Preferences::ParallelismOverrideSort, Preferences::ParallelismOverrideFilter);

                Preferences::HasTestedParallelism = true; //not actually tested, but they've been here to see it and changed it, so we'll count it
            }
        }
        else if ((HWND)lParam == hwndTestParallelism)
        {
            RunTestParallelismDialog();

            std::stringstream ssGeneral;
            ssGeneral << cpuCountGeneral;
            Edit_SetText(hwndCpuCountGeneral, ssGeneral.str().c_str());

            std::stringstream ssParse;
            ssParse << cpuCountParse;
            Edit_SetText(hwndCpuCountParse, ssParse.str().c_str());

            std::stringstream ssSort;
            ssSort << cpuCountSort;
            Edit_SetText(hwndCpuCountSort, ssSort.str().c_str());

            std::stringstream ssFilter;
            ssFilter << cpuCountFilter;
            Edit_SetText(hwndCpuCountFilter, ssFilter.str().c_str());
        }
        else if ((HWND)lParam == hwndAllowCats)
        {
            Button_SetCheck((HWND)lParam, !Button_GetCheck((HWND)lParam));
            Preferences::AllowCats = (Button_GetCheck((HWND)lParam) != 0);

            if (!Preferences::AllowCats)
            {
                Preferences::ForceCats = false;
                Button_SetCheck(hwndForceCats, false);
            }

            EnableWindow(hwndForceCats, Preferences::AllowCats);

            InitCatWindow(hwndMain);
        }
        else if ((HWND)lParam == hwndForceCats)
        {
            Button_SetCheck((HWND)lParam, !Button_GetCheck((HWND)lParam));
            Preferences::ForceCats = (Button_GetCheck((HWND)lParam) != 0);

            InitCatWindow(hwndMain);
        }
        else if ((HWND)lParam == hwndPromptForMemoryUse)
        {
            Button_SetCheck((HWND)lParam, !Button_GetCheck((HWND)lParam));
            allowMemoryUseChecks = (Button_GetCheck((HWND)lParam) != 0);
        }
    }
    };

    return FALSE;
}
