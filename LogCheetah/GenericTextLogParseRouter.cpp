// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "GenericTextLogParseRouter.h"
#include "GuiStatusMonitor.h"
#include "LogFormatter.h"
#include "JsonParser.h"
#include "DSVParser.h"
#include "TRXParser.h"
#include "DebugWindow.h"

namespace
{
    LogType DetermineType(const std::vector<std::string> &logs)
    {
        LogType type = LogType::Unknown;

        for (auto linei = logs.begin(); linei != logs.end(); ++linei)
        {
            //pick based on first delimiter
            bool isCommentStart = false;
            do
            {
                isCommentStart = false;

                for (auto c : *linei)
                {
                    if (c == '#') //skip comment lines
                    {
                        //unless it starts with a special token, which means IIS ssv logs
                        if (linei->find("#Software:") == 0)
                        {
                            type = LogType::SSV;
                            break;
                        }

                        ++linei;
                        isCommentStart = true;
                        break;
                    }
                    if (c == '|')
                    {
                        type = LogType::PSV;
                        break;
                    }
                    else if (c == ',')
                    {
                        type = LogType::CSV;
                        break;
                    }
                    else if (c == '\t')
                    {
                        type = LogType::TSV;
                        break;
                    }
                    else if (c == '{')
                    {
                        type = LogType::Json;

                        if (linei->find("\"data\":\"{", 0) != std::string::npos || linei->find("ext.sll.level") != std::string::npos)
                            type = LogType::NestedJson;
                        break;
                    }
                    else if (c == '<')
                    {
                        type = LogType::TRX;
                    }
                }
            } while (isCommentStart && linei < logs.end());

            if (linei >= logs.end())
                break;

            if (type != LogType::Unknown)
                break;
        }

        if (type == LogType::Unknown)
        {
            GlobalDebugOutput("Unable to determine log format.  Assuming CSV.");
            type = LogType::CSV;
        }

        return type;
    }
}

ParserInterface& DetermineTextLogParser(const std::vector<std::string> &logs, LogType logTypeIfknown)
{
    LogType type = logTypeIfknown;
    if (type == LogType::Unknown)
        type = DetermineType(logs);

    if (type == LogType::PSV)
        return DSV::ParserPSV;
    else if (type == LogType::CSV)
        return DSV::ParserCSV;
    else if (type == LogType::TSV)
        return DSV::ParserTSV;
    else if (type == LogType::SSV)
        return DSV::ParserSSV;
    else if (type == LogType::Json)
        return JSON::NormalParser;
    else if (type == LogType::NestedJson)
        return JSON::NestedParser;
    else if (type == LogType::SSV)
        return DSV::ParserSSV;
    else if (type == LogType::TRX)
        return TRX::Parser;
    else
        return DSV::ParserCSV;
}

ParserInterface& DetermineTextLogParser(const std::vector<char> &logs, LogType logTypeIfknown)
{
    //just parse out a 5 line sample and use that
    std::vector<std::string> sample;
    sample.emplace_back();
    auto cur = logs.begin();
    while (sample.size() <= 5 && cur < logs.end())
    {
        if (*cur == '\r')
            ; //ignore it
        else if (*cur == '\n')
        {
            if (sample.size() == 5)
                break;

            sample.emplace_back();
        }
        else
            sample.back().push_back(*cur);

        ++cur;
    }

    return DetermineTextLogParser(sample, logTypeIfknown);
}
