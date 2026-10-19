// D1 (RPG Tsukuru Dante98) engine entry point.
#pragma once
#include <memory>
#include <string>

#include "core/host.h"

namespace d1 {

// Loads all game data (throws core::DataError on failure) and returns an engine ready to run.
std::unique_ptr<core::Engine> createEngine(core::Host& host, const std::string& root, const core::StartOptions& opt);

}  // namespace d1
