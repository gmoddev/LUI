#pragma once
#include <string>
namespace Lui::Mobile {
// Before provider installation, under its own exclusive lock. Never promote a
// partial write. Unknown files and unsafe temporary types remain fail-closed.
bool RecoverStorage(const std::string& Root);
}
