#pragma once
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <memory>
#include <string>
#include <string_view>

namespace Lui::Tls {
// Private provider objects. No transport or credential handle crosses the host ABI.
class Settings {
public:
    static std::shared_ptr<Settings> Load(std::string_view Trust, std::string_view Credential,
        const std::string& Password, std::string& Error);
    asio::ssl::context Client{asio::ssl::context::tls_client};
    std::unique_ptr<asio::ssl::context> Server;
};

class Stream {
public:
    using Transport = asio::ssl::stream<asio::ip::tcp::socket&>;
    Stream(asio::ip::tcp::socket& Socket, std::shared_ptr<Settings> Settings, bool Server);
    bool SetPeerName(const std::string& Name);
    std::string ErrorName(const asio::error_code& Error, bool Handshake = false);
    // The caller holds Stream and the socket until composed I/O completes.
    Transport Socket;
private:
    std::shared_ptr<Settings> Owner;
};
}
