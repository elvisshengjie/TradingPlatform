#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file TradingClient.cpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 interactive trading client implementation and console UI handling.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#include "TradingClient.hpp"

#include <iostream>

#include "SocketSupport.hpp"

namespace trading::client
{
    TradingClient::TradingClient(std::string host, unsigned short port, std::string username)
        : host_(std::move(host))
        , port_(port)
        , username_(std::move(username))
    {
    }

    TradingClient::~TradingClient()
    {
        running_ = false;
        CloseSocket(socketHandle_);
        if (receiverThread_.joinable())
        {
            receiverThread_.join();
        }
    }

    void TradingClient::Run()
    {
        if (!Connect())
        {
            return;
        }

        running_ = true;
        receiverThread_ = std::thread(&TradingClient::ReceiveLoop, this);

        if (username_.empty())
        {
            WriteLine("Connected to " + host_ + ":" + std::to_string(port_));
            {
                std::lock_guard<std::mutex> lock(consoleMutex_);
                std::cout << "Username: ";
            }

            std::getline(std::cin, username_);
        }

        username_ = SanitizeToken(username_);
        SendCommand("LOGIN|" + username_);
        PrintHelp();

        std::string input;
        while (running_)
        {
            {
                std::lock_guard<std::mutex> lock(consoleMutex_);
                std::cout << "> ";
            }

            if (!std::getline(std::cin, input))
            {
                break;
            }

            if (!HandleCommand(input))
            {
                break;
            }
        }

        running_ = false;
        CloseSocket(socketHandle_);
        if (receiverThread_.joinable())
        {
            receiverThread_.join();
        }
    }

    bool TradingClient::Connect()
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* results = nullptr;
        const std::string portText = std::to_string(port_);
        if (getaddrinfo(host_.c_str(), portText.c_str(), &hints, &results) != 0)
        {
            WriteLine("Failed to resolve host.");
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

            CloseSocket(socketHandle_);
        }

