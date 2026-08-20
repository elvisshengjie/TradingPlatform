#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file TradingClient.hpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 interactive trading client declarations.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "Protocol.hpp"

namespace trading::client
{
    class TradingClient
    {
    public:
        TradingClient(std::string host, unsigned short port, std::string username);
        ~TradingClient();

        void Run();

    private:
        std::string host_;
        unsigned short port_{ 5000 };
        std::string username_;
        SOCKET socketHandle_{ INVALID_SOCKET };
        std::atomic<bool> running_{ false };
        std::thread receiverThread_;
        mutable std::mutex consoleMutex_;
        mutable std::mutex snapshotMutex_;
        mutable std::mutex sendMutex_;
        SnapshotData snapshot_;

        void ReceiveLoop();
        bool Connect();
        bool SendCommand(const std::string& line);
        bool HandleCommand(const std::string& input);
        void PrintHelp() const;
        void PrintSummary() const;
        void PrintMarket() const;
        void PrintAccount() const;
        void PrintOrders() const;
        void PrintHistory() const;
        void WriteLine(const std::string& line) const;
    };
}
