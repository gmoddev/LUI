#include "Provider.h"
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/x509v3.h>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace Lui::Tls {
namespace {
#ifdef _WIN32
int VerifyWindowsCertificate(int Valid, X509_STORE_CTX* Context) {
    if (!Valid) return 0;
    unsigned char Digest[EVP_MAX_MD_SIZE];
    unsigned int Length = 0;
    if (!X509_digest(X509_STORE_CTX_get_current_cert(Context), EVP_sha1(), Digest, &Length)) {
        X509_STORE_CTX_set_error(Context, X509_V_ERR_CERT_REJECTED); return 0;
    }
    CRYPT_HASH_BLOB Hash{Length, Digest}; // Windows identifies denied certificates by their SHA-1 fingerprint.
    for (DWORD Scope : {CERT_SYSTEM_STORE_CURRENT_USER, CERT_SYSTEM_STORE_LOCAL_MACHINE}) {
        HCERTSTORE Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
            Scope | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG, L"Disallowed");
        if (!Store) {
            const DWORD Error = GetLastError();
            if (Error == ERROR_FILE_NOT_FOUND || Error == static_cast<DWORD>(CRYPT_E_NOT_FOUND)) continue;
            X509_STORE_CTX_set_error(Context, X509_V_ERR_CERT_REJECTED); return 0;
        }
        auto Found = CertFindCertificateInStore(Store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            0, CERT_FIND_SHA1_HASH, &Hash, nullptr);
        if (Found) CertFreeCertificateContext(Found);
        CertCloseStore(Store, 0);
        if (Found) { X509_STORE_CTX_set_error(Context, X509_V_ERR_CERT_REJECTED); return 0; }
    }
    return 1;
}
#endif
bool Configure(asio::ssl::context& Context, bool Client) {
    auto* Native = Context.native_handle();
    if (!SSL_CTX_set_min_proto_version(Native, TLS1_2_VERSION) ||
        !SSL_CTX_set_max_proto_version(Native, TLS1_3_VERSION)) return false;
    SSL_CTX_set_options(Native, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_TICKET);
    SSL_CTX_set_session_cache_mode(Native, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_max_cert_list(Native, 128 * 1024);
    SSL_CTX_set_verify_depth(Native, 8);
#ifdef _WIN32
    SSL_CTX_set_verify(Native, Client ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, Client ? VerifyWindowsCertificate : nullptr);
#else
    SSL_CTX_set_verify(Native, Client ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, nullptr);
#endif
    // HTTP/1.1 only. Do not advertise another protocol before LUI implements it.
    return true;
}
bool LoadSystemTrust(asio::ssl::context& Context) {
#ifdef _WIN32
    HCERTSTORE Store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
        CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG, L"ROOT");
    if (!Store) return false;
    PCCERT_CONTEXT Certificate = nullptr;
    size_t Loaded = 0;
    while ((Certificate = CertEnumCertificatesInStore(Store, Certificate))) {
        const unsigned char* Data = Certificate->pbCertEncoded;
        X509* Parsed = d2i_X509(nullptr, &Data, Certificate->cbCertEncoded);
        if (Parsed) {
            if (X509_STORE_add_cert(SSL_CTX_get_cert_store(Context.native_handle()), Parsed)) ++Loaded;
            X509_free(Parsed);
            ERR_clear_error(); // Duplicate roots are harmless.
        }
    }
    CertCloseStore(Store, 0);
    return Loaded != 0;
#else
    return SSL_CTX_set_default_verify_paths(Context.native_handle()) == 1;
#endif
}
bool LoadTrust(asio::ssl::context& Context, std::string_view Text) {
    if (Text.empty()) return LoadSystemTrust(Context);
    size_t Count = 0;
    while (!Text.empty()) {
        const auto First = Text.find_first_not_of(" \r\n\t");
        if (First == std::string_view::npos) break;
        Text.remove_prefix(First);
        const std::string_view Begin = "-----BEGIN CERTIFICATE-----", End = "-----END CERTIFICATE-----";
        if (Text.substr(0, Begin.size()) != Begin || ++Count > 128) return false;
        auto Last = Text.find(End);
        if (Last == std::string_view::npos) return false;
        Last += End.size();
        BIO* Input = BIO_new_mem_buf(Text.data(), static_cast<int>(Last));
        if (!Input) return false;
        X509* Certificate = PEM_read_bio_X509(Input, nullptr, nullptr, nullptr);
        BIO_free(Input);
        bool Ok = Certificate && X509_check_ca(Certificate) > 0 &&
            X509_STORE_add_cert(SSL_CTX_get_cert_store(Context.native_handle()), Certificate) == 1;
        X509_free(Certificate);
        if (!Ok) return false;
        Text.remove_prefix(Last);
    }
    return Count != 0;
}
bool LoadServer(asio::ssl::context& Context, std::string_view Bytes, const std::string& Password) {
    const unsigned char* Data = reinterpret_cast<const unsigned char*>(Bytes.data());
    PKCS12* Input = d2i_PKCS12(nullptr, &Data, static_cast<long>(Bytes.size()));
    if (!Input || Data != reinterpret_cast<const unsigned char*>(Bytes.data()) + Bytes.size()) {
        PKCS12_free(Input); return false;
    }
    EVP_PKEY* Key = nullptr;
    X509* Certificate = nullptr;
    STACK_OF(X509)* Chain = nullptr;
    bool Ok = PKCS12_parse(Input, Password.c_str(), &Key, &Certificate, &Chain) == 1 &&
        SSL_CTX_use_certificate(Context.native_handle(), Certificate) == 1 &&
        SSL_CTX_use_PrivateKey(Context.native_handle(), Key) == 1 &&
        SSL_CTX_check_private_key(Context.native_handle()) == 1;
    if (Ok && Chain && sk_X509_num(Chain) > 8) Ok = false;
    if (Ok && Chain) {
        for (int Index = 0; Index < sk_X509_num(Chain); ++Index)
            if (SSL_CTX_add1_chain_cert(Context.native_handle(), sk_X509_value(Chain, Index)) != 1) { Ok = false; break; }
    }
    sk_X509_pop_free(Chain, X509_free);
    X509_free(Certificate);
    EVP_PKEY_free(Key);
    PKCS12_free(Input);
    return Ok;
}
}

