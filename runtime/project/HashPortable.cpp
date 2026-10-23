#include "Project.h"
#include "../../third_party/picosha2/picosha2.h"
namespace Lui::Project {
std::string Hash(std::string_view Bytes) {
    return picosha2::hash256_hex_string(Bytes.begin(), Bytes.end());
}
}
