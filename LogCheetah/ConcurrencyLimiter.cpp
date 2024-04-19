// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "ConcurrencyLimiter.h"

ConcurrencyLimiter::ConcurrencyLimiter(size_t maxConcurrency)
{
    allowedConcurrency = maxConcurrency;
}

void ConcurrencyLimiter::lock()
{
    std::unique_lock<std::mutex> ul(mut);
    block.wait(ul, [this] {return currentConcurrency < allowedConcurrency; });
    ++currentConcurrency;
}

void ConcurrencyLimiter::unlock()
{
    {
        std::lock_guard<std::mutex> guard(mut);
        --currentConcurrency;
    }
    block.notify_all();
}
