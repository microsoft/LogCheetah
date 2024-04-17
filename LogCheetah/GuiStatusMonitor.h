#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <list>
#include <vector>
#include <mutex>
#include <functional>
#include "SharedGlobals.h"

//monitor for a single part within a section
class GuiStatusMonitor : public AppStatusMonitor
{
public:
    GuiStatusMonitor() = default;
    GuiStatusMonitor(const GuiStatusMonitor&) = delete;
    GuiStatusMonitor& operator=(const GuiStatusMonitor&) = delete;
    GuiStatusMonitor(GuiStatusMonitor&&) = default;
    GuiStatusMonitor& operator=(GuiStatusMonitor&&) = default;

    //AppStatusMonitor
    void SetControlFeatures(bool isCancellable) override;
    void SetProgressFeatures(uint64_t maxProgress, const std::string speedUnitIfVisible = std::string(), uint64_t speedDivisor = 1) override;
    void AddProgress(uint64_t amount) override;
    void Complete() override;
    bool IsCancelling() const override;
    void AddDebugOutput(const std::string &str) override;

    //accessors we need
    uint64_t PollProgress(bool returnMaxIfComplete) const;
    uint64_t MaxProgress() const;
    uint64_t SpeedDivider() const;
    std::string SpeedUnit() const;
    bool CanCancel() const;

private:
    bool canCancel = false;
    uint64_t progressMax = 0;
    std::atomic<uint64_t> progressCur = 0;
    std::string speedUnit;
    uint64_t speedDivider = 1;
    bool forcedComplete = false;

    mutable std::mutex mut;
};

class DebugStatusOnlyMonitor : public AppStatusMonitor
{
public:
    bool IsCancelling() const override;
    void AddDebugOutput(const std::string &str) override;

    static DebugStatusOnlyMonitor Instance;
};

//manages a single section of the busy dialog which represents a single activity that may be comprised of homogenous parts
class GuiStatusSection
{
public:
    GuiStatusSection() = delete;
    GuiStatusSection(const GuiStatusSection&) = delete;
    GuiStatusSection& operator=(const GuiStatusSection&) = delete;
    GuiStatusSection(GuiStatusSection&&) = default;
    GuiStatusSection& operator=(GuiStatusSection&&) = default;

    GuiStatusSection(const std::string &activity, size_t partCount);

    void ChangeName(const std::string &newName);
    void SetAsWriteOperation(bool isWriteOperation);
    void SetZeroSpeedMessage(const std::string &messageForZeroSpeed);

    size_t PartCount() const;
    GuiStatusMonitor& PartIndex(size_t i);

    std::string Activity() const;
    bool IsWriteOperation() const;

    uint64_t TotalProgress() const;
    uint64_t BalancedProgressCurrent() const;
    uint64_t BalancedProgressMax() const;

    uint64_t SpeedDivider() const;
    std::string SpeedUnit() const;
    std::string ZeroSpeedMessage() const;

    bool AggregateCancellable() const;

private:
    mutable std::mutex mut;
    std::string sectionActivity;
    std::vector<std::unique_ptr<GuiStatusMonitor>> parts;
    std::string zeroSpeedMessage;
    bool writeOperation = true;
};

//manages the busy dialog, and the list of sections shown within in it
class GuiStatusManager
{
public:
    static void ShowBusyDialogAndRunText(const std::string &simpleBusyName, bool isWriteOperation, std::function<void(void)> work);
    static void ShowBusyDialogAndRunMonitor(const std::string &simpleBusyName, bool isWriteOperation, std::function<void(GuiStatusMonitor&)> work);
    static void ShowBusyDialogAndRunManager(std::function<void(GuiStatusManager&)> work);

    std::shared_ptr<GuiStatusSection> AddSection(const std::string &activity, size_t partCount);
    void RemoveSection(std::shared_ptr<GuiStatusSection> &section);

    static bool IsBusyWithWrites();
    static bool IsBusyAnything();
    bool IsCancelling() const;

    //prefer to use this over manual Add/Remove calls
    class AutoSection
    {
    public:
        AutoSection() = delete;
        AutoSection(const AutoSection&) = delete;
        AutoSection& operator=(const AutoSection&) = delete;

        inline AutoSection(GuiStatusManager &manager, bool isWriteOperation, const std::string &activity, size_t partCount) : man(manager)
        {
            section = man.AddSection(activity, partCount);
            section->SetAsWriteOperation(isWriteOperation);
        }

        inline ~AutoSection()
        {
            man.RemoveSection(section);
        }

        inline GuiStatusSection& Section()
        {
            return *section;
        }

    private:
        std::shared_ptr<GuiStatusSection> section;
        GuiStatusManager &man;
    };

private:
    GuiStatusManager() = default;

    static GuiStatusManager global;
};
