// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <vector>
#include <string>
#include "SharedGlobals.h"
#include "LogParserCommon.h"
#include "DialogPickLogFormat.h"

ParserInterface& DetermineTextLogParser(const std::vector<std::string> &logs, LogType logTypeIfknown);
ParserInterface& DetermineTextLogParser(const std::vector<char> &logs, LogType logTypeIfknown);
