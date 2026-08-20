#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file TradingServer.hpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 TCP trading server declarations and client session management.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "TradingEngine.hpp"

namespace trading::server
{
    class TradingServer
    {
    public:
        explicit TradingServer(unsigned short port);
        ~TradingServer();

        void Run();

    private:
        struct ClientSession
        {
            SOCKET socketHandle{ INVALID_SOCKET };
            std::string username;
            std::string focusSymbol{ kDefaultSymbol };
            std::mutex stateMutex;
            std::mutex sendMutex;
        };

        SOCKET listenSocket_{ INVALID_SOCKET };
        unsigned short port_{ 5000 };
        mutable std::mutex clientsMutex_;
        std::unordered_map<std::string, std::shared_ptr<ClientSession>> clientsByUser_;
        TradingEngine engine_;

        static std::string KeyFor(const std::string& username);
        void HandleClient(const std::shared_ptr<ClientSession>& session);
        bool DispatchMessage(const std::shared_ptr<ClientSession>& session, const std::string& line);
        void ProcessLogin(const std::shared_ptr<ClientSession>& session, const std::string& username);
        void ProcessOperationResult(
            const std::shared_ptr<ClientSession>& originSession,
            const TradingEngine::OperationResult& result);
        bool EnsureLoggedIn(const std::shared_ptr<ClientSession>& session);
        void BroadcastSnapshots();
        void SendSnapshot(const std::string& username);
        std::string GetFocusSymbol(const std::shared_ptr<ClientSession>& session) const;
        void SetFocusSymbol(const std::shared_ptr<ClientSession>& session, const std::string& symbol);
        void SendInfoToUser(const std::string& username, const std::string& message);
        void BroadcastInfo(const std::string& message, const std::string& exceptUsername = std::string());
        std::shared_ptr<ClientSession> TryGetSession(const std::string& username);
        void CleanupSession(const std::shared_ptr<ClientSession>& session);
        static bool SendRaw(const std::shared_ptr<ClientSession>& session, const std::string& line);
        static bool SendLines(const std::shared_ptr<ClientSession>& session, const std::vector<std::string>& lines);
        static void SendInfo(const std::shared_ptr<ClientSession>& session, const std::string& message);
        static void SendError(const std::shared_ptr<ClientSession>& session, const std::string& message);
    };
}
