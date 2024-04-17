#include "Preferences.h"
#include "IniLexicon.h"
#include "StringUtils.h"
#include <stdlib.h>
#include <fstream>

namespace Preferences
{
    std::vector<std::string> APEnlistmentPaths;
    bool SearchLegacyAPEnlistmentPaths;
    std::string LastAPLogType;
    std::string LastAPCluster;
    std::string LastAPEnvironment;
    std::vector<std::string> LastMachineFunctions;
    std::vector<std::string> LastMachines;
    ParserFilter DefaultPrefilter;
    bool ProcessDeadMachines = false;
    bool ForceAPUnsecureProxy = false;

    std::vector<std::string> BlockListedDefaultColumns;
    int ParallelismOverrideGeneral = 0;
    int ParallelismOverrideParse = 0;
    int ParallelismOverrideSort = 0;
    int ParallelismOverrideFilter = 0;
    bool HasTestedParallelism = false;
    bool AllowCats = true;
    bool ForceCats = false;

    time_t TimeFilterStart = 0;
    time_t TimeFilterEnd = 0;
    int TimeFilterSeconds = 0;
    bool TimeFilterUTC = true;
    bool TimeFilterStream = false;
    ParserFilter Prefilter;
    bool TimeFilterLoglines = true;
    std::string LastLogFilePrefix;
    std::string LastLogFilePath;
    std::string LastLogFileExtension;
    int LastLogFileRecursiveDepth = -1;

    std::string GetFullFilename()
    {
        const char *path = getenv("USERPROFILE");
        if (!path)
            return std::string();

        std::string fullFilename = path;
        fullFilename += "\\AppData\\Local\\LogCheetah.ini";
        return fullFilename;
    }

