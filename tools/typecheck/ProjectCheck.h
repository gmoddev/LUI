#pragma once
#include "Project.h"
#include <cstdint>
#include <functional>

namespace Lui::TypeCheck {
struct Position { uint32_t Line = 0, Column = 0; };
struct Diagnostic {
    std::string Source, Kind, Code, Message;
    Position Start, End;
};
struct Result {
    std::string SourceDigest, DefinitionsDigest, Digest;
    std::vector<Diagnostic> Diagnostics;
    bool Truncated = false;
};
struct Options {
    bool DevelopmentDiagnostics = false;
    double ModuleSeconds = 10.0;
    std::function<bool()> Canceled;
};
// Captured-data analysis only; never enters a VM or loads an extension.
std::shared_ptr<const Result> Check(const Lui::Project::Snapshot& Sources, const std::string& Definitions,
    const Options& Settings, std::string& Error);
std::string Serialize(const Lui::Project::Snapshot& Sources, const Result& Value, uint64_t Generation = 0);
}
