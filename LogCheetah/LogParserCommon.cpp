// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "LogParserCommon.h"
#include "SharedGlobals.h"
#include <atomic>
#include <thread>
#include <cctype>
#include <cassert>

namespace
{
    bool DoesStringMatchFilter(const ExternalSubstring<const char> &str, const LogFilterEntry &filter)
    {
        if (str.empty() && filter.Value.empty())
        {
            bool match = !filter.Not;
            if (!match)
                return false;
        }
        else
        {
            bool compareResult;
            if (filter.MatchSubstring)
            {
                if (filter.MatchCase)
                    compareResult = (str.find(filter.Value) != std::string::npos);
                else
                    compareResult = (std::search(str.begin(), str.end(), filter.Value.begin(), filter.Value.end(), [](char c0, char c1) {return std::toupper(c0) == std::toupper(c1); }) != str.end());
            }
            else
            {
                if (filter.MatchCase)
                    compareResult = (str == filter.Value);
                else
                    compareResult = str.CaseInsensitiveCompare(filter.Value);
            }

            if (filter.Not)
                compareResult = !compareResult;

            if (!compareResult)
                return false;
        }

        return true;
    }

    //find the lowest log index within haystack that is above the lowest low within needles, based on date (or whatever column is the sort priority)
    size_t FindMinDateOverlapIndex(const LogCollection &haystack, const LogCollection &needles, uint16_t sortColumn, bool sortAscending)
    {
        if (needles.Lines.empty() || haystack.Lines.empty())
            return haystack.Lines.size();

        auto lowestNeedleIter = std::min_element(needles.Lines.begin(), needles.Lines.end(), [sortColumn, sortAscending](const LogEntry &a, const LogEntry &b) { return a.Compare(b, sortColumn, sortAscending); });

        size_t ind = haystack.Lines.size() - 1;
        while (ind > 0)
        {
            if (haystack.Lines[ind].Compare(*lowestNeedleIter, sortColumn, sortAscending))
                break;

            --ind;
        }

        return ind;
    }
}

bool DoesLogEntryPassFilters(const LogEntry &entry, const std::vector<LogFilterEntry> &filters)
{
    for (auto &f : filters)
    {
        ExternalSubstring<const char> entryString;
        if (f.Column < 0) //raw line - check both original log data and extra data
        {
            entryString = ExternalSubstring<const char>(entry.OriginalLogBegin(), entry.OriginalLogEnd());
            bool match = DoesStringMatchFilter(entryString, f);
            if (f.Not && !match)
                return false;

            if (!match)
            {
                entryString = ExternalSubstring<const char>(entry.ExtraDataBegin(), entry.ExtraDataEnd());
                match = DoesStringMatchFilter(entryString, f);
            }

            if (!match)
                return false;
        }
        else
        {
            entryString = entry.GetColumnNumberValue((uint16_t)f.Column);
            if (!DoesStringMatchFilter(entryString, f))
                return false;
        }
    }

    return true;
}

std::tuple<bool, int64_t> FindNextLogline(int64_t initialPosition, int direction, const std::vector<LogFilterEntry> &filters, const LogCollection &logs, const std::vector<uint32_t> &rowVisibilityMap)
{
    int64_t bestFound = -1;
    if (!(initialPosition < 0 || initialPosition >= (int64_t)rowVisibilityMap.size()))
    {
        std::atomic<int64_t> nextThreadStart = initialPosition;
        std::vector<int64_t> threadResults;
        threadResults.resize(cpuCountFilter, -1);
        std::atomic<bool> anyFound = false;

        std::vector<std::thread> threads;
        threads.reserve(cpuCountFilter);
        for (int cpu = 0; cpu < cpuCountFilter; ++cpu)
        {
            threads.emplace_back([&](int threadIndex)
            {
                while (!anyFound)
                {
                    int64_t currentThreadBlockStart = nextThreadStart.fetch_add(direction * 100);
                    int64_t currentThreadBlockEnd = currentThreadBlockStart + direction * 100;
                    currentThreadBlockEnd = std::clamp(currentThreadBlockEnd, -1ll, (int64_t)rowVisibilityMap.size());

                    if (currentThreadBlockStart < 0 || currentThreadBlockStart >= (int64_t)rowVisibilityMap.size())
                        break;

                    for (int64_t visibleRow = currentThreadBlockStart; visibleRow != currentThreadBlockEnd; visibleRow += direction)
                    {
                        uint32_t dataRow = rowVisibilityMap[visibleRow];
                        const LogEntry &le = logs.Lines[dataRow];

                        if (DoesLogEntryPassFilters(le, filters))
                        {
                            anyFound = true;
                            threadResults[threadIndex] = visibleRow;
                            break;
                        }
                    }
                }
            }, cpu);
        }

        for (auto &t : threads)
            t.join();

        for (int64_t f : threadResults)
        {
            if (bestFound == -1 || (f != -1 && f < bestFound))
                bestFound = f;
        }
    }

    if (bestFound < 0)
        return { false, -1 };
    else
        return { true, bestFound };
}

