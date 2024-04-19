// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include <Windows.h>
#include <Windowsx.h>
#include <CommCtrl.h>
#include <cstdint>
#include <time.h>
#include <ctime>
#include <functional>
#include <charconv>
#include <iomanip>

class DateTimePickerHelper
{
public:
    std::function<void()> OnValueChange;

    bool TimeModeUTC = true;
    time_t TimeStart = 0;
    time_t TimeEnd = 100ull * 365 * 24 * 60 * 60 + 24 * 24 * 60 * 60; //about 100 365-day years after epoch, with 24 days added to account for this being a bad measurement

    inline void SetupWindow(HWND parentHwnd, int datesXStart, int datesYStart, bool centeredTZ)
    {
        if (centeredTZ)
        {
            hwndRangeTZUTC = CreateWindow(WC_BUTTON, "UTC", WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON, datesXStart + 45, datesYStart + 25, 40, 23, parentHwnd, 0, hInstance, 0);
            hwndRangeTZLocal = CreateWindow(WC_BUTTON, "Local", WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON, datesXStart + 90, datesYStart + 25, 48, 23, parentHwnd, 0, hInstance, 0);
        }
        else
        {
            datesYStart -= 20;
            hwndRangeTZUTC = CreateWindow(WC_BUTTON, "UTC", WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON, datesXStart + 95, datesYStart + 25, 40, 23, parentHwnd, 0, hInstance, 0);
            hwndRangeTZLocal = CreateWindow(WC_BUTTON, "Local", WS_VISIBLE | WS_CHILD | BS_RADIOBUTTON, datesXStart + 140, datesYStart + 25, 48, 23, parentHwnd, 0, hInstance, 0);
        }

        hwndRangeLabelFrom = CreateWindow(WC_STATIC, "From", WS_VISIBLE | WS_CHILD, datesXStart + 85, datesYStart + 46, 100, 21, parentHwnd, 0, hInstance, 0);
        hwndRangeStartDate = CreateWindow(DATETIMEPICK_CLASS, "Date", WS_VISIBLE | WS_CHILD | WS_BORDER | DTS_SHORTDATECENTURYFORMAT, datesXStart + 15, datesYStart + 65, 120, 20, parentHwnd, 0, hInstance, 0);
        hwndRangeStartTime = CreateWindow(DATETIMEPICK_CLASS, "Time", WS_VISIBLE | WS_CHILD | WS_BORDER | DTS_TIMEFORMAT, datesXStart + 15, datesYStart + 85, 120, 20, parentHwnd, 0, hInstance, 0);
        hwndNowStart = CreateWindow(WC_BUTTON, "Now", WS_VISIBLE | WS_CHILD, datesXStart + 140, datesYStart + 65, 40, 19, parentHwnd, 0, hInstance, 0);
        hwndParseStart = CreateWindow(WC_BUTTON, "Paste", WS_VISIBLE | WS_CHILD, datesXStart + 140, datesYStart + 85, 40, 19, parentHwnd, 0, hInstance, 0);
        hwndRangeLabelTo = CreateWindow(WC_STATIC, "To", WS_VISIBLE | WS_CHILD, datesXStart + 90, datesYStart + 107, 100, 21, parentHwnd, 0, hInstance, 0);
        hwndRangeEndDate = CreateWindow(DATETIMEPICK_CLASS, "Date", WS_VISIBLE | WS_CHILD | WS_BORDER | DTS_SHORTDATECENTURYFORMAT, datesXStart + 15, datesYStart + 124, 120, 20, parentHwnd, 0, hInstance, 0);
        hwndRangeEndTime = CreateWindow(DATETIMEPICK_CLASS, "Time", WS_VISIBLE | WS_CHILD | WS_BORDER | DTS_TIMEFORMAT, datesXStart + 15, datesYStart + 144, 120, 20, parentHwnd, 0, hInstance, 0);
        hwndNowEnd = CreateWindow(WC_BUTTON, "Now", WS_VISIBLE | WS_CHILD, datesXStart + 140, datesYStart + 124, 40, 19, parentHwnd, 0, hInstance, 0);
        hwndParseEnd = CreateWindow(WC_BUTTON, "Paste", WS_VISIBLE | WS_CHILD, datesXStart + 140, datesYStart + 144, 40, 19, parentHwnd, 0, hInstance, 0);

        SyncParamsToWindow();
    }

