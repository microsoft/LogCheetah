// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <functional>
#include <vector>
#include <string>
#include <Windows.h>

//must be called after the message loop has started
void SetupDragDropForWindow(HWND hwnd, std::function<void(const std::vector<std::string>&)> callbackResults, std::function<bool()> callbackBusyCheck);