    void Load()
    {
        //initialize values for the case where we have no config file or a section is missing
        BlockListedDefaultColumns = { "ApiBuildVersion", "ClientDeviceOSVersion", "ClientLanguageLocale", "ContractVersion", "EventDateTime", "EventLevel", "EventSequenceClock", "FormatString", "SLLSchema", "SampleRate", "ServerApplicationId", "ServerOSVersion", "ServerRole", "ServiceDateTime", "TrackingEventNameSet1", "TrackingEventNameSet2", "TrackingInterval", "TrackingRecordBytes", "TrackingRecordCount", "TrackingSequence", "TrackingSeriesId", "TrackingUntrackedBound", "appVer", "data.baseType", "epoch", "ext.cloud.environment", "ext.cloud.location", "ext.cloud.name", "ext.sll.libVer", "os", "osVer", "seqNum", "ext.ap.env", "data.baseData.succeeded", "ext.cloud.ver", "ext.xbl.ver", "PreciseTimeStamp", "SqeFlatteningVersion", "ver", "SourceEvent__", "SourceMoniker__" };

        DefaultPrefilter.LineFilters.emplace_back();
        DefaultPrefilter.LineFilters.back().MatchCase = true;
        DefaultPrefilter.LineFilters.back().Not = true;
        DefaultPrefilter.LineFilters.back().Value = "|SLLDataQuality.";

        DefaultPrefilter.LineFilters.emplace_back();
        DefaultPrefilter.LineFilters.back().MatchCase = true;
        DefaultPrefilter.LineFilters.back().Not = true;
        DefaultPrefilter.LineFilters.back().Value = ",\"name\":\"Ms.Internal.SchemaData\",";

        //load persisted values
        const std::string &filename = GetFullFilename();
        if (filename.empty())
            return;

        std::ifstream file(filename);
        if (!file.is_open())
            return;

        IniLexicon ini(file);
        SearchLegacyAPEnlistmentPaths = ini.GetValue("AP", "SearchLegacyAPEnlistmentPaths") == "true";
        APEnlistmentPaths = StringSplit(',', ini.GetValue("AP", "APEnlistmentPaths"));
        if (!APEnlistmentPaths.empty() && APEnlistmentPaths.front().empty())
            APEnlistmentPaths.pop_back();

        LastAPLogType = ini.GetValue("AP", "LastAPLogType");
        LastAPCluster = ini.GetValue("AP", "LastAPCluster");
        LastAPEnvironment = ini.GetValue("AP", "LastAPEnvironment");
        LastMachineFunctions = StringSplit(',', ini.GetValue("AP", "LastMachineFunctions"));
        LastMachines = StringSplit(',', ini.GetValue("AP", "LastMachines"));
        ProcessDeadMachines = ini.GetValue("AP", "ProcessDeadMachines") == "true";
        ForceAPUnsecureProxy = ini.GetValue("AP", "ForceAPUnsecureProxy") == "true";

        if (ini.ValueExists("General", "BlockListedDefaultColumnsV4"))
        {
            BlockListedDefaultColumns = StringSplit(',', ini.GetValue("General", "BlockListedDefaultColumnsV4"));
            if (BlockListedDefaultColumns.size() == 1 && BlockListedDefaultColumns[0].empty())
                BlockListedDefaultColumns.clear();
        }

        std::string parallelismOverrideString = ini.GetValue("General", "ParallelismOverrideGeneral");
        ParallelismOverrideGeneral = 0;
        if (!parallelismOverrideString.empty())
            std::stringstream(parallelismOverrideString) >> ParallelismOverrideGeneral;
        if (ParallelismOverrideGeneral < 0)
            ParallelismOverrideGeneral = 0;

        parallelismOverrideString = ini.GetValue("General", "ParallelismOverrideParse");
        ParallelismOverrideParse = 0;
        if (!parallelismOverrideString.empty())
            std::stringstream(parallelismOverrideString) >> ParallelismOverrideParse;
        if (ParallelismOverrideParse < 0)
            ParallelismOverrideParse = 0;

        parallelismOverrideString = ini.GetValue("General", "ParallelismOverrideSort");
        ParallelismOverrideSort = 0;
        if (!parallelismOverrideString.empty())
            std::stringstream(parallelismOverrideString) >> ParallelismOverrideSort;
        if (ParallelismOverrideSort < 0)
            ParallelismOverrideSort = 0;

        parallelismOverrideString = ini.GetValue("General", "ParallelismOverrideFilter");
        ParallelismOverrideFilter = 0;
        if (!parallelismOverrideString.empty())
            std::stringstream(parallelismOverrideString) >> ParallelismOverrideFilter;
        if (ParallelismOverrideFilter < 0)
            ParallelismOverrideFilter = 0;

        std::string testedParallelismString = ini.GetValue("General", "HasTestedParallelism");
        HasTestedParallelism = (TrimString(testedParallelismString) == "1");

        std::string allowCatsString = ini.GetValue("Cats", "Allow");
        if (!allowCatsString.empty())
            AllowCats = (TrimString(allowCatsString) == "1");

        std::string forceCatsString = ini.GetValue("Cats", "Force");
        if (!forceCatsString.empty())
            ForceCats = (TrimString(forceCatsString) == "1");

        allowMemoryUseChecks = ini.GetValue("General", "PromptWhenMemoryFull") != "false";

        DefaultPrefilter.Clear();
        if (ini.ValueExists("AP", "DefaultPrefilter"))
        {
            std::vector<std::string> prefilterParts = StringSplit(',', ini.GetValue("AP", "DefaultPrefilter"));
            for (const auto &part : prefilterParts)
            {
                if (part.size() >= 2)
                {
                    uint8_t bits = part[0] & 0x3;
                    DefaultPrefilter.LineFilters.emplace_back();
                    DefaultPrefilter.LineFilters.back().MatchCase = (bits & 1) != 0;
                    DefaultPrefilter.LineFilters.back().Not = (bits & 2) != 0;
                    DefaultPrefilter.LineFilters.back().Value = std::string(part.begin() + 1, part.end());
                    std::transform(DefaultPrefilter.LineFilters.back().Value.begin(), DefaultPrefilter.LineFilters.back().Value.end(), DefaultPrefilter.LineFilters.back().Value.begin(), [](char c) {return c != 0x1f ? c : ','; });
                }
            }
        }

        //reset non-persisted values
        TimeFilterStart = 0;
        TimeFilterSeconds = 180;
        TimeFilterUTC = true;
        TimeFilterStream = false;
        Prefilter = DefaultPrefilter;
    }

