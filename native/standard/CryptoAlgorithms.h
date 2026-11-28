#pragma once
#include <cstddef>

namespace Lui::Standard {
struct ByteSpan { const unsigned char* Data; size_t Size; };
bool DeriveSha256(ByteSpan Secret,ByteSpan Salt,ByteSpan Info,unsigned char* Output,size_t Size);
bool Aes256Gcm(bool Encrypt,ByteSpan Key,ByteSpan Nonce,ByteSpan Aad,ByteSpan Input,
    unsigned char* Output,unsigned char* Tag);
}