void LogEntry::Set(const std::string &originalLog, const std::string &extraData, const std::vector<LogEntryColumn> &originalLogColumns, const std::vector<LogEntryColumn> &extraDataColumns)
{
    rawData.resize((extraDataColumns.size() + originalLogColumns.size()) * sizeof(LogEntryColumn) + extraData.size() + originalLog.size());
    columnDataEnd = (uint32_t)((extraDataColumns.size() + originalLogColumns.size()) * sizeof(LogEntryColumn));
    extraDataEnd = (uint32_t)(columnDataEnd + extraData.size());

    //merge, adjust offsets, and sort column data
    LogEntryColumn *lecBegin = ColumnDataBegin();
    LogEntryColumn *lecCur = lecBegin;

    for (auto &l : originalLogColumns)
    {
        *lecCur = l;

        if (lecCur->IndexDataBegin > lecCur->IndexDataEnd) //error in the parser, should never happen...
        {
            lecCur->IndexDataBegin = 0;
            lecCur->IndexDataEnd = 0;
            ParseFailed = true;
        }

        uint64_t updatedBegin = (uint64_t)lecCur->IndexDataBegin + (uint64_t)originalLogBegin;
        uint64_t updatedEnd = (uint64_t)lecCur->IndexDataEnd + (uint64_t)originalLogBegin;
        if (updatedEnd < MaxLogEntryDataIndex + 1)
        {
            lecCur->IndexDataBegin = (uint32_t)updatedBegin;
            lecCur->IndexDataEnd = (uint32_t)updatedEnd;
        }
        else
        {
            //no way to report an error here.. label it as failed and truncate
            lecCur->IndexDataBegin = (uint32_t)(updatedBegin < MaxLogEntryDataIndex + 1 ? updatedBegin : MaxLogEntryDataIndex);
            lecCur->IndexDataEnd = MaxLogEntryDataIndex;
            ParseFailed = true;
        }

        ++lecCur;
    }

    for (auto &l : extraDataColumns)
    {
        *lecCur = l;

        if (lecCur->IndexDataBegin > lecCur->IndexDataEnd) //error in the parser, should never happen...
        {
            lecCur->IndexDataBegin = 0;
            lecCur->IndexDataEnd = 0;
            ParseFailed = true;
        }

        uint64_t updatedBegin = (uint64_t)lecCur->IndexDataBegin + (uint64_t)extraDataBegin;
        uint64_t updatedEnd = (uint64_t)lecCur->IndexDataEnd + (uint64_t)extraDataBegin;
        if (updatedEnd < MaxLogEntryDataIndex + 1)
        {
            lecCur->IndexDataBegin = (uint32_t)updatedBegin;
            lecCur->IndexDataEnd = (uint32_t)updatedEnd;
        }
        else
        {
            //no way to report an error here.. label it as failed and truncate
            lecCur->IndexDataBegin = (uint32_t)(updatedBegin < MaxLogEntryDataIndex + 1 ? updatedBegin : MaxLogEntryDataIndex);
            lecCur->IndexDataEnd = MaxLogEntryDataIndex;
            ParseFailed = true;
        }

        ++lecCur;
    }

    std::sort(lecBegin, lecCur, [](const LogEntryColumn &a, const LogEntryColumn &b) { return a.ColumnNumber < b.ColumnNumber; });

    //merge extra data
    char *destData = ExtraDataBegin();
    for (auto c : extraData)
    {
        *destData = c;
        ++destData;
    }

    //merge original data
    destData = OriginalLogBegin();
    for (auto c : originalLog)
    {
        *destData = c;
        ++destData;
    }
}

