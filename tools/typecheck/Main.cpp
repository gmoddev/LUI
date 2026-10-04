#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/TypeInfer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
constexpr size_t MaxBytes = 8 * 1024 * 1024;
constexpr size_t MaxDiagnostics = 256;
constexpr size_t MaxMessageBytes = 2048;

std::string Read(std::istream& Input) {
    std::string Value;
    char Block[4096];
    while (Input.read(Block, sizeof(Block)) || Input.gcount()) {
        Value.append(Block, static_cast<size_t>(Input.gcount()));
        if (Value.size() > MaxBytes) throw std::runtime_error("Input exceeds 8 MiB");
    }
    if (Input.bad()) throw std::runtime_error("Unable to read input");
    if (Value.find('\0') != std::string::npos) throw std::runtime_error("Input contains a NUL character");
    return Value;
}

void Quote(const std::string& Value) {
    const char* Hex = "0123456789abcdef";
    std::cout << '"';
    for (unsigned char Character : Value) {
        if (Character == '"' || Character == '\\') std::cout << '\\' << Character;
        else if (Character < 32) std::cout << "\\u00" << Hex[Character >> 4] << Hex[Character & 15];
        else std::cout << Character;
    }
    std::cout << '"';
}

// Luau locations count UTF-8 bytes. The tooling protocol uses zero-based UTF-16 columns.
unsigned Column(const std::string& Source, const std::vector<size_t>& Lines, Luau::Position Position) {
    if (Position.line >= Lines.size()) return 0;
    size_t Start = Lines[Position.line];
    size_t End = std::min(Start + Position.column, Source.size());
    unsigned Units = 0;
    for (size_t Index = Start; Index < End && Source[Index] != '\n'; ++Index) {
        unsigned char Byte = static_cast<unsigned char>(Source[Index]);
        if ((Byte & 0xc0) != 0x80) Units += Byte >= 0xf0 ? 2 : 1;
    }
    return Units;
}

struct Resolver final : Luau::FileResolver {
    std::string Source;
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& Name) override {
        if (Name != "entry") return std::nullopt;
        return Luau::SourceCode{Source, Luau::SourceCode::Script};
    }
};
}

int main(int Count, char** Arguments) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        if (Count != 2) throw std::runtime_error("Usage: LuiTypeCheck <LUI.d.luau>; source is read from stdin");
        std::ifstream File(Arguments[1], std::ios::binary);
        if (!File) throw std::runtime_error("Generated LUI definitions were not found");
        const std::string Definitions = Read(File);
        Resolver Scripts;
        Scripts.Source = Read(std::cin);
        Luau::NullConfigResolver Config;
        Config.defaultConfig.mode = Luau::Mode::Strict;
        Luau::Frontend Frontend(Luau::SolverMode::New, &Scripts, &Config);
        Luau::unfreeze(Frontend.globals.globalTypes);
        Luau::registerBuiltinGlobals(Frontend, Frontend.globals);
        const auto Loaded = Frontend.loadDefinitionFile(
            Frontend.globals, Frontend.globals.globalScope, Definitions, "LUI", false);
        Luau::freeze(Frontend.globals.globalTypes);
        if (!Loaded.success) throw std::runtime_error("Generated LUI definitions failed validation");
        Luau::FrontendOptions Options;
        Options.moduleTimeLimitSec = 10.0;
        const auto Result = Frontend.check("entry", Options);
        if (!Result.timeoutHits.empty()) throw std::runtime_error("Luau type analysis exceeded its time limit");
        std::vector<size_t> Lines{0};
        for (size_t Index = 0; Index < Scripts.Source.size(); ++Index)
            if (Scripts.Source[Index] == '\n') Lines.push_back(Index + 1);
        bool Truncated = Result.errors.size() > MaxDiagnostics;
        std::cout << "{\"version\":1,\"diagnostics\":[";
        for (size_t Index = 0; Index < std::min(Result.errors.size(), MaxDiagnostics); ++Index) {
            const auto& Error = Result.errors[Index];
            std::string Message = Luau::toString(Error);
            if (Message.size() > MaxMessageBytes) {
                size_t End = MaxMessageBytes;
                while (End && (static_cast<unsigned char>(Message[End]) & 0xc0) == 0x80) --End;
                Message.resize(End);
                Truncated = true;
            }
            if (Index) std::cout << ',';
            std::cout << "{\"kind\":\"" << (Luau::get<Luau::SyntaxError>(Error) ? "syntax" : "type")
                      << "\",\"message\":";
            Quote(Message);
            std::cout << ",\"range\":{\"start\":{\"line\":" << Error.location.begin.line
                      << ",\"column\":" << Column(Scripts.Source, Lines, Error.location.begin)
                      << "},\"end\":{\"line\":" << Error.location.end.line
                      << ",\"column\":" << Column(Scripts.Source, Lines, Error.location.end) << "}}}";
        }
        std::cout << "],\"truncated\":" << (Truncated ? "true" : "false") << "}\n";
        return Result.errors.empty() ? 0 : 1;
    } catch (const std::exception& Error) {
        std::cerr << "[LUI:TypeCheck] " << Error.what() << '\n';
        return 2;
    }
}
