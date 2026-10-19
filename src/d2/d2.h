// D2 (RPG Tsukuru Dante98 II) engine entry point.
#pragma once
#include <memory>
#include <string>

#include "core/host.h"

namespace d2 {

// Loads all game data (throws core::DataError on failure) and returns an engine ready to run.
std::unique_ptr<core::Engine> createEngine(core::Host& host, const std::string& root, const core::StartOptions& opt);

// RPG.EXE build label for the launcher: one of "1996-06", "1996-08", "1997-03", "1997-10"
// (spec 20, build detection; unknown builds are "1997-10").
std::string buildLabel(const std::string& root);

}  // namespace d2
