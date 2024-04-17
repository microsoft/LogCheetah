#pragma once

#include <vector>
#include <string>
#include <Windows.h>

inline std::string Win32ListBoxGetText(HWND hwndCtl, int index)
{
    std::vector<char> buff;
    buff.resize(ListBox_GetTextLen(hwndCtl, index) + 1);
    ListBox_GetText(hwndCtl, index, buff.data());
    return buff.data();
}

inline std::string Win32ComboBoxGetText(HWND hwndCtl)
{
    std::vector<char> buff;
    buff.resize(ComboBox_GetTextLength(hwndCtl) + 1);
    ComboBox_GetText(hwndCtl, buff.data(), (int)buff.size());
    return buff.data();
}