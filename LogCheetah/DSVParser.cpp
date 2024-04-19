// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "DSVParser.h"
#include "SharedGlobals.h"
#include "StringUtils.h"
#include <cctype>

namespace
{
    const std::vector<std::string> sortColumns { "date", "time" };

    template<typename T, typename F>
    T FindSortColumn(T begin, T end, F transform)
    {
        //exact match first
        for (auto &sc : sortColumns)
        {
            for (auto i = begin; i != end; ++i)
            {
                std::string columnNameLower = transform(i);
                TransformStringToLower(columnNameLower);
                if (columnNameLower == sc)
                {
                    return i;
                }
            }
        }

        //partial match if that failed
        for (auto &sc : sortColumns)
        {
            for (auto i = begin; i != end; ++i)
            {
                std::string columnNameLower = transform(i);
                TransformStringToLower(columnNameLower);
                if (columnNameLower.find(sc) != std::string::npos)
                {
                    return i;
                }
            }
        }

        return end;
    }

    void DoParseDSVLogsHeader(LogCollection &logs, std::vector<std::string> &allLines, char deliminator, uint64_t &nextLine)
    {
        //try parsing the header by looking for a csv-style comment
        while (nextLine < allLines.size())
        {
            std::string trimmedLine = TrimString(allLines[nextLine]);
            if (!trimmedLine.empty())
            {
                if (trimmedLine[0] != '#')
                    break;

                if (StartsWith(trimmedLine, "#Fields:"))
                {
                    size_t fieldsStart = 8;
                    while (fieldsStart < trimmedLine.size())
                    {
                        if (trimmedLine[fieldsStart] == deliminator)
                            ++fieldsStart;
                        else
                            break;
                    }

                    std::vector<std::string> pieces = StringSplit(deliminator, std::string(trimmedLine.begin() + fieldsStart, trimmedLine.end()));
                    for (auto &p : pieces)
                    {
                        //remove outer quotes if present
                        if (p.size() > 2 && p.front() == '"' && p.back() == '"')
                        {
                            p.erase(p.begin());
                            p.pop_back();
                        }

                        //trim any remaining garbage
                        p = TrimString(p);
                        StripSymbolPrefixFromString(p);

                        logs.Columns.emplace_back(p);
                    }

                    ++nextLine;
                    break;
                }
            }

            ++nextLine;
        }

        //parse the header by just using the first line if previous attempts didn't work
        if (logs.Columns.empty() && allLines.size() > 1)
        {
            logs.Columns.emplace_back();

            while (nextLine < allLines.size())
            {
                if (!TrimString(allLines[nextLine]).empty())
                {
                    for (auto c : allLines[nextLine])
                    {
                        if (c == deliminator)
                            logs.Columns.emplace_back();
                        else
                            logs.Columns.back().UniqueName.push_back(c);
                    }

                    break;
                }

                ++nextLine;
            }

            for (auto &cn : logs.Columns)
            {
                cn = TrimString(cn.UniqueName);

                //remove outer quotes if present
                if (cn.UniqueName.size() > 2 && cn.UniqueName.front() == '"' && cn.UniqueName.back() == '"')
                {
                    cn.UniqueName.erase(cn.UniqueName.begin());
                    cn.UniqueName.pop_back();
                }

                //trim any remaining garbage
                StripSymbolPrefixFromString(cn.UniqueName);
            }

            ++nextLine;
        }
    }

