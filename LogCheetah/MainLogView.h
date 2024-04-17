#pragma once

#include "LogParserCommon.h"

#include <vector>
#include <string>
#include <Windows.h>

//F#$%ing windows.h macros
#undef min
#undef max

//we need some weird hooks into the windows message processing loop
void LogViewPreprocessMessage(const MSG &msg);
bool LogViewRequiresSpecificWindowTranslated(HWND hwnd);

//setup and main window hookup
bool LogViewInitialize();

//called when log source data has changed, so views can be updated
void LogViewNotifyDataChanged(size_t beginRow, size_t endRow, size_t beginColumn, size_t endColumn);

//closes all log view windows
void LogViewCloseAllWindows();

//similar to WinMain's
void LogViewWindowLockoutInteraction(bool interactionEnabled);

//status part of the title text to show for a window
std::string LogViewGetWindowStatusText(HWND window);

void OpenNewFilteredMainLogView(std::vector<LogFilterEntry> filters);
