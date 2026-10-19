#pragma once
#include "Files.h"
#include <nlohmann/json.hpp>
#include <array>
#include <set>
namespace Lui::Project::Internal {
inline nlohmann::json ParseJson(std::string_view Text) {
    using Json = nlohmann::json;
    if (!Utf8(Text)) Fail("InvalidJson");
    std::array<std::set<std::string>, 18> Keys;
    try {
        return Json::parse(Text.begin(), Text.end(), [&](int Depth, Json::parse_event_t Event, Json& Value) {
            if (Depth < 0 || Depth > 16) Fail("LimitExceeded");
            if (Event == Json::parse_event_t::object_start) Keys[Depth + 1].clear();
            if (Event == Json::parse_event_t::key && !Keys[Depth].insert(Value.get<std::string>()).second) Fail("InvalidJson");
            return true;
        });
    } catch (const Failure&) { throw; }
    catch (const std::bad_alloc&) { throw; }
    catch (...) { Fail("InvalidJson"); }
}
}
