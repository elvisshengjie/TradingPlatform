#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file TradingServer.cpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 TCP trading server implementation and snapshot broadcasting logic.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#include "TradingServer.hpp"

#include <iostream>
#include <stdexcept>
#include <thread>

#include "SocketSupport.hpp"

namespace trading::server
{
    TradingServer::TradingServer(unsigned short port)
        : port_(port)
    {
        listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket_ == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to create server socket.");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port_);

        if (bind(listenSocket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            CloseSocket(listenSocket_);
            throw std::runtime_error("Failed to bind server socket.");
        }

        if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR)
        {
            CloseSocket(listenSocket_);
            throw std::runtime_error("Failed to listen on server socket.");
        }
    }

    TradingServer::~TradingServer()
    {
        CloseSocket(listenSocket_);
    }

    void TradingServer::Run()
    {
        std::cout << "Trading C++ server listening on 0.0.0.0:" << port_ << '\n';
        std::cout << "Supported symbols: " << SupportedSymbolsCsv() << '\n';
        std::cout << "Press Ctrl+C to stop the server.\n";

        while (true)
        {
            sockaddr_in clientAddress{};
            int addressLength = sizeof(clientAddress);
            SOCKET clientSocket = accept(
                listenSocket_,
                reinterpret_cast<sockaddr*>(&clientAddress),
                &addressLength);

            if (clientSocket == INVALID_SOCKET)
            {
                continue;
            }

            auto session = std::make_shared<ClientSession>();
            session->socketHandle = clientSocket;

            std::cout << "Client connected.\n";
            std::thread(&TradingServer::HandleClient, this, session).detach();
        }
    }

    std::string TradingServer::KeyFor(const std::string& username)
    {
        return MakeKey(username);
    }

    void TradingServer::HandleClient(const std::shared_ptr<ClientSession>& session)
    {
        SendInfo(session, "Connected to the C++ trading server. Please login with your username.");

        std::string line;
        while (RecvLine(session->socketHandle, line))
        {
            if (!DispatchMessage(session, line))
            {
                break;
            }
        }

        CleanupSession(session);
        CloseSocket(session->socketHandle);

        if (session->username.empty())
        {
            std::cout << "Client disconnected.\n";
        }
        else
        {
            std::cout << "Client disconnected (" << session->username << ").\n";
        }
    }

    bool TradingServer::DispatchMessage(const std::shared_ptr<ClientSession>& session, const std::string& line)
    {
        const std::vector<std::string> parts = Split(line, '|');
        if (parts.empty())
        {
            return true;
        }

        const std::string command = ToUpper(Trim(parts[0]));

        if (command == "LOGIN")
        {
            if (parts.size() < 2)
            {
                SendError(session, "Usage: LOGIN|username");
                return true;
            }

            ProcessLogin(session, parts[1]);
            return true;
        }

        if (command == "BUY" || command == "SELL")
        {
            if (!EnsureLoggedIn(session))
            {
                return true;
            }

            std::string symbol = GetFocusSymbol(session);
            std::size_t quantityIndex = 1;
            std::size_t priceIndex = 2;
            if (parts.size() >= 4)
            {
                symbol = parts[1];
                quantityIndex = 2;
                priceIndex = 3;
            }
            else if (parts.size() < 3)
            {
                SendError(session, "Usage: BUY|quantity|price or BUY|symbol|quantity|price");
                return true;
            }

            int quantity = 0;
            int priceCents = 0;
            if (!ParseInteger(parts[quantityIndex], quantity) || !ParsePriceCents(parts[priceIndex], priceCents))
            {
                SendError(session, "Quantity and price must be valid numbers.");
                return true;
            }

            ProcessOperationResult(
                session,
                engine_.PlaceOrder(session->username, symbol, command, quantity, priceCents));
            return true;
        }

        if (command == "CANCEL")
        {
            if (!EnsureLoggedIn(session))
            {
                return true;
            }

            if (parts.size() < 2)
            {
                SendError(session, "Usage: CANCEL|orderId");
                return true;
            }

            ProcessOperationResult(session, engine_.CancelOrder(session->username, parts[1]));
            return true;
        }

        if (command == "REFRESH")
        {
            if (!EnsureLoggedIn(session))
            {
                return true;
            }

            if (parts.size() >= 2)
            {
                SetFocusSymbol(session, parts[1]);
            }

            SendSnapshot(session->username);
            return true;
        }

        if (command == "SYMBOL")
        {
            if (!EnsureLoggedIn(session))
            {
                return true;
            }

            if (parts.size() < 2)
            {
                SendError(session, "Usage: SYMBOL|symbol");
                return true;
            }

            SetFocusSymbol(session, parts[1]);
            SendSnapshot(session->username);
            return true;
        }

        if (command == "QUIT")
        {
            return false;
        }

        SendError(session, "Unknown command: " + command);
        return true;
    }