std::shared_ptr<Settings> Settings::Load(std::string_view Trust, std::string_view Credential,
    const std::string& Password, std::string& Error) {
    if (Trust.size() > 256 * 1024 || Credential.size() > 256 * 1024 || Password.size() > 1024 ||
        Password.find('\0') != std::string::npos || (Credential.empty() && !Password.empty())) {
        Error = "[LUI:Tls] InvalidOptions"; return {};
    }
    try {
        auto Result = std::make_shared<Settings>();
        Error = "[LUI:Tls] ProviderUnavailable";
        if (!Configure(Result->Client, true)) return {};
        Error = "[LUI:Tls] InvalidTrust";
        if (!LoadTrust(Result->Client, Trust)) { ERR_clear_error(); return {}; }
        if (!Credential.empty()) {
            Result->Server = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_server);
            Error = "[LUI:Tls] InvalidCredential";
            if (!Configure(*Result->Server, false) || !LoadServer(*Result->Server, Credential, Password)) {
                ERR_clear_error(); return {};
            }
        }
        Error.clear();
        return Result;
    } catch (...) { ERR_clear_error(); Error = "[LUI:Tls] ProviderUnavailable"; return {}; }
}

Stream::Stream(asio::ip::tcp::socket& Socket, std::shared_ptr<Settings> Settings, bool Server)
    : Socket(Socket, Server ? *Settings->Server : Settings->Client), Owner(std::move(Settings)) {}

bool Stream::SetPeerName(const std::string& Name) {
    asio::error_code Error;
    asio::ip::make_address(Name, Error);
    auto* Native = Socket.native_handle();
    if (!Error) return X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(Native), Name.c_str()) == 1;
    return SSL_set1_host(Native, Name.c_str()) == 1 && SSL_set_tlsext_host_name(Native, Name.c_str()) == 1;
}

std::string Stream::ErrorName(const asio::error_code& Error, bool Handshake) {
    if (Handshake && SSL_get_verify_result(Socket.native_handle()) != X509_V_OK) return "CertificateRejected";
    if (Error == asio::ssl::error::stream_truncated) return "UnexpectedEof";
    return Handshake ? "TlsHandshakeFailed" : "TlsIoError";
}
}
