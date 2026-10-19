#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Lui::Project {
struct MethodDeclaration {
    std::vector<std::string> Arguments;
    std::string Returns;
};
struct ServiceDeclaration {
    std::map<std::string, MethodDeclaration> Methods;
    std::map<std::string, std::vector<std::string>> Signals;
};
struct DeclarationFile { std::string Bytes, Hash; };
struct BinaryBinding { std::string Path, Hash; };
struct NativeDeclaration {
    uint32_t Version = 0;
    uint64_t RequiredCapabilities = 0;
    std::string ProvenanceKind, ProvenanceId;
    std::map<std::string, BinaryBinding> Bindings;
};
struct DeclarationSet {
    std::map<std::string, DeclarationFile> Files;
    std::map<std::string, ServiceDeclaration> Services;
    // Empty for the original Host-only profile; native profiles record every
    // service's provider (Host or the declared extension identity).
    std::map<std::string, std::string> ServiceProviders;
    std::map<std::string, NativeDeclaration> NativeProviders;
    std::string Descriptor, Digest;
};
// Data-only ABI v1 sidecars. No arbitrary Luau declaration text or DLL loading.
void AddDeclaration(DeclarationSet& Set, const std::string& Path, std::string Bytes);
void FinishDeclarations(DeclarationSet& Set);
// Canonical registration metadata ignores parameter labels and whitespace,
// preserves primitive types, arity/order and member kind. Throws generic Failure.
MethodDeclaration ParseMethod(const std::string& Service, std::string_view Type);
std::vector<std::string> ParseSignal(std::string_view Type);
std::string MergeDefinitions(const DeclarationSet& Set, const std::string& Definitions);
// Trusted owner observation/receipt. This matcher performs no loading/query/
// filesystem access and cannot prove a receipt describes a particular OS image.
struct NativeObservation {
    std::string Name, Platform, BinaryHash;
    uint32_t AbiVersion = 0, Version = 0;
    uint64_t RequiredCapabilities = 0;
    std::map<std::string, ServiceDeclaration> Services;
};
bool MatchNative(const DeclarationSet& Set, const NativeObservation& Observed, std::string& Error);
}
