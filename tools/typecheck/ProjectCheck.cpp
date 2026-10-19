#include "ProjectCheck.h"
#include "Imports.h"
#include "Files.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/ConfigResolver.h"
#include "Luau/Frontend.h"
#include "Luau/Constraint.h"
#include "Luau/ConstraintSolver.h"
#include "Luau/Parser.h"
#include "Luau/TypeInfer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <thread>
#include <set>
#include <tuple>

namespace Lui::TypeCheck {
namespace {
using Json = nlohmann::json;
constexpr size_t MaxDiagnostics = 256, MaxMessage = 2048, MaxOutput = 1024 * 1024;
uint64_t Key(Luau::Position Value) { return (uint64_t(Value.line) << 32) | Value.column; }
struct Sites : Luau::AstVisitor {
    std::set<uint64_t> Positions;
    bool TypeFunction = false;
    bool visit(Luau::AstStatTypeFunction*) override { TypeFunction = true; return false; }
    bool visit(Luau::AstExprCall* Call) override {
        if (const auto* Literal = Lui::Project::GetLiteralImport(Call)) Positions.insert(Key(Literal->location.begin));
        return true;
    }
};
struct SourceImports { bool Rebound = false, TypeFunction = false, NoCheck = false; std::set<uint64_t> Sites; };
Position Convert(const std::string& Text, Luau::Position Value) {
    size_t Offset = 0; uint32_t Line = 0;
    while (Line < Value.line && Offset < Text.size()) if (Text[Offset++] == '\n') ++Line;
    size_t End = std::min(Text.size(), Offset + Value.column); uint32_t Column = 0;
    for (size_t Index = Offset; Index < End && Text[Index] != '\n'; ++Index) {
        const auto Byte = static_cast<unsigned char>(Text[Index]);
        if ((Byte & 0xc0) != 0x80) Column += Byte >= 0xf0 ? 2 : 1;
    }
    return {Line,Column};
}
const char* ProjectCode(const std::string& Error) {
    for (const auto* Code : {"Cycle","InvalidPath","MissingModule","ForbiddenImport","EntryImportDenied","CompileFailed","Canceled","LimitExceeded"})
        if (Error == std::string("[LUI:Project] ")+Code) return Code;
    return "InvalidInput";
}
void Add(Result& Output, const Lui::Project::Snapshot& Sources, const std::string& Id,
    const char* Kind, const char* Code, std::string Message, Luau::Location Location = {}) {
    if (Output.Diagnostics.size() >= MaxDiagnostics) { Output.Truncated = true; return; }
    // Decoded Luau string literals can contain non-UTF-8 bytes. Project IPC
    // remains valid UTF-8 even when an analyzer message includes such a name.
    const auto Encoded=Json(Message).dump(-1,' ',false,Json::error_handler_t::replace);
    Message=Json::parse(Encoded).get<std::string>();
    if (Message.size() > MaxMessage) {
        size_t End = MaxMessage;
        while (End && (static_cast<unsigned char>(Message[End]) & 0xc0) == 0x80) --End;
        Message.resize(End); Output.Truncated = true;
    }
    Diagnostic Value{Id,Kind,Code,std::move(Message)};
    if (const auto* Source = Sources.GetSource(Id)) {
        Value.Start = Convert(Source->Bytes, Location.begin); Value.End = Convert(Source->Bytes, Location.end);
    }
    Output.Diagnostics.push_back(std::move(Value));
}
struct Resolver : Luau::FileResolver {
    Resolver(const Lui::Project::Snapshot& Sources, Result& Output) : Sources(Sources), Output(Output) {}
    const Lui::Project::Snapshot& Sources;
    Result& Output;
    std::map<std::string,SourceImports> Imports;
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& Name) override {
        const auto* Source = Sources.GetSource(Name); if (!Source) return std::nullopt;
        if (!Imports.count(Name)) {
            Luau::Allocator Allocator; Luau::AstNameTable Names(Allocator);
            const auto Parsed = Luau::Parser::parse(Source->Bytes.data(),Source->Bytes.size(),Names,Allocator);
            SourceImports Info;
            Info.NoCheck=Luau::parseMode(Parsed.hotcomments)==Luau::Mode::NoCheck;
            if (Parsed.root) {
                Info.Rebound = Lui::Project::RequireRebound(Parsed.root);
                Sites Visitor; Parsed.root->visit(&Visitor); Info.TypeFunction=Visitor.TypeFunction;
                if (!Info.Rebound) Info.Sites=std::move(Visitor.Positions);
            }
            Imports.emplace(Name,std::move(Info));
        }
        return Luau::SourceCode{Source->Bytes, Name == Sources.GetEntry() ? Luau::SourceCode::Script : Luau::SourceCode::Module};
    }
    std::optional<Luau::ModuleInfo> resolveModule(const Luau::ModuleInfo* Context, Luau::AstExpr* Expression,
        const Luau::TypeCheckLimits&) override {
        auto* Literal = Expression->as<Luau::AstExprConstantString>();
        if (!Context || !Literal || !Imports.count(Context->name) ||
            !Imports.at(Context->name).Sites.count(Key(Literal->location.begin))) return std::nullopt;
        std::string Error;
        const auto* Source = Sources.Resolve(Context->name,std::string_view(Literal->value.data,Literal->value.size),Error);
        if (!Source) {
            const auto* Code=ProjectCode(Error);
            Add(Output,Sources,Context->name,"import",Code,std::string("[LUI:Project] ")+Code,Literal->location);
            return std::nullopt;
        }
        return Luau::ModuleInfo{Source->Id};
    }
};
struct RequireMagic : Luau::MagicFunction {
    explicit RequireMagic(Resolver& Files) : Files(Files) {}
    Resolver& Files;
    Luau::MagicRequire Builtin;
    std::optional<Luau::WithPredicate<Luau::TypePackId>> handleOldSolver(Luau::TypeChecker&,
        const std::shared_ptr<Luau::Scope>&, const Luau::AstExprCall&, Luau::WithPredicate<Luau::TypePackId>) override { return std::nullopt; }
    bool infer(const Luau::MagicFunctionCallContext& Context) override {
        const auto* Literal = Lui::Project::GetLiteralImport(Context.callSite.get());
        const auto& Name = Context.constraint->moduleName ? *Context.constraint->moduleName : Context.solver->module->name;
        if (!Literal || !Files.Imports.count(Name) || Files.Imports.at(Name).Rebound) return false;
        return Builtin.infer(Context);
    }
};
struct DeadlineGuard {
    std::mutex Mutex; std::condition_variable Wake; bool Stopped=false;
    std::shared_ptr<Luau::FrontendCancellationToken> Token=std::make_shared<Luau::FrontendCancellationToken>();
    std::thread Worker;
    explicit DeadlineGuard(std::chrono::steady_clock::time_point End) {
        Token->cancelled.store(false);
        Worker=std::thread([this,End] {
            std::unique_lock<std::mutex> Lock(Mutex);
            if (!Wake.wait_until(Lock,End,[this] { return Stopped; })) Token->cancel();
        });
    }
    ~DeadlineGuard() { { std::lock_guard<std::mutex> Lock(Mutex); Stopped=true; } Wake.notify_all(); Worker.join(); }
};
bool ContainsNil(Luau::TypeId Value, Luau::TypeId Nil, std::set<Luau::TypeId>& Seen) {
    Value=Luau::follow(Value); if (Value==Nil) return true;
    if (!Seen.insert(Value).second || Seen.size()>256) return false;
    if (const auto* Union=Luau::get<Luau::UnionType>(Value))
        for (auto Option:Union->options) if (ContainsNil(Option,Nil,Seen)) return true;
    return false;
}
}
std::shared_ptr<const Result> Check(const Lui::Project::Snapshot& Sources, const std::string& Definitions,
    const Options& Settings, std::string& Error) {
    Error.clear();
    auto Fail=[&Error](const char* Code)->std::shared_ptr<const Result> { Error=std::string("[LUI:TypeCheck] ")+Code; return {}; };
    try {
        if (!Settings.DevelopmentDiagnostics || Json::parse(Sources.GetDescriptor()).at("Manifest").at("Diagnostics").at("Mode") != "Development")
            return Fail("PolicyNotSupported");
        if (Definitions.empty() || Definitions.size()>8*1024*1024 || !Lui::Project::Internal::Utf8(Definitions) ||
            !Sources.GetSource(Sources.GetEntry()) || !std::isfinite(Settings.ModuleSeconds) || Settings.ModuleSeconds<=0 || Settings.ModuleSeconds>10) return Fail("InvalidInput");
        const auto Deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        auto Canceled=[&] { return std::chrono::steady_clock::now()>Deadline || (Settings.Canceled && Settings.Canceled()); };
        if (Canceled()) return Fail("Canceled");
        const auto EffectiveDefinitions=Lui::Project::MergeDefinitions(Sources.GetDeclarations(),Definitions);
        auto Output=std::make_shared<Result>(); Output->SourceDigest=Sources.GetDigest(); Output->DefinitionsDigest=Lui::Project::Hash(EffectiveDefinitions);
        Output->Digest=Lui::Project::Hash(Json{{"SourceDigest",Output->SourceDigest},{"DefinitionsDigest",Output->DefinitionsDigest},
            {"Profile","ProjectTypesV1"},{"LuauRevision",Json::parse(Sources.GetDescriptor()).at("LuauRevision")}}.dump());
        // Refuse known cycles before asking the analyzer to infer cyclic exports.
        std::string Validation; Lui::Project::Options Bounds; Bounds.Canceled=Canceled;
        if (!Lui::Project::ValidateImports(Sources,Bounds,Validation) && ProjectCode(Validation)==std::string("Cycle")) {
            Add(*Output,Sources,"","project","Cycle","[LUI:Project] Cycle"); return Output;
        }
        if (Canceled()) return Fail("Canceled");
        Resolver Files(Sources,*Output);
        // User type-function bodies would execute during analysis. This initial
        // profile rejects them before the frontend is allowed to evaluate any.
        for (const auto& Pair:Sources.GetSources()) {
            if (Canceled()) return Fail("Canceled");
            Files.readSource(Pair.first);
            if (Files.Imports.at(Pair.first).TypeFunction || Files.Imports.at(Pair.first).NoCheck) return Fail("UnsupportedFeature");
        }
        {
            Luau::Allocator Allocator; Luau::AstNameTable Names(Allocator); Luau::ParseOptions Parse;
            Parse.allowDeclarationSyntax=true;
            const auto Parsed=Luau::Parser::parse(EffectiveDefinitions.data(),EffectiveDefinitions.size(),Names,Allocator,Parse);
            if (Parsed.root) { Sites Visitor; Parsed.root->visit(&Visitor); if (Visitor.TypeFunction) return Fail("UnsupportedFeature"); }
        }
        Luau::NullConfigResolver Config; Config.defaultConfig.mode=Luau::Mode::Strict;
        Luau::Frontend Frontend(Luau::SolverMode::New,&Files,&Config);
        Luau::unfreeze(Frontend.globals.globalTypes); Luau::registerBuiltinGlobals(Frontend,Frontend.globals);
        const auto Loaded=Frontend.loadDefinitionFile(Frontend.globals,Frontend.globals.globalScope,EffectiveDefinitions,"LUI",false);
        if (!Loaded.success) return Fail("DefinitionsInvalid");
        auto Binding=Frontend.globals.globalScope->linearSearchForBinding("require");
        if (!Binding) return Fail("DefinitionsInvalid");
        Luau::attachMagicFunction(Binding->typeId,std::make_shared<RequireMagic>(Files));
        Luau::freeze(Frontend.globals.globalTypes);
        DeadlineGuard Guard(Deadline);
        Luau::FrontendOptions Limits; Limits.moduleTimeLimitSec=Settings.ModuleSeconds; Limits.cancellationToken=Guard.Token;
        // Check every declared file, including modules not reachable from entry.
        for (const auto& Pair:Sources.GetSources()) {
            if (Canceled()) return Fail("Canceled");
            const auto Checked=Frontend.check(Pair.first,Limits);
            if (Guard.Token->requested()) return Fail("LimitExceeded");
            if (!Checked.timeoutHits.empty()) return Fail("LimitExceeded");
        }
        for (const auto& Pair:Sources.GetSources()) {
            auto Module=Frontend.moduleResolver.getModule(Pair.first);
            if (!Module) return Fail("AnalysisFailed");
            for (const auto& Issue:Module->errors) Add(*Output,Sources,Pair.first,Luau::get<Luau::SyntaxError>(Issue)?"syntax":"type",
                Luau::get<Luau::SyntaxError>(Issue)?"SyntaxError":"TypeError",
                Luau::get<Luau::SyntaxError>(Issue)?"[LUI:TypeCheck] Invalid syntax":"[LUI:TypeCheck] Type mismatch",Issue.location);
            if (Pair.first!=Sources.GetEntry() && Module->errors.empty() && Module->returnType) {
                const auto Pack=Luau::flatten(Module->returnType);
                std::set<Luau::TypeId> Seen;
                if ((!Pack.second && Pack.first.size()!=1) || (Pack.first.size()==1 && ContainsNil(Pack.first[0],Frontend.builtinTypes->nilType,Seen)))
                    Add(*Output,Sources,Pair.first,"module","InvalidResult","[LUI:Modules] InvalidResult");
            }
        }
        std::sort(Output->Diagnostics.begin(),Output->Diagnostics.end(),[](const Diagnostic& A,const Diagnostic& B) {
            return std::tie(A.Source,A.Start.Line,A.Start.Column,A.Kind,A.Message)<std::tie(B.Source,B.Start.Line,B.Start.Column,B.Kind,B.Message);
        });
        Output->Diagnostics.erase(std::unique(Output->Diagnostics.begin(),Output->Diagnostics.end(),[](const Diagnostic& A,const Diagnostic& B) {
            return A.Source==B.Source && A.Kind==B.Kind && A.Start.Line==B.Start.Line && A.Start.Column==B.Start.Column && A.Message==B.Message;
        }),Output->Diagnostics.end());
        if (Canceled()) return Fail("Canceled");
        return Output;
    } catch (const Lui::Project::Internal::Failure& Reason) {
        return Fail(std::string(Reason.what())=="LimitExceeded" ? "LimitExceeded" : "DefinitionsInvalid");
    } catch (const std::bad_alloc&) { return Fail("LimitExceeded"); }
    catch (...) { return Fail("AnalysisFailed"); }
}
std::string Serialize(const Lui::Project::Snapshot& Sources,const Result& Value,uint64_t Generation) {
    if (Generation>9007199254740991ULL) throw std::runtime_error("[LUI:TypeCheck] InvalidInput");
    Json Output{{"version",2},{"generation",Generation},{"profile","ProjectTypesV1"},{"digest",Value.Digest},
        {"sourceDigest",Value.SourceDigest},{"definitionsDigest",Value.DefinitionsDigest},{"entry",Sources.GetEntry()},
        {"diagnosticsMode","Development"},{"redacted",false},{"truncated",Value.Truncated},{"sources",Json::array()},{"diagnostics",Json::array()}};
    for (const auto& Pair:Sources.GetSources()) {
        const auto& Source=Pair.second;
        Output["sources"].push_back({{"id",Source.Id},{"path",Source.StagedPath},{"hash",Source.Hash},{"bytes",Source.Bytes.size()},
            {"lines",1+std::count(Source.Bytes.begin(),Source.Bytes.end(),'\n')}});
    }
    for (const auto& Issue:Value.Diagnostics) {
        Json Item{{"source",Issue.Source.empty()?Json(nullptr):Json(Issue.Source)},{"kind",Issue.Kind},{"code",Issue.Code},{"message",Issue.Message}};
        Item["range"]=Issue.Source.empty()?Json(nullptr):Json{{"start",{{"line",Issue.Start.Line},{"column",Issue.Start.Column}}},
            {"end",{{"line",Issue.End.Line},{"column",Issue.End.Column}}}};
        Output["diagnostics"].push_back(std::move(Item));
    }
    auto Text=Output.dump(); if (Text.size()>MaxOutput) throw std::runtime_error("[LUI:TypeCheck] LimitExceeded"); return Text;
}
}
