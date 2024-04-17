#pragma once

#include "LogParserCommon.h"
#include <vector>

namespace TRX
{
    LogCollection ParseLogs(AppStatusMonitor &monitor, const std::vector<char> &rawDataToConsume, const ParserFilter &filter);

    // Trx isn't binary, it's xml.  But xml can't be interpreted as a set of indepedant lines, so we treat it as binary blobs in order to get the whole file to parse at once.
    static ParserInterface Parser=ParserInterface::MakeBinaryParser("TRX", ParseLogs);
}