    void DoParseDSVLogsBody(AppStatusMonitor &monitor, LogCollection &logs, std::vector<std::string> &allLines, char deliminator, bool parseHeader, uint64_t &nextLine)
    {
        //determine which column is the special Date column, if any
        auto dateColumn = FindSortColumn(logs.Columns.begin(), logs.Columns.end(), [](auto ci) {return ci->UniqueName; });

        if (dateColumn != logs.Columns.end())
            logs.SortColumn = (uint16_t)(dateColumn - logs.Columns.begin());

        //prevent duplicate column names and handle empty column names
        for (size_t o = 0; o < logs.Columns.size(); ++o)
        {
            if (logs.Columns[o].UniqueName.empty())
                logs.Columns[o].UniqueName = "[blank]";

            size_t nextDupeColumnNumber = 2;
            std::string origName = logs.Columns[o].UniqueName;

            bool hadDupe = true;
            while (hadDupe)
            {
                hadDupe = false;
                for (size_t i = 0; i < o; ++i)
                {
                    if (logs.Columns[i].UniqueName == logs.Columns[o].UniqueName)
                    {
                        hadDupe = true;
                        do
                        {
                            logs.Columns[o].UniqueName = origName + "[" + std::to_string(nextDupeColumnNumber) + "]";
                            ++nextDupeColumnNumber;
                        } while (logs.Columns[i].UniqueName == logs.Columns[o].UniqueName);
                    }
                }
            }
        }

        //parse the lines
        if (nextLine < allLines.size())
            logs.Lines.reserve(allLines.size() - nextLine);

        int dummyColumnMax = 0;
        std::vector<LogEntryColumn> columnDataOrig;

        for (; nextLine < allLines.size(); ++nextLine)
        {
            if (monitor.IsCancelling())
                break;

            monitor.AddProgress(1);

            std::string &rawString = allLines[nextLine];
            if (!rawString.empty() && rawString[0] == '#')
            {
                //if we hit a comment and we are parsing headers completely bail, otherwise just skip the comment
                if (parseHeader)
                    break;
                else
                    continue;
            }

            bool parseFailed = false;
            uint16_t colIndex = 0;
            bool inQuotedField = false;
            bool doneWithColumnData = false;
            bool allowQuotedField = true;

            columnDataOrig.clear();
            columnDataOrig.emplace_back();
            columnDataOrig.back().ColumnNumber = colIndex;
            columnDataOrig.back().IndexDataBegin = 0;
            columnDataOrig.back().IndexDataEnd = 0;

            for (int i = 0; i < rawString.size(); ++i)
            {
                char c = rawString[i];

                bool changeColumn = false;
                if (c == '"')
                {
                    if (allowQuotedField && !inQuotedField)
                    {
                        inQuotedField = true;
                        columnDataOrig.back().IndexDataBegin = i + 1;
                        columnDataOrig.back().IndexDataEnd = i + 1;
                    }
                    else if (inQuotedField)
                    {
                        if (i + 1 < rawString.size() && rawString[i + 1] == '"') //walk past escape sequence
                            ++i;
                        else
                        {
                            inQuotedField = false;
                            doneWithColumnData = true;
                        }
                    }
                }
                else if (!inQuotedField && c == deliminator)
                    changeColumn = true;
                else if (!doneWithColumnData)
                {
                    allowQuotedField = false;
                    columnDataOrig.back().IndexDataEnd = i + 1;
                }

                if (changeColumn)
                {
                    if (columnDataOrig.back().IndexDataBegin == columnDataOrig.back().IndexDataEnd) //discard empty columns
                        columnDataOrig.pop_back();

                    columnDataOrig.emplace_back();

                    if (columnDataOrig.size() > logs.Columns.size())
                    {
                        if (columnDataOrig.size() > dummyColumnMax)
                            dummyColumnMax = (int)columnDataOrig.size();
                    }

                    doneWithColumnData = false;
                    allowQuotedField = true;
                    ++colIndex;

                    columnDataOrig.back().ColumnNumber = colIndex;
                    columnDataOrig.back().IndexDataBegin = i + 1;
                    columnDataOrig.back().IndexDataEnd = i + 1;
                }

                if (i >= MaxLogEntryDataIndex - 2)
                {
                    parseFailed = true;
                    monitor.AddDebugOutput("DSVParser: Line is too long to store and has been truncated.");
                    break;
                }
            }

            if (!logs.Columns.empty() && colIndex + 1 != logs.Columns.size())
                parseFailed = true;

            if (dummyColumnMax < colIndex)
                dummyColumnMax = colIndex;

            if (!columnDataOrig.empty() && columnDataOrig.back().IndexDataBegin == columnDataOrig.back().IndexDataEnd) //discard empty columns
                columnDataOrig.pop_back();

            logs.Lines.emplace_back(rawString, std::string(), columnDataOrig, std::vector<LogEntryColumn>());
            logs.Lines.back().ParseFailed = parseFailed;
            rawString = std::string(); //free old memory now to reduce max memory usage during parsing
        }

        //it's possible for invalid files to have more columns in the data than the header declared, so add dummy columns for those
        for (int i = (int)logs.Columns.size(); i <= dummyColumnMax; ++i)
        {
            std::string unknownName("UNKNOWN_");
            unknownName.append(std::to_string(i));
            logs.Columns.emplace_back(unknownName);
        }
    }

    //This will optionally read the header, then read CSV data until a comment is hit, at which point it will return with nextLine pointing to the start of that comment
    LogCollection DoParseDSVLogs(AppStatusMonitor &monitor, std::vector<std::string> &allLines, char deliminator, bool parseHeader, uint64_t &nextLine)
    {
        LogCollection logs;
        logs.IsRawRepresentationValid = true;

        if (allLines.empty())
            return std::move(logs);

        if (parseHeader)
            DoParseDSVLogsHeader(logs, allLines, deliminator, nextLine);

        DoParseDSVLogsBody(monitor, logs, allLines, deliminator, parseHeader, nextLine);

        if (monitor.IsCancelling())
        {
            logs.Lines.clear();
            logs.Columns.clear();
        }

        return std::move(logs);
    }