void LogEntry::Clear()
{
    rawData.clear();
    columnDataEnd = 0;
    extraDataEnd = 0;
    ParseFailed = false;
    Tagged = false;
}

void LogEntry::AppendExtra(const std::string &extraData, const std::vector<LogEntryColumn> &extraDataColumns)
{
    std::vector<char> newData;
    newData.resize(rawData.size() + extraData.size() + extraDataColumns.size() * sizeof(LogEntryColumn));

    uint32_t newColumnDataEnd = (uint32_t)(columnDataEnd + extraDataColumns.size() * sizeof(LogEntryColumn));
    uint32_t newExtraDataEnd = (uint32_t)(newColumnDataEnd + extraData.size());

    //copy old column data and append new while adjusting offsets, then resort
    LogEntryColumn *newCol = (LogEntryColumn*)newData.data();
    LogEntryColumn *oldCol = ColumnDataBegin();
    while (oldCol != ColumnDataEnd())
    {
        *newCol = *oldCol;

        bool isInOriginalSection = false;
        if (oldCol->IndexDataBegin >= originalLogBegin && oldCol->IndexDataBegin < originalLogEnd())
            isInOriginalSection = true;

        if (isInOriginalSection)
        {
            newCol->IndexDataBegin += (uint32_t)(extraDataColumns.size() * sizeof(LogEntryColumn) + extraData.size());
            newCol->IndexDataEnd += (uint32_t)(extraDataColumns.size() * sizeof(LogEntryColumn) + extraData.size());
        }
        else //extra data section
        {
            newCol->IndexDataBegin += (uint32_t)(extraDataColumns.size() * sizeof(LogEntryColumn));
            newCol->IndexDataEnd += (uint32_t)(extraDataColumns.size() * sizeof(LogEntryColumn));
        }

        ++oldCol;
        ++newCol;
    }

    for (auto &lec : extraDataColumns)
    {
        *newCol = lec;
        newCol->IndexDataBegin += newColumnDataEnd + (extraDataEnd - extraDataBegin);
        newCol->IndexDataEnd += newColumnDataEnd + (extraDataEnd - extraDataBegin);
        ++newCol;
    }

    std::sort((LogEntryColumn*)newData.data(), newCol, [](const LogEntryColumn &a, const LogEntryColumn &b) { return a.ColumnNumber < b.ColumnNumber; });

    //copy old extra data over, and append new
    char *newDest = (char*)newCol;
    for (auto c = ExtraDataBegin(); c != ExtraDataEnd(); ++c)
    {
        *newDest = *c;
        ++newDest;
    }

    for (auto c : extraData)
    {
        *newDest = c;
        ++newDest;
    }

    //copy old original data over
    for (auto c = OriginalLogBegin(); c != OriginalLogEnd(); ++c)
    {
        *newDest = *c;
        ++newDest;
    }

    //move new data in on top of old
    rawData = std::move(newData);
    columnDataEnd = newColumnDataEnd;
    extraDataEnd = newExtraDataEnd;
}

void LogCollection::MoveAndMergeInLogs(AppStatusMonitor &monitor, LogCollection &&other, bool filterDuplicateLogs, bool resortLogs)
{
    size_t unused0, unused1;
    MoveAndMergeInLogs(monitor, std::move(other), filterDuplicateLogs, resortLogs, unused0, unused1);
}

