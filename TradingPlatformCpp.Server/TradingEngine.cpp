#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file TradingEngine.cpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 multi-symbol trading engine implementation, matching logic, and persistence.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#include "TradingEngine.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace
{
    constexpr std::int64_t kStartingCashCents = 1000000;
    constexpr int kStartingHoldings = 50;
    constexpr int kSystemLiquidity = 1000;
    constexpr int kSpreadCents = 100;

    bool TryParseInt64(const std::string& text, std::int64_t& value)
    {
        try
        {
            const std::string trimmed = trading::Trim(text);
            std::size_t consumed = 0;
            const long long parsed = std::stoll(trimmed, &consumed);
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

    bool TryParsePrefixedNumber(const std::string& text, const std::string& prefix, std::int64_t& value)
    {
        if (text.size() <= prefix.size() || text.substr(0, prefix.size()) != prefix)
        {
            return false;
        }

        return TryParseInt64(text.substr(prefix.size()), value);
    }
}

namespace trading::server
{
    TradingEngine::TradingEngine()
    {
        LoadState();
    }

    TradingEngine::LoginResult TradingEngine::LoginResult::Failure(const std::string& error)
    {
        LoginResult result;
        result.errorMessage = error;
        return result;
    }

    TradingEngine::OperationResult TradingEngine::OperationResult::Failure(const std::string& error)
    {
        OperationResult result;
        result.errorMessage = error;
        return result;
    }

    TradingEngine::OperationResult TradingEngine::OperationResult::Success(bool stateChangedValue)
    {
        OperationResult result;
        result.success = true;
        result.stateChanged = stateChangedValue;
        return result;
    }

    void TradingEngine::OperationResult::AddBroadcast(const std::string& message)
    {
        if (!message.empty())
        {
            broadcastMessages.push_back(message);
        }
    }

    void TradingEngine::OperationResult::AddPrivate(const std::string& username, const std::string& message)
    {
        if (!username.empty() && !message.empty())
        {
            privateMessages[KeyFor(username)].push_back(message);
        }
    }

    TradingEngine::LoginResult TradingEngine::Login(const std::string& username)
    {
        const std::string trimmed = Trim(username);
        if (trimmed.empty())
        {
            return LoginResult::Failure("Username is required.");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        EnsureMarketsInitializedLocked();

        const std::string key = KeyFor(trimmed);
        auto accountIt = accounts_.find(key);
        bool created = false;

        if (accountIt == accounts_.end())
        {
            AccountState account;
            account.username = trimmed;
            account.cashCents = kStartingCashCents;
            InitializeAccountPositions(account);
            accountIt = accounts_.emplace(key, std::move(account)).first;
            created = true;
        }
        else
        {
            InitializeAccountPositions(accountIt->second);
        }

        LoginResult result;
        result.success = true;
        result.username = accountIt->second.username;
        result.message = created
            ? ("Created account for " + accountIt->second.username + ". Starting cash "
                + FormatMoney(kStartingCashCents) + ", starting holdings "
                + std::to_string(kStartingHoldings) + " each for " + SupportedSymbolsCsv() + ".")
            : ("Welcome back, " + accountIt->second.username + ". Your account state has been restored.");

        if (created)
        {
            SaveStateLocked();
        }

        return result;
    }

    TradingEngine::OperationResult TradingEngine::PlaceOrder(
        const std::string& username,
        const std::string& symbol,
        const std::string& sideText,
        int quantity,
        int priceCents)
    {
        if (quantity <= 0)
        {
            return OperationResult::Failure("Quantity must be greater than zero.");
        }

        if (priceCents <= 0)
        {
            return OperationResult::Failure("Price must be greater than zero.");
        }

        const std::string normalizedSymbol = NormalizeSymbol(symbol);
        if (!IsSupportedSymbol(normalizedSymbol))
        {
            return OperationResult::Failure("Unsupported symbol. Use one of: " + SupportedSymbolsCsv() + '.');
        }

        Side side = Side::Buy;
        if (!TryParseSide(sideText, side))
        {
            return OperationResult::Failure("Side must be BUY or SELL.");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        EnsureMarketsInitializedLocked();

        const std::string key = KeyFor(username);
        auto accountIt = accounts_.find(key);
        if (accountIt == accounts_.end())
        {
            return OperationResult::Failure("Please login before placing orders.");
        }

        AccountState& account = accountIt->second;
        InitializeAccountPositions(account);

        const std::int64_t orderNumber = nextOrderNumber_++;
        auto order = std::make_shared<OrderRecord>();
        order->orderId = FormatOrderId(orderNumber);
        order->username = account.username;
        order->symbol = normalizedSymbol;
        order->side = side;
        order->originalQuantity = quantity;
        order->remainingQuantity = quantity;
        order->priceCents = priceCents;
        order->status = OrderStatus::Open;
        order->sequence = orderNumber;
        order->createdUtc = TimestampUtc();
        order->updatedUtc = order->createdUtc;

        if (side == Side::Buy)
        {
            const std::int64_t requiredCash = static_cast<std::int64_t>(order->priceCents) * order->originalQuantity;
            if (AvailableCash(account) < requiredCash)
            {
                return OperationResult::Failure(
                    "Insufficient available cash. Need " + FormatMoney(requiredCash)
                        + ", available " + FormatMoney(AvailableCash(account)) + '.');
            }

            account.reservedCashCents += requiredCash;
        }
        else
        {
            if (AvailableHoldings(account, normalizedSymbol) < order->originalQuantity)
            {
                return OperationResult::Failure(
                    "Insufficient available holdings for " + normalizedSymbol + ". Need "
                        + std::to_string(order->originalQuantity) + ", available "
                        + std::to_string(AvailableHoldings(account, normalizedSymbol)) + '.');
            }

            account.reservedHoldingsBySymbol[normalizedSymbol] += order->originalQuantity;
        }

        orderHistory_.push_back(order);

        OperationResult result = OperationResult::Success(true);
        result.AddPrivate(
            account.username,
            "Accepted " + SideToString(side) + " order " + order->orderId + ": "
                + std::to_string(order->originalQuantity) + " " + order->symbol + " @ "
                + FormatPrice(order->priceCents) + '.');

        if (side == Side::Buy)
        {
            MatchBuyOrder(order->symbol, order, account, result);
        }
        else
        {
            MatchSellOrder(order->symbol, order, account, result);
        }

        UpdateOrderStatus(*order);

        if (order->remainingQuantity > 0)
        {
            AddToBook(order);
            result.AddBroadcast(
                order->username + " placed " + SideToString(side) + " "
                + std::to_string(order->originalQuantity) + " " + order->symbol + " @ "
                + FormatPrice(order->priceCents) + ". Remaining open quantity: "
                + std::to_string(order->remainingQuantity) + '.');
        }
        else
        {
            result.AddBroadcast(
                "Order " + order->orderId + " for " + order->username + " on " + order->symbol
                + " was fully filled on entry.");
        }

        SaveStateLocked();
        return result;
    }

    TradingEngine::OperationResult TradingEngine::CancelOrder(const std::string& username, const std::string& orderId)
    {
        const std::string trimmed = Trim(orderId);
        if (trimmed.empty())
        {
            return OperationResult::Failure("Order ID is required.");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        EnsureMarketsInitializedLocked();

        const std::string userKey = KeyFor(username);
        auto accountIt = accounts_.find(userKey);
        if (accountIt == accounts_.end())
        {
            return OperationResult::Failure("Please login before cancelling orders.");
        }

        InitializeAccountPositions(accountIt->second);

        std::shared_ptr<OrderRecord> order;
        for (const char* symbol : kSupportedSymbols)
        {
            auto& buyOrders = buyOrdersBySymbol_[symbol];
            const auto buyIt = std::find_if(
                buyOrders.begin(),
                buyOrders.end(),
                [&](const std::shared_ptr<OrderRecord>& candidate) {
                    return KeyFor(candidate->username) == userKey && candidate->orderId == trimmed;
                });

            if (buyIt != buyOrders.end())
            {
                order = *buyIt;
                buyOrders.erase(buyIt);
                accountIt->second.reservedCashCents -=
                    static_cast<std::int64_t>(order->remainingQuantity) * order->priceCents;
                if (accountIt->second.reservedCashCents < 0)
                {
                    accountIt->second.reservedCashCents = 0;
                }

                break;
            }

            auto& sellOrders = sellOrdersBySymbol_[symbol];
            const auto sellIt = std::find_if(
                sellOrders.begin(),
                sellOrders.end(),
                [&](const std::shared_ptr<OrderRecord>& candidate) {
                    return KeyFor(candidate->username) == userKey && candidate->orderId == trimmed;
                });

            if (sellIt != sellOrders.end())
            {
                order = *sellIt;
                sellOrders.erase(sellIt);
                int& reserved = accountIt->second.reservedHoldingsBySymbol[order->symbol];
                reserved -= order->remainingQuantity;
                if (reserved < 0)
                {
                    reserved = 0;
                }

                break;
            }
        }

        if (!order)
        {
            return OperationResult::Failure("Open order not found for this user.");
        }

        order->status = OrderStatus::Cancelled;
        order->updatedUtc = TimestampUtc();

        OperationResult result = OperationResult::Success(true);
        result.AddPrivate(accountIt->second.username, "Cancelled order " + order->orderId + '.');
        result.AddBroadcast(accountIt->second.username + " cancelled order " + order->orderId + " on " + order->symbol + '.');
        SaveStateLocked();
        return result;
    }

    trading::SnapshotData TradingEngine::BuildSnapshot(const std::string& username, const std::string& focusSymbol) const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const std::string key = KeyFor(username);
        const auto accountIt = accounts_.find(key);
        if (accountIt == accounts_.end())
        {
            return {};
        }

        const std::string normalizedSymbol = IsSupportedSymbol(focusSymbol) ? NormalizeSymbol(focusSymbol) : kDefaultSymbol;
        const AccountState& account = accountIt->second;

        SnapshotData snapshot;
        snapshot.valid = true;
        snapshot.username = account.username;
        snapshot.focusSymbol = normalizedSymbol;
        snapshot.cashCents = account.cashCents;
        snapshot.reservedCashCents = account.reservedCashCents;
        snapshot.availableCashCents = AvailableCash(account);
        snapshot.holdings = HoldingsForSymbol(account, normalizedSymbol);
        snapshot.reservedHoldings = ReservedHoldingsForSymbol(account, normalizedSymbol);
        snapshot.availableHoldings = AvailableHoldings(account, normalizedSymbol);
        snapshot.bestBidCents = SystemBidCents(LastTradeCents(normalizedSymbol));
        snapshot.bestAskCents = SystemAskCents(LastTradeCents(normalizedSymbol));
        snapshot.lastTradeCents = LastTradeCents(normalizedSymbol);

        for (const char* symbol : kSupportedSymbols)
        {
            HoldingView holding;
            holding.symbol = symbol;
            holding.holdings = HoldingsForSymbol(account, symbol);
            holding.reservedHoldings = ReservedHoldingsForSymbol(account, symbol);
            holding.availableHoldings = AvailableHoldings(account, symbol);
            snapshot.holdingsBySymbol.push_back(holding);
        }

        static const std::vector<std::shared_ptr<OrderRecord>> kEmptyBook;
        const auto buyBookIt = buyOrdersBySymbol_.find(normalizedSymbol);
        const auto sellBookIt = sellOrdersBySymbol_.find(normalizedSymbol);
        const std::vector<std::shared_ptr<OrderRecord>>& buyBook =
            buyBookIt == buyOrdersBySymbol_.end() ? kEmptyBook : buyBookIt->second;
        const std::vector<std::shared_ptr<OrderRecord>>& sellBook =
            sellBookIt == sellOrdersBySymbol_.end() ? kEmptyBook : sellBookIt->second;

        if (!buyBook.empty())
        {
            snapshot.bestBidCents = std::max(snapshot.bestBidCents, buyBook.front()->priceCents);
        }

        if (!sellBook.empty())
        {
            snapshot.bestAskCents = std::min(snapshot.bestAskCents, sellBook.front()->priceCents);
        }

        std::map<int, int, std::greater<int>> buyLevels;
        std::map<int, int> sellLevels;
        buyLevels[SystemBidCents(snapshot.lastTradeCents)] += kSystemLiquidity;
        sellLevels[SystemAskCents(snapshot.lastTradeCents)] += kSystemLiquidity;

        for (const auto& order : buyBook)
        {
            buyLevels[order->priceCents] += order->remainingQuantity;
        }

        for (const auto& order : sellBook)
        {
            sellLevels[order->priceCents] += order->remainingQuantity;
        }

        for (const auto& level : buyLevels)
        {
            if (snapshot.buyLevels.size() >= 5)
            {
                break;
            }

            snapshot.buyLevels.push_back({ level.first, level.second });
        }

        for (const auto& level : sellLevels)
        {
            if (snapshot.sellLevels.size() >= 5)
            {
                break;
            }

            snapshot.sellLevels.push_back({ level.first, level.second });
        }

        for (const auto& order : orderHistory_)
        {
            if (KeyFor(order->username) != key || order->symbol != normalizedSymbol)
            {
                continue;
            }

            OrderView view;
            view.orderId = order->orderId;
            view.symbol = order->symbol;
            view.side = SideToString(order->side);
            view.remainingQuantity = order->remainingQuantity;
            view.originalQuantity = order->originalQuantity;
            view.priceCents = order->priceCents;
            view.status = StatusToString(order->status);

            if (order->status == OrderStatus::Open || order->status == OrderStatus::PartiallyFilled)
            {
                snapshot.openOrders.push_back(view);
            }
        }

        for (auto it = orderHistory_.rbegin(); it != orderHistory_.rend() && snapshot.recentOrders.size() < 15; ++it)
        {
            if (KeyFor((*it)->username) != key || (*it)->symbol != normalizedSymbol)
            {
                continue;
            }

            OrderView view;
            view.orderId = (*it)->orderId;
            view.symbol = (*it)->symbol;
            view.side = SideToString((*it)->side);
            view.remainingQuantity = (*it)->remainingQuantity;
            view.originalQuantity = (*it)->originalQuantity;
            view.priceCents = (*it)->priceCents;
            view.status = StatusToString((*it)->status);
            snapshot.recentOrders.push_back(view);
        }

        for (auto it = tradeHistory_.rbegin(); it != tradeHistory_.rend() && snapshot.recentTrades.size() < 15; ++it)
        {
            if (it->symbol != normalizedSymbol)
            {
                continue;
            }

            snapshot.recentTrades.push_back(
                { it->tradeId, it->symbol, it->buyUser, it->sellUser, it->quantity, it->priceCents });
        }

        return snapshot;
    }

    std::string TradingEngine::KeyFor(const std::string& username)
    {
        return MakeKey(username);
    }

    bool TradingEngine::TryParseSide(const std::string& value, Side& side)
    {
        const std::string normalized = ToUpper(Trim(value));
        if (normalized == "BUY")
        {
            side = Side::Buy;
            return true;
        }

        if (normalized == "SELL")
        {
            side = Side::Sell;
            return true;
        }

        return false;
    }

    bool TradingEngine::TryParseStatus(const std::string& value, OrderStatus& status)
    {
        const std::string normalized = ToUpper(Trim(value));
        if (normalized == "OPEN")
        {
            status = OrderStatus::Open;
            return true;
        }

        if (normalized == "PARTIAL")
        {
            status = OrderStatus::PartiallyFilled;
            return true;
        }

        if (normalized == "FILLED")
        {
            status = OrderStatus::Filled;
            return true;
        }

        if (normalized == "CANCELLED")
        {
            status = OrderStatus::Cancelled;
            return true;
        }

        return false;
    }

    std::string TradingEngine::SideToString(Side side)
    {
        return side == Side::Buy ? "BUY" : "SELL";
    }

    std::string TradingEngine::StatusToString(OrderStatus status)
    {
        switch (status)
        {
        case OrderStatus::Open:
            return "OPEN";
        case OrderStatus::PartiallyFilled:
            return "PARTIAL";
        case OrderStatus::Filled:
            return "FILLED";
        case OrderStatus::Cancelled:
            return "CANCELLED";
        default:
            return "UNKNOWN";
        }
    }

    std::int64_t TradingEngine::AvailableCash(const AccountState& account)
    {
        return account.cashCents - account.reservedCashCents;
    }

    int TradingEngine::HoldingsForSymbol(const AccountState& account, const std::string& symbol)
    {
        const auto it = account.holdingsBySymbol.find(symbol);
        return it == account.holdingsBySymbol.end() ? kStartingHoldings : it->second;
    }

    int TradingEngine::ReservedHoldingsForSymbol(const AccountState& account, const std::string& symbol)
    {
        const auto it = account.reservedHoldingsBySymbol.find(symbol);
        return it == account.reservedHoldingsBySymbol.end() ? 0 : it->second;
    }

    int TradingEngine::AvailableHoldings(const AccountState& account, const std::string& symbol)
    {
        return HoldingsForSymbol(account, symbol) - ReservedHoldingsForSymbol(account, symbol);
    }

    int TradingEngine::StartingLastTradeCents(const std::string& symbol)
    {
        if (symbol == "BETA")
        {
            return 7500;
        }

        if (symbol == "GAMMA")
        {
            return 12500;
        }

        return 10000;
    }

    int TradingEngine::SystemBidCents(int lastTradeCents)
    {
        return std::max(100, lastTradeCents - kSpreadCents);
    }

    int TradingEngine::SystemAskCents(int lastTradeCents)
    {
        return lastTradeCents + kSpreadCents;
    }

    std::string TradingEngine::FormatOrderId(std::int64_t number)
    {
        std::ostringstream builder;
        builder << "ORD" << std::setw(4) << std::setfill('0') << number;
        return builder.str();
    }

    std::string TradingEngine::FormatTradeId(std::int64_t number)
    {
        std::ostringstream builder;
        builder << "TRD" << std::setw(4) << std::setfill('0') << number;
        return builder.str();
    }

    void TradingEngine::SortBuyOrders(std::vector<std::shared_ptr<OrderRecord>>& orders)
    {
        std::sort(
            orders.begin(),
            orders.end(),
            [](const std::shared_ptr<OrderRecord>& left, const std::shared_ptr<OrderRecord>& right) {
                if (left->priceCents != right->priceCents)
                {
                    return left->priceCents > right->priceCents;
                }

                return left->sequence < right->sequence;
            });
    }

    void TradingEngine::SortSellOrders(std::vector<std::shared_ptr<OrderRecord>>& orders)
    {
        std::sort(
            orders.begin(),
            orders.end(),
            [](const std::shared_ptr<OrderRecord>& left, const std::shared_ptr<OrderRecord>& right) {
                if (left->priceCents != right->priceCents)
                {
                    return left->priceCents < right->priceCents;
                }

                return left->sequence < right->sequence;
            });
    }

    void TradingEngine::EnsureMarketsInitializedLocked()
    {
        for (const char* symbol : kSupportedSymbols)
        {
            buyOrdersBySymbol_.try_emplace(symbol);
            sellOrdersBySymbol_.try_emplace(symbol);
            lastTradeCentsBySymbol_.try_emplace(symbol, StartingLastTradeCents(symbol));
        }
    }

    void TradingEngine::InitializeAccountPositions(AccountState& account) const
    {
        for (const char* symbol : kSupportedSymbols)
        {
            account.holdingsBySymbol.try_emplace(symbol, kStartingHoldings);
            account.reservedHoldingsBySymbol.try_emplace(symbol, 0);
        }
    }

    int TradingEngine::LastTradeCents(const std::string& symbol) const
    {
        const auto it = lastTradeCentsBySymbol_.find(symbol);
        return it == lastTradeCentsBySymbol_.end() ? StartingLastTradeCents(symbol) : it->second;
    }

    void TradingEngine::MatchBuyOrder(
        const std::string& symbol,
        const std::shared_ptr<OrderRecord>& incomingBuy,
        AccountState& buyerAccount,
        OperationResult& result)
    {
        auto& sellOrders = sellOrdersBySymbol_[symbol];
        const std::string buyerKey = KeyFor(incomingBuy->username);

        while (incomingBuy->remainingQuantity > 0)
        {
            std::shared_ptr<OrderRecord> bestSell;
            for (const auto& candidate : sellOrders)
            {
                if (candidate->priceCents > incomingBuy->priceCents)
                {
                    break;
                }

                if (KeyFor(candidate->username) != buyerKey)
                {
                    bestSell = candidate;
                    break;
                }
            }

            const int systemAsk = SystemAskCents(LastTradeCents(symbol));
            const bool canMatchUser = bestSell != nullptr;
            const bool canMatchSystem = systemAsk <= incomingBuy->priceCents;

            if (!canMatchUser && !canMatchSystem)
            {
                break;
            }

            if (canMatchUser && (!canMatchSystem || bestSell->priceCents <= systemAsk))
            {
                ExecuteAgainstRestingSell(symbol, incomingBuy, buyerAccount, bestSell, result);
            }
            else
            {
                ExecuteAgainstSystemAsk(symbol, incomingBuy, buyerAccount, systemAsk, result);
            }
        }
    }

    void TradingEngine::MatchSellOrder(
        const std::string& symbol,
        const std::shared_ptr<OrderRecord>& incomingSell,
        AccountState& sellerAccount,
        OperationResult& result)
    {
        auto& buyOrders = buyOrdersBySymbol_[symbol];
        const std::string sellerKey = KeyFor(incomingSell->username);

        while (incomingSell->remainingQuantity > 0)
        {
            std::shared_ptr<OrderRecord> bestBuy;
            for (const auto& candidate : buyOrders)
            {
                if (candidate->priceCents < incomingSell->priceCents)
                {
                    break;
                }

                if (KeyFor(candidate->username) != sellerKey)
                {
                    bestBuy = candidate;
                    break;
                }
            }

            const int systemBid = SystemBidCents(LastTradeCents(symbol));
            const bool canMatchUser = bestBuy != nullptr;
            const bool canMatchSystem = systemBid >= incomingSell->priceCents;

            if (!canMatchUser && !canMatchSystem)
            {
                break;
            }

            if (canMatchUser && (!canMatchSystem || bestBuy->priceCents >= systemBid))
            {
                ExecuteAgainstRestingBuy(symbol, incomingSell, sellerAccount, bestBuy, result);
            }
            else
            {
                ExecuteAgainstSystemBid(symbol, incomingSell, sellerAccount, systemBid, result);
            }
        }
    }

    void TradingEngine::ExecuteAgainstRestingSell(
        const std::string& symbol,
        const std::shared_ptr<OrderRecord>& incomingBuy,
        AccountState& buyerAccount,
        const std::shared_ptr<OrderRecord>& restingSell,
        OperationResult& result)
    {
        AccountState& sellerAccount = accounts_.at(KeyFor(restingSell->username));
        InitializeAccountPositions(sellerAccount);

        const int executedQuantity = std::min(incomingBuy->remainingQuantity, restingSell->remainingQuantity);
        const int executionPriceCents = restingSell->priceCents;

        incomingBuy->remainingQuantity -= executedQuantity;
        incomingBuy->updatedUtc = TimestampUtc();
        ApplyBuyerFill(buyerAccount, incomingBuy->priceCents, executedQuantity, executionPriceCents);
        buyerAccount.holdingsBySymbol[symbol] += executedQuantity;

        restingSell->remainingQuantity -= executedQuantity;
        restingSell->updatedUtc = TimestampUtc();
        ApplySellerFill(sellerAccount, symbol, executedQuantity, executionPriceCents);
        UpdateOrderStatus(*restingSell);
        if (restingSell->remainingQuantity <= 0)
        {
            auto& sellOrders = sellOrdersBySymbol_[symbol];
            sellOrders.erase(std::remove(sellOrders.begin(), sellOrders.end(), restingSell), sellOrders.end());
        }

        RecordTrade(symbol, incomingBuy->username, restingSell->username, executedQuantity, executionPriceCents, result);
    }

    void TradingEngine::ExecuteAgainstRestingBuy(
        const std::string& symbol,
        const std::shared_ptr<OrderRecord>& incomingSell,
        AccountState& sellerAccount,
        const std::shared_ptr<OrderRecord>& restingBuy,
        OperationResult& result)
    {
        AccountState& buyerAccount = accounts_.at(KeyFor(restingBuy->username));
        InitializeAccountPositions(buyerAccount);

        const int executedQuantity = std::min(incomingSell->remainingQuantity, restingBuy->remainingQuantity);
        const int executionPriceCents = restingBuy->priceCents;

        incomingSell->remainingQuantity -= executedQuantity;
        incomingSell->updatedUtc = TimestampUtc();
        ApplySellerFill(sellerAccount, symbol, executedQuantity, executionPriceCents);

        restingBuy->remainingQuantity -= executedQuantity;
        restingBuy->updatedUtc = TimestampUtc();
        ApplyBuyerFill(buyerAccount, restingBuy->priceCents, executedQuantity, executionPriceCents);
        buyerAccount.holdingsBySymbol[symbol] += executedQuantity;
        UpdateOrderStatus(*restingBuy);
        if (restingBuy->remainingQuantity <= 0)
        {
            auto& buyOrders = buyOrdersBySymbol_[symbol];
            buyOrders.erase(std::remove(buyOrders.begin(), buyOrders.end(), restingBuy), buyOrders.end());
        }

        RecordTrade(symbol, restingBuy->username, incomingSell->username, executedQuantity, executionPriceCents, result);
    }

    void TradingEngine::ExecuteAgainstSystemAsk(
        const std::string& symbol,
        const std::shared_ptr<OrderRecord>& incomingBuy,
        AccountState& buyerAccount,
        int systemAskCents,
        OperationResult& result)
    {
        const int executedQuantity = incomingBuy->remainingQuantity;
        incomingBuy->remainingQuantity = 0;
        incomingBuy->updatedUtc = TimestampUtc();

        ApplyBuyerFill(buyerAccount, incomingBuy->priceCents, executedQuantity, systemAskCents);
        buyerAccount.holdingsBySymbol[symbol] += executedQuantity;

        RecordTrade(symbol, incomingBuy->username, "MARKET_MAKER", executedQuantity, systemAskCents, result);
    }

    void TradingEngine::ExecuteAgainstSystemBid(
        const std::string& symbol,
        const std::shared_ptr<OrderRecord>& incomingSell,
        AccountState& sellerAccount,
        int systemBidCents,
        OperationResult& result)
    {
        const int executedQuantity = incomingSell->remainingQuantity;
        incomingSell->remainingQuantity = 0;
        incomingSell->updatedUtc = TimestampUtc();

        ApplySellerFill(sellerAccount, symbol, executedQuantity, systemBidCents);
        RecordTrade(symbol, "MARKET_MAKER", incomingSell->username, executedQuantity, systemBidCents, result);
    }

    void TradingEngine::RecordTrade(
        const std::string& symbol,
        const std::string& buyUser,
        const std::string& sellUser,
        int quantity,
        int priceCents,
        OperationResult& result)
    {
        TradeRecord trade;
        trade.tradeId = FormatTradeId(nextTradeNumber_++);
        trade.symbol = symbol;
        trade.buyUser = buyUser;
        trade.sellUser = sellUser;
        trade.quantity = quantity;
        trade.priceCents = priceCents;
        trade.executedUtc = TimestampUtc();

        tradeHistory_.push_back(trade);
        lastTradeCentsBySymbol_[symbol] = priceCents;

        result.AddBroadcast(
            "Trade " + trade.tradeId + ": " + trade.buyUser + " bought "
                + std::to_string(trade.quantity) + " " + trade.symbol + " from "
                + trade.sellUser + " @ " + FormatPrice(trade.priceCents) + '.');
    }

    void TradingEngine::AddToBook(const std::shared_ptr<OrderRecord>& order)
    {
        if (order->side == Side::Buy)
        {
            auto& orders = buyOrdersBySymbol_[order->symbol];
            orders.push_back(order);
            SortBuyOrders(orders);
        }
        else
        {
            auto& orders = sellOrdersBySymbol_[order->symbol];
            orders.push_back(order);
            SortSellOrders(orders);
        }
    }

    void TradingEngine::ApplyBuyerFill(
        AccountState& account,
        int reservedPriceCents,
        int quantity,
        int executionPriceCents)
    {
        account.reservedCashCents -= static_cast<std::int64_t>(reservedPriceCents) * quantity;
        if (account.reservedCashCents < 0)
        {
            account.reservedCashCents = 0;
        }

        account.cashCents -= static_cast<std::int64_t>(executionPriceCents) * quantity;
    }

    void TradingEngine::ApplySellerFill(
        AccountState& account,
        const std::string& symbol,
        int quantity,
        int executionPriceCents)
    {
        int& reserved = account.reservedHoldingsBySymbol[symbol];
        reserved -= quantity;
        if (reserved < 0)
        {
            reserved = 0;
        }

        account.holdingsBySymbol[symbol] -= quantity;
        account.cashCents += static_cast<std::int64_t>(executionPriceCents) * quantity;
    }

    void TradingEngine::UpdateOrderStatus(OrderRecord& order)
    {
        if (order.remainingQuantity <= 0)
        {
            order.remainingQuantity = 0;
            order.status = OrderStatus::Filled;
        }
        else if (order.remainingQuantity < order.originalQuantity)
        {
            order.status = OrderStatus::PartiallyFilled;
        }
        else
        {
            order.status = OrderStatus::Open;
        }

        order.updatedUtc = TimestampUtc();
    }

    void TradingEngine::LoadState()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        accounts_.clear();
        buyOrdersBySymbol_.clear();
        sellOrdersBySymbol_.clear();
        orderHistory_.clear();
        tradeHistory_.clear();
        lastTradeCentsBySymbol_.clear();
        nextOrderNumber_ = 1;
        nextTradeNumber_ = 1;
        EnsureMarketsInitializedLocked();

        std::ifstream input(stateFilePath_);
        if (!input.is_open())
        {
            return;
        }

        std::string line;
        while (std::getline(input, line))
        {
            const std::vector<std::string> parts = Split(Trim(line), '|');
            if (parts.empty())
            {
                continue;
            }

            const std::string recordType = ToUpper(parts[0]);

            if (recordType == "META")
            {
                if (parts.size() >= 3)
                {
                    std::int64_t nextOrder = 0;
                    std::int64_t nextTrade = 0;
                    if (TryParseInt64(parts[1], nextOrder) && TryParseInt64(parts[2], nextTrade))
                    {
                        nextOrderNumber_ = std::max<std::int64_t>(1, nextOrder);
                        nextTradeNumber_ = std::max<std::int64_t>(1, nextTrade);
                    }
                }

                if (parts.size() >= 4)
                {
                    int legacyLastTrade = 0;
                    if (ParseInteger(parts[3], legacyLastTrade))
                    {
                        lastTradeCentsBySymbol_[kDefaultSymbol] = std::max(1, legacyLastTrade);
                    }
                }

                continue;
            }

            if (recordType == "MARKET" && parts.size() >= 3)
            {
                int lastTrade = 0;
                const std::string symbol = NormalizeSymbol(parts[1]);
                if (IsSupportedSymbol(symbol) && ParseInteger(parts[2], lastTrade))
                {
                    lastTradeCentsBySymbol_[symbol] = std::max(1, lastTrade);
                }

                continue;
            }

            if (recordType == "ACCOUNT")
            {
                const std::string username = SanitizeToken(parts[1]);
                if (username.empty())
                {
                    continue;
                }

                AccountState account;
                account.username = username;

                if (parts.size() >= 6)
                {
                    std::int64_t cash = 0;
                    std::int64_t reservedCash = 0;
                    int holdings = 0;
                    int reservedHoldings = 0;
                    if (!TryParseInt64(parts[2], cash) || !TryParseInt64(parts[3], reservedCash)
                        || !ParseInteger(parts[4], holdings) || !ParseInteger(parts[5], reservedHoldings))
                    {
                        continue;
                    }

                    account.cashCents = cash;
                    account.reservedCashCents = reservedCash;
                    account.holdingsBySymbol[kDefaultSymbol] = holdings;
                    account.reservedHoldingsBySymbol[kDefaultSymbol] = reservedHoldings;
                }
                else if (parts.size() >= 4)
                {
                    std::int64_t cash = 0;
                    std::int64_t reservedCash = 0;
                    if (!TryParseInt64(parts[2], cash) || !TryParseInt64(parts[3], reservedCash))
                    {
                        continue;
                    }

                    account.cashCents = cash;
                    account.reservedCashCents = reservedCash;
                }
                else
                {
                    continue;
                }

                InitializeAccountPositions(account);
                accounts_[KeyFor(account.username)] = account;
                continue;
            }

            if (recordType == "POSITION" && parts.size() >= 5)
            {
                const std::string username = SanitizeToken(parts[1]);
                const std::string symbol = NormalizeSymbol(parts[2]);
                int holdings = 0;
                int reserved = 0;
                if (username.empty() || !IsSupportedSymbol(symbol)
                    || !ParseInteger(parts[3], holdings) || !ParseInteger(parts[4], reserved))
                {
                    continue;
                }

                AccountState& account = accounts_[KeyFor(username)];
                if (account.username.empty())
                {
                    account.username = username;
                    account.cashCents = kStartingCashCents;
                }

                account.holdingsBySymbol[symbol] = holdings;
                account.reservedHoldingsBySymbol[symbol] = reserved;
                continue;
            }

            if (recordType == "ORDER")
            {
                Side side = Side::Buy;
                OrderStatus status = OrderStatus::Open;
                std::int64_t sequence = 0;
                int originalQuantity = 0;
                int remainingQuantity = 0;
                int priceCents = 0;

                auto order = std::make_shared<OrderRecord>();

                if (parts.size() >= 12)
                {
                    order->orderId = SanitizeToken(parts[1]);
                    order->username = SanitizeToken(parts[2]);
                    order->symbol = NormalizeSymbol(parts[3]);
                    if (!IsSupportedSymbol(order->symbol) || !TryParseSide(parts[4], side) || !TryParseStatus(parts[8], status)
                        || !ParseInteger(parts[5], originalQuantity) || !ParseInteger(parts[6], remainingQuantity)
                        || !ParseInteger(parts[7], priceCents) || !TryParseInt64(parts[9], sequence))
                    {
                        continue;
                    }

                    order->createdUtc = SanitizeToken(parts[10]);
                    order->updatedUtc = SanitizeToken(parts[11]);
                }
                else if (parts.size() >= 11)
                {
                    order->orderId = SanitizeToken(parts[1]);
                    order->username = SanitizeToken(parts[2]);
                    order->symbol = kDefaultSymbol;
                    if (!TryParseSide(parts[3], side) || !TryParseStatus(parts[7], status)
                        || !ParseInteger(parts[4], originalQuantity) || !ParseInteger(parts[5], remainingQuantity)
                        || !ParseInteger(parts[6], priceCents) || !TryParseInt64(parts[8], sequence))
                    {
                        continue;
                    }

                    order->createdUtc = SanitizeToken(parts[9]);
                    order->updatedUtc = SanitizeToken(parts[10]);
                }
                else
                {
                    continue;
                }

                if (order->orderId.empty() || order->username.empty())
                {
                    continue;
                }

                order->side = side;
                order->originalQuantity = originalQuantity;
                order->remainingQuantity = remainingQuantity;
                order->priceCents = priceCents;
                order->status = status;
                order->sequence = sequence;
                orderHistory_.push_back(order);

                if (status == OrderStatus::Open || status == OrderStatus::PartiallyFilled)
                {
                    if (side == Side::Buy)
                    {
                        buyOrdersBySymbol_[order->symbol].push_back(order);
                    }
                    else
                    {
                        sellOrdersBySymbol_[order->symbol].push_back(order);
                    }
                }

                nextOrderNumber_ = std::max(nextOrderNumber_, sequence + 1);
                continue;
            }

            if (recordType == "TRADE")
            {
                TradeRecord trade;
                int quantity = 0;
                int priceCents = 0;

                if (parts.size() >= 8)
                {
                    trade.tradeId = SanitizeToken(parts[1]);
                    trade.symbol = NormalizeSymbol(parts[2]);
                    trade.buyUser = SanitizeToken(parts[3]);
                    trade.sellUser = SanitizeToken(parts[4]);
                    if (!IsSupportedSymbol(trade.symbol) || !ParseInteger(parts[5], quantity) || !ParseInteger(parts[6], priceCents))
                    {
                        continue;
                    }

                    trade.executedUtc = SanitizeToken(parts[7]);
                }
                else if (parts.size() >= 7)
                {
                    trade.tradeId = SanitizeToken(parts[1]);
                    trade.symbol = kDefaultSymbol;
                    trade.buyUser = SanitizeToken(parts[2]);
                    trade.sellUser = SanitizeToken(parts[3]);
                    if (!ParseInteger(parts[4], quantity) || !ParseInteger(parts[5], priceCents))
                    {
                        continue;
                    }

                    trade.executedUtc = SanitizeToken(parts[6]);
                }
                else
                {
                    continue;
                }

                trade.quantity = quantity;
                trade.priceCents = priceCents;
                tradeHistory_.push_back(trade);
                lastTradeCentsBySymbol_[trade.symbol] = trade.priceCents;

                std::int64_t tradeNumber = 0;
                if (TryParsePrefixedNumber(trade.tradeId, "TRD", tradeNumber))
                {
                    nextTradeNumber_ = std::max(nextTradeNumber_, tradeNumber + 1);
                }
            }
        }

        for (auto& pair : accounts_)
        {
            InitializeAccountPositions(pair.second);
        }

        for (auto& pair : buyOrdersBySymbol_)
        {
            SortBuyOrders(pair.second);
        }

        for (auto& pair : sellOrdersBySymbol_)
        {
            SortSellOrders(pair.second);
        }
    }

    void TradingEngine::SaveStateLocked() const
    {
        namespace fs = std::filesystem;

        const std::string tempPath = stateFilePath_ + ".tmp";
        std::ofstream output(tempPath, std::ios::trunc);
        if (!output.is_open())
        {
            return;
        }

        output << "META|" << nextOrderNumber_ << '|' << nextTradeNumber_ << '\n';
        for (const char* symbol : kSupportedSymbols)
        {
            output << "MARKET|" << symbol << '|' << LastTradeCents(symbol) << '\n';
        }

        std::vector<std::string> accountKeys;
        accountKeys.reserve(accounts_.size());
        for (const auto& pair : accounts_)
        {
            accountKeys.push_back(pair.first);
        }
        std::sort(accountKeys.begin(), accountKeys.end());

        for (const auto& key : accountKeys)
        {
            const AccountState& account = accounts_.at(key);
            output
                << "ACCOUNT|" << SanitizeToken(account.username)
                << '|' << account.cashCents
                << '|' << account.reservedCashCents
                << '\n';

            for (const char* symbol : kSupportedSymbols)
            {
                output
                    << "POSITION|" << SanitizeToken(account.username)
                    << '|' << symbol
                    << '|' << HoldingsForSymbol(account, symbol)
                    << '|' << ReservedHoldingsForSymbol(account, symbol)
                    << '\n';
            }
        }

        for (const auto& order : orderHistory_)
        {
            output
                << "ORDER|" << SanitizeToken(order->orderId)
                << '|' << SanitizeToken(order->username)
                << '|' << order->symbol
                << '|' << SideToString(order->side)
                << '|' << order->originalQuantity
                << '|' << order->remainingQuantity
                << '|' << order->priceCents
                << '|' << StatusToString(order->status)
                << '|' << order->sequence
                << '|' << SanitizeToken(order->createdUtc)
                << '|' << SanitizeToken(order->updatedUtc)
                << '\n';
        }

        for (const auto& trade : tradeHistory_)
        {
            output
                << "TRADE|" << SanitizeToken(trade.tradeId)
                << '|' << trade.symbol
                << '|' << SanitizeToken(trade.buyUser)
                << '|' << SanitizeToken(trade.sellUser)
                << '|' << trade.quantity
                << '|' << trade.priceCents
                << '|' << SanitizeToken(trade.executedUtc)
                << '\n';
        }

        output.close();
        if (!output)
        {
            std::error_code cleanupError;
            fs::remove(tempPath, cleanupError);
            return;
        }

        std::error_code renameError;
        fs::remove(stateFilePath_, renameError);
        renameError.clear();
        fs::rename(tempPath, stateFilePath_, renameError);
        if (renameError)
        {
            std::error_code cleanupError;
            fs::remove(tempPath, cleanupError);
        }
    }
}
