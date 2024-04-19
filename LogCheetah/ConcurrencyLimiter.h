// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <mutex>
#include <condition_variable>

class ConcurrencyLimiter
{
public:
    ConcurrencyLimiter(size_t maxConcurrency);

    void lock();
    void unlock();

private:
    size_t allowedConcurrency;
    volatile size_t currentConcurrency = 0;
    std::mutex mut;
    std::condition_variable block;
};
