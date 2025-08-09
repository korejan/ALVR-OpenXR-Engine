#pragma once
#ifndef ALXR_VRCFT_PROXY_SERVER_H

#include <cstdint>
#include <array>
#include <memory>
#include <functional>
#include <thread>
#include <asio/buffer.hpp>
#include <asio/write.hpp>
#include <asio/ts/internet.hpp>
#include <deque>

#include "alxr_facial_eye_tracking_packet.h"

namespace ALXR::VRCFT {

    using asio::ip::tcp;
    using asio::socket_base;

    struct Session final : public std::enable_shared_from_this<Session>
    {
        inline Session(tcp::socket&& socket)
        : m_socket(std::move(socket))
        {}

        inline Session(const Session&) = delete;
        inline Session(Session&&) = default;
        inline Session& operator=(const Session&) = delete;
        inline Session& operator=(Session&&) = default;

        inline void Close() {
            Log::Write(Log::Level::Info, "VRCFTServer: shutting down connection.");
            try {
                m_socket.shutdown(tcp::socket::shutdown_both);
            }
            catch (const asio::system_error& sysError) {
                Log::Write(Log::Level::Warning, Fmt("VRCFTServer: Failed to shutdown gracefully, reason: \"%s\"", sysError.what()));
            }
            try {
                m_socket.close();
            }
            catch (const asio::system_error& sysError) {
                Log::Write(Log::Level::Error, Fmt("VRCFTServer: Failed to close connection, reason: \"%s\"", sysError.what()));
            }
            Log::Write(Log::Level::Info, "VRCFTServer: connection closed.");
        }

        inline ~Session() {
            Close();
        }

        inline void SendAsync(const ALXRFacialEyePacket& buffer) {
            const bool wasEmpty = m_sendQueue.empty();
            m_sendQueue.push_back(buffer);
            if (wasEmpty) {
                Transmit();
            }
        }

    private:
        void Transmit() {
            const auto& buffer = m_sendQueue.front();
            asio::async_write
            (
                m_socket, asio::buffer(&buffer, sizeof(ALXRFacialEyePacket)),
                [weakThis = weak_from_this()](const std::error_code ec, const std::size_t /*bytesTransferred*/)
                {
                    if (ec == asio::error::operation_aborted)
                        return;
                    if (const auto sharedThis = weakThis.lock()) {
                        auto& sendQueue = sharedThis->m_sendQueue;
                        sendQueue.pop_front();
                        if (ec) {
                            const auto errMsg = ec.message();
                            Log::Write(Log::Level::Error, Fmt("VRCFTServer: Failed to send, reason: \"%s\"", errMsg.c_str()));
                            return;
                        }
                        if (!sendQueue.empty()) {
                            sharedThis->Transmit();
                        }
                    }
                }
            );
        }

        tcp::socket m_socket;
        using PacketQueue = std::deque<ALXRFacialEyePacket>;
        PacketQueue m_sendQueue{};
    };

    struct Server final
    {
        constexpr static const std::uint16_t DefaultPortNo = 49192;

        inline Server(const std::uint16_t portNo = DefaultPortNo)
        : m_acceptor(m_ioContext, tcp::endpoint(tcp::v4(), portNo)),
          m_socket(m_ioContext)
        {
            m_acceptor.set_option(socket_base::reuse_address{true});
            AsyncAccept();
            m_ioCtxThread = std::thread([this]() { m_ioContext.run(); });
        }

        inline Server(const Server&) = delete;
        inline Server(Server&&) = delete;

        inline Server& operator=(const Server&) = delete;
        inline Server& operator=(Server&&) = delete;

        inline bool IsConnected() const {
            return m_session != nullptr;
        }

        inline void SendAsync(const ALXRFacialEyePacket& buf) {
            assert(IsConnected());
            m_session->SendAsync(buf);
        }

        void Close() {
            try {
                Log::Write(Log::Level::Info, "VRCFTServer: shutting down server.");
                
                Log::Write(Log::Level::Info, "VRCFTServer: stopping io-context run thread.");
                m_ioContext.stop();

                if (m_ioCtxThread.joinable()) {
                    Log::Write(Log::Level::Info, "VRCFTServer: waiting for io-context run thread to stop.");
                    m_ioCtxThread.join();
                }
                Log::Write(Log::Level::Info, "VRCFTServer: io-context thread stopped.");

                Log::Write(Log::Level::Info, "VRCFTServer: closing acceptor.");
                m_acceptor.close();

                Log::Write(Log::Level::Info, "VRCFTServer: closing session.");
                m_session.reset();

                Log::Write(Log::Level::Info, "VRCFTServer: closing socket.");
                m_socket.close();

                Log::Write(Log::Level::Info, "VRCFTServer: server successfully shutdown.");
            }
            catch (const std::exception& sysError) {
                Log::Write(Log::Level::Error, Fmt("VRCFTServer: Failed to cleanly shutdown server, reason: \"%s\"", sysError.what()));
            }
            catch (...) {
                Log::Write(Log::Level::Error, "VRCFTServer: Failed to cleanly shutdown server");
            }
        }

        template < typename Fun >
        inline void SetOnNewConnection(Fun&& fn) {
            m_onNewConnectionFn = fn;
        }

        inline ~Server() {
            Close();
        }

    private:
        using SessionPtr = std::shared_ptr<Session>;
        using OnNewConFn = std::function<void()>;

        inline void AsyncAccept() {
            m_acceptor.async_accept(m_socket, [this](std::error_code ec) {
                if (!ec) {
                    try {
                        Log::Write(Log::Level::Info, "VRCFTServer: connection accepted.");
                        m_socket.set_option(socket_base::linger{ false,0 });
                        m_socket.set_option(asio::socket_base::keep_alive{ true });
#ifndef XR_USE_PLATFORM_WIN32
                        using quick_ack = asio::detail::socket_option::boolean<IPPROTO_TCP, TCP_QUICKACK>;
                        m_socket.set_option(quick_ack{ true });
#endif
                        m_socket.set_option(tcp::no_delay{ true });
                        m_session = std::make_shared<Session>(std::move(m_socket));
                        if (m_onNewConnectionFn)
                            m_onNewConnectionFn();
                    }
                    catch (const std::exception& sysError) {
                        Log::Write(Log::Level::Error, Fmt("VRCFTServer: async_accept failed, reason: \"%s\"", sysError.what()));
                    }
                    catch (...) {
                        Log::Write(Log::Level::Error, "VRCFTServer: async_accept failed");
                    }
                }
                AsyncAccept();
            });
        }

        asio::io_context m_ioContext{};
        tcp::acceptor    m_acceptor;
        tcp::socket      m_socket;
        SessionPtr       m_session{ nullptr };
        OnNewConFn       m_onNewConnectionFn{};
        std::thread      m_ioCtxThread{};
    };
}
#endif
