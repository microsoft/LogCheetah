#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <array>
#include <functional>
#include "StringUtils.h"
#include "SharedGlobals.h"

class ParserInterface;

// Packed data uses 16-bit column indices, with 24-bit data indices
const size_t MaxLogEntryColumnIndex = 0x0000ffff;
const size_t MaxLogEntryDataIndex = 0x00ffffff;

#pragma pack(push, 1)
struct LogEntryColumn
{
    uint16_t ColumnNumber;
    uint32_t IndexDataBegin : 24;
    uint32_t IndexDataEnd : 24;

    inline LogEntryColumn() : ColumnNumber(0), IndexDataBegin(0), IndexDataEnd(0) {}
    inline LogEntryColumn(uint16_t c, uint32_t s, uint32_t e) : ColumnNumber(c), IndexDataBegin(s), IndexDataEnd(e) {}

    inline bool operator<(const LogEntryColumn &o)
    {
        return ColumnNumber < o.ColumnNumber;
    }
};

struct LogEntry
{
private:
    std::vector<char> rawData;

    inline uint32_t columnDataBegin() const { return 0; }
    union
    {
        uint32_t columnDataEnd : 24;
        uint32_t extraDataBegin : 24;
    };
    union
    {
        uint32_t extraDataEnd : 24;
        uint32_t originalLogBegin : 24;
    };
    inline uint32_t originalLogEnd() const { return rawData.size() > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max() : (uint32_t)rawData.size(); }

public:
    bool ParseFailed : 1;
    bool Tagged : 1;

    //
    inline LogEntry() : columnDataEnd(0), extraDataEnd(0), ParseFailed(false), Tagged(false)
    {
    }

    inline LogEntry(const std::string &originalLog, const std::string &extraData, const std::vector<LogEntryColumn> &originalLogColumns, const std::vector<LogEntryColumn> &extraDataColumns) : ParseFailed(false), Tagged(false)
    {
        Set(originalLog, extraData, originalLogColumns, extraDataColumns);
    }

    LogEntry(const LogEntry &o) = delete;
    LogEntry& operator=(const LogEntry &o) = delete;
    LogEntry(LogEntry &&o) = default;
    LogEntry& operator=(LogEntry &&o) = default;

    //assign a value to this log entry
    void Set(const std::string &originalLog, const std::string &extraData, const std::vector<LogEntryColumn> &originalLogColumns, const std::vector<LogEntryColumn> &extraDataColumns);

    //clear everything stored in this log entry
    void Clear();

    //returns a pointer to the begin/end of the original logline
    inline const char* OriginalLogBegin() const { return rawData.data() + originalLogBegin; }
    inline char* OriginalLogBegin() { return rawData.data() + originalLogBegin; }
    inline const char* OriginalLogEnd() const { return rawData.data() + originalLogEnd(); }
    inline char* OriginalLogEnd() { return rawData.data() + originalLogEnd(); }

    //returns a pointer to the begin/end of the extra data stored with a logline
    inline const char* ExtraDataBegin() const { return rawData.data() + extraDataBegin; }
    inline char* ExtraDataBegin() { return rawData.data() + extraDataBegin; }
    inline const char* ExtraDataEnd() const { return rawData.data() + extraDataEnd; }
    inline char* ExtraDataEnd() { return rawData.data() + extraDataEnd; }

    //returns a pointer to the begin/end of the column data.  column numbers are expected to appear in ascending order.
    inline const LogEntryColumn* ColumnDataBegin() const { return (const LogEntryColumn*)(rawData.data() + columnDataBegin()); }
    inline LogEntryColumn* ColumnDataBegin() { return (LogEntryColumn*)(rawData.data() + columnDataBegin()); }
    inline const LogEntryColumn* ColumnDataEnd() const { return (const LogEntryColumn*)(rawData.data() + columnDataEnd); }
    inline LogEntryColumn* ColumnDataEnd() { return (LogEntryColumn*)(rawData.data() + columnDataEnd); }
    inline size_t ColumnCount() const { return (columnDataEnd - columnDataBegin()) / sizeof(LogEntryColumn); }

    inline bool IsEmpty() const { return rawData.empty(); }

    //retrieves the value of a specific column
    inline ExternalSubstring<const char> GetColumnNumberValue(uint16_t columnNumber) const
    {
        const LogEntryColumn *found = std::lower_bound(ColumnDataBegin(), ColumnDataEnd(), columnNumber, [](const LogEntryColumn &a, uint16_t b) { return a.ColumnNumber < b; });
        if (found != ColumnDataEnd() && found->ColumnNumber == columnNumber)
            return ExternalSubstring<const char>(rawData.data() + found->IndexDataBegin, rawData.data() + found->IndexDataEnd);

        return ExternalSubstring<const char>();
    }