    bool ParseDateString(const ExternalSubstring<const char> &dateString, int &outYear, int &outMonth, int &outDay, int &outHour, int &outMinute, int &outSecond, int &outMilliseconds)
    {
        char *endChar = nullptr;

        //month
        size_t cur = 0;
        size_t next = dateString.find('/');
        if (next == std::string::npos)
            return false;
        outMonth = strtol(dateString.begin() + cur, &endChar, 10);

        //day
        cur = next + 1;
        next = dateString.find('/', cur);
        if (next == std::string::npos)
            return false;
        outDay = strtol(dateString.begin() + cur, &endChar, 10);

        //year
        cur = next + 1;
        next = dateString.find(' ', cur);
        if (next == std::string::npos)
            return false;
        outYear = strtol(dateString.begin() + cur, &endChar, 10);

        //hour
        cur = next + 1;
        next = dateString.find(':', cur);
        if (next == std::string::npos)
            return false;
        outHour = strtol(dateString.begin() + cur, &endChar, 10);

        //minute
        cur = next + 1;
        next = dateString.find(':', cur);
        if (next == std::string::npos)
            return false;
        outMinute = strtol(dateString.begin() + cur, &endChar, 10);

        //second
        cur = next + 1;
        next = dateString.find('.', cur);
        if (next == std::string::npos) //no milliseconds present, so done
        {
            outSecond = strtol(dateString.begin() + cur, &endChar, 10);
            outMilliseconds = 0;
            return true;
        }
        else
            outSecond = strtol(dateString.begin() + cur, &endChar, 10);

        //millisecond
        cur = next + 1;
        next = dateString.size();
        if (next - cur <= 0)
            return false;
        outMilliseconds = strtol(dateString.begin() + cur, &endChar, 10);

        return true;
    }

    time_t ParseTimeFromDateString(const ExternalSubstring<const char> &date, int &outMilliseconds)
    {
        std::tm outTime = { 0 };
        if (ParseDateString(date, outTime.tm_year, outTime.tm_mon, outTime.tm_mday, outTime.tm_hour, outTime.tm_min, outTime.tm_sec, outMilliseconds))
        {
            --outTime.tm_mon;
            outTime.tm_year -= 1900;

            time_t ret = _mkgmtime(&outTime);
            if (ret == -1)
                return 0;
            return ret;
        }

        return 0;
    }

    time_t ParseTimeFromLine(const ExternalSubstring<const char> &line, int &outMilliseconds)
    {
        size_t start = line.find(',');
        if (start == std::string::npos)
            return 0;

        ++start;
        size_t end = line.find(',', start);
        if (end == std::string::npos)
            return 0;

        return ParseTimeFromDateString(ExternalSubstring<const char>(line.begin() + start, line.begin() + end), outMilliseconds);
    }

    time_t ParseTimeFromLine(const ExternalSubstring<const char> &line)
    {
        int ms;
        return ParseTimeFromLine(line, ms);
    }
}

namespace DSV
{
    void FilterLines(std::vector<LogEntry> &lines, const std::vector<ColumnInformation> columns, const ParserFilter &filter)
    {
        auto dateColumnInfoIter = FindSortColumn(columns.begin(), columns.end(), [](auto ci) {return ci->UniqueName; });
        size_t dateColumn = std::string::npos;
        if (dateColumnInfoIter != columns.end())
            dateColumn = dateColumnInfoIter - columns.begin();

        for (auto &line : lines)
        {
            if (!filter.PassesLineFilters(ExternalSubstring<const char>(line.OriginalLogBegin(), line.OriginalLogEnd())))
            {
                line.Clear();
                continue;
            }

            if (dateColumn != std::string::npos && (filter.MinTime != 0 || filter.MaxTime != 0))
            {
                time_t lineTime = ParseTimeFromLine(line.GetColumnNumberValue((uint16_t)dateColumn));
                if (lineTime != 0 && (lineTime<filter.MinTime || lineTime>filter.MaxTime))
                {
                    line.Clear();
                    continue;
                }
            }
        }
    }

    LogCollection ParseDSVLogs(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume, char deliminator, bool parseHeader)
    {
        LogCollection logs;
        logs.IsRawRepresentationValid = true;

        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(linesToConsume.size(), "kilolines", 1000);

        //Run the parser multiple times.  DoParseDSVLogs will return each time a new comment is hit and we will merge in the new data.
        uint64_t nextLine = 0;
        while (nextLine < linesToConsume.size() && !monitor.IsCancelling())
        {
            LogCollection partialLogs = DoParseDSVLogs(monitor, linesToConsume, deliminator, parseHeader, nextLine);
            logs.MoveAndMergeInLogs(AppStatusMonitor::Instance, std::move(partialLogs), false, false);
        }

        //
        if (monitor.IsCancelling())
        {
            logs.Lines.clear();
            logs.Columns.clear();
        }

        linesToConsume = std::vector<std::string>(); //free old logs
        monitor.Complete();
        return std::move(logs);
    }
}
