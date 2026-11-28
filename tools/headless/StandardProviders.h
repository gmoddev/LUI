#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace Lui::Headless {
bool CheckProviders(const nlohmann::json& Descriptor, const std::set<std::string>& Selected);
}
