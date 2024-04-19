// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "ObtainParseCoordinator.h"
#include "GuiStatusMonitor.h"
#include "ConcurrencyLimiter.h"
#include "GenericTextLogParseRouter.h"
#include "DebugWindow.h"
#include "Preferences.h"
#include <future>
#include <atomic>

LogCollection ObtainRawDataAndParse(const std::string &obtainDescription, std::vector<ObtainerSource> obtainers, size_t maxObtainParallelism, const ParserFilter &filter)
{
    if (obtainers.empty())
        return LogCollection();

    LogCollection finalLogs;

    auto overallTimeStart = std::chrono::high_resolution_clock::now();

    GuiStatusManager::ShowBusyDialogAndRunManager([&](GuiStatusManager &manager)
    {
        //set up status monitors
        VMUState vmu(0);
        GuiStatusManager::AutoSection statusObtain { manager, true, obtainDescription, obtainers.size() };
        std::unique_ptr<GuiStatusManager::AutoSection> statusLineParse;
        statusLineParse = std::make_unique<GuiStatusManager::AutoSection>(manager, true, "Parsing Lines From Data", obtainers.size());
        statusLineParse->Section().SetZeroSpeedMessage("Waiting");

        GuiStatusManager::AutoSection statusLogParse { manager, true, "Parsing Logs", obtainers.size() };
        statusLogParse.Section().SetZeroSpeedMessage("Waiting");

        for (size_t i = 0; i < obtainers.size(); ++i)
        {
            auto &monitorObtain = statusObtain.Section().PartIndex(i);
            monitorObtain.SetProgressFeatures(1);

            if (statusLineParse)
            {
                auto &monitorLineParse = statusLineParse->Section().PartIndex(i);
                monitorLineParse.SetControlFeatures(true);
                monitorLineParse.SetProgressFeatures(1);
            }

            auto &monitorLogParse = statusLogParse.Section().PartIndex(i);
            monitorLogParse.SetProgressFeatures(1);
        }

        //set up the obtainers
        std::vector<std::future<std::vector<char>>> futureObtains;
        futureObtains.resize(obtainers.size());
        std::vector<bool> futureObtainDone;
        futureObtainDone.resize(obtainers.size(), false);

        for (size_t i = 0; i < obtainers.size(); ++i)
        {
            futureObtains[i] = std::async(std::launch::deferred, [&, i]()
            {
                auto &monitor = statusObtain.Section().PartIndex(i);
                if (monitor.IsCancelling())
                    return std::vector<char>();

                auto &&ret = obtainers[i].Obtain(monitor);
                monitor.Complete();
                return std::move(ret);
            });
        }

        //start obtaining data
        std::mutex dataReadyMut;
        std::vector<std::pair<size_t, std::vector<char>>> dataReady;
        std::atomic<size_t> nextFutureIndex = 0;
        std::atomic<size_t> futuresComplete = 0;
        std::vector<std::thread> obtainThreads;
        size_t obtainThreadCount = maxObtainParallelism == 0 ? Preferences::ParallelismOverrideGeneral : maxObtainParallelism;
        for (size_t i = 0; i < obtainThreadCount; ++i)
        {
            obtainThreads.emplace_back([&]
            {
                for (;;)
                {
                    if (!VerifyMemoryUse(vmu))
                        break;

                    size_t futureIndex = nextFutureIndex++;
                    if (futureIndex >= futureObtains.size())
                        break;

                    std::vector<char> data = futureObtains[futureIndex].get();
                    if (!data.empty())
                    {
                        std::lock_guard<std::mutex> guard(dataReadyMut);
                        dataReady.emplace_back(futureIndex, std::move(data));
                    }
                    ++futuresComplete;
                }
            });
        }

        //pass obtained data along to parsers
        for (;;)
        {
            if (!VerifyMemoryUse(vmu))
                break;

            //grab the next block of data that's ready
            std::vector<char> data;
            size_t obtainerIndex = 0;
            {
                std::lock_guard<std::mutex> guard(dataReadyMut);
                if (!dataReady.empty())
                {
                    obtainerIndex = dataReady.back().first;
                    data = std::move(dataReady.back().second);
                    dataReady.pop_back();
                }
            }

            if (!data.empty())
            {
                //find the parser for this, then transform to lines and parse
                ParserInterface &parser = DetermineTextLogParser(data, obtainers[obtainerIndex].LogTypeIfKnown);
                statusLogParse.Section().ChangeName("Parsing " + parser.Name + " Logs");

                if (!obtainers[obtainerIndex].AdditionalSchemaData.empty())
                {
                    statusLogParse.Section().PartIndex(obtainerIndex).AddDebugOutput("Loading additional schema data");
                    parser.LoadSchemaData(DebugStatusOnlyMonitor::Instance, obtainers[obtainerIndex].AdditionalSchemaData);
                }
                finalLogs = parser.ProcessRawData(statusLineParse ? (AppStatusMonitor&)statusLineParse->Section().PartIndex(obtainerIndex) : (AppStatusMonitor&)DebugStatusOnlyMonitor::Instance, statusLogParse.Section().PartIndex(obtainerIndex), DebugStatusOnlyMonitor::Instance, std::move(finalLogs), std::move(data), filter);
            }

            //are we done?
            {
                std::lock_guard<std::mutex> guard(dataReadyMut);
                if (futuresComplete == futureObtains.size() && dataReady.empty())
                    break;
            }

            if (data.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        for (auto &t : obtainThreads)
            t.join();
    });

    auto overallTimeEnd = std::chrono::high_resolution_clock::now();
    GlobalDebugOutput("ObtainRawDataAndParse total time: " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(overallTimeEnd - overallTimeStart).count()) + "ms");

    return std::move(finalLogs);
}