    inline void ChangeVisibility(bool visible)
    {
        ShowWindow(hwndRangeLabelFrom, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndRangeLabelTo, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndRangeTZUTC, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndRangeTZLocal, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndRangeStartDate, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndRangeStartTime, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndNowStart, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndParseStart, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndRangeEndDate, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndRangeEndTime, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndNowEnd, visible ? SW_SHOW : SW_HIDE);
        ShowWindow(hwndParseEnd, visible ? SW_SHOW : SW_HIDE);
    }

    inline void ChangeEnabled(bool enabled)
    {
        EnableWindow(hwndRangeLabelFrom, enabled);
        EnableWindow(hwndRangeLabelTo, enabled);
        EnableWindow(hwndRangeTZUTC, enabled);
        EnableWindow(hwndRangeTZLocal, enabled);
        EnableWindow(hwndRangeStartDate, enabled);
        EnableWindow(hwndRangeStartTime, enabled);
        EnableWindow(hwndNowStart, enabled);
        EnableWindow(hwndParseStart, enabled);
        EnableWindow(hwndRangeEndDate, enabled);
        EnableWindow(hwndRangeEndTime, enabled);
        EnableWindow(hwndNowEnd, enabled);
        EnableWindow(hwndParseEnd, enabled);
    }

    inline void SyncParamsToWindow()
    {
        Button_SetCheck(hwndRangeTZLocal, !TimeModeUTC);
        Button_SetCheck(hwndRangeTZUTC, TimeModeUTC);

        std::tm tmStart = {0};
        gmtime_s(&tmStart, &TimeStart);
        SYSTEMTIME stStart = {0};
        stStart.wYear = (WORD)(tmStart.tm_year + 1900);
        stStart.wMonth = (WORD)(tmStart.tm_mon + 1);
        stStart.wDay = (WORD)(tmStart.tm_mday);
        stStart.wHour = (WORD)(tmStart.tm_hour);
        stStart.wMinute = (WORD)(tmStart.tm_min);
        stStart.wSecond = (WORD)(tmStart.tm_sec);

        SYSTEMTIME stEnd = AddSeconds(stStart, TimeEnd - TimeStart);

        SetDateTimeControlPairST(hwndRangeStartDate, hwndRangeStartTime, stStart);
        SetDateTimeControlPairST(hwndRangeEndDate, hwndRangeEndTime, stEnd);
    }

