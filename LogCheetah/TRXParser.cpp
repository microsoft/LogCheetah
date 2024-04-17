#include "SharedGlobals.h"
#include "TRXParser.h"
#include "StringUtils.h"
#include "XmlLexicon.h"
#include <cstdint>
#include <vector>
#include <sstream>
#include <array>
#include <map>
#include <set>
#include <iomanip>
#include <string_view>

namespace
{
    const uint16_t COLUMNINDEX_DATE = 0;
    const uint16_t COLUMNINDEX_TESTRUNID = 1;
    const uint16_t COLUMNINDEX_ITEMTYPE = 2;
    const uint16_t COLUMNINDEX_CLASSNAME = 3;
    const uint16_t COLUMNINDEX_TESTNAME = 4;
    const uint16_t COLUMNINDEX_OUTCOME = 5;
    const uint16_t COLUMNINDEX_DURATION = 6;
    const uint16_t COLUMNINDEX_TESTASSEMBLY = 7;
    const uint16_t COLUMNINDEX_TESTCATEGORY = 8;
    const uint16_t COLUMNINDEX_DESCRIPTION = 9;
    const uint16_t COLUMNINDEX_OUTPUT = 10;
    const uint16_t COLUMNINDEX_MAXFIXED = COLUMNINDEX_OUTPUT;

    // Must be treated read-only
    struct VectorToIStream : std::streambuf
    {
        VectorToIStream(const std::vector<char> &vec)
        {
            std::vector<char> &vecNonConst = const_cast<std::vector<char>&>(vec);
            setg(vecNonConst.data(), vecNonConst.data(), vecNonConst.data() + vecNonConst.size());
        }
    };

    inline void AddColumnExtraData(std::vector<LogEntryColumn> &columnData, std::string &extraData, uint16_t columnIndex, const std::string_view valueToAdd)
    {
        columnData.emplace_back(columnIndex, (uint32_t)extraData.size(), (uint32_t)(extraData.size() + valueToAdd.size()));
        extraData.append(valueToAdd);
    }

    struct TestDefinition
    {
        std::string Id;
        std::string TestName;
        std::string ClassName;
        std::string AssemblyName;
        std::string Description;
        std::string Category;
    };

    std::unordered_map<std::string, TestDefinition> FindTestDefinitions(const XmlLexicon::Node &testRunNode)
    {
        std::unordered_map<std::string, TestDefinition> idToTestDefinition;

        for (const XmlLexicon::Node &testRunChild : testRunNode.Children)
        {
            if (testRunChild.Name == "TestDefinitions")
            {
                for (const XmlLexicon::Node &testDefinitionsChild : testRunChild.Children)
                {
                    if (testDefinitionsChild.Name == "UnitTest")
                    {
                        TestDefinition testDef;
                        for (auto &kvp : testDefinitionsChild.PropertyList)
                        {
                            if (kvp.first == "name")
                                testDef.TestName = kvp.second;
                            else if (kvp.first == "id")
                                testDef.Id = kvp.second;
                        }

                        for (const XmlLexicon::Node &unitTestChild : testDefinitionsChild.Children)
                        {
                            if (unitTestChild.Name == "Description")
                                testDef.Description = unitTestChild.Value;
                            else if (unitTestChild.Name == "TestCategory")
                            {
                                for (const XmlLexicon::Node &testCategoryChild : unitTestChild.Children)
                                {
                                    if (testCategoryChild.Name == "TestCategoryItem")
                                    {
                                        const std::string *category = testCategoryChild.FindProperty("TestCategory");
                                        if (category)
                                        {
                                            if (!testDef.Category.empty())
                                                testDef.Category.append(", ");
                                            testDef.Category.append(*category);
                                        }
                                    }
                                }
                            }
                            else if (unitTestChild.Name == "TestMethod")
                            {
                                //VSTest is inconsistent here. className could be 2 parts separated by a comma (class and assembly), or it could be just the class, in which case we'll have to scrape the assembly out of another field
                                const std::string *classNameBlob = unitTestChild.FindProperty("className");
                                if (classNameBlob)
                                {
                                    std::vector<std::string> classNameBlobParts = StringSplit({ ',' }, *classNameBlob);
                                    if (classNameBlobParts.size() >= 2)
                                    {
                                        testDef.ClassName = classNameBlobParts[0];
                                        testDef.AssemblyName = classNameBlobParts[1];
                                    }
                                    else
                                        testDef.ClassName = *classNameBlob;
                                }

                                if (testDef.AssemblyName.empty())
                                {
                                    const std::string *assemblyFullPath = unitTestChild.FindProperty("codeBase");
                                    if (assemblyFullPath)
                                    {
                                        std::vector<std::string> pathParts = StringSplit({ '/','\\' }, *assemblyFullPath);
                                        testDef.AssemblyName = pathParts.back();
                                        if (EndsWith(StringToLower(testDef.AssemblyName), ".dll"))
                                            testDef.AssemblyName.resize(testDef.AssemblyName.size() - 4);
                                    }
                                }
                            }
                        }

                        idToTestDefinition.emplace(StringToLower(testDef.Id), std::move(testDef));
                    }
                }
            }
        }

        return std::move(idToTestDefinition);
    }

    std::string FindRunFinishedTime(const XmlLexicon::Node &testRunNode)
    {
        for (const XmlLexicon::Node &testRunChild : testRunNode.Children)
        {
            if (testRunChild.Name == "Times")
            {
                const std::string *time = testRunChild.FindProperty("finish");
                if (time)
                    return *time;
            }
        }

        return std::string();
    }