void LogCollection::MoveAndMergeInLogs(AppStatusMonitor &monitor, LogCollection &&other, bool filterDuplicateLogs, bool resortLogs, size_t &outBeginRowAffected, size_t &outEndRowAffected)
{
    //debug code to isolate issues with parsers that generated bad row data
#if _DEBUG
    for (size_t i = 0; i < other.Lines.size(); ++i)
    {
        for (auto cvIter = other.Lines[i].ColumnDataBegin(); cvIter != other.Lines[i].ColumnDataEnd(); ++cvIter)
            assert(cvIter->ColumnNumber < other.Columns.size());
    }
#endif

    outBeginRowAffected = outEndRowAffected = 0;

    monitor.SetControlFeatures(true);
    monitor.SetProgressFeatures(other.Lines.size(), "kiloline", 1000);

    auto tpBegin = std::chrono::high_resolution_clock::now();

    //if the destination is empty then take the sources supplemental data
    if (Lines.empty())
    {
        SortColumn = other.SortColumn;
        SortAscending = other.SortAscending;

        Parser = std::move(other.Parser);
    }
    else if (Parser != other.Parser) //different parsers were used, so just clear this optional field
        Parser = nullptr;

    IsRawRepresentationValid = IsRawRepresentationValid && other.IsRawRepresentationValid;

    //merge in the columns and mapping columns in the other set to ours
    std::vector<int> otherColumnToExistingColumnMapping;
    for (size_t otherColumnIndex = 0; otherColumnIndex != other.Columns.size(); ++otherColumnIndex)
    {
        const ColumnInformation &otherColumn = other.Columns[otherColumnIndex];

        int existing = -1;
        for (int c = 0; c < (int)Columns.size(); ++c)
        {
            if (otherColumn.UniqueName == Columns[c].UniqueName)
            {
                existing = c;
                break;
            }
        }

        if (existing == -1)
        {
            Columns.emplace_back(otherColumn);
            existing = (int)Columns.size() - 1;
        }
        else
        {
            if (Columns[existing].Description.empty() && !otherColumn.Description.empty())
                Columns[existing].Description = otherColumn.Description;
        }

        otherColumnToExistingColumnMapping.emplace_back(existing);
    }

    //deduplicate if needed
    size_t minIndexToAlter = 0;
    if (resortLogs || filterDuplicateLogs)
        minIndexToAlter = FindMinDateOverlapIndex(*this, other, SortColumn, SortAscending);
    outBeginRowAffected = minIndexToAlter;

    std::vector<LogEntry> lines = std::move(other.Lines);

    if (filterDuplicateLogs)
    {
        std::vector<LogEntry> uniqueLines;
        for (auto &potential : lines)
        {
            bool match = false;
            for (size_t ind = minIndexToAlter; ind < Lines.size(); ++ind)
            {
                if (ExternalSubstring<const char>(potential.OriginalLogBegin(), potential.OriginalLogEnd()) == ExternalSubstring<const char>(Lines[ind].OriginalLogBegin(), Lines[ind].OriginalLogEnd())
                    && ExternalSubstring<const char>(potential.ExtraDataBegin(), potential.ExtraDataEnd()) == ExternalSubstring<const char>(Lines[ind].ExtraDataBegin(), Lines[ind].ExtraDataEnd()))
                {
                    match = true;
                    break;
                }
            }

            if (!match)
                uniqueLines.emplace_back(std::move(potential));
        }

        lines = std::move(uniqueLines);
    }

    //merge in the lines and remap their column indices
    for (auto &sourceEntry : lines)
    {
        Lines.emplace_back(std::move(sourceEntry));
        auto &destEntry = Lines.back();

        for (auto cvIter = destEntry.ColumnDataBegin(); cvIter != destEntry.ColumnDataEnd(); ++cvIter)
        {
            LogEntryColumn &cv = *cvIter;
            cv.ColumnNumber = (uint16_t)otherColumnToExistingColumnMapping[cv.ColumnNumber];
        }

        if (!destEntry.IsEmpty() && destEntry.ColumnCount() > 0)
            std::sort(destEntry.ColumnDataBegin(), destEntry.ColumnDataEnd());

        monitor.AddProgress(1);
    }

    if (resortLogs)
    {
        SortRange(minIndexToAlter, Lines.size());
    }

    outEndRowAffected = Lines.size();
    monitor.Complete();

    auto tpEnd = std::chrono::high_resolution_clock::now();
    monitor.AddDebugOutputTime("MoveAndMergeInLogs", std::chrono::duration_cast<std::chrono::microseconds>(tpEnd - tpBegin).count() / 1000.0);
}