    void Save()
    {
        const std::string &filename = GetFullFilename();
        if (filename.empty())
            return;

        std::ofstream file(filename);
        if (!file.is_open())
            return;

        IniLexicon ini;
        ini.SetValue("AP", "APEnlistmentPaths", StringJoin(',', APEnlistmentPaths.begin(), APEnlistmentPaths.end()));
        ini.SetValue("AP", "SearchLegacyAPEnlistmentPaths", SearchLegacyAPEnlistmentPaths ? "true" : "false");
        ini.SetValue("AP", "LastAPLogType", LastAPLogType);
        ini.SetValue("AP", "LastAPCluster", LastAPCluster);
        ini.SetValue("AP", "LastAPEnvironment", LastAPEnvironment);
        ini.SetValue("AP", "LastMachineFunctions", StringJoin(',', LastMachineFunctions.begin(), LastMachineFunctions.end()));
        ini.SetValue("AP", "LastMachines", StringJoin(',', LastMachines.begin(), LastMachines.end()));
        ini.SetValue("AP", "ProcessDeadMachines", ProcessDeadMachines ? "true" : "false");
        ini.SetValue("AP", "ForceAPUnsecureProxy", ForceAPUnsecureProxy ? "true" : "false");

        ini.SetValue("General", "BlockListedDefaultColumnsV4", StringJoin(',', BlockListedDefaultColumns.begin(), BlockListedDefaultColumns.end()));

        std::stringstream parallelismOverrideGeneralString;
        parallelismOverrideGeneralString << ParallelismOverrideGeneral;
        ini.SetValue("General", "ParallelismOverrideGeneral", parallelismOverrideGeneralString.str());

        std::stringstream parallelismOverrideParseString;
        parallelismOverrideParseString << ParallelismOverrideParse;
        ini.SetValue("General", "ParallelismOverrideParse", parallelismOverrideParseString.str());

        std::stringstream parallelismOverrideSortString;
        parallelismOverrideSortString << ParallelismOverrideSort;
        ini.SetValue("General", "ParallelismOverrideSort", parallelismOverrideSortString.str());

        std::stringstream parallelismOverrideFilterString;
        parallelismOverrideFilterString << ParallelismOverrideFilter;
        ini.SetValue("General", "ParallelismOverrideFilter", parallelismOverrideFilterString.str());

        ini.SetValue("General", "HasTestedParallelism", HasTestedParallelism ? "1" : "0");

        ini.SetValue("Cats", "Allow", AllowCats ? "1" : "0");
        ini.SetValue("Cats", "Force", ForceCats ? "1" : "0");

        ini.SetValue("General", "PromptWhenMemoryFull", allowMemoryUseChecks ? "true" : "false");

        std::vector<std::string> defPrefilterParts;
        for (const auto &pf : DefaultPrefilter.LineFilters)
        {
            std::string part;
            part.push_back((char)0x30); //put the special bits byte into the common ascii character range
            part[0] |= pf.MatchCase ? 1 : 0;
            part[0] |= pf.Not ? 2 : 0;
            part += pf.Value;
            std::transform(part.begin(), part.end(), part.begin(), [](char c) {return c != ',' ? c : (char)0x1f; });
            defPrefilterParts.emplace_back(std::move(part));
        }
        ini.SetValue("AP", "DefaultPrefilter", StringJoin(',', defPrefilterParts.begin(), defPrefilterParts.end()));

        ini.Save(file);
    }
}