    //compares the values of a column for two lines, returns true if less than.  If ascending is false, returns true if greater than instead.
    inline bool Compare(const LogEntry &o, uint16_t column, bool ascending) const
    {
        if (ascending)
            return GetColumnNumberValue(column) < o.GetColumnNumberValue(column);
        else
            return GetColumnNumberValue(column) > o.GetColumnNumberValue(column);
    }

    //appends data to the extra data section of the log, and adds columns for it
    void AppendExtra(const std::string &extraData, const std::vector<LogEntryColumn> &extraDataColumns);
};
#pragma pack(pop)

struct ColumnInformation
{
    ColumnInformation() = default;
    inline ColumnInformation(const std::string &name) : UniqueName(name) {}

    std::string UniqueName;
    std::string DisplayNameOverride;
    std::string Description;

    inline const std::string& GetDisplayName()
    {
        if (!DisplayNameOverride.empty())
            return DisplayNameOverride;

        return UniqueName;
    }
};

struct LogCollection
{
    //these are set by the parsers and should only be read by the application
    std::vector<ColumnInformation> Columns;
    std::vector<LogEntry> Lines;
    bool IsRawRepresentationValid = true;

    //this is set by ParserInterface and should only be read by the application.  it may be nullptr if logs from different parsers are merged
    ParserInterface* Parser = nullptr;

    //run-time adjustable options
    uint16_t SortColumn = 0;
    bool SortAscending = true;

    //
    LogCollection() = default;
    inline LogCollection(const LogCollection &o) = delete;
    inline LogCollection& operator=(const LogCollection &o) = delete;
    inline LogCollection(LogCollection &&o) = default;
    inline LogCollection& operator=(LogCollection &&o) = default;

    void MoveAndMergeInLogs(AppStatusMonitor &monitor, LogCollection &&other, bool filterDuplicateLogs, bool resortLogs);
    void MoveAndMergeInLogs(AppStatusMonitor &monitor, LogCollection &&other, bool filterDuplicateLogs, bool resortLogs, size_t &outBeginRowAffected, size_t &outEndRowAffected);

    void SortRange(size_t lineStart, size_t lineEnd);
};

struct LogFilterEntry
{
    int Column = 0;
    std::string Value;
    bool Not = false;
    bool MatchCase = false;
    bool MatchSubstring = true;
};

bool DoesLogEntryPassFilters(const LogEntry &entry, const std::vector<LogFilterEntry> &filters);
std::tuple<bool, int64_t> FindNextLogline(int64_t initialPosition, int direction, const std::vector<LogFilterEntry> &filters, const LogCollection &logs, const std::vector<uint32_t> &rowVisibilityMap);

struct ParserLineFilterEntry
{
    std::string Value;
    bool Not = false;
    bool MatchCase = true;
};

struct ParserFilter
{
    time_t MinTime = 0;
    time_t MaxTime = 0;

    std::vector<ParserLineFilterEntry> LineFilters;

    inline void Clear()
    {
        MinTime = MaxTime = 0;
        LineFilters.clear();
    }

    bool PassesLineFilters(const ExternalSubstring<const char> &line) const;
};

class ParserInterface
{
public:
    inline ParserInterface(const std::string &name)
        : Name(name), PreFilterLine(NoopPreFilterLine), PostFilterLines(NoopPostFilterLines), ParseRaw(NoopParseRaw), ParseLines(NoopParseLines), PreloadKnownSchemas(NoopPreloadKnownSchemas), LoadSchemaData(NoopLoadSchemaData), SaveSchemaData(NoopSaveSchemaData)
    {
    }

    ParserInterface(const ParserInterface&) = delete;
    ParserInterface& operator=(const ParserInterface&) = delete;
    ParserInterface(ParserInterface&&) = default;
    ParserInterface& operator=(ParserInterface&&) = default;

    inline static ParserInterface MakePreFilterTextParser(const std::string &name,
        std::function<bool(const ExternalSubstring<const char> &line, const ParserFilter &filter)> preFilterLine,
        std::function<LogCollection(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume)> parseLogs,
        std::function<void(AppStatusMonitor &monitor)> preloadKnownSchemas,
        std::function<void(AppStatusMonitor &monitor, const std::string &blob)> loadSchemaData,
        std::function<std::string()> saveSchemaData,
        bool isJson = false,
        bool producesFakeJson = false)
    {
        ParserInterface pi { name };
        pi.PreFilterLine = preFilterLine;
        pi.ParseLines = parseLogs;
        pi.PreloadKnownSchemas = preloadKnownSchemas;
        pi.LoadSchemaData = loadSchemaData;
        pi.SaveSchemaData = saveSchemaData;
        pi.IsJsonParser = isJson;
        pi.ProducesFakeJson = producesFakeJson;
        return std::move(pi);
    }