        freeaddrinfo(results);
        WriteLine("Could not connect to server.");
        return false;
    }

    void TradingClient::ReceiveLoop()
    {
        std::string line;
        SnapshotData pendingSnapshot;
        bool inSnapshot = false;

        while (running_ && RecvLine(socketHandle_, line))
        {
            const std::vector<std::string> parts = Split(line, '|');
            if (parts.empty())
            {
                continue;
            }

            const std::string command = ToUpper(parts[0]);

            if (command == "SNAPSHOT_BEGIN")
            {
                pendingSnapshot = SnapshotData{};
                inSnapshot = true;
                continue;
            }

            if (command == "SNAPSHOT_END")
            {
                pendingSnapshot.valid = true;
                {
                    std::lock_guard<std::mutex> lock(snapshotMutex_);
                    snapshot_ = pendingSnapshot;
                }

                PrintSummary();
                inSnapshot = false;
                continue;
            }

            if (inSnapshot)
            {
                if (command == "SUMMARY" && parts.size() >= 12)
                {
                    pendingSnapshot.username = parts[1];
                    pendingSnapshot.focusSymbol = parts[2];
                    int cash = 0;
                    int reservedCash = 0;
                    int availableCash = 0;
                    int holdings = 0;
                    int reservedHoldings = 0;
                    int availableHoldings = 0;
                    int bestBid = 0;
                    int bestAsk = 0;
                    int lastTrade = 0;

                    ParsePriceCents(parts[3], cash);
                    ParsePriceCents(parts[4], reservedCash);
                    ParsePriceCents(parts[5], availableCash);
                    ParseInteger(parts[6], holdings);
                    ParseInteger(parts[7], reservedHoldings);
                    ParseInteger(parts[8], availableHoldings);
                    ParsePriceCents(parts[9], bestBid);
                    ParsePriceCents(parts[10], bestAsk);
                    ParsePriceCents(parts[11], lastTrade);

                    pendingSnapshot.cashCents = cash;
                    pendingSnapshot.reservedCashCents = reservedCash;
                    pendingSnapshot.availableCashCents = availableCash;
                    pendingSnapshot.holdings = holdings;
                    pendingSnapshot.reservedHoldings = reservedHoldings;
                    pendingSnapshot.availableHoldings = availableHoldings;
                    pendingSnapshot.bestBidCents = bestBid;
                    pendingSnapshot.bestAskCents = bestAsk;
                    pendingSnapshot.lastTradeCents = lastTrade;
                    continue;
                }

                if (command == "HOLDING" && parts.size() >= 5)
                {
                    HoldingView holding;
                    int holdings = 0;
                    int reserved = 0;
                    int available = 0;
                    holding.symbol = parts[1];
                    ParseInteger(parts[2], holdings);
                    ParseInteger(parts[3], reserved);
                    ParseInteger(parts[4], available);
                    holding.holdings = holdings;
                    holding.reservedHoldings = reserved;
                    holding.availableHoldings = available;
                    pendingSnapshot.holdingsBySymbol.push_back(holding);
                    continue;
                }

                if (command == "BUYLVL" && parts.size() >= 3)
                {
                    int price = 0;
                    int quantity = 0;
                    ParsePriceCents(parts[1], price);
                    ParseInteger(parts[2], quantity);
                    pendingSnapshot.buyLevels.push_back({ price, quantity });
                    continue;
                }

                if (command == "SELLLVL" && parts.size() >= 3)
                {
                    int price = 0;
                    int quantity = 0;
                    ParsePriceCents(parts[1], price);
                    ParseInteger(parts[2], quantity);
                    pendingSnapshot.sellLevels.push_back({ price, quantity });
                    continue;
                }

                if (command == "OPENORDER" && parts.size() >= 8)
                {
                    OrderView order;
                    int remaining = 0;
                    int original = 0;
                    int price = 0;
                    order.orderId = parts[1];
                    order.symbol = parts[2];
                    order.side = parts[3];
                    ParseInteger(parts[4], remaining);
                    ParseInteger(parts[5], original);
                    ParsePriceCents(parts[6], price);
                    order.remainingQuantity = remaining;
                    order.originalQuantity = original;
                    order.priceCents = price;
                    order.status = parts[7];
                    pendingSnapshot.openOrders.push_back(order);
                    continue;
                }

                if (command == "RECENTORDER" && parts.size() >= 8)
                {
                    OrderView order;
                    int remaining = 0;
                    int original = 0;
                    int price = 0;
                    order.orderId = parts[1];
                    order.symbol = parts[2];
                    order.side = parts[3];
                    ParseInteger(parts[4], remaining);
                    ParseInteger(parts[5], original);
                    ParsePriceCents(parts[6], price);
                    order.remainingQuantity = remaining;
                    order.originalQuantity = original;
                    order.priceCents = price;
                    order.status = parts[7];
                    pendingSnapshot.recentOrders.push_back(order);
                    continue;
                }

                if (command == "TRADE" && parts.size() >= 7)
                {
                    TradeView trade;
                    int quantity = 0;
                    int price = 0;
                    trade.tradeId = parts[1];
                    trade.symbol = parts[2];
                    trade.buyUser = parts[3];
                    trade.sellUser = parts[4];
                    ParseInteger(parts[5], quantity);
                    ParsePriceCents(parts[6], price);
                    trade.quantity = quantity;
                    trade.priceCents = price;
                    pendingSnapshot.recentTrades.push_back(trade);
                    continue;
                }

                continue;
            }

            if (command == "INFO" && parts.size() >= 2)
            {
                WriteLine("[INFO] " + parts[1]);
            }
            else if (command == "ERROR" && parts.size() >= 2)
            {
                WriteLine("[ERROR] " + parts[1]);
            }
        }

        running_ = false;
        WriteLine("Disconnected from server.");
    }

    bool TradingClient::SendCommand(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        return SendLine(socketHandle_, line);
    }

    bool TradingClient::HandleCommand(const std::string& input)
    {
        const std::vector<std::string> parts = Split(Trim(input), ' ');
        if (parts.empty() || parts[0].empty())
        {
            return true;
        }

        const std::string command = ToUpper(parts[0]);

        if (command == "HELP")
        {
            PrintHelp();
            return true;
        }

        if (command == "MARKET")
        {
            if (parts.size() >= 2)
            {
                SendCommand("REFRESH|" + NormalizeSymbol(parts[1]));
                return true;
            }

            PrintMarket();
            return true;
        }

        if (command == "ACCOUNT")
        {
            PrintAccount();
            return true;
        }

        if (command == "ORDERS")
        {
            PrintOrders();
            return true;
        }

        if (command == "HISTORY")
        {
            PrintHistory();
            return true;
        }

        if (command == "BUY" || command == "SELL")
        {
            SnapshotData snapshotCopy;
            {
                std::lock_guard<std::mutex> lock(snapshotMutex_);
                snapshotCopy = snapshot_;
            }

            std::string symbol = snapshotCopy.focusSymbol.empty() ? kDefaultSymbol : snapshotCopy.focusSymbol;
            std::size_t quantityIndex = 1;
            std::size_t priceIndex = 2;

            if (parts.size() >= 4)
            {
                symbol = NormalizeSymbol(parts[1]);
                quantityIndex = 2;
                priceIndex = 3;
            }
            else if (parts.size() < 3)
            {
                WriteLine("Usage: buy <qty> <price> or buy <symbol> <qty> <price>");
                return true;
            }

            int quantity = 0;
            int priceCents = 0;
            if (!ParseInteger(parts[quantityIndex], quantity) || !ParsePriceCents(parts[priceIndex], priceCents))
            {
                WriteLine("Quantity and price must be valid numbers. Example: buy BETA 10 75.50");
                return true;
            }

            if (!IsSupportedSymbol(symbol))
            {
                WriteLine("Unsupported symbol. Use one of: " + SupportedSymbolsCsv());
                return true;
            }

            SendCommand(command + "|" + symbol + "|" + std::to_string(quantity) + "|" + FormatPrice(priceCents));
            return true;
        }

        if (command == "CANCEL")
        {
            if (parts.size() < 2)
            {
                WriteLine("Usage: cancel <orderId>");
                return true;
            }

            SendCommand("CANCEL|" + parts[1]);
            return true;
        }

        if (command == "REFRESH")
        {
            if (parts.size() >= 2)
            {
                SendCommand("REFRESH|" + NormalizeSymbol(parts[1]));
            }
            else
            {
                SendCommand("REFRESH");
            }

            return true;
        }

        if (command == "SYMBOL")
        {
            if (parts.size() < 2)
            {
                WriteLine("Usage: symbol <ACME|BETA|GAMMA>");
                return true;
            }

            SendCommand("SYMBOL|" + NormalizeSymbol(parts[1]));
            return true;
        }

        if (command == "QUIT" || command == "EXIT")
        {
            SendCommand("QUIT");
            return false;
        }

        WriteLine("Unknown command. Type 'help' to see the supported commands.");
        return true;
    }

    void TradingClient::PrintHelp() const
    {
        WriteLine("Commands:");
        WriteLine("  symbol <sym>         Switch focus symbol (ACME, BETA, GAMMA)");
        WriteLine("  market [sym]         Show the current book or refresh a different symbol");
        WriteLine("  account              Show balances and holdings for all symbols");
        WriteLine("  orders               Show open orders and recent order history for the focus symbol");
        WriteLine("  history              Show recent trades for the focus symbol");
        WriteLine("  buy <qty> <price>    Submit a buy limit order on the focus symbol");
        WriteLine("  buy <sym> <q> <p>    Submit a buy limit order on a chosen symbol");
        WriteLine("  sell <qty> <price>   Submit a sell limit order on the focus symbol");
        WriteLine("  sell <sym> <q> <p>   Submit a sell limit order on a chosen symbol");
        WriteLine("  cancel <orderId>     Cancel one of your open orders");
        WriteLine("  refresh [sym]        Request the latest snapshot");
        WriteLine("  quit                 Disconnect the client");
    }

    void TradingClient::PrintSummary() const
    {
        SnapshotData snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshotCopy = snapshot_;
        }

        if (!snapshotCopy.valid)
        {
            return;
        }

        WriteLine(
            "[SNAPSHOT] " + snapshotCopy.username
                + " | symbol " + snapshotCopy.focusSymbol
                + " | last " + FormatPrice(snapshotCopy.lastTradeCents)
                + " | bid " + FormatPrice(snapshotCopy.bestBidCents)
                + " | ask " + FormatPrice(snapshotCopy.bestAskCents)
                + " | cash " + FormatMoney(snapshotCopy.cashCents)
                + " avail " + FormatMoney(snapshotCopy.availableCashCents)
                + " | holdings " + std::to_string(snapshotCopy.holdings)
                + " avail " + std::to_string(snapshotCopy.availableHoldings));
    }

    void TradingClient::PrintMarket() const
    {
        SnapshotData snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshotCopy = snapshot_;
        }

        if (!snapshotCopy.valid)
        {
            WriteLine("No market snapshot received yet.");
            return;
        }

        WriteLine("Market: " + snapshotCopy.focusSymbol);
        WriteLine("  Last trade: " + FormatPrice(snapshotCopy.lastTradeCents));
        WriteLine("  Best bid:   " + FormatPrice(snapshotCopy.bestBidCents));
        WriteLine("  Best ask:   " + FormatPrice(snapshotCopy.bestAskCents));
        WriteLine("  Buy book:");
        for (const auto& level : snapshotCopy.buyLevels)
        {
            WriteLine("    " + std::to_string(level.quantity) + " @ " + FormatPrice(level.priceCents));
        }

        WriteLine("  Sell book:");
        for (const auto& level : snapshotCopy.sellLevels)
        {
            WriteLine("    " + std::to_string(level.quantity) + " @ " + FormatPrice(level.priceCents));
        }
    }

    void TradingClient::PrintAccount() const
    {
        SnapshotData snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshotCopy = snapshot_;
        }

        if (!snapshotCopy.valid)
        {
            WriteLine("No account snapshot received yet.");
            return;
        }

        WriteLine("Account: " + snapshotCopy.username);
        WriteLine("  Cash:             " + FormatMoney(snapshotCopy.cashCents));
        WriteLine("  Reserved cash:    " + FormatMoney(snapshotCopy.reservedCashCents));
        WriteLine("  Available cash:   " + FormatMoney(snapshotCopy.availableCashCents));
        for (const auto& holding : snapshotCopy.holdingsBySymbol)
        {
            WriteLine(
                "  " + holding.symbol + ": held " + std::to_string(holding.holdings)
                    + " | reserved " + std::to_string(holding.reservedHoldings)
                    + " | available " + std::to_string(holding.availableHoldings));
        }
        WriteLine(
            "  Focus (" + snapshotCopy.focusSymbol + "): "
                + std::to_string(snapshotCopy.holdings) + " held, "
                + std::to_string(snapshotCopy.availableHoldings) + " available");
    }

    void TradingClient::PrintOrders() const
    {
        SnapshotData snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshotCopy = snapshot_;
        }

        if (!snapshotCopy.valid)
        {
            WriteLine("No snapshot received yet.");
            return;
        }

        WriteLine("Open orders (" + snapshotCopy.focusSymbol + "):");
        if (snapshotCopy.openOrders.empty())
        {
            WriteLine("  None");
        }
        else
        {
            for (const auto& order : snapshotCopy.openOrders)
            {
                WriteLine(
                    "  " + order.orderId + ": " + order.side + " " + order.symbol + " "
                        + std::to_string(order.remainingQuantity) + "/"
                        + std::to_string(order.originalQuantity) + " @ "
                        + FormatPrice(order.priceCents) + " [" + order.status + "]");
            }
        }

        WriteLine("Recent orders (" + snapshotCopy.focusSymbol + "):");
        if (snapshotCopy.recentOrders.empty())
        {
            WriteLine("  None");
        }
        else
        {
            for (const auto& order : snapshotCopy.recentOrders)
            {
                WriteLine(
                    "  " + order.orderId + ": " + order.side + " " + order.symbol + " "
                        + std::to_string(order.remainingQuantity) + "/"
                        + std::to_string(order.originalQuantity) + " @ "
                        + FormatPrice(order.priceCents) + " [" + order.status + "]");
            }
        }
    }

    void TradingClient::PrintHistory() const
    {
        SnapshotData snapshotCopy;
        {
            std::lock_guard<std::mutex> lock(snapshotMutex_);
            snapshotCopy = snapshot_;
        }

        if (!snapshotCopy.valid)
        {
            WriteLine("No trade history received yet.");
            return;
        }

        WriteLine("Recent trades (" + snapshotCopy.focusSymbol + "):");
        if (snapshotCopy.recentTrades.empty())
        {
            WriteLine("  None");
            return;
        }

        for (const auto& trade : snapshotCopy.recentTrades)
        {
            WriteLine(
                "  " + trade.tradeId + ": " + trade.buyUser + " bought from "
                    + trade.sellUser + " | " + std::to_string(trade.quantity) + " "
                    + trade.symbol + " @ " + FormatPrice(trade.priceCents));
        }
    }

    void TradingClient::WriteLine(const std::string& line) const
    {
        std::lock_guard<std::mutex> lock(consoleMutex_);
        std::cout << line << '\n';
    }
}
