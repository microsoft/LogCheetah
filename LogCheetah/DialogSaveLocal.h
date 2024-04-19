// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once
#include <vector>
#include <string>

void DoSaveLogsDialog(const std::string &sourceDescription, const std::vector<uint32_t> &filteredRows, const std::vector<uint32_t> &selectedRows, std::vector<uint32_t> &filteredColumns);
