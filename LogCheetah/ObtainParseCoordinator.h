#pragma once
#include <functional>
#include <vector>
#include <string>
#include "SharedGlobals.h"
#include "DialogPickLogFormat.h"
#include "LogParserCommon.h"

struct ObtainerSource
{
    std::function<std::vector<char>(AppStatusMonitor &monitor)> Obtain;
    LogType LogTypeIfKnown = LogType::Unknown;
    std::string AdditionalSchemaData;
};

LogCollection ObtainRawDataAndParse(const std::string &obtainDescription, std::vector<ObtainerSource> obtainers, size_t maxObtainParallelism, const ParserFilter &filter);