    void WalkAllChildOutput(std::string &output, const XmlLexicon::Node &node, std::string prefix = "")
    {
        if (!node.Value.empty())
        {
            if (!output.empty())
                output.append("\r\n");

            if (!prefix.empty())
                output.append(prefix + ":\r\n");

            output.append(node.Value);
        }

        for (const XmlLexicon::Node &child : node.Children)
            WalkAllChildOutput(output, child, prefix.empty() ? child.Name : prefix + "." + child.Name);
    }
}

namespace TRX
{
    LogCollection ParseLogs(AppStatusMonitor &monitor, const std::vector<char> &rawDataToConsume, const ParserFilter &filter)
    {
        LogCollection logs;
        logs.IsRawRepresentationValid = false;

        logs.Columns.emplace_back("Date");
        logs.Columns.emplace_back("TestRunId");
        logs.Columns.emplace_back("Item");
        logs.Columns.emplace_back("ClassName");
        logs.Columns.emplace_back("TestName");
        logs.Columns.emplace_back("Outcome");
        logs.Columns.emplace_back("Duration");
        logs.Columns.emplace_back("Assembly");
        logs.Columns.emplace_back("Category");
        logs.Columns.emplace_back("Description");
        logs.Columns.emplace_back("Output");

        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(0, "xml files", 1);

        // rawDataToConsume should contain a complete xml file
        VectorToIStream xmlStreamWrapper { rawDataToConsume };
        std::istream xmlStream { &xmlStreamWrapper };
        XmlLexicon xml { xmlStream };

        for (const XmlLexicon::Node &testRunNode : xml.GetRoots())
        {
            if (testRunNode.Name == "TestRun")
            {
                std::string runId;
                std::string runFinishedTime = FindRunFinishedTime(testRunNode);
                std::unordered_map<std::string, TestDefinition> idToTestDefinition = FindTestDefinitions(testRunNode);

                const std::string *id = testRunNode.FindProperty("id");
                if (id)
                    runId = *id;

                for (const XmlLexicon::Node &testRunChild : testRunNode.Children)
                {
                    if (testRunChild.Name == "ResultSummary")
                    {
                        std::string extraData;
                        std::vector<LogEntryColumn> columnData;
                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_ITEMTYPE, "Summary");
                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_TESTRUNID, runId);
                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_DATE, runFinishedTime);

                        const std::string *outcome = testRunChild.FindProperty("outcome");
                        if (outcome)
                            AddColumnExtraData(columnData, extraData, COLUMNINDEX_OUTCOME, *outcome);

                        std::string resultsBlob;
                        for (const XmlLexicon::Node &resultSummaryChild : testRunChild.Children)
                        {
                            if (resultSummaryChild.Name == "Counters")
                            {
                                for (auto &kvp : resultSummaryChild.PropertyList)
                                {
                                    if (!resultsBlob.empty())
                                        resultsBlob.append(" ");

                                    resultsBlob.append(kvp.first + "=" + kvp.second);
                                }
                            }
                        }

                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_OUTPUT, resultsBlob);

                        logs.Lines.emplace_back(std::string(), extraData, std::vector<LogEntryColumn>(), columnData);
                    }
                    else if (testRunChild.Name == "Results")
                    {
                        for (const XmlLexicon::Node &resultsChild : testRunChild.Children)
                        {
                            if (resultsChild.Name == "UnitTestResult")
                            {
                                std::string extraData;
                                std::vector<LogEntryColumn> columnData;
                                AddColumnExtraData(columnData, extraData, COLUMNINDEX_ITEMTYPE, "TestResult");
                                AddColumnExtraData(columnData, extraData, COLUMNINDEX_TESTRUNID, runId);

                                const std::string *startTime = resultsChild.FindProperty("startTime");
                                if (startTime)
                                    AddColumnExtraData(columnData, extraData, COLUMNINDEX_DATE, *startTime);

                                const std::string *outcome = resultsChild.FindProperty("outcome");
                                if (outcome)
                                    AddColumnExtraData(columnData, extraData, COLUMNINDEX_OUTCOME, *outcome);

                                const std::string *duration = resultsChild.FindProperty("duration");
                                if (duration)
                                    AddColumnExtraData(columnData, extraData, COLUMNINDEX_DURATION, *duration);

                                const std::string *testId = resultsChild.FindProperty("testId");
                                if (testId)
                                {
                                    auto testDefIter = idToTestDefinition.find(StringToLower(*testId));
                                    if (testDefIter != idToTestDefinition.end())
                                    {
                                        const TestDefinition &testDef = testDefIter->second;
                                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_TESTASSEMBLY, testDef.AssemblyName);
                                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_TESTNAME, testDef.TestName);
                                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_CLASSNAME, testDef.ClassName);
                                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_DESCRIPTION, testDef.Description);
                                        AddColumnExtraData(columnData, extraData, COLUMNINDEX_TESTCATEGORY, testDef.Category);
                                    }
                                }
                                else
                                    AddColumnExtraData(columnData, extraData, COLUMNINDEX_TESTNAME, "TestIdNotFound??");

                                std::string output;
                                for (const XmlLexicon::Node &unitTestResultChild : resultsChild.Children)
                                {
                                    if (unitTestResultChild.Name == "Output")
                                        WalkAllChildOutput(output, unitTestResultChild);
                                }

                                AddColumnExtraData(columnData, extraData, COLUMNINDEX_OUTPUT, output);

                                logs.Lines.emplace_back(std::string(), extraData, std::vector<LogEntryColumn>(), columnData);
                            }
                        }
                    }
                }
            }
        }

        // done
        if (monitor.IsCancelling())
        {
            logs.Lines.clear();
            logs.Columns.clear();
        }

        monitor.Complete();
        return std::move(logs);
    }
}
