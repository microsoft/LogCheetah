#include "LogFormatter.h"
#include "Globals.h"
#include <CommCtrl.h>
#include <set>

namespace
{
    inline std::string SeperatorForLogFormat(uint32_t logFormat)
    {
        if (logFormat == LOGFORMAT_FIELDS_PSV)
            return "|";
        else if (logFormat == LOGFORMAT_FIELDS_CSV)
            return ",";
        else if (logFormat == LOGFORMAT_FIELDS_TSV)
            return "\t";

        return std::string();
    }

    inline std::string StripForbiddenCharactersFromField(std::string &&field, char forbiddenCharacter)
    {
        bool hasForbiddenCharacters = false;
        for (const auto &c : field)
        {
            if (c == forbiddenCharacter || c == '\r' || c == '\n')
            {
                hasForbiddenCharacters = true;
                break;
            }
        }

        //field is ok, just move it back out as-is
        if (!hasForbiddenCharacters)
        {
            return std::move(field);
        }

        //need to rebuild it
        std::string strippedField;
        strippedField.reserve(field.size());
        for (const auto &c : field)
        {
            if (!(c == forbiddenCharacter || c == '\r' || c == '\n'))
                strippedField.push_back(c);
        }

        return std::move(strippedField);
    }
}

void FormatLogLine(int row, uint32_t logFormat, const std::vector<uint32_t> &usedColumns, std::ostream &outStream)
{
    const LogEntry &entry = globalLogs.Lines[row];

    if (logFormat == LOGFORMAT_RAW)
        outStream << std::string(entry.OriginalLogBegin(), entry.OriginalLogEnd()) << "\r\n";
    else
    {
        std::string seperator = SeperatorForLogFormat(logFormat);
        char forbiddenCharacter = seperator[0];

        bool isFirst = true;
        for (auto &col : usedColumns)
        {
            if (!isFirst)
                outStream << seperator;

            outStream << StripForbiddenCharactersFromField(entry.GetColumnNumberValue((uint16_t)col).str(), forbiddenCharacter);

            isFirst = false;
        }
        outStream << "\r\n";
    }
}

void FormatLogData(uint32_t logFormat, const std::vector<uint32_t> &logRowFilter, const std::vector<uint32_t> &logColFilter, std::ostream &outStream)
{
    if (logFormat != LOGFORMAT_RAW)
    {
        //header row
        std::string seperator = SeperatorForLogFormat(logFormat);
        char forbiddenCharacter = seperator[0];

        bool isFirst = true;
        for (auto &col : logColFilter)
        {
            if (!isFirst)
                outStream << seperator;
            std::string temp = globalLogs.Columns[col].GetDisplayName();
            outStream << StripForbiddenCharactersFromField(std::move(temp), forbiddenCharacter);
            isFirst = false;
        }
        outStream << "\r\n";
    }

    //data rows
    for (auto &row : logRowFilter)
        FormatLogLine(row, logFormat, logColFilter, outStream);
}