    inline void HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
            case WM_COMMAND:
            {
                if ((HWND)lParam == hwndRangeTZUTC)
                {
                    Button_SetCheck(hwndRangeTZUTC, true);
                    Button_SetCheck(hwndRangeTZLocal, false);

                    if (!TimeModeUTC)
                    {
                        SYSTEMTIME stStart = ReadDateTimeControlPairST(hwndRangeStartDate, hwndRangeStartTime);
                        SYSTEMTIME stEnd = ReadDateTimeControlPairST(hwndRangeEndDate, hwndRangeEndTime);
                        stStart = AddSeconds(stStart, -GetLocalTimeZoneOffsetMinutes() * 60);
                        stEnd = AddSeconds(stEnd, -GetLocalTimeZoneOffsetMinutes() * 60);
                        SetDateTimeControlPairST(hwndRangeStartDate, hwndRangeStartTime, stStart);
                        SetDateTimeControlPairST(hwndRangeEndDate, hwndRangeEndTime, stEnd);
                    }
                    TimeModeUTC = true;

                    ReadCurrentTimeFilterValues();
                    if (OnValueChange)
                        OnValueChange();
                }
                else if ((HWND)lParam == hwndRangeTZLocal)
                {
                    Button_SetCheck(hwndRangeTZUTC, false);
                    Button_SetCheck(hwndRangeTZLocal, true);

                    if (TimeModeUTC)
                    {
                        SYSTEMTIME stStart = ReadDateTimeControlPairST(hwndRangeStartDate, hwndRangeStartTime);
                        SYSTEMTIME stEnd = ReadDateTimeControlPairST(hwndRangeEndDate, hwndRangeEndTime);
                        stStart = AddSeconds(stStart, GetLocalTimeZoneOffsetMinutes() * 60);
                        stEnd = AddSeconds(stEnd, GetLocalTimeZoneOffsetMinutes() * 60);
                        SetDateTimeControlPairST(hwndRangeStartDate, hwndRangeStartTime, stStart);
                        SetDateTimeControlPairST(hwndRangeEndDate, hwndRangeEndTime, stEnd);
                    }
                    TimeModeUTC = false;

                    ReadCurrentTimeFilterValues();
                    if (OnValueChange)
                        OnValueChange();
                }
                else if ((HWND)lParam == hwndNowStart)
                {
                    TimeStart = time(nullptr);
                    SyncParamsToWindow();
                }
                else if ((HWND)lParam == hwndNowEnd)
                {
                    TimeEnd = time(nullptr);
                    SyncParamsToWindow();
                }
                else if ((HWND)lParam == hwndParseStart)
                {
                    time_t newValue = ParseDateFromClipboard();
                    if (newValue)
                    {
                        TimeStart = newValue;
                        SyncParamsToWindow();
                    }
                }
                else if ((HWND)lParam == hwndParseEnd)
                {
                    time_t newValue = ParseDateFromClipboard();
                    if (newValue)
                    {
                        TimeEnd = newValue;
                        SyncParamsToWindow();
                    }
                }
            }
            break;
            case WM_NOTIFY:
            {
                NMHDR *nmhdr = (NMHDR*)lParam;
                if (nmhdr->hwndFrom == hwndRangeStartDate || nmhdr->hwndFrom == hwndRangeStartTime || nmhdr->hwndFrom == hwndRangeEndDate || nmhdr->hwndFrom == hwndRangeEndTime)
                {
                    ReadCurrentTimeFilterValues();
                    if (OnValueChange)
                        OnValueChange();
                }
            }
            break;
        };
    }

