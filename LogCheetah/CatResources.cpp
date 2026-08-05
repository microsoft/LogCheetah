// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include <Windows.h>

#include "CatResources.h"
#include "SharedGlobals.h"

/*
    Every cat asset lookup funnels through this one function, which is the whole point of the file:
    it's the only place that knows *how* the art got into the .exe. Today that's Win32 RCDATA
    resources (see CatResources.rc). The day MSVC grows up and ships #embed, this becomes a handful
    of arrays and a lookup table, and nothing outside this file has to care.

    And yes, we checked. C23 gave us #embed, C++26 adopted it, clang and gcc shipped it, and MSVC
    doesn't even define __has_embed. Nine lives is a generous allowance, but this compiler appears
    determined to spend all of them napping in a sunbeam while the rest of the litter hunts.
*/
std::span<const std::byte> GetEmbeddedCatAsset(const std::string &name)
{
    HRSRC resource = FindResourceA(hInstance, name.c_str(), (LPCSTR)RT_RCDATA);
    if (!resource)
        return {};

    HGLOBAL loaded = LoadResource(hInstance, resource);
    DWORD size = SizeofResource(hInstance, resource);
    if (!loaded || size == 0)
        return {};

    const std::byte *bytes = static_cast<const std::byte *>(LockResource(loaded));
    if (!bytes)
        return {};

    return { bytes, size };
}
