#include "Files.h"

namespace Lui::Project::Internal {
std::string Fold(std::string_view Text) {
    std::string Result(Text);
    for (char& Character : Result) if (Character >= 'A' && Character <= 'Z') Character += 'a' - 'A';
    return Result;
}
bool PathValid(std::string_view Path, bool Root) {
    if (Root && Path == ".") return true;
    if (Path.empty() || Path.size() > 512) return false;
    size_t Start = 0;
    while (Start < Path.size()) {
        auto End = Path.find('/', Start);
        if (End == std::string_view::npos) End = Path.size();
        auto Part = Path.substr(Start, End - Start);
        if (Part.empty() || Part == "." || Part == ".." || Part.back() == '.') return false;
        for (char Character : Part) if (!((Character >= 'a' && Character <= 'z') ||
            (Character >= 'A' && Character <= 'Z') || (Character >= '0' && Character <= '9') ||
            Character == '_' || Character == '-' || Character == '.')) return false;
        auto Base = Fold(Part.substr(0, Part.find('.')));
        if (Base == "con" || Base == "prn" || Base == "aux" || Base == "nul" ||
            (Base.size() == 4 && (Base.substr(0,3) == "com" || Base.substr(0,3) == "lpt") &&
                Base[3] >= '1' && Base[3] <= '9')) return false;
        Start = End + 1;
    }
    return Path.back() != '/';
}
void PathIndex::Add(const std::string& Path) {
    if (!PathValid(Path)) Fail("InvalidPath");
    size_t End = 0;
    do {
        End = Path.find('/', End + 1);
        const bool Directory = End != std::string::npos;
        const auto Prefix = Path.substr(0, End);
        auto Inserted = Entries.emplace(Fold(Prefix), std::make_pair(Prefix, Directory));
        if (!Inserted.second && (Inserted.first->second.first != Prefix ||
            !Directory || !Inserted.first->second.second)) Fail("DuplicateIdentity");
    } while (End != std::string::npos);
}
bool Utf8(std::string_view Text) {
    for (size_t Index = 0; Index < Text.size();) {
        unsigned char First = static_cast<unsigned char>(Text[Index++]);
        if (!First) return false;
        if (First < 0x80) continue;
        unsigned Count = First >= 0xc2 && First <= 0xdf ? 1 : First >= 0xe0 && First <= 0xef ? 2 :
            First >= 0xf0 && First <= 0xf4 ? 3 : 0;
        if (!Count || Index + Count > Text.size()) return false;
        unsigned Value = First & (Count == 1 ? 0x1f : Count == 2 ? 0x0f : 0x07);
        for (unsigned Offset = 0; Offset < Count; ++Offset) {
            unsigned char Byte = static_cast<unsigned char>(Text[Index++]);
            if ((Byte & 0xc0) != 0x80) return false;
            Value = (Value << 6) | (Byte & 0x3f);
        }
        if (Value < (Count == 1 ? 0x80u : Count == 2 ? 0x800u : 0x10000u) ||
            Value > 0x10ffff || (Value >= 0xd800 && Value <= 0xdfff)) return false;
    }
    return Text.substr(0,3) != "\xef\xbb\xbf";
}

}

