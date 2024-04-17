#pragma once

#include <string>

enum class LogType
{
    None,
    Unknown,
    PSV,
    TSV,
    CSV,
    SSV,
    Json,
    NestedJson,
    TRX
};

//note this will only ask for text-based formats.  Returns None on cancel.
LogType DoAskForLogType(const std::string  &description, bool allowCancel);

//returns Unknown if it can't be identified
LogType IdentifyLogTypeFromFileExtension(const std::string &filename);
