#include "JsonParser.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <cctype>
#include "SharedGlobals.h"

namespace
{
    size_t FindOrAddColumn(LogCollection &logs, std::unordered_map<std::string, size_t> &sharedColumnIndex, std::mutex &sharedMutex, std::unordered_map<std::string, size_t> &threadColumnIndex, const std::string &colName, const std::string &colDescription)
    {
        //first try thread's copy
        auto existingIndex = threadColumnIndex.find(colName);
        if (existingIndex == threadColumnIndex.end())
        {
            //sync thread's copy to global copy and try again
            std::lock_guard<std::mutex> lock(sharedMutex);

            threadColumnIndex = sharedColumnIndex;
            existingIndex = threadColumnIndex.find(colName);
            if (existingIndex == threadColumnIndex.end())
            {
                //need to add it
                logs.Columns.emplace_back(colName);
                logs.Columns.back().Description = colDescription;
                size_t mappedColumn = logs.Columns.size() - 1;
                sharedColumnIndex.insert(std::make_pair(colName, mappedColumn));
                threadColumnIndex = sharedColumnIndex;
                return mappedColumn;
            }
            else
                return existingIndex->second;
        }
        else
            return existingIndex->second;
    }

    inline bool IsValueChar(const char c)
    {
        if (c >= 'a' && c <= 'z')
            return true;
        else if (c >= 'A' && c <= 'Z')
            return true;
        else if (c >= '0' && c <= '9')
            return true;
        else if (c == '.' || c == '-' || c == '+')
            return true;

        return false;
    }

    size_t WalkValueString(const std::string_view line, size_t start)
    {
        size_t pos = start;
        while (pos < line.size())
        {
            if (!IsValueChar(line[pos]))
                break;

            ++pos;
        }

        if (pos > line.size())
            pos = line.size();

        return pos;
    }

    size_t WalkQuotedString(const std::string_view line, size_t startInsideQuote)
    {
        size_t pos = startInsideQuote;
        while (pos < line.size())
        {
            if (line[pos] == '\\')
                ++pos;
            else if (line[pos] == '\"')
                break;

            ++pos;
        }

        if (pos > line.size())
            pos = line.size();

        return pos;
    }

    size_t WalkArrayMegaString(const std::string_view line, size_t startInsideBracket)
    {
        size_t pos = startInsideBracket;
        while (pos < line.size())
        {
            if (line[pos] == '\"')
                pos = WalkQuotedString(line, pos + 1) + 1;
            else if (line[pos] == ']')
                break;
            else
                ++pos;
        }

        if (pos > line.size())
            pos = line.size();

        return pos;
    }

    time_t ParseTimeFromLine(const std::string_view line)
    {
        //first extract the date string by itself
        size_t dateStart = 0;
        size_t dateEnd = 0;
        const static std::string timeMatch { "\"time\"" };
        size_t pos = line.find(timeMatch);
        while (pos < line.size())
        {
            if (line.begin()[pos] == ':')
                break;
            ++pos;
        }
        while (pos < line.size())
        {
            if (line.begin()[pos] == '"')
            {
                dateStart = pos + 1;
                dateEnd = WalkQuotedString(line, dateStart);
                break;
            }

            ++pos;
        }

        if (dateStart == dateEnd)
            return 0;

        //string will be of the form: 2016-02-25T20:08:38.6443339Z
        char *endChar = nullptr;
        size_t cur = dateStart;
        size_t next = cur;

        //year
        next = line.find('-', cur);
        if (next == std::string::npos)
            return 0;
        int outYear = strtol(line.data() + cur, &endChar, 10);

        //month
        cur = next + 1;
        next = line.find('-', cur);
        if (next == std::string::npos)
            return 0;
        int outMonth = strtol(line.data() + cur, &endChar, 10);

        //day
        cur = next + 1;
        next = line.find('T', cur);
        if (next == std::string::npos)
            return 0;
        int outDay = strtol(line.data() + cur, &endChar, 10);

        //hour
        cur = next + 1;
        next = line.find(':', cur);
        if (next == std::string::npos)
            return 0;
        int outHour = strtol(line.data() + cur, &endChar, 10);

        //minute
        cur = next + 1;
        next = line.find(':', cur);
        if (next == std::string::npos)
            return 0;
        int outMinute = strtol(line.data() + cur, &endChar, 10);

        //second
        cur = next + 1;
        next = line.find('.', cur);
        int outSecond = strtol(line.data() + cur, &endChar, 10);

        if (outYear != 0)
        {
            std::tm outTime = { 0 };
            outTime.tm_year = outYear;
            outTime.tm_mon = outMonth;
            outTime.tm_mday = outDay;
            outTime.tm_hour = outHour;
            outTime.tm_min = outMinute;
            outTime.tm_sec = outSecond;

            --outTime.tm_mon;
            outTime.tm_year -= 1900;

            time_t ret = _mkgmtime(&outTime);
            if (ret == -1)
                return 0;
            return ret;
        }

        return 0;
    }

