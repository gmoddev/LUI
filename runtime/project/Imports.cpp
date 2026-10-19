#include "Project.h"
#include "Files.h"
#include "Imports.h"
#include "Luau/Parser.h"
#include <set>
#include <cstring>

namespace Lui::Project {
using namespace Internal;
namespace {
bool IsRequire(Luau::AstExpr* Expression) {
    auto* Global = Expression->as<Luau::AstExprGlobal>();
    return Global && std::strcmp(Global->name.value, "require") == 0;
}
bool IsRequireWrite(Luau::AstExpr* Expression) {
    if (IsRequire(Expression)) return true;
    auto IsGlobals = [](Luau::AstExpr* Value) {
        auto* Global = Value->as<Luau::AstExprGlobal>();
        return Global && std::strcmp(Global->name.value, "_G") == 0;
    };
    if (auto* Field = Expression->as<Luau::AstExprIndexName>())
        return IsGlobals(Field->expr) && std::strcmp(Field->index.value, "require") == 0;
    if (auto* Field = Expression->as<Luau::AstExprIndexExpr>())
        if (auto* Key = Field->index->as<Luau::AstExprConstantString>())
            return IsGlobals(Field->expr) && std::string_view(Key->value.data, Key->value.size) == "require";
    return false;
}
struct Writes : Luau::AstVisitor {
    bool Rebound = false;
    bool visit(Luau::AstStatAssign* Node) override {
        for (auto* Variable : Node->vars) Rebound |= IsRequireWrite(Variable);
        return true;
    }
    bool visit(Luau::AstStatFunction* Node) override { Rebound |= IsRequireWrite(Node->name); return true; }
    bool visit(Luau::AstStatCompoundAssign* Node) override { Rebound |= IsRequireWrite(Node->var); return true; }
};
struct Imports : Luau::AstVisitor {
    Imports(const Snapshot& Sources, const std::string& Importer, std::set<std::string>& Edges)
        : Sources(Sources), Importer(Importer), Edges(Edges) {}
    const Snapshot& Sources;
    const std::string& Importer;
    std::set<std::string>& Edges;
    std::string Error;
    bool visit(Luau::AstExprCall* Node) override {
        if (auto* Literal = GetLiteralImport(Node)) {
            std::string Issue;
            auto* Source = Sources.Resolve(Importer, std::string_view(Literal->value.data, Literal->value.size), Issue);
            if (Source) Edges.insert(Source->Id);
            else if (Error.empty()) Error = std::move(Issue);
        }
        return true;
    }
};
void Visit(const std::string& Id, const std::map<std::string,std::set<std::string>>& Graph, std::map<std::string,int>& States) {
    if (States[Id] == 1) Fail("Cycle");
    if (States[Id] == 2) return;
    States[Id] = 1;
    for (const auto& Target : Graph.at(Id)) Visit(Target, Graph, States);
    States[Id] = 2;
}
}
bool RequireRebound(Luau::AstStatBlock* Root) {
    Writes Assignments; if (Root) Root->visit(&Assignments); return Assignments.Rebound;
}
const Luau::AstExprConstantString* GetLiteralImport(const Luau::AstExprCall* Call) {
    if (!Call || !IsRequire(Call->func) || Call->self || Call->args.size != 1) return nullptr;
    return Call->args.data[0]->as<Luau::AstExprConstantString>();
}
bool ValidateImports(const Snapshot& Sources, const Options& Settings, std::string& Error) {
    Error.clear();
    try {
        std::map<std::string,std::set<std::string>> Graph;
        std::string FirstError;
        for (const auto& Pair : Sources.GetSources()) {
            if (Settings.Canceled && Settings.Canceled()) Fail("Canceled");
            Luau::Allocator Allocator; Luau::AstNameTable Names(Allocator);
            auto Parsed = Luau::Parser::parse(Pair.second.Bytes.data(), Pair.second.Bytes.size(), Names, Allocator);
            auto& Edges = Graph[Pair.first];
            if (!Parsed.root || !Parsed.errors.empty()) {
                if (FirstError.empty()) FirstError = "[LUI:Project] CompileFailed";
                continue;
            }
            // A source which rebinds global require needs runtime resolution;
            // local/function-parameter shadowing is already explicit in the AST.
            if (!RequireRebound(Parsed.root)) {
                Imports Visitor(Sources, Pair.first, Edges); Parsed.root->visit(&Visitor);
                if (FirstError.empty()) FirstError = std::move(Visitor.Error);
            }
        }
        std::map<std::string,int> States;
        for (const auto& Pair : Graph) Visit(Pair.first, Graph, States);
        if (Settings.Canceled && Settings.Canceled()) Fail("Canceled");
        if (!FirstError.empty()) { Error = std::move(FirstError); return false; }
        return true;
    } catch (const Failure& Reason) { Error = "[LUI:Project] " + std::string(Reason.what()); }
    catch (const std::bad_alloc&) { Error = "[LUI:Project] LimitExceeded"; }
    catch (...) { Error = "[LUI:Project] CompileFailed"; }
    return false;
}
}
