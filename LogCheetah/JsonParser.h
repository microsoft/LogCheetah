#pragma once

#include <string>
#include <vector>
#include "LogParserCommon.h"

namespace JSON
{
    bool FilterLine(const ExternalSubstring<const char> &line, const ParserFilter &filter);
    LogCollection ParseLogs(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume, bool allowNestedJson);
    void LoadSchemaData(AppStatusMonitor &monitor, const std::string &blob);
    std::string SaveSchemaData();

    static ParserInterface NormalParser = ParserInterface::MakePreFilterTextParser("JSON", FilterLine, [](AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume){ return ParseLogs(monitor, std::move(linesToConsume), false); }, ParserInterface::NoopPreloadKnownSchemas, LoadSchemaData, SaveSchemaData, true, false);
    static ParserInterface NestedParser = ParserInterface::MakePreFilterTextParser("JSON", FilterLine, [](AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume){ return ParseLogs(monitor, std::move(linesToConsume), true); }, ParserInterface::NoopPreloadKnownSchemas, LoadSchemaData, SaveSchemaData, true, true);
}
