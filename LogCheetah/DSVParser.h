// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <string>
#include <vector>
#include "LogParserCommon.h"
#include "SharedGlobals.h"

namespace DSV
{
    void FilterLines(std::vector<LogEntry> &lines, const std::vector<ColumnInformation> columns, const ParserFilter &filter);
    LogCollection ParseDSVLogs(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume, char deliminator, bool parseHeader = true);

    //specializations
    inline LogCollection ParseLogsPSV(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume)
    {
        return ParseDSVLogs(monitor, std::move(linesToConsume), '|');
    }

    inline LogCollection ParseLogsTSV(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume)
    {
        return ParseDSVLogs(monitor, std::move(linesToConsume), '\t');
    }

    inline LogCollection ParseLogsCSV(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume)
    {
        return ParseDSVLogs(monitor, std::move(linesToConsume), ',');
    }

    inline LogCollection ParseLogsSSV(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume)
    {
        return ParseDSVLogs(monitor, std::move(linesToConsume), ' ');
    }

    static ParserInterface ParserPSV = ParserInterface::MakePostFilterTextParser("DSV PSV", FilterLines, ParseLogsPSV, ParserInterface::NoopPreloadKnownSchemas, ParserInterface::NoopLoadSchemaData, ParserInterface::NoopSaveSchemaData);
    static ParserInterface ParserTSV = ParserInterface::MakePostFilterTextParser("DSV TSV", FilterLines, ParseLogsTSV, ParserInterface::NoopPreloadKnownSchemas, ParserInterface::NoopLoadSchemaData, ParserInterface::NoopSaveSchemaData);
    static ParserInterface ParserCSV = ParserInterface::MakePostFilterTextParser("DSV CSV", FilterLines, ParseLogsCSV, ParserInterface::NoopPreloadKnownSchemas, ParserInterface::NoopLoadSchemaData, ParserInterface::NoopSaveSchemaData);
    static ParserInterface ParserSSV = ParserInterface::MakePostFilterTextParser("DSV SSV", FilterLines, ParseLogsSSV, ParserInterface::NoopPreloadKnownSchemas, ParserInterface::NoopLoadSchemaData, ParserInterface::NoopSaveSchemaData);
}