    inline static ParserInterface MakePostFilterTextParser(const std::string &name,
        std::function<void(std::vector<LogEntry> &lines, const std::vector<ColumnInformation> &columns, const ParserFilter &filter)> postFilterLines,
        std::function<LogCollection(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume)> parseLogs,
        std::function<void(AppStatusMonitor &monitor)> preloadKnownSchemas,
        std::function<void(AppStatusMonitor &monitor, const std::string &blob)> loadSchemaData,
        std::function<std::string()> saveSchemaData)
    {
        ParserInterface pi { name };
        pi.PostFilterLines = postFilterLines;
        pi.ParseLines = parseLogs;
        pi.PreloadKnownSchemas = preloadKnownSchemas;
        pi.LoadSchemaData = loadSchemaData;
        pi.SaveSchemaData = saveSchemaData;
        return std::move(pi);
    }

    inline static ParserInterface MakeBinaryParser(const std::string &name,
        std::function<LogCollection(AppStatusMonitor &monitor, const std::vector<char> &rawDataToConsume, const ParserFilter &filter)> parseLogs)
    {
        ParserInterface pi { name };
        pi.ParseRaw = parseLogs;
        pi.IsTextParser = false;
        return std::move(pi);
    }

    //
    std::string Name;
    bool IsTextParser = true;
    bool IsJsonParser = false;
    bool ProducesFakeJson = false;

    //call one of these to parse sets of data
    LogCollection ProcessRawData(AppStatusMonitor &monitorLineParse, AppStatusMonitor &monitorLogParser, AppStatusMonitor &monitorMergeCompact, LogCollection &&existingLogsToMerge, std::vector<char> &&rawDataToConsume, const ParserFilter &filter);

    //optional schema management
    std::function<void(AppStatusMonitor &monitor)> PreloadKnownSchemas;
    std::function<void(AppStatusMonitor &monitor, const std::string &blob)> LoadSchemaData;
    std::function<std::string()> SaveSchemaData;

    //noop implementations
    inline static void NoopLoadSchemaData(AppStatusMonitor &monitor, const std::string &blob) {}
    inline static std::string NoopSaveSchemaData() { return std::string(); }
    inline static void NoopPreloadKnownSchemas(AppStatusMonitor &monitor) {}
    inline static bool NoopPreFilterLine(const ExternalSubstring<const char> &line, const ParserFilter &filter) { return true; }
    inline static void NoopPostFilterLines(std::vector<LogEntry> &lines, const std::vector<ColumnInformation> &columns, const ParserFilter &filter) {}
    inline static LogCollection NoopParseRaw(AppStatusMonitor &monitor, const std::vector<char> &rawData, const ParserFilter &filter) { return LogCollection(); }
    inline static LogCollection NoopParseLines(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume) { return LogCollection(); }

private:
    //for text-line-based logs ParseRaw will call ParseRawToLines then call ParseLines followed by PostFilterLines.  For binary-based logs ParseRaw will parse and filter, leaving ParseLines as a Noop.
    std::function<LogCollection(AppStatusMonitor &monitor, std::vector<char> &&rawDataToConsume, const ParserFilter &filter)> ParseRaw;
    std::function<LogCollection(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume)> ParseLines;

    //exactly one of these will be implemented, the other will be noop
    std::function<bool(const ExternalSubstring<const char> &line, const ParserFilter &filter)> PreFilterLine; //returns true if the line should be accepted
    std::function<void(std::vector<LogEntry> &lines, const std::vector<ColumnInformation> columns, const ParserFilter &filter)> PostFilterLines; //clears out any lines that don't match

    //internal helpers
    LogCollection ProcessPreFilteredLines(AppStatusMonitor &monitor, std::vector<std::string> &&linesToConsume, const ParserFilter &filter);
    void ProcessCompact(AppStatusMonitor &debugOnlyMonitor, LogCollection &logs);
    std::vector<std::string> ParseRawToLines(AppStatusMonitor &monitor, const std::vector<char> &rawDataToConsume, const ParserFilter &filter);
};

//general helper
std::vector<std::string> ParseBlobToLines(AppStatusMonitor &monitor, const char *blobBegin, const char *blobEnd);