    void TradingServer::ProcessLogin(const std::shared_ptr<ClientSession>& session, const std::string& username)
    {
        const std::string trimmed = SanitizeToken(username);
        if (trimmed.empty())
        {
            SendError(session, "Username is required.");
            return;
        }

        const std::string key = KeyFor(trimmed);
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            const auto existing = clientsByUser_.find(key);
            if (existing != clientsByUser_.end() && existing->second != session)
            {
                SendError(session, "That username is already connected from another client.");
                return;
            }
        }

        TradingEngine::LoginResult loginResult = engine_.Login(trimmed);
        if (!loginResult.success)
        {
            SendError(session, loginResult.errorMessage);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            const auto existing = clientsByUser_.find(key);
            if (existing != clientsByUser_.end() && existing->second != session)
            {
                SendError(session, "That username connected elsewhere before this login completed.");
                return;
            }

            if (!session->username.empty() && KeyFor(session->username) != key)
            {
                SendError(session, "This client is already logged in as " + session->username + '.');
                return;
            }

            session->username = loginResult.username;
            session->focusSymbol = kDefaultSymbol;
            clientsByUser_[key] = session;
        }

        SendInfo(session, loginResult.message);
        BroadcastInfo(loginResult.username + " is now online.", loginResult.username);
        BroadcastSnapshots();