private:
    HWND hwndRangeLabelFrom = 0;
    HWND hwndRangeLabelTo = 0;
    HWND hwndRangeTZUTC = 0;
    HWND hwndRangeTZLocal = 0;
    HWND hwndRangeStartDate = 0;
    HWND hwndRangeStartTime = 0;
    HWND hwndNowStart = 0;
    HWND hwndParseStart = 0;
    HWND hwndRangeEndDate = 0;
    HWND hwndRangeEndTime = 0;
    HWND hwndNowEnd = 0;
    HWND hwndParseEnd = 0;

    inline static SYSTEMTIME AddSeconds(const SYSTEMTIME &stOrig, int64_t secondsToAdd)
    {
        FILETIME ft = { 0 };
        SystemTimeToFileTime(&stOrig, &ft);
        *(int64_t*)&ft += secondsToAdd * 10000000;

        SYSTEMTIME st = stOrig;
        FileTimeToSystemTime(&ft, &st);
        return st;
    }

    inline static int GetLocalTimeZoneOffsetMinutes()
    {
        DYNAMIC_TIME_ZONE_INFORMATION tzi = { 0 };
        DWORD ret = GetDynamicTimeZoneInformation(&tzi);

        int dstBias = tzi.StandardBias;
        if (ret == TIME_ZONE_ID_DAYLIGHT)
            dstBias = tzi.DaylightBias;

        return -tzi.Bias - dstBias;
    }

    inline static SYSTEMTIME ApplyTimeZoneOffset(const SYSTEMTIME &stOrig, bool add)
    {
        return AddSeconds(stOrig, GetLocalTimeZoneOffsetMinutes() * 60 * (add ? 1 : -1));
    }

    inline SYSTEMTIME ReadDateTimeControlPairST(HWND dateControl, HWND timeControl)
    {
        SYSTEMTIME stDate = { 0 };
        DateTime_GetSystemtime(dateControl, &stDate);
        SYSTEMTIME stTime = { 0 };
        DateTime_GetSystemtime(timeControl, &stTime);

        SYSTEMTIME st = stDate;
        st.wHour = stTime.wHour;
        st.wMinute = stTime.wMinute;
        st.wSecond = stTime.wSecond;

        if (!TimeModeUTC)
            st = ApplyTimeZoneOffset(st, false);

        return st;
    }

    inline time_t ReadDateTimeControlPair(HWND dateControl, HWND timeControl)
    {
        SYSTEMTIME st = ReadDateTimeControlPairST(dateControl, timeControl);

        std::tm combinedTime = { 0 };
        combinedTime.tm_year = st.wYear - 1900;
        combinedTime.tm_mon = st.wMonth - 1;
        combinedTime.tm_mday = st.wDay;
        combinedTime.tm_hour = st.wHour;
        combinedTime.tm_min = st.wMinute;
        combinedTime.tm_sec = st.wSecond;

        time_t ret = _mkgmtime(&combinedTime);
        if (ret == -1)
            return 0;
        return ret;
    }

    inline void SetDateTimeControlPairST(HWND dateControl, HWND timeControl, const SYSTEMTIME &st)
    {
        SYSTEMTIME stWithTZ = st;

        if (!TimeModeUTC)
            stWithTZ = ApplyTimeZoneOffset(stWithTZ, true);

        DateTime_SetSystemtime(dateControl, GDT_VALID, &stWithTZ);
        DateTime_SetSystemtime(timeControl, GDT_VALID, &stWithTZ);
    }

    inline void ReadCurrentTimeFilterValues()
    {
        TimeStart = ReadDateTimeControlPair(hwndRangeStartDate, hwndRangeStartTime);
        TimeEnd = ReadDateTimeControlPair(hwndRangeEndDate, hwndRangeEndTime);
    }

    inline time_t ParseDateFromClipboard()
    {
        std::string text;
        if (OpenClipboard(hwndMain))
        {
            HANDLE clipDataHandle = GetClipboardData(CF_TEXT);
            if (clipDataHandle)
            {
                char *clipMemory = (char*)GlobalLock(clipDataHandle);
                if (clipMemory)
                {
                    text = clipMemory;
                    GlobalUnlock(clipDataHandle);
                }
            }
            CloseClipboard();
        }

        if (text.empty())
        {
            MessageBox(hwndParseStart, "No clipboard data.", "Clipboard error.", MB_OK);
            return 0;
        }

        //Text must be in a form similar to: 2021-07-06T20:16:08.9539628Z - We'll normalize whitespace then parse that, to allow variations to work
        std::transform(text.begin(), text.end(), text.begin(), [](char c){ return c == '-' || c == 'T' || c == '+' || c == ':' || c == '.' || c == '_' ? ' ' : c; });

        std::stringstream textStream;
        textStream << text;
        std::tm combinedTime = {0};
        textStream >> std::get_time(&combinedTime, "%Y%t%m%t%d%t%H%t%M%t%S");
        if (textStream.fail())
        {
            MessageBox(hwndParseStart, "Clipboard data is not the correct format.\r\nShould be: YYYY?MM?DD?HH?MM?SS[...][Z]", "Bad clipboard data.", MB_OK);
            return 0;
        }

        time_t ret = _mkgmtime(&combinedTime);
        if (ret == -1)
            return 0;

        //If Z is present at the end, this is a UTC time, else it's a local time
        if (text.back() != 'Z')
            ret -= GetLocalTimeZoneOffsetMinutes() * 60;

        return ret;
    }
};
