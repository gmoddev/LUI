#include "CryptoAlgorithms.h"
#include <algorithm>
#include <memory>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#endif

namespace Lui::Standard {
#if defined(_WIN32)
namespace {
struct KeyHandle {
    BCRYPT_KEY_HANDLE Value = nullptr;
    ~KeyHandle() { if (Value) BCryptDestroyKey(Value); }
};
PUCHAR Bytes(ByteSpan Input) { return const_cast<PUCHAR>(Input.Data); }
}
#endif
bool DeriveSha256(ByteSpan Secret,ByteSpan Salt,ByteSpan Info,unsigned char* Output,size_t Size) {
#if defined(_WIN32)
    KeyHandle Key;
    if (BCryptGenerateSymmetricKey(BCRYPT_HKDF_ALG_HANDLE,&Key.Value,nullptr,0,Bytes(Secret),
        static_cast<ULONG>(Secret.Size),0)<0) return false;
    wchar_t Algorithm[]=BCRYPT_SHA256_ALGORITHM;
    if (BCryptSetProperty(Key.Value,BCRYPT_HKDF_HASH_ALGORITHM,reinterpret_cast<PUCHAR>(Algorithm),sizeof(Algorithm),0)<0) return false;
    unsigned char DefaultSalt[32]{};
    if (BCryptSetProperty(Key.Value,BCRYPT_HKDF_SALT_AND_FINALIZE,Salt.Size?Bytes(Salt):DefaultSalt,
        Salt.Size?static_cast<ULONG>(Salt.Size):sizeof(DefaultSalt),0)<0) return false;
    BCryptBuffer Parameter{static_cast<ULONG>(Info.Size),KDF_HKDF_INFO,Bytes(Info)};
    BCryptBufferDesc Arguments{BCRYPTBUFFER_VERSION,1,&Parameter};
    ULONG Written=0;
    return BCryptKeyDerivation(Key.Value,Info.Size?&Arguments:nullptr,Output,static_cast<ULONG>(Size),&Written,0)>=0 && Written==Size;
#else
    std::unique_ptr<EVP_KDF,decltype(&EVP_KDF_free)> Kdf(EVP_KDF_fetch(nullptr,"HKDF",nullptr),EVP_KDF_free);
    if (!Kdf) return false;
    std::unique_ptr<EVP_KDF_CTX,decltype(&EVP_KDF_CTX_free)> Context(EVP_KDF_CTX_new(Kdf.get()),EVP_KDF_CTX_free);
    if (!Context) return false;
    char Hash[]="SHA256";
    OSSL_PARAM Params[]={
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST,Hash,0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,const_cast<unsigned char*>(Secret.Data),Secret.Size),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,const_cast<unsigned char*>(Salt.Data),Salt.Size),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,const_cast<unsigned char*>(Info.Data),Info.Size),
        OSSL_PARAM_construct_end()};
    return EVP_KDF_derive(Context.get(),Output,Size,Params)==1;
#endif
}
bool Aes256Gcm(bool Encrypt,ByteSpan Key,ByteSpan Nonce,ByteSpan Aad,ByteSpan Input,unsigned char* Output,unsigned char* Tag) {
#if defined(_WIN32)
    KeyHandle Handle;
    if (BCryptGenerateSymmetricKey(BCRYPT_AES_GCM_ALG_HANDLE,&Handle.Value,nullptr,0,Bytes(Key),32,0)<0) return false;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Mode;
    BCRYPT_INIT_AUTH_MODE_INFO(Mode);
    Mode.pbNonce=Bytes(Nonce); Mode.cbNonce=12;
    Mode.pbTag=Tag; Mode.cbTag=16;
    if (Aad.Size) { Mode.pbAuthData=Bytes(Aad); Mode.cbAuthData=static_cast<ULONG>(Aad.Size); }
    ULONG Written=0;
    const auto Transform=Encrypt?BCryptEncrypt:BCryptDecrypt;
    const auto Length=static_cast<ULONG>(Input.Size);
    return Transform(Handle.Value,Bytes(Input),Length,&Mode,nullptr,0,Output,std::max<ULONG>(1,Length),&Written,0)>=0 && Written==Length;
#else
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> Context(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    if (!Context || EVP_CipherInit_ex(Context.get(),EVP_aes_256_gcm(),nullptr,Key.Data,Nonce.Data,Encrypt?1:0)!=1) return false;
    int Count=0, Tail=0;
    if (Aad.Size && EVP_CipherUpdate(Context.get(),nullptr,&Count,Aad.Data,static_cast<int>(Aad.Size))!=1) return false;
    if (EVP_CipherUpdate(Context.get(),Output,&Count,Input.Data,static_cast<int>(Input.Size))!=1 ||
        static_cast<size_t>(Count)!=Input.Size) return false;
    if (!Encrypt && EVP_CIPHER_CTX_ctrl(Context.get(),EVP_CTRL_GCM_SET_TAG,16,Tag)!=1) return false;
    if (EVP_CipherFinal_ex(Context.get(),Output+Count,&Tail)!=1 || Tail) return false;
    return !Encrypt || EVP_CIPHER_CTX_ctrl(Context.get(),EVP_CTRL_GCM_GET_TAG,16,Tag)==1;
#endif
}
}
