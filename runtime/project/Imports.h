#pragma once
namespace Luau { class AstStatBlock; class AstExprCall; class AstExprConstantString; }
namespace Lui::Project {
// Shared AST classification. Resolver callers must also use Snapshot::Resolve.
bool RequireRebound(Luau::AstStatBlock* Root);
const Luau::AstExprConstantString* GetLiteralImport(const Luau::AstExprCall* Call);
}