void LogCollection::SortRange(size_t lineStart, size_t lineEnd)
{
    //tiny case
    if (lineEnd - lineStart < cpuCountSort)
    {
        std::stable_sort(Lines.begin() + lineStart, Lines.begin() + lineEnd, [this](const LogEntry &a, const LogEntry &b) { return a.Compare(b, SortColumn, SortAscending); });
        return;
    }

    //determine the ranges to divide among the CPUs
    std::vector<std::pair<size_t, size_t>> ranges;
    for (int cpu = 0; cpu < cpuCountSort; ++cpu)
    {
        ranges.emplace_back();
        ranges.back().first = lineStart + (lineEnd - lineStart) / cpuCountSort * cpu;
        ranges.back().second = ranges.back().first + (lineEnd - lineStart) / cpuCountSort;
        if (cpu == cpuCountSort - 1)
            ranges.back().second = lineEnd;
    }

    //sort chunks seperately
    if (lineEnd - lineStart > cpuCountSort)
    {
        std::vector<std::thread> threads;
        threads.reserve(cpuCountSort);
        for (int cpu = 0; cpu < cpuCountSort; ++cpu)
        {
            threads.emplace_back([&](int threadIndex)
            {
                size_t myStart = ranges[threadIndex].first;
                size_t myEnd = ranges[threadIndex].second;
                std::stable_sort(Lines.begin() + myStart, Lines.begin() + myEnd, [this](const LogEntry &a, const LogEntry &b) { return a.Compare(b, SortColumn, SortAscending); });
            }, cpu);
        }

        for (auto &t : threads)
            t.join();
    }

    //merge sorted chunks together
    for (int i = 1; i < cpuCountSort; ++i)
        std::inplace_merge(Lines.begin(), Lines.begin() + ranges[i].first, Lines.begin() + ranges[i].second, [this](const LogEntry &a, const LogEntry &b) { return a.Compare(b, SortColumn, SortAscending); });
}

bool ParserFilter::PassesLineFilters(const ExternalSubstring<const char> &line) const
{
    for (auto &pf : LineFilters)
    {
        bool lineMatch;
        if (pf.MatchCase)
            lineMatch = (line.find(pf.Value) != std::string::npos);
        else
            lineMatch = (std::search(line.begin(), line.end(), pf.Value.begin(), pf.Value.end(), [](char c0, char c1) {return std::toupper(c0) == std::toupper(c1); }) != line.end());

        if (pf.Not)
            lineMatch = !lineMatch;

        if (!lineMatch)
            return false;
    }

    return true;
}

LogCollection ParserInterface::ProcessRawData(AppStatusMonitor &monitorLineParse, AppStatusMonitor &monitorLogParser, AppStatusMonitor &monitorMergeCompact, LogCollection &&existingLogsToMerge, std::vector<char> &&rawDataToConsume, const ParserFilter &filter)
{
    if (monitorLineParse.IsCancelling())
        return LogCollection();

    //parse
    LogCollection destLogs = std::move(existingLogsToMerge);
    LogCollection newLogs;

    if (IsTextParser)
    {
        std::vector<std::string> lines = ParseRawToLines(monitorLineParse, rawDataToConsume, filter);
        rawDataToConsume = std::vector<char>(); //free it now

        newLogs = ProcessPreFilteredLines(monitorLogParser, std::move(lines), filter);
    }
    else
    {
        monitorLineParse.Complete();

        newLogs = ParseRaw(monitorLogParser, std::move(rawDataToConsume), filter);
    }

    newLogs.Parser = this;

    monitorLogParser.Complete(); //just in case a parser misses it...

    //merge and compact
    destLogs.MoveAndMergeInLogs(monitorMergeCompact, std::move(newLogs), false, false);
    ProcessCompact(monitorMergeCompact, destLogs);
    monitorMergeCompact.Complete();

    if (monitorLineParse.IsCancelling())
    {
        destLogs.Lines.clear();
        destLogs.Columns.clear();
    }

    return std::move(destLogs);
}

LogCollection ParserInterface::ProcessPreFilteredLines(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume, const ParserFilter &filter)
{
    auto tpBegin = std::chrono::high_resolution_clock::now();
    LogCollection logs = ParseLines(monitor, std::move(linesToConsume));
    auto tpAfterParse = std::chrono::high_resolution_clock::now();
    PostFilterLines(logs.Lines, logs.Columns, filter);
    auto tpAfterFilter = std::chrono::high_resolution_clock::now();

    monitor.AddDebugOutputTime(Name + " - ProcessPreFilteredLines - ParseLines", std::chrono::duration_cast<std::chrono::microseconds>(tpAfterParse - tpBegin).count() / 1000.0);
    monitor.AddDebugOutputTime(Name + " - ProcessPreFilteredLines - PostFilterLines", std::chrono::duration_cast<std::chrono::microseconds>(tpAfterFilter - tpAfterParse).count() / 1000.0);

    return std::move(logs);
}

