// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <cstddef>
#include <span>
#include <string>

// Returns the bytes of a cat frame baked into the executable, named the way CatResources.rc names
// them (e.g. "plain/idle1.png"). Returns an empty span if there's no such frame. The bytes live for
// the life of the process.
std::span<const std::byte> GetEmbeddedCatAsset(const std::string &name);
