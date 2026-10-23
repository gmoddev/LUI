#include "Project.h"
#include "Files.h"
#include <openssl/evp.h>
namespace Lui::Project {
using namespace Internal;
std::string Hash(std::string_view Bytes) {
    unsigned char Digest[EVP_MAX_MD_SIZE]; unsigned int Size = 0;
    if (!EVP_Digest(Bytes.data(), Bytes.size(), Digest, &Size, EVP_sha256(), nullptr) || Size != 32)
        Fail("ProviderUnavailable");
    const char* Hex = "0123456789abcdef";
    std::string Result;
    for (unsigned Index = 0; Index < Size; ++Index) {
        Result += Hex[Digest[Index] >> 4]; Result += Hex[Digest[Index] & 15];
    }
    return Result;
}
}