    std::string DeEscapeString(std::string_view orig)
    {
        std::string s;
        s.reserve(orig.size());

        bool processingEscape = false;

        for (char c : orig)
        {
            if (processingEscape)
            {
                processingEscape = false;
                s.push_back(c);
            }
            else if (c == '\\')
                processingEscape = true;
            else
                s.push_back(c);
        }

        return s;
    }
}

namespace JSON
{
    bool FilterLine(const ExternalSubstring<const char> &line, const ParserFilter &filter)
    {
        //FUTURE: Grab any schema definition lines we happen to see here and process them

        if (!filter.PassesLineFilters(line))
            return false;

        if (filter.MinTime != 0 || filter.MaxTime != 0)
        {
            time_t lineTime = ParseTimeFromLine(std::string_view(line.begin(), line.size()));
            if (lineTime != 0 && (lineTime<filter.MinTime || lineTime>filter.MaxTime))
                return false;
        }

        return true;
    }

    void LoadSchemaData(AppStatusMonitor &monitor, const std::string &blob)
    {
        monitor.Complete();
        //FUTURE
    }

    std::string SaveSchemaData()
    {
        //FUTURE
        return std::string();
    }

    LogCollection ParseLogs(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume, bool allowNestedJson)
    {
        LogCollection logs;
        logs.IsRawRepresentationValid = true;

        if (linesToConsume.empty())
            return std::move(logs);

        //add "well known" columns first so the results are more sane
        std::unordered_map<std::string, size_t> sharedColumnIndex;
        logs.Columns.emplace_back("time");
        logs.Columns.emplace_back("name");
        sharedColumnIndex.emplace("time", 0);
        sharedColumnIndex.emplace("name", 1);

        //presize our destination storage
        logs.Lines.resize(linesToConsume.size());

        //spread work accross threads and parse
        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(logs.Lines.size(), "kiloline", 1000);

        std::mutex mut;
        std::vector<std::thread> threads;
        threads.reserve(cpuCountParse);
        for (int cpu = 0; cpu < cpuCountParse; ++cpu)
        {
            threads.emplace_back([&](int threadIndex)
            {
                size_t chunkSize = logs.Lines.size() / cpuCountParse;
                if (!chunkSize)
                    chunkSize = 1;

                size_t iLogsStartIndex = chunkSize * threadIndex;
                size_t iLogsEndIndex = chunkSize * (threadIndex + 1);
                if (iLogsEndIndex > logs.Lines.size() || threadIndex == cpuCountParse - 1)
                    iLogsEndIndex = logs.Lines.size();
                if (iLogsStartIndex > iLogsEndIndex)
                    iLogsStartIndex = iLogsEndIndex;

                mut.lock();
                std::unordered_map<std::string, size_t> threadColumnIndex(sharedColumnIndex);
                mut.unlock();

                std::vector<LogEntryColumn> columnDataOrig;
                std::vector<LogEntryColumn> columnDataExtra;
                std::string extraData; //holds any column data (such as fields that had to be de-escaped or interpreted)

                std::vector<std::string> columnNameStack;
                std::string curColumnName;

                for (int row = (int)iLogsStartIndex; row < (int)iLogsEndIndex; ++row)
                {
                    if (monitor.IsCancelling())
                        break;

                    monitor.AddProgress(1);

                    //
                    std::string &line = linesToConsume[row];
                    if (line.empty())
                        continue;

                    columnDataOrig.clear();
                    columnDataExtra.clear();
                    extraData.clear();

                    columnNameStack.clear();
                    curColumnName.clear();

                    //parse the line
                    bool parseFailed = false;

                    auto emitValue = [&](std::string_view blob, size_t start, size_t end, bool &isInLeftSide, bool emitAsExtra)
                    {
                        if (isInLeftSide)
                        {
                            isInLeftSide = false;
                            if (start == end)
                                columnNameStack.emplace_back("(empty)");
                            else
                                columnNameStack.emplace_back(std::string(blob.data() + start, blob.data() + end));
                        }
                        else
                        {
                            isInLeftSide = true;

                            if (threadColumnIndex.size() > 1000) //something is probably horribly wrong.. fall back to used a fixed value to prevent exploding too badly
                                curColumnName = "(broken_json)";
                            else
                            {
                                curColumnName = StringJoin('.', columnNameStack.begin(), columnNameStack.end());
                                StripSymbolPrefixFromString(curColumnName);
                            }

                            size_t colIndex = FindOrAddColumn(logs, sharedColumnIndex, mut, threadColumnIndex, curColumnName, std::string());

                            if (start > MaxLogEntryDataIndex)
                            {
                                start = MaxLogEntryDataIndex;
                                parseFailed = true;
                            }

                            if (end > MaxLogEntryDataIndex)
                            {
                                end = MaxLogEntryDataIndex;
                                parseFailed = true;
                            }

                            LogEntryColumn *lce;
                            if (emitAsExtra)
                            {
                                columnDataExtra.emplace_back();
                                lce = &columnDataExtra.back();
                            }
                            else
                            {
                                columnDataOrig.emplace_back();
                                lce = &columnDataOrig.back();
                            }

                            lce->ColumnNumber = (uint16_t)colIndex;
                            lce->IndexDataBegin = (uint32_t)start;
                            lce->IndexDataEnd = (uint32_t)end;

                            if (!columnNameStack.empty())
                                columnNameStack.pop_back();
                        }
                    };

                    std::function<void(std::string_view blob, size_t start, size_t end, bool emitAsExtra)> walkBlob;
                    walkBlob = [&](std::string_view blob, size_t start, size_t end, bool emitAsExtra)
                    {
                        bool isInLeftSide = true;
                        size_t pos = start;
                        while (pos < end && !parseFailed)
                        {
                            const char &cur = blob[pos];

                            if (cur == '[') //NOTE: For now we will treat the entire contents as a "mega string".. may revisit this later..
                            {
                                if (pos != 0) //xpert exports the logs as a list, which screws up parsing the first logline.. just filter that out if it's the first thing
                                {
                                    size_t blobStart = pos + 1;
                                    size_t blobEnd = WalkArrayMegaString(blob, blobStart);
                                    pos = blobEnd;
                                    emitValue(blob, blobStart, blobEnd, isInLeftSide, true);
                                }
                            }
                            else if (cur == '\"')
                            {
                                size_t blobStart = pos + 1;
                                size_t blobEnd = WalkQuotedString(blob, blobStart);
                                int64_t blobSize = blobEnd - blobStart;
                                pos = blobEnd;

                                //The nested mode allows for json data inside of a nested string.. de-escape that and store it as extra data with the line
                                if (allowNestedJson && !isInLeftSide && blobSize > 4 && blob[blobStart] == '{' && blob[blobStart + 1] == '\\' && blob[blobStart + 2] == '\"' && blob[blobEnd - 1] == '}')
                                {
                                    std::string nestedString = DeEscapeString(std::string_view(blob.data() + blobStart, blobEnd - blobStart));
                                    size_t nestedBlobStart = extraData.size();
                                    extraData.reserve(line.size()); //prevent re-alloc, since we should never exceed this
                                    extraData += nestedString;
                                    walkBlob(extraData, nestedBlobStart, extraData.size(), true);
                                    isInLeftSide = true;
                                }
                                else
                                    emitValue(blob, blobStart, blobEnd, isInLeftSide, emitAsExtra);
                            }
                            else if (IsValueChar(cur))
                            {
                                size_t blobStart = pos;
                                size_t blobEnd = WalkValueString(blob, blobStart);
                                pos = blobEnd - 1;
                                emitValue(blob, blobStart, blobEnd, isInLeftSide, emitAsExtra);
                            }
                            else if (cur == '{')
                            {
                                isInLeftSide = true;
                            }
                            else if (cur == '}')
                            {
                                if (!columnNameStack.empty())
                                    columnNameStack.pop_back();
                            }

                            ++pos;
                        }
                    };

                    walkBlob(line, 0, line.size(), false);

                    if (!columnNameStack.empty())
                        parseFailed = true;

                    //store data for the line
                    LogEntry &le = logs.Lines[row];
                    le.Set(line, extraData, columnDataOrig, columnDataExtra);
                    le.ParseFailed = parseFailed;
                    line = std::string(); //free old memory now to reduce max memory usage during parsing
                }
            }, cpu);
        }

        for (auto &t : threads)
            t.join();

        //select the default sort column
        int sortColumn = -1;
        for (size_t cnum = 0; cnum < logs.Columns.size(); ++cnum)
        {
            std::string cval = logs.Columns[cnum].UniqueName;
            TransformStringToLower(cval);
            if (cval == "time") //always favor this one
                sortColumn = (int)cnum;
            else if (sortColumn == -1 && cval.find("time") != std::string::npos)
                sortColumn = (int)cnum;
            else if (sortColumn == -1 && cval.find("date") != std::string::npos)
                sortColumn = (int)cnum;
        }

        if (sortColumn != -1)
            logs.SortColumn = (uint16_t)sortColumn;

        //generate more readable display names for the columns
        for (auto &c : logs.Columns)
        {
            size_t dot = c.UniqueName.find_last_of('.');
            if (dot != std::string::npos)
            {
                c.DisplayNameOverride = std::string(c.UniqueName.begin() + dot + 1, c.UniqueName.end()) + " (" + std::string(c.UniqueName.begin(), c.UniqueName.begin() + dot) + ")";
            }
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
