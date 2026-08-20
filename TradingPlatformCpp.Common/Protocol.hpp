#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file Protocol.hpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 shared trading protocol declarations and helper utilities.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

namespace trading
{
    constexpr const char* kDefaultSymbol = "ACME";
    constexpr std::array<const char*, 3> kSupportedSymbols{ "ACME", "BETA", "GAMMA" };

    struct LevelView
    {
        int priceCents{ 0 };
        int quantity{ 0 };
    };

    struct HoldingView
    {
        std::string symbol;
        int holdings{ 0 };
        int reservedHoldings{ 0 };
        int availableHoldings{ 0 };
    };

    struct OrderView
    {
        std::string orderId;
        std::string symbol;
        std::string side;
        int remainingQuantity{ 0 };
        int originalQuantity{ 0 };
        int priceCents{ 0 };
        std::string status;
    };

    struct TradeView
    {
        std::string tradeId;
        std::string symbol;
        std::string buyUser;
        std::string sellUser;
        int quantity{ 0 };
        int priceCents{ 0 };
    };

    struct SnapshotData
    {
        bool valid{ false };
        std::string username;
        std::string focusSymbol{ kDefaultSymbol };
        std::int64_t cashCents{ 0 };
        std::int64_t reservedCashCents{ 0 };
        std::int64_t availableCashCents{ 0 };
        int holdings{ 0 };
        int reservedHoldings{ 0 };
        int availableHoldings{ 0 };
        int bestBidCents{ 0 };
        int bestAskCents{ 0 };
        int lastTradeCents{ 0 };
        std::vector<HoldingView> holdingsBySymbol;
        std::vector<LevelView> buyLevels;
        std::vector<LevelView> sellLevels;
        std::vector<OrderView> openOrders;
        std::vector<OrderView> recentOrders;
        std::vector<TradeView> recentTrades;
    };

    inline std::string Trim(const std::string& value)
    {
        std::size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
        {
            ++start;
        }

        std::size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
        {
            --end;
        }

        return value.substr(start, end - start);
    }

    inline std::string ToUpper(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        return value;
    }

    inline std::string NormalizeSymbol(const std::string& value)
    {
        return ToUpper(Trim(value));
    }

    inline bool IsSupportedSymbol(const std::string& value)
    {
        const std::string normalized = NormalizeSymbol(value);
        for (const char* symbol : kSupportedSymbols)
        {
            if (normalized == symbol)
            {
                return true;
            }
        }

        return false;
    }

    inline std::string SupportedSymbolsCsv()
    {
        std::ostringstream builder;
        for (std::size_t index = 0; index < kSupportedSymbols.size(); ++index)
        {
            if (index > 0)
            {
                builder << ", ";
            }

            builder << kSupportedSymbols[index];
        }

        return builder.str();
    }

    inline std::string MakeKey(const std::string& value)
    {
        return ToUpper(Trim(value));
    }

    inline std::vector<std::string> Split(const std::string& text, char delimiter)
    {
        std::vector<std::string> parts;
        std::string current;

        for (char ch : text)
        {
            if (ch == delimiter)
            {
                parts.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(ch);
            }
        }

        parts.push_back(current);
        return parts;
    }

    inline std::string SanitizeToken(std::string text)
    {
        for (char& ch : text)
        {
            if (ch == '|' || ch == '\r' || ch == '\n')
            {
                ch = ' ';
            }
        }

        return Trim(text);
    }

    inline bool ParseInteger(const std::string& text, int& value)
    {
        try
        {
            const std::string trimmed = Trim(text);
            std::size_t consumed = 0;
            const int parsed = std::stoi(trimmed, &consumed);
            if (consumed != trimmed.size())
            {
                return false;
            }

            value = parsed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    inline bool ParsePriceCents(const std::string& text, int& cents)
    {
        const std::string trimmed = Trim(text);
        if (trimmed.empty())
        {
            return false;
        }

        const std::size_t dot = trimmed.find('.');
        if (dot != std::string::npos && trimmed.find('.', dot + 1) != std::string::npos)
        {
            return false;
        }

        const std::string wholePart = (dot == std::string::npos) ? trimmed : trimmed.substr(0, dot);
        std::string fractionPart = (dot == std::string::npos) ? "" : trimmed.substr(dot + 1);

        if (wholePart.empty())
        {
            return false;
        }

        for (char ch : wholePart)
        {
            if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
            {
                return false;
            }
        }

        for (char ch : fractionPart)
        {
            if (std::isdigit(static_cast<unsigned char>(ch)) == 0)
            {
                return false;
            }
        }

        if (fractionPart.size() > 2)
        {
            return false;
        }

        while (fractionPart.size() < 2)
        {
            fractionPart.push_back('0');
        }

        long long wholeValue = 0;
        for (char ch : wholePart)
        {
            wholeValue = (wholeValue * 10) + (ch - '0');
            if (wholeValue > (std::numeric_limits<int>::max() / 100LL))
            {
                return false;
            }
        }

        const long long fractionValue = fractionPart.empty() ? 0LL : std::stoll(fractionPart);
        const long long total = (wholeValue * 100LL) + fractionValue;
        if (total < 0 || total > std::numeric_limits<int>::max())
        {
            return false;
        }

        cents = static_cast<int>(total);
        return true;
    }

    inline std::string FormatMoney(std::int64_t cents)
    {
        const bool negative = cents < 0;
        const std::int64_t absolute = negative ? -cents : cents;

        std::ostringstream builder;
        if (negative)
        {
            builder << '-';
        }

        builder << (absolute / 100) << '.' << std::setw(2) << std::setfill('0') << (absolute % 100);
        return builder.str();
    }

    inline std::string FormatPrice(int cents)
    {
        return FormatMoney(cents);
    }

    inline std::string TimestampUtc()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
        std::tm utcTime{};
        gmtime_s(&utcTime, &currentTime);

        std::ostringstream builder;
        builder << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
        return builder.str();
    }

    inline bool SendLine(SOCKET socketHandle, const std::string& line)
    {
        std::string buffer = line;
        buffer.push_back('\n');

        std::size_t sent = 0;
        while (sent < buffer.size())
        {
            const int written = send(
                socketHandle,
                buffer.data() + sent,
                static_cast<int>(buffer.size() - sent),
                0);

            if (written == SOCKET_ERROR || written == 0)
            {
                return false;
            }

            sent += static_cast<std::size_t>(written);
        }

        return true;
    }

    inline bool RecvLine(SOCKET socketHandle, std::string& line)
    {
        line.clear();

        char ch = '\0';
        while (true)
        {
            const int received = recv(socketHandle, &ch, 1, 0);
            if (received == 0)
            {
                return !line.empty();
            }

            if (received == SOCKET_ERROR)
            {
                return false;
            }

            if (ch == '\n')
            {
                return true;
            }

            if (ch != '\r')
            {
                line.push_back(ch);
            }
        }
    }
}
