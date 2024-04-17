#include "DialogOpenLocal.h"
#include "DialogPickLogFormat.h"
#include "WinMain.h"
#include "Globals.h"
#include "GuiStatusMonitor.h"
#include "GenericTextLogParseRouter.h"
#include "ObtainParseCoordinator.h"

#include <Windows.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cctype>
#include <map>

void DoLoadLogsFromFileBatchWorker(const std::vector<std::string> &files, std::vector<LogType> fileLogType, bool merge)
{
    if (!merge)
        MoveAndLoadLogs(LogCollection(), false); //clear old logs before we start to reduce memory use

    //load schema data
    std::vector<std::string> additionalSchemas;
    additionalSchemas.resize(files.size());
    GuiStatusManager::ShowBusyDialogAndRunMonitor("Loading Schema Files From Disk", true, [&](GuiStatusMonitor &monitor)
    {
        monitor.SetControlFeatures(true);
        monitor.SetProgressFeatures(files.size());

        for (size_t fileNumber = 0; fileNumber < files.size(); ++fileNumber)
        {
            if (monitor.IsCancelling())
                break;

            size_t extensionLength = std::filesystem::path(files[fileNumber]).extension().string().size();
            std::string fileWithoutExtension = files[fileNumber];
            if (extensionLength > 0 && fileWithoutExtension.size() > extensionLength)
                fileWithoutExtension.resize(fileWithoutExtension.size() - extensionLength);

            std::ifstream schemaFile(fileWithoutExtension + ".schema");
            if (schemaFile.is_open())
            {
                std::streampos startPos = schemaFile.tellg();
                schemaFile.seekg(0, std::ios::end);
                std::streampos endPos = schemaFile.tellg();
                schemaFile.seekg(startPos, std::ios::beg);

                std::string s;
                s.reserve(endPos - startPos);
                s.assign(std::istreambuf_iterator<char>(schemaFile), std::istreambuf_iterator<char>());

                additionalSchemas[fileNumber] = std::move(s);
            }

            monitor.AddProgress(1);
        }
    });

    //set us up the obtainers, and obtain
    std::vector<ObtainerSource> obtainers;

    for (size_t fileNumber = 0; fileNumber < files.size(); ++fileNumber)
    {
        obtainers.emplace_back();
        obtainers.back().Obtain = [&, fileNumber](AppStatusMonitor &monitor)
        {
            monitor.SetControlFeatures(true);
            monitor.SetProgressFeatures(0, "MB", 1000000);

            std::vector<char> rawData;
            size_t bytesReadSoFar = 0, bytesReadForLastUpdate = 0;

            std::ifstream file(files[fileNumber], std::ios::binary | std::ios::in);
            if (file.is_open())
            {
                //first determine the size so we can make a guess to prealloc
                std::streampos fileStartPos = file.tellg();
                file.seekg(0, std::ios::end);
                std::streampos fileEndPos = file.tellg();
                file.seekg(0, std::ios::beg);

                monitor.SetProgressFeatures(fileEndPos - fileStartPos, "MB", 1000000);
                rawData.reserve(fileEndPos - fileStartPos);

                //read all data
                std::array<char, 4096> temp;

                while (!file.eof() && !monitor.IsCancelling())
                {
                    file.read(temp.data(), temp.size());
                    size_t readAmount = file.gcount();
                    rawData.insert(rawData.end(), temp.begin(), temp.begin() + readAmount);

                    bytesReadSoFar += readAmount;
                    if (bytesReadSoFar - bytesReadForLastUpdate >= 100000)
                    {
                        monitor.AddProgress(bytesReadSoFar - bytesReadForLastUpdate);
                        bytesReadForLastUpdate = bytesReadSoFar;
                    }
                }
            }
            else
                monitor.AddDebugOutput("Failed to read from file " + files[fileNumber]);

            monitor.Complete();
            return std::move(rawData);
        };
        obtainers.back().LogTypeIfKnown = fileLogType[fileNumber];
        obtainers.back().AdditionalSchemaData = additionalSchemas[fileNumber];
    }

    auto newLogs = ObtainRawDataAndParse("Reading " + std::to_string(files.size()) + " Files from Disk", obtainers, 1, ParserFilter());
    MoveAndLoadLogs(std::move(newLogs), merge);
}

void DoLoadLogsFromFileDialog()
{
    std::vector<char> tempFilenameBuffer;
    tempFilenameBuffer.resize(MAX_PATH * 1024);

    OPENFILENAME ofn = { 0 };
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = activeMainWindow;
    ofn.lpstrFilter = "Normal Log Files (*.log, *.psv, *.csv, *.tsv)\0*.log;*psv;*.csv;*.tsv\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrFile = tempFilenameBuffer.data();
    ofn.nMaxFile = (DWORD)tempFilenameBuffer.size();

    std::string loadPath;
    if (!RunLoadSaveDialog(false, ofn, loadPath))
        return;

    std::vector<std::string> allFiles;

    std::string path = std::string(ofn.lpstrFile, ofn.lpstrFile + ofn.nFileOffset - 1);

    char *p = ofn.lpstrFile + ofn.nFileOffset;
    if (!*p)
        ++p;

    while (true)
    {
        std::stringstream filenameStream;
        filenameStream << path;
        filenameStream << '\\';

        while (*p)
        {
            filenameStream << *p;
            ++p;
        }

        allFiles.emplace_back(filenameStream.str());

        ++p;
        if (!*p)
            break;
    }

    DoLoadLogsFromFileBatch(allFiles);
}

void DoLoadLogsFromFileBatch(const std::vector<std::string> &files)
{
    bool merge = false;
    if (!globalLogs.Lines.empty())
        merge = MessageBox(hwndMain, "Merge new logs with existing logs?", "", MB_YESNO) == IDYES;

    //if we any non-evl files, ask for their format
    std::vector<LogType> logTypes;
    logTypes.resize(files.size());
    bool hasAskedDefaultType = false;
    LogType defaultLogType = LogType::Unknown;
    for (size_t i = 0; i < files.size(); ++i)
    {
        logTypes[i] = IdentifyLogTypeFromFileExtension(files[i]);
        if (logTypes[i] == LogType::Unknown)
        {
            if (!hasAskedDefaultType)
            {
                hasAskedDefaultType = true;
                defaultLogType = DoAskForLogType(files[i], false);
            }

            logTypes[i] = defaultLogType;
        }
    }

    DoLoadLogsFromFileBatchWorker(files, logTypes, merge);
}
