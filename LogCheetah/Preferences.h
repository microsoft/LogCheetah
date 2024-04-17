#pragma once
#include <string>
#include <vector>
#include "LogParserCommon.h"

namespace Preferences
{
    //General
    extern std::vector<std::string> BlockListedDefaultColumns;
    extern int ParallelismOverrideGeneral;
    extern int ParallelismOverrideParse;
    extern int ParallelismOverrideSort;
    extern int ParallelismOverrideFilter;
    extern bool HasTestedParallelism;
    extern bool AllowCats;
    extern bool ForceCats;

    //
    void Load();
    void Save();
}