void ParserInterface::ProcessCompact(AppStatusMonitor &debugOnlyMonitor, LogCollection &logs)
{
    debugOnlyMonitor.AddDebugOutput("Compacting logs...");
    auto tpBefore = std::chrono::high_resolution_clock::now();

    auto src = logs.Lines.begin();
    auto dest = logs.Lines.begin();

    while (src != logs.Lines.end())
    {
        if (dest->IsEmpty())
        {
            if (!src->IsEmpty())
            {
                if (src != dest)
                    *dest = std::move(*src);
                ++dest;
            }

            ++src;
        }
        else
        {
            ++src;
            ++dest;
        }
    }

    logs.Lines.resize(dest - logs.Lines.begin());
    logs.Lines.shrink_to_fit();

    auto tpAfter = std::chrono::high_resolution_clock::now();
    debugOnlyMonitor.AddDebugOutputTime("ProcessCompact", std::chrono::duration_cast<std::chrono::microseconds>(tpAfter - tpBefore).count() / 1000.0);
}

std::vector<std::string> ParserInterface::ParseRawToLines(AppStatusMonitor &monitor, const std::vector<char> &rawData, const ParserFilter &filter)
{
    monitor.SetControlFeatures(true);
    monitor.SetProgressFeatures(rawData.size(), "MB", 1000000);

    auto tpBegin = std::chrono::high_resolution_clock::now();
    std::vector<std::string> allLines;
    allLines.reserve(rawData.size() / 500); //stab in the dark

    auto emitCurrentData = [&](decltype(rawData.begin()) start, decltype(rawData.begin()) end)
    {
        if (start < rawData.end() && start < end)
        {
            allLines.emplace_back(start, end);
            if (!PreFilterLine(ExternalSubstring<const char>(allLines.back().begin(), allLines.back().end()), filter))
                allLines.pop_back();
        }

        auto skipTo = end;
        for (; skipTo < rawData.end(); ++skipTo)
        {
            if (!(*skipTo == '\r' || *skipTo == '\n'))
                break;
        }

        return skipTo;
    };

    auto curStart = rawData.begin();
    for (auto ci = rawData.begin(); ci != rawData.end(); ++ci)
    {
        if (*ci == '\r' || *ci == '\n')
            curStart = emitCurrentData(curStart, ci);

        if ((ci - rawData.begin()) % 100000 == 99999)
        {
            monitor.AddProgress(100000);
            if (monitor.IsCancelling())
                break;
        }
    }

    emitCurrentData(curStart, rawData.end());

    auto tpAfterLines = std::chrono::high_resolution_clock::now();
    monitor.AddDebugOutputTime(Name + " - ParseRawToLines", std::chrono::duration_cast<std::chrono::microseconds>(tpAfterLines - tpBegin).count() / 1000.0);
    monitor.Complete();

    return std::move(allLines);
}

std::vector<std::string> ParseBlobToLines(AppStatusMonitor &monitor, const char *blobBegin, const char *blobEnd)
{
    std::vector<std::string> allLines;
    const size_t size = blobEnd - blobBegin;
    allLines.reserve(size / 500); //stab in the dark

    auto curStart = blobBegin;
    bool anyData = false;
    for (auto &ci = blobBegin; ci != blobEnd && !monitor.IsCancelling(); ++ci)
    {
        if (*ci == '\n' && (ci - curStart) >= 2)
        {
            anyData = false;
            int skip = (*(ci - 1) == '\r' ? 2 : 1);
            allLines.emplace_back(curStart, ci - skip + 1); //skip the line break
            curStart = ci + 1;
        }
        else
            anyData = true;
    }

    if (anyData && (blobEnd - curStart) >= 2)
    {
        int skip = (*(blobEnd - 2) == '\r' ? 2 : 1);
        allLines.emplace_back(curStart, blobEnd - skip);
    }

    return std::move(allLines);
}