        std::cout << "User logged in: " << loginResult.username << '\n';
    }

    void TradingServer::ProcessOperationResult(
        const std::shared_ptr<ClientSession>& originSession,
        const TradingEngine::OperationResult& result)
    {
        if (!result.success)
        {
            SendError(originSession, result.errorMessage);
            return;
        }

        for (const auto& pair : result.privateMessages)
        {
            for (const auto& message : pair.second)
            {
                SendInfoToUser(pair.first, message);
            }
        }

        for (const auto& message : result.broadcastMessages)
        {
            BroadcastInfo(message);
        }

        if (result.stateChanged)
        {
            BroadcastSnapshots();
        }
    }

    bool TradingServer::EnsureLoggedIn(const std::shared_ptr<ClientSession>& session)
    {
        if (!session->username.empty())
        {
            return true;
        }

        SendError(session, "Please login first.");
        return false;
    }

    void TradingServer::BroadcastSnapshots()
    {
        std::vector<std::string> usernames;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            usernames.reserve(clientsByUser_.size());
            for (const auto& pair : clientsByUser_)
            {
                usernames.push_back(pair.second->username);
            }
        }

        for (const auto& username : usernames)
        {
            SendSnapshot(username);
        }
    }

    void TradingServer::SendSnapshot(const std::string& username)
    {
        const std::shared_ptr<ClientSession> session = TryGetSession(username);
        if (!session)
        {
            return;
        }

        const SnapshotData snapshot = engine_.BuildSnapshot(username, GetFocusSymbol(session));
        if (!snapshot.valid)
        {
            return;
        }

        std::vector<std::string> lines;
        lines.reserve(
            3 + snapshot.holdingsBySymbol.size() + snapshot.buyLevels.size() + snapshot.sellLevels.size()
            + snapshot.openOrders.size() + snapshot.recentOrders.size() + snapshot.recentTrades.size());

        lines.push_back("SNAPSHOT_BEGIN");
        lines.push_back(
            "SUMMARY|" + SanitizeToken(snapshot.username)
                + "|" + snapshot.focusSymbol
                + "|" + FormatMoney(snapshot.cashCents)
                + "|" + FormatMoney(snapshot.reservedCashCents)
                + "|" + FormatMoney(snapshot.availableCashCents)
                + "|" + std::to_string(snapshot.holdings)
                + "|" + std::to_string(snapshot.reservedHoldings)
                + "|" + std::to_string(snapshot.availableHoldings)
                + "|" + FormatPrice(snapshot.bestBidCents)
                + "|" + FormatPrice(snapshot.bestAskCents)
                + "|" + FormatPrice(snapshot.lastTradeCents));

        for (const auto& holding : snapshot.holdingsBySymbol)
        {
            lines.push_back(
                "HOLDING|" + holding.symbol
                    + "|" + std::to_string(holding.holdings)
                    + "|" + std::to_string(holding.reservedHoldings)
                    + "|" + std::to_string(holding.availableHoldings));
        }

        for (const auto& level : snapshot.buyLevels)
        {
            lines.push_back("BUYLVL|" + FormatPrice(level.priceCents) + "|" + std::to_string(level.quantity));
        }

        for (const auto& level : snapshot.sellLevels)
        {
            lines.push_back("SELLLVL|" + FormatPrice(level.priceCents) + "|" + std::to_string(level.quantity));
        }

        for (const auto& order : snapshot.openOrders)
        {
            lines.push_back(
                "OPENORDER|" + SanitizeToken(order.orderId)
                    + "|" + order.symbol
                    + "|" + order.side
                    + "|" + std::to_string(order.remainingQuantity)
                    + "|" + std::to_string(order.originalQuantity)
                    + "|" + FormatPrice(order.priceCents)
                    + "|" + order.status);
        }

        for (const auto& order : snapshot.recentOrders)
        {
            lines.push_back(
                "RECENTORDER|" + SanitizeToken(order.orderId)
                    + "|" + order.symbol
                    + "|" + order.side
                    + "|" + std::to_string(order.remainingQuantity)
                    + "|" + std::to_string(order.originalQuantity)
                    + "|" + FormatPrice(order.priceCents)
                    + "|" + order.status);
        }

        for (const auto& trade : snapshot.recentTrades)
        {
            lines.push_back(
                "TRADE|" + SanitizeToken(trade.tradeId)
                    + "|" + trade.symbol
                    + "|" + SanitizeToken(trade.buyUser)
                    + "|" + SanitizeToken(trade.sellUser)
                    + "|" + std::to_string(trade.quantity)
                    + "|" + FormatPrice(trade.priceCents));
        }

        lines.push_back("SNAPSHOT_END");
        SendLines(session, lines);
    }

    void TradingServer::SendInfoToUser(const std::string& username, const std::string& message)
    {
        const std::shared_ptr<ClientSession> session = TryGetSession(username);
        if (session)
        {
            SendInfo(session, message);
        }
    }

    void TradingServer::BroadcastInfo(const std::string& message, const std::string& exceptUsername)
    {
        std::vector<std::shared_ptr<ClientSession>> sessions;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            for (const auto& pair : clientsByUser_)
            {
                if (!exceptUsername.empty() && KeyFor(pair.second->username) == KeyFor(exceptUsername))
                {
                    continue;
                }

                sessions.push_back(pair.second);
            }
        }

        for (const auto& session : sessions)
        {
            SendInfo(session, message);
        }
    }

    std::shared_ptr<TradingServer::ClientSession> TradingServer::TryGetSession(const std::string& username)
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        const auto it = clientsByUser_.find(KeyFor(username));
        return it == clientsByUser_.end() ? nullptr : it->second;
    }

    void TradingServer::CleanupSession(const std::shared_ptr<ClientSession>& session)
    {
        if (session->username.empty())
        {
            return;
        }

        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            const auto it = clientsByUser_.find(KeyFor(session->username));
            if (it != clientsByUser_.end() && it->second == session)
            {
                clientsByUser_.erase(it);
                removed = true;
            }
        }

        if (removed)
        {
            BroadcastInfo(session->username + " disconnected. Their state remains available for reconnect.");
        }
    }

    std::string TradingServer::GetFocusSymbol(const std::shared_ptr<ClientSession>& session) const
    {
        if (!session)
        {
            return kDefaultSymbol;
        }

        std::lock_guard<std::mutex> lock(session->stateMutex);
        return session->focusSymbol;
    }

    void TradingServer::SetFocusSymbol(const std::shared_ptr<ClientSession>& session, const std::string& symbol)
    {
        if (!session)
        {
            return;
        }

        const std::string normalizedSymbol = NormalizeSymbol(symbol);
        if (!IsSupportedSymbol(normalizedSymbol))
        {
            SendError(session, "Unsupported symbol. Use one of: " + SupportedSymbolsCsv() + '.');
            return;
        }

        {
            std::lock_guard<std::mutex> lock(session->stateMutex);
            session->focusSymbol = normalizedSymbol;
        }

        SendInfo(session, "Focus symbol set to " + normalizedSymbol + '.');
    }

    bool TradingServer::SendRaw(const std::shared_ptr<ClientSession>& session, const std::string& line)
    {
        if (!session)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(session->sendMutex);
        return SendLine(session->socketHandle, line);
    }

    bool TradingServer::SendLines(const std::shared_ptr<ClientSession>& session, const std::vector<std::string>& lines)
    {
        if (!session)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(session->sendMutex);
        for (const auto& line : lines)
        {
            if (!SendLine(session->socketHandle, line))
            {
                return false;
            }
        }

        return true;
    }

    void TradingServer::SendInfo(const std::shared_ptr<ClientSession>& session, const std::string& message)
    {
        SendRaw(session, "INFO|" + SanitizeToken(message));
    }

    void TradingServer::SendError(const std::shared_ptr<ClientSession>& session, const std::string& message)
    {
        SendRaw(session, "ERROR|" + SanitizeToken(message));
    }
}
