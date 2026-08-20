#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file main.cpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 stress client for busy communication and concurrent load testing.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "Protocol.hpp"
#include "SocketSupport.hpp"

namespace
{
    std::string PriceForSymbol(const std::string& symbol, const std::string& side, int index)
    {
        int priceCents = 10000;
        if (symbol == "BETA")
        {
            priceCents = 7500;
        }
        else if (symbol == "GAMMA")
        {
            priceCents = 12500;
        }

        if (side == "BUY" && (index % 10) == 0)
        {
            priceCents += 100;
        }
        else if (side == "SELL" && (index % 10) == 0)
        {
            priceCents -= 100;
        }

        return trading::FormatPrice(priceCents);
    }

    struct Metrics
    {
        std::atomic<int> commandsSent{ 0 };
        std::atomic<int> acceptedOrders{ 0 };
        std::atomic<int> tradeNotices{ 0 };
        std::atomic<int> snapshotsObserved{ 0 };
        std::atomic<int> errorsObserved{ 0 };
        std::atomic<int> clientFailures{ 0 };
        std::mutex errorMutex;
        std::vector<std::string> sampleErrors;
    };

    struct StressOptions
    {
        std::string host{ "127.0.0.1" };
        unsigned short port{ 5000 };
        int clients{ 8 };
        int operationsPerClient{ 30 };
        int maxDelayMs{ 25 };
        std::string prefix;
    };

    class StressClient
    {
    public:
        StressClient(StressOptions options, std::string username, std::string side, Metrics& metrics)
            : options_(std::move(options))
            , username_(std::move(username))
            , side_(std::move(side))
            , metrics_(metrics)
        {
        }

        void Run()
        {
            try
            {
                if (!Connect())
                {
                    ReportFailure("Could not connect to server.");
                    return;
                }

                running_ = true;
                receiverThread_ = std::thread(&StressClient::ReceiveLoop, this);

                SendCommand("LOGIN|" + username_);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));

                std::mt19937 generator(static_cast<unsigned int>(
                    std::chrono::high_resolution_clock::now().time_since_epoch().count()
                    ^ std::hash<std::string>{}(username_)));
                std::uniform_int_distribution<int> delayDistribution(0, std::max(0, options_.maxDelayMs));
                std::uniform_real_distribution<double> rollDistribution(0.0, 1.0);

                for (int index = 0; index < options_.operationsPerClient; ++index)
                {
                    if (rollDistribution(generator) < 0.8)
                    {
                        const std::string symbol = trading::kSupportedSymbols[
                            static_cast<std::size_t>(generator() % trading::kSupportedSymbols.size())];
                        SendCommand(side_ + "|" + symbol + "|1|" + PriceForSymbol(symbol, side_, index));
                    }
                    else
                    {
                        const std::string symbol = trading::kSupportedSymbols[
                            static_cast<std::size_t>(generator() % trading::kSupportedSymbols.size())];
                        SendCommand("REFRESH|" + symbol);
                    }

                    const int delay = delayDistribution(generator);
                    if (delay > 0)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                SendCommand("REFRESH|" + std::string(trading::kSupportedSymbols[0]));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            catch (const std::exception& ex)
            {
                ReportFailure(ex.what());
            }
            catch (...)
            {
                ReportFailure("Unknown stress client failure.");
            }

            Close();
        }

    private:
        StressOptions options_;
        std::string username_;
        std::string side_;
        Metrics& metrics_;
        SOCKET socketHandle_{ INVALID_SOCKET };
        std::atomic<bool> running_{ false };
        std::thread receiverThread_;
        std::mutex sendMutex_;

        bool Connect()
        {
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            addrinfo* results = nullptr;
            const std::string portText = std::to_string(options_.port);
            if (getaddrinfo(options_.host.c_str(), portText.c_str(), &hints, &results) != 0)
            {
                return false;
            }

            for (addrinfo* current = results; current != nullptr; current = current->ai_next)
            {
                socketHandle_ = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
                if (socketHandle_ == INVALID_SOCKET)
                {
                    continue;
                }

                if (connect(socketHandle_, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0)
                {
                    freeaddrinfo(results);
                    return true;
                }

                trading::CloseSocket(socketHandle_);
            }

            freeaddrinfo(results);
            return false;
        }

        void Close()
        {
            if (socketHandle_ == INVALID_SOCKET)
            {
                return;
            }

            try
            {
                SendCommand("QUIT");
            }
            catch (...)
            {
            }

            running_ = false;
            trading::CloseSocket(socketHandle_);
            if (receiverThread_.joinable())
            {
                receiverThread_.join();
            }
        }

        void SendCommand(const std::string& line)
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            if (socketHandle_ == INVALID_SOCKET)
            {
                throw std::runtime_error("Socket is not connected.");
            }

            if (!trading::SendLine(socketHandle_, line))
            {
                throw std::runtime_error("Failed to send command.");
            }

            ++metrics_.commandsSent;
        }

