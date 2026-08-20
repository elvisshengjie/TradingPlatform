#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file TradingEngine.hpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 multi-symbol trading engine declarations with persistent account state.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Protocol.hpp"

namespace trading::server
{
    class TradingEngine
    {
    public:
        TradingEngine();

        struct LoginResult
        {
            bool success{ false };
            std::string errorMessage;
            std::string username;
            std::string message;

            static LoginResult Failure(const std::string& error);
        };

        struct OperationResult
        {
            bool success{ false };
            bool stateChanged{ false };
            std::string errorMessage;
            std::vector<std::string> broadcastMessages;
            std::map<std::string, std::vector<std::string>> privateMessages;

            static OperationResult Failure(const std::string& error);
            static OperationResult Success(bool stateChangedValue);
            void AddBroadcast(const std::string& message);
            void AddPrivate(const std::string& username, const std::string& message);
        };

        LoginResult Login(const std::string& username);
        OperationResult PlaceOrder(
            const std::string& username,
            const std::string& symbol,
            const std::string& sideText,
            int quantity,
            int priceCents);
        OperationResult CancelOrder(const std::string& username, const std::string& orderId);
        trading::SnapshotData BuildSnapshot(const std::string& username, const std::string& focusSymbol) const;

    private:
        enum class Side
        {
            Buy,
            Sell
        };

        enum class OrderStatus
        {
            Open,
            PartiallyFilled,
            Filled,
            Cancelled
        };

        struct AccountState
        {
            std::string username;
            std::int64_t cashCents{ 0 };
            std::int64_t reservedCashCents{ 0 };
            std::unordered_map<std::string, int> holdingsBySymbol;
            std::unordered_map<std::string, int> reservedHoldingsBySymbol;
        };

        struct OrderRecord
        {
            std::string orderId;
            std::string username;
            std::string symbol;
            Side side{ Side::Buy };
            int originalQuantity{ 0 };
            int remainingQuantity{ 0 };
            int priceCents{ 0 };
            OrderStatus status{ OrderStatus::Open };
            std::int64_t sequence{ 0 };
            std::string createdUtc;
            std::string updatedUtc;
        };

        struct TradeRecord
        {
            std::string tradeId;
            std::string symbol;
            std::string buyUser;
            std::string sellUser;
            int quantity{ 0 };
            int priceCents{ 0 };
            std::string executedUtc;
        };

        mutable std::mutex mutex_;
        std::unordered_map<std::string, AccountState> accounts_;
        std::unordered_map<std::string, std::vector<std::shared_ptr<OrderRecord>>> buyOrdersBySymbol_;
        std::unordered_map<std::string, std::vector<std::shared_ptr<OrderRecord>>> sellOrdersBySymbol_;
        std::vector<std::shared_ptr<OrderRecord>> orderHistory_;
        std::vector<TradeRecord> tradeHistory_;
        std::unordered_map<std::string, int> lastTradeCentsBySymbol_;
        std::int64_t nextOrderNumber_{ 1 };
        std::int64_t nextTradeNumber_{ 1 };
        std::string stateFilePath_{ "trading_state.txt" };

        static std::string KeyFor(const std::string& username);
        static bool TryParseSide(const std::string& value, Side& side);
        static bool TryParseStatus(const std::string& value, OrderStatus& status);
        static std::string SideToString(Side side);
        static std::string StatusToString(OrderStatus status);
        static std::int64_t AvailableCash(const AccountState& account);
        static int HoldingsForSymbol(const AccountState& account, const std::string& symbol);
        static int ReservedHoldingsForSymbol(const AccountState& account, const std::string& symbol);
        static int AvailableHoldings(const AccountState& account, const std::string& symbol);
        static int StartingLastTradeCents(const std::string& symbol);
        static int SystemBidCents(int lastTradeCents);
        static int SystemAskCents(int lastTradeCents);
        static std::string FormatOrderId(std::int64_t number);
        static std::string FormatTradeId(std::int64_t number);
        static void SortBuyOrders(std::vector<std::shared_ptr<OrderRecord>>& orders);
        static void SortSellOrders(std::vector<std::shared_ptr<OrderRecord>>& orders);

        void EnsureMarketsInitializedLocked();
        void InitializeAccountPositions(AccountState& account) const;
        int LastTradeCents(const std::string& symbol) const;
        void MatchBuyOrder(
            const std::string& symbol,
            const std::shared_ptr<OrderRecord>& incomingBuy,
            AccountState& buyerAccount,
            OperationResult& result);
        void MatchSellOrder(
            const std::string& symbol,
            const std::shared_ptr<OrderRecord>& incomingSell,
            AccountState& sellerAccount,
            OperationResult& result);
        void ExecuteAgainstRestingSell(
            const std::string& symbol,
            const std::shared_ptr<OrderRecord>& incomingBuy,
            AccountState& buyerAccount,
            const std::shared_ptr<OrderRecord>& restingSell,
            OperationResult& result);
        void ExecuteAgainstRestingBuy(
            const std::string& symbol,
            const std::shared_ptr<OrderRecord>& incomingSell,
            AccountState& sellerAccount,
            const std::shared_ptr<OrderRecord>& restingBuy,
            OperationResult& result);
        void ExecuteAgainstSystemAsk(
            const std::string& symbol,
            const std::shared_ptr<OrderRecord>& incomingBuy,
            AccountState& buyerAccount,
            int systemAskCents,
            OperationResult& result);
        void ExecuteAgainstSystemBid(
            const std::string& symbol,
            const std::shared_ptr<OrderRecord>& incomingSell,
            AccountState& sellerAccount,
            int systemBidCents,
            OperationResult& result);
        void RecordTrade(
            const std::string& symbol,
            const std::string& buyUser,
            const std::string& sellUser,
            int quantity,
            int priceCents,
            OperationResult& result);
        void AddToBook(const std::shared_ptr<OrderRecord>& order);
        static void ApplyBuyerFill(AccountState& account, int reservedPriceCents, int quantity, int executionPriceCents);
        static void ApplySellerFill(AccountState& account, const std::string& symbol, int quantity, int executionPriceCents);
        static void UpdateOrderStatus(OrderRecord& order);
        void LoadState();
        void SaveStateLocked() const;
    };
}