        void ReceiveLoop()
        {
            std::string line;
            while (running_ && trading::RecvLine(socketHandle_, line))
            {
                if (line.rfind("INFO|Accepted ", 0) == 0)
                {
                    ++metrics_.acceptedOrders;
                }
                else if (line.rfind("INFO|Trade ", 0) == 0)
                {
                    ++metrics_.tradeNotices;
                }
                else if (line == "SNAPSHOT_END")
                {
                    ++metrics_.snapshotsObserved;
                }
                else if (line.rfind("ERROR|", 0) == 0)
                {
                    ++metrics_.errorsObserved;
                    RecordSampleError(line.substr(6));
                }
            }
        }

        void ReportFailure(const std::string& message)
        {
            ++metrics_.clientFailures;
            RecordSampleError(username_ + ": " + message);
        }

        void RecordSampleError(const std::string& message)
        {
            std::lock_guard<std::mutex> lock(metrics_.errorMutex);
            for (const auto& existing : metrics_.sampleErrors)
            {
                if (existing == message)
                {
                    return;
                }
            }

            if (metrics_.sampleErrors.size() < 5)
            {
                metrics_.sampleErrors.push_back(message);
            }
        }
    };

    bool ParseIntArgument(const char* text, int& value)
    {
        try
        {
            value = std::stoi(text);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ParseUnsignedShortArgument(const char* text, unsigned short& value)
    {
        int parsed = 0;
        if (!ParseIntArgument(text, parsed) || parsed <= 0 || parsed > 65535)
        {
            return false;
        }

        value = static_cast<unsigned short>(parsed);
        return true;
    }

    std::string DefaultPrefix()
    {
        const auto now = std::chrono::system_clock::now();
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        return "load" + std::to_string(seconds);
    }

    void PrintUsage()
    {
        std::cout
            << "Usage: TradingPlatformCpp.Stress.exe [host] [port] [clients] [opsPerClient] [maxDelayMs] [prefix]\n"
            << "Example: TradingPlatformCpp.Stress.exe 127.0.0.1 5000 10 40 20 demo\n";
    }
}

int main(int argc, char* argv[])
{
    StressOptions options;
    options.prefix = DefaultPrefix();

    if (argc > 1)
    {
        options.host = argv[1];
    }

    if (argc > 2 && !ParseUnsignedShortArgument(argv[2], options.port))
    {
        PrintUsage();
        return 1;
    }

    if (argc > 3 && !ParseIntArgument(argv[3], options.clients))
    {
        PrintUsage();
        return 1;
    }

    if (argc > 4 && !ParseIntArgument(argv[4], options.operationsPerClient))
    {
        PrintUsage();
        return 1;
    }

    if (argc > 5 && !ParseIntArgument(argv[5], options.maxDelayMs))
    {
        PrintUsage();
        return 1;
    }

    if (argc > 6)
    {
        options.prefix = trading::SanitizeToken(argv[6]);
        if (options.prefix.empty())
        {
            PrintUsage();
            return 1;
        }
    }

    if (options.clients <= 0 || options.operationsPerClient <= 0 || options.maxDelayMs < 0)
    {
        PrintUsage();
        return 1;
    }

    trading::WinsockSession winsock;
    if (!winsock.IsValid())
    {
        std::cerr << "Failed to initialize Winsock.\n";
        return 1;
    }

    Metrics metrics;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(options.clients));

    const auto startTime = std::chrono::steady_clock::now();
    for (int index = 0; index < options.clients; ++index)
    {
        const std::string username = options.prefix + "_" + (index + 1 < 10 ? "0" : "") + std::to_string(index + 1);
        const std::string side = (index % 2 == 0) ? "BUY" : "SELL";
        workers.emplace_back([options, username, side, &metrics]() mutable {
            StressClient client(std::move(options), username, side, metrics);
            client.Run();
        });
    }

    for (auto& worker : workers)
    {
        worker.join();
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

    std::cout << "Stress test completed.\n";
    std::cout << "Clients: " << options.clients << '\n';
    std::cout << "Operations per client: " << options.operationsPerClient << '\n';
    std::cout << "Elapsed seconds: " << elapsed << '\n';
    std::cout << "Commands sent: " << metrics.commandsSent.load() << '\n';
    std::cout << "Accepted orders observed: " << metrics.acceptedOrders.load() << '\n';
    std::cout << "Trade notices observed: " << metrics.tradeNotices.load() << '\n';
    std::cout << "Snapshots observed: " << metrics.snapshotsObserved.load() << '\n';
    std::cout << "Errors observed: " << metrics.errorsObserved.load() << '\n';
    std::cout << "Client failures: " << metrics.clientFailures.load() << '\n';

    {
        std::lock_guard<std::mutex> lock(metrics.errorMutex);
        if (!metrics.sampleErrors.empty())
        {
            std::cout << "Sample errors:\n";
            for (const auto& error : metrics.sampleErrors)
            {
                std::cout << "  " << error << '\n';
            }
        }
    }

    return 0;
}
