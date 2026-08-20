(function () {
    "use strict";

    const STORAGE_KEY = "assignment5TradingWebDemo.v2";
    const SESSION_STORAGE_KEY = "assignment5TradingWebDemo.session.v1";
    const STARTING_CASH_CENTS = 1000000;
    const STARTING_HOLDINGS = 50;
    const SYSTEM_LIQUIDITY = 1000;
    const SPREAD_CENTS = 100;
    const MAX_HISTORY_POINTS = 24;
    const MAX_RECENT_TRADES = 30;
    const MAX_ACTIVITY_ITEMS = 18;

    const SYMBOLS = [
        { key: "ACME", name: "Acme Motion", basePriceCents: 10000 },
        { key: "BETA", name: "Beta Forge", basePriceCents: 7500 },
        { key: "GAMMA", name: "Gamma Grid", basePriceCents: 12500 },
    ];

    const BASELINE_EQUITY_CENTS = STARTING_CASH_CENTS
        + STARTING_HOLDINGS * SYMBOLS.reduce((total, symbol) => total + symbol.basePriceCents, 0);

    const dom = {};
    let state = loadState();
    let session = loadSession();

    document.addEventListener("DOMContentLoaded", init);

    function init() {
        cacheDom();
        bindEvents();
        populateSymbolSelect();

        reconcileSession();

        if (currentUserKey() && state.accounts[currentUserKey()]) {
            showDashboard();
        } else {
            showLogin();
        }

        render();
        warnIfFileProtocol();
    }

    function cacheDom() {
        [
            "loginView",
            "dashboardView",
            "loginForm",
            "loginName",
            "logoutBtn",
            "resetDemoBtn",
            "tickerTrack",
            "currentUserName",
            "heroEquity",
            "heroOpenOrders",
            "lastUpdatedText",
            "connectedUsersText",
            "symbolGrid",
            "selectedSymbolLabel",
            "chartTitle",
            "chartLastPrice",
            "chartSpread",
            "chartTrades",
            "priceChart",
            "timeStrip",
            "orderForm",
            "orderSymbol",
            "orderSide",
            "orderQuantity",
            "orderPrice",
            "estimatedOrderValue",
            "availableBalanceText",
            "portfolioReturnText",
            "portfolioCash",
            "portfolioReservedCash",
            "portfolioAvailableCash",
            "portfolioEquity",
            "portfolioTableBody",
            "buyBookLevels",
            "sellBookLevels",
            "orderBookSymbolText",
            "ordersMetaText",
            "ordersTableBody",
            "tradeCountText",
            "tradesTableBody",
            "activityMetaText",
            "activityList",
            "toastLayer",
        ].forEach((id) => {
            dom[id] = document.getElementById(id);
        });
    }

    function bindEvents() {
        dom.loginForm.addEventListener("submit", handleLogin);
        dom.logoutBtn.addEventListener("click", handleLogout);
        dom.resetDemoBtn.addEventListener("click", handleResetDemo);
        dom.orderForm.addEventListener("submit", handleOrderSubmit);
        dom.orderSymbol.addEventListener("change", handleSymbolSelectChange);
        dom.orderSide.addEventListener("change", renderOrderEstimate);
        dom.orderQuantity.addEventListener("input", renderOrderEstimate);
        dom.orderPrice.addEventListener("input", renderOrderEstimate);
        window.addEventListener("storage", handleStorageSync);
        window.addEventListener("focus", handleVisibilitySync);
        document.addEventListener("visibilitychange", handleVisibilitySync);

        document.querySelectorAll("[data-demo-user]").forEach((button) => {
            button.addEventListener("click", () => {
                dom.loginName.value = button.getAttribute("data-demo-user") || "";
                dom.loginForm.requestSubmit();
            });
        });

        document.querySelectorAll("[data-preset]").forEach((button) => {
            button.addEventListener("click", () => applyPreset(button.getAttribute("data-preset") || ""));
        });

        dom.symbolGrid.addEventListener("click", (event) => {
            const button = event.target.closest("[data-symbol]");
            if (button) {
                switchSymbol(button.getAttribute("data-symbol") || "ACME", true);
            }
        });

        dom.ordersTableBody.addEventListener("click", (event) => {
            const button = event.target.closest("[data-cancel-order]");
            if (!button) {
                return;
            }

            const orderId = button.getAttribute("data-cancel-order");
            if (!orderId) {
                return;
            }

            try {
                refreshSharedState();
                cancelOrder(state, orderId, currentUserKey());
                saveState();
                render();
                toast("Order cancelled", orderId + " has been removed from the book.", "success");
            } catch (error) {
                toast("Cancellation failed", error.message, "error");
            }
        });
    }

    function populateSymbolSelect() {
        dom.orderSymbol.innerHTML = SYMBOLS
            .map((symbol) => `<option value="${symbol.key}">${symbol.key} · ${symbol.name}</option>`)
            .join("");
    }

    function handleLogin(event) {
        event.preventDefault();
        refreshSharedState();

        const username = sanitizeUsername(dom.loginName.value);
        if (!username) {
            toast("Username required", "Enter a username to continue.", "error");
            return;
        }

        const result = ensureAccount(state, username);
        session.currentUser = result.key;
        saveSession();
        state.metrics.logins += 1;
        markUpdated(state);

        addActivity(
            state,
            `${result.account.username} signed in`,
            result.created
                ? "New demo account created with starting cash and holdings."
                : "Existing account restored from local browser storage.",
            "success");

        saveState();
        showDashboard();
        render();

        toast(
            result.created ? "Account created" : "Welcome back",
            result.created
                ? `${result.account.username} is ready to trade.`
                : `${result.account.username}'s dashboard has been restored.`,
            "success");
    }

    function handleLogout() {
        session.currentUser = "";
        saveSession();
        showLogin();
        render();
    }

    function handleResetDemo() {
        if (!window.confirm("Reset the browser demo data and return to the login page?")) {
            return;
        }

        window.localStorage.removeItem(STORAGE_KEY);
        window.sessionStorage.removeItem(SESSION_STORAGE_KEY);
        state = createInitialState();
        session = loadSession();
        saveState();
        showLogin();
        render();
        toast("Demo reset", "Mercury Lane Exchange has been restored to its showcase state.", "success");
    }

    function handleOrderSubmit(event) {
        event.preventDefault();
        refreshSharedState();

        if (!currentUserKey()) {
            toast("Login required", "Sign in before placing an order.", "error");
            return;
        }

        const symbol = dom.orderSymbol.value;
        const side = dom.orderSide.value;
        const quantity = Number.parseInt(dom.orderQuantity.value, 10);
        const priceNumber = Number.parseFloat(dom.orderPrice.value);
        const priceCents = Number.isFinite(priceNumber) ? Math.round(priceNumber * 100) : 0;

        try {
            const order = placeOrder(state, currentUserKey(), symbol, side, quantity, priceCents);
            saveState();
            render();
            toast(
                "Order submitted",
                `${order.id} ${side.toLowerCase()} ${quantity} ${symbol} @ ${formatMoney(priceCents)}.`,
                "success");
        } catch (error) {
            toast("Order rejected", error.message, "error");
        }
    }

    function handleSymbolSelectChange() {
        switchSymbol(dom.orderSymbol.value, false);
    }

    function showLogin() {
        dom.loginView.classList.remove("hidden");
        dom.dashboardView.classList.add("hidden");
        dom.loginName.value = currentUserKey() && state.accounts[currentUserKey()]
            ? state.accounts[currentUserKey()].username
            : "";
    }

    function showDashboard() {
        dom.loginView.classList.add("hidden");
        dom.dashboardView.classList.remove("hidden");
    }

    function render() {
        reconcileSession();

        renderHeader();
        renderTicker();
        renderSymbolGrid();
        renderChart();
        renderPortfolio();
        renderOrderForm();
        renderOrderBook();
        renderOrders();
        renderTrades();
        renderActivity();
    }

    function currentUserKey() {
        return session.currentUser || "";
    }

    function focusSymbol() {
        return session.focusSymbol || SYMBOLS[0].key;
    }

    function getCurrentAccount() {
        const key = currentUserKey();
        return key ? state.accounts[key] : null;
    }

    function renderHeader() {
        const account = getCurrentAccount();
        const openOrders = account
            ? getUserOrders(currentUserKey()).filter((order) => isOpenStatus(order.status)).length
            : 0;
        const equity = account ? computeTotalEquityCents(account) : 0;

        dom.currentUserName.textContent = account ? account.username : "Guest";
        dom.heroEquity.textContent = formatMoney(equity);
        dom.heroOpenOrders.textContent = String(openOrders);
        dom.lastUpdatedText.textContent = formatClockLabel(state.lastUpdatedAt || new Date().toISOString());
        dom.connectedUsersText.textContent = `${Object.keys(state.accounts).length} traders tracked`;
    }

    function renderTicker() {
        dom.tickerTrack.innerHTML = SYMBOLS.map((symbol) => {
            const market = state.markets[symbol.key];
            const changeCents = getMarketChangeCents(symbol.key);
            const bid = getSystemBidCents(symbol.key);
            const ask = getSystemAskCents(symbol.key);

            return `
                <article class="ticker-item">
                    <div>
                        <p class="eyebrow">${symbol.key}</p>
                        <strong>${formatMoney(market.lastTradeCents)}</strong>
                    </div>
                    <div>
                        <div class="${changeCents >= 0 ? "positive" : "negative"}">
                            ${formatSignedMoney(changeCents)}
                        </div>
                        <small class="muted">Bid ${formatMoney(bid)} · Ask ${formatMoney(ask)}</small>
                    </div>
                </article>`;
        }).join("");
    }

    function renderSymbolGrid() {
        dom.selectedSymbolLabel.textContent = `${focusSymbol()} selected`;

        dom.symbolGrid.innerHTML = SYMBOLS.map((symbol) => {
            const market = state.markets[symbol.key];
            const changeCents = getMarketChangeCents(symbol.key);
            const bestBid = getBestBid(symbol.key);
            const bestAsk = getBestAsk(symbol.key);

            return `
                <button class="symbol-card ${symbol.key === focusSymbol() ? "active" : ""}" type="button" data-symbol="${symbol.key}">
                    <div class="symbol-card-top">
                        <div>
                            <p class="eyebrow">${symbol.key}</p>
                            <strong>${formatMoney(market.lastTradeCents)}</strong>
                        </div>
                        <span class="${changeCents >= 0 ? "positive" : "negative"}">${formatSignedMoney(changeCents)}</span>
                    </div>
                    <div class="symbol-card-bottom">
                        <span>${symbol.name}</span>
                        <span>Bid ${formatMoney(bestBid)} · Ask ${formatMoney(bestAsk)}</span>
                    </div>
                </button>`;
        }).join("");
    }

    function renderChart() {
        const symbol = focusSymbol();
        const market = state.markets[symbol];
        const history = market.history;
        const width = 860;
        const height = 340;
        const padding = { top: 24, right: 26, bottom: 44, left: 44 };
        const min = Math.min(...history.map((point) => point.priceCents));
        const max = Math.max(...history.map((point) => point.priceCents));
        const range = Math.max(1, max - min);
        const usableWidth = width - padding.left - padding.right;
        const usableHeight = height - padding.top - padding.bottom;

        const points = history.map((point, index) => {
            const x = padding.left + (usableWidth * index) / Math.max(1, history.length - 1);
            const y = padding.top + usableHeight - ((point.priceCents - min) / range) * usableHeight;
            return { x, y, label: point.label };
        });

        const path = points.map((point, index) => `${index === 0 ? "M" : "L"} ${point.x.toFixed(2)} ${point.y.toFixed(2)}`).join(" ");
        const area = `${path} L ${points[points.length - 1].x.toFixed(2)} ${(height - padding.bottom).toFixed(2)} L ${points[0].x.toFixed(2)} ${(height - padding.bottom).toFixed(2)} Z`;

        const gridLines = [];
        for (let index = 0; index <= 4; index += 1) {
            const ratio = index / 4;
            const y = padding.top + usableHeight * ratio;
            const price = max - range * ratio;
            gridLines.push(`<line class="chart-grid-line" x1="${padding.left}" y1="${y}" x2="${width - padding.right}" y2="${y}"></line>`);
            gridLines.push(`<text class="chart-axis-label" x="4" y="${y + 4}">${formatCompactMoney(price)}</text>`);
        }

        const xLabels = points
            .filter((_, index) => index % Math.max(1, Math.floor(points.length / 5)) === 0 || index === points.length - 1)
            .map((point) => `<text class="chart-axis-label" x="${point.x}" y="${height - 10}" text-anchor="middle">${point.label}</text>`)
            .join("");

        const lastPoint = points[points.length - 1];

        dom.chartTitle.textContent = `${symbol} price arc`;
        dom.chartLastPrice.textContent = formatMoney(market.lastTradeCents);
        dom.chartSpread.textContent = formatMoney(getBestAsk(symbol) - getBestBid(symbol));
        dom.chartTrades.textContent = String(getTradesForSymbol(symbol).length);

        dom.priceChart.innerHTML = `
            <defs>
                <linearGradient id="priceFill" x1="0" y1="0" x2="0" y2="1">
                    <stop offset="0%" stop-color="rgba(121, 212, 243, 0.42)"></stop>
                    <stop offset="100%" stop-color="rgba(121, 212, 243, 0.02)"></stop>
                </linearGradient>
            </defs>
            ${gridLines.join("")}
            <path class="chart-area" d="${area}"></path>
            <path class="chart-line" d="${path}"></path>
            <circle class="chart-marker" cx="${lastPoint.x}" cy="${lastPoint.y}" r="6"></circle>
            ${xLabels}`;

        dom.timeStrip.innerHTML = history
            .slice(-6)
            .map((point) => `<span>${point.label}</span>`)
            .join("");
    }

    function renderPortfolio() {
        const account = getCurrentAccount();
        if (!account) {
            dom.portfolioCash.textContent = formatMoney(0);
            dom.portfolioReservedCash.textContent = formatMoney(0);
            dom.portfolioAvailableCash.textContent = formatMoney(0);
            dom.portfolioEquity.textContent = formatMoney(0);
            dom.portfolioReturnText.textContent = "0.00%";
            dom.portfolioTableBody.innerHTML = renderEmptyTableRow(5, "Login to see account details.");
            return;
        }

        const equityCents = computeTotalEquityCents(account);
        const returnPct = ((equityCents - BASELINE_EQUITY_CENTS) / BASELINE_EQUITY_CENTS) * 100;

        dom.portfolioCash.textContent = formatMoney(account.cashCents);
        dom.portfolioReservedCash.textContent = formatMoney(account.reservedCashCents);
        dom.portfolioAvailableCash.textContent = formatMoney(availableCash(account));
        dom.portfolioEquity.textContent = formatMoney(equityCents);
        dom.portfolioReturnText.textContent = `${returnPct >= 0 ? "+" : ""}${returnPct.toFixed(2)}%`;
        dom.portfolioReturnText.className = returnPct >= 0 ? "positive" : "negative";

        dom.portfolioTableBody.innerHTML = SYMBOLS.map((symbol) => {
            const held = holdingsForSymbol(account, symbol.key);
            const reserved = reservedHoldingsForSymbol(account, symbol.key);
            const available = availableHoldings(account, symbol.key);
            const marketValue = held * state.markets[symbol.key].lastTradeCents;

            return `
                <tr>
                    <td>${symbol.key}</td>
                    <td>${held}</td>
                    <td>${reserved}</td>
                    <td>${available}</td>
                    <td>${formatMoney(marketValue)}</td>
                </tr>`;
        }).join("");
    }

    function renderOrderForm() {
        const symbol = focusSymbol();
        dom.orderSymbol.value = symbol;

        if (!dom.orderPrice.value || Number.parseFloat(dom.orderPrice.value) <= 0) {
            dom.orderPrice.value = (getBestAsk(symbol) / 100).toFixed(2);
        }

        renderOrderEstimate();
    }

    function renderOrderEstimate() {
        const account = getCurrentAccount();
        const symbol = dom.orderSymbol.value || focusSymbol();
        const side = dom.orderSide.value;
        const quantity = Number.parseInt(dom.orderQuantity.value, 10) || 0;
        const price = Number.parseFloat(dom.orderPrice.value) || 0;
        const priceCents = Math.round(price * 100);

        dom.estimatedOrderValue.textContent = formatMoney(quantity * priceCents);

        if (!account) {
            dom.availableBalanceText.textContent = "Login required";
            return;
        }

        dom.availableBalanceText.textContent = side === "BUY"
            ? formatMoney(availableCash(account))
            : `${availableHoldings(account, symbol)} shares`;
    }

    function renderOrderBook() {
        const symbol = focusSymbol();
        const bidLevels = buildLevels(symbol, "BUY");
        const askLevels = buildLevels(symbol, "SELL");
        const topQuantity = Math.max(1, ...bidLevels.concat(askLevels).map((level) => level.quantity));

        dom.orderBookSymbolText.textContent = symbol;
        dom.buyBookLevels.innerHTML = renderBookLevels(bidLevels, topQuantity, "buy");
        dom.sellBookLevels.innerHTML = renderBookLevels(askLevels, topQuantity, "sell");
    }

    function renderOrders() {
        if (!currentUserKey()) {
            dom.ordersMetaText.textContent = "0 open / 0 recent";
            dom.ordersTableBody.innerHTML = renderEmptyTableRow(7, "Login to manage orders.");
            return;
        }

        const orders = getUserOrders(currentUserKey());
        const openCount = orders.filter((order) => isOpenStatus(order.status)).length;
        const recentOrders = orders.slice(-10).reverse();

        dom.ordersMetaText.textContent = `${openCount} open / ${recentOrders.length} recent`;

        if (recentOrders.length === 0) {
            dom.ordersTableBody.innerHTML = renderEmptyTableRow(7, "No orders yet. Submit one from the order panel.");
            return;
        }

        dom.ordersTableBody.innerHTML = recentOrders.map((order) => `
            <tr>
                <td>${order.id}</td>
                <td>${order.symbol}</td>
                <td class="${order.side === "BUY" ? "positive" : "negative"}">${order.side}</td>
                <td>${formatMoney(order.priceCents)}</td>
                <td>${order.remainingQuantity}/${order.originalQuantity}</td>
                <td>${order.status}</td>
                <td>
                    ${isOpenStatus(order.status)
                        ? `<button type="button" class="order-action cancel" data-cancel-order="${order.id}">Cancel</button>`
                        : `<span class="muted">-</span>`}
                </td>
            </tr>`).join("");
    }

    function renderTrades() {
        const symbol = focusSymbol();
        const trades = getTradesForSymbol(symbol).slice(-10).reverse();

        dom.tradeCountText.textContent = `${getTradesForSymbol(symbol).length} executions`;

        if (trades.length === 0) {
            dom.tradesTableBody.innerHTML = renderEmptyTableRow(6, "No trades yet for this symbol.");
            return;
        }

        dom.tradesTableBody.innerHTML = trades.map((trade) => `
            <tr>
                <td>${trade.id}</td>
                <td>${trade.symbol}</td>
                <td>${formatMoney(trade.priceCents)}</td>
                <td>${trade.quantity}</td>
                <td>${trade.buyUser}</td>
                <td>${trade.sellUser}</td>
            </tr>`).join("");
    }

    function renderActivity() {
        const items = state.activity.slice(-10).reverse();
        dom.activityMetaText.textContent = `${items.length} latest events`;

        if (items.length === 0) {
            dom.activityList.innerHTML = `<li class="activity-item"><strong>No activity yet</strong><p>Login and place an order to start the timeline.</p></li>`;
            return;
        }

        dom.activityList.innerHTML = items.map((item) => `
            <li class="activity-item">
                <time>${item.time}</time>
                <strong>${item.title}</strong>
                <p>${item.body}</p>
            </li>`).join("");
    }

    function switchSymbol(symbol, syncOrderPrice) {
        if (!state.markets[symbol]) {
            return;
        }

        session.focusSymbol = symbol;
        saveSession();
        if (syncOrderPrice) {
            dom.orderSymbol.value = symbol;
            dom.orderPrice.value = (getBestAsk(symbol) / 100).toFixed(2);
        }

        render();
    }

    function applyPreset(preset) {
        const symbol = dom.orderSymbol.value || focusSymbol();
        const bid = getBestBid(symbol);
        const ask = getBestAsk(symbol);

        if (preset === "maker-buy") {
            dom.orderSide.value = "BUY";
            dom.orderPrice.value = (bid / 100).toFixed(2);
        } else if (preset === "maker-sell") {
            dom.orderSide.value = "SELL";
            dom.orderPrice.value = (ask / 100).toFixed(2);
        } else if (preset === "cross-spread") {
            dom.orderPrice.value = ((dom.orderSide.value === "BUY" ? ask : bid) / 100).toFixed(2);
        }

        renderOrderEstimate();
    }

    function loadState() {
        try {
            const raw = window.localStorage.getItem(STORAGE_KEY);
            if (!raw) {
                return createInitialState();
            }

            const parsed = JSON.parse(raw);
            if (!parsed || (parsed.version !== 1 && parsed.version !== 2)) {
                return createInitialState();
            }

            return normalizeState(parsed);
        } catch (_) {
            return createInitialState();
        }
    }

    function saveState() {
        window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    }

    function loadSession() {
        try {
            const raw = window.sessionStorage.getItem(SESSION_STORAGE_KEY);
            if (!raw) {
                return createInitialSession();
            }

            const parsed = JSON.parse(raw);
            if (!parsed || typeof parsed !== "object") {
                return createInitialSession();
            }

            return {
                currentUser: typeof parsed.currentUser === "string" ? parsed.currentUser : "",
                focusSymbol: typeof parsed.focusSymbol === "string" ? parsed.focusSymbol : SYMBOLS[0].key,
            };
        } catch (_) {
            return createInitialSession();
        }
    }

    function saveSession() {
        window.sessionStorage.setItem(SESSION_STORAGE_KEY, JSON.stringify(session));
    }

    function createInitialSession() {
        return {
            currentUser: "",
            focusSymbol: SYMBOLS[0].key,
        };
    }

    function normalizeState(parsed) {
        const normalized = {
            ...parsed,
            version: 2,
        };

        delete normalized.currentUser;
        delete normalized.focusSymbol;
        return normalized;
    }

    function reconcileSession() {
        let changed = false;

        if (!session.focusSymbol || !state.markets[session.focusSymbol]) {
            session.focusSymbol = SYMBOLS[0].key;
            changed = true;
        }

        if (session.currentUser && !state.accounts[session.currentUser]) {
            session.currentUser = "";
            changed = true;
        }

        if (changed) {
            saveSession();
        }
    }

    function refreshSharedState() {
        state = loadState();
        reconcileSession();
    }

    function handleStorageSync(event) {
        if (event.key && event.key !== STORAGE_KEY) {
            return;
        }

        refreshSharedState();
        render();
    }

    function handleVisibilitySync() {
        if (document.visibilityState && document.visibilityState !== "visible") {
            return;
        }

        refreshSharedState();
        render();
    }

    function warnIfFileProtocol() {
        if (window.location.protocol !== "file:") {
            return;
        }

        toast(
            "Local file mode detected",
            "For alice/bob matching across tabs, run this demo on http://localhost. Opening index.html directly can isolate storage per tab.",
            "info");
    }

    function createInitialState() {
        const targetState = {
            version: 2,
            orderSequence: 1,
            tradeSequence: 1,
            activitySequence: 1,
            lastUpdatedAt: new Date().toISOString(),
            metrics: {
                logins: 0,
                ordersPlaced: 0,
                tradesExecuted: 0,
            },
            accounts: {},
            markets: {},
            orders: [],
            trades: [],
            activity: [],
        };

        SYMBOLS.forEach((symbol, index) => {
            targetState.markets[symbol.key] = {
                lastTradeCents: symbol.basePriceCents,
                history: generateHistory(symbol.basePriceCents, index + 1),
            };
        });

        targetState.metrics.tradesExecuted = targetState.trades.length;
        targetState.metrics.ordersPlaced = targetState.orders.length;
        targetState.activity = [];

        addActivity(
            targetState,
            "Mercury Lane Exchange ready",
            "Manual trading mode is active. Your logged-in account trades directly against neutral market liquidity.",
            "success");
        addActivity(
            targetState,
            "Manual account control",
            "Login with any username, then place your own buys and sells without random seeded accounts taking the other side.",
            "info");

        return targetState;
    }

    function ensureAccount(targetState, username) {
        const clean = sanitizeUsername(username);
        const key = makeKey(clean);
        if (targetState.accounts[key]) {
            return { account: targetState.accounts[key], key, created: false };
        }

        const holdingsBySymbol = {};
        const reservedHoldingsBySymbol = {};
        SYMBOLS.forEach((symbol) => {
            holdingsBySymbol[symbol.key] = STARTING_HOLDINGS;
            reservedHoldingsBySymbol[symbol.key] = 0;
        });

        targetState.accounts[key] = {
            username: clean,
            cashCents: STARTING_CASH_CENTS,
            reservedCashCents: 0,
            holdingsBySymbol,
            reservedHoldingsBySymbol,
        };

        return { account: targetState.accounts[key], key, created: true };
    }

    function placeOrder(targetState, userKey, symbol, side, quantity, priceCents) {
        if (!SYMBOLS.some((entry) => entry.key === symbol)) {
            throw new Error("Unsupported symbol. Use ACME, BETA, or GAMMA.");
        }

        if (!Number.isInteger(quantity) || quantity <= 0) {
            throw new Error("Quantity must be a whole number greater than zero.");
        }

        if (!Number.isInteger(priceCents) || priceCents <= 0) {
            throw new Error("Price must be greater than zero.");
        }

        const account = targetState.accounts[userKey];
        if (!account) {
            throw new Error("Please login before placing orders.");
        }

        if (side === "BUY") {
            const requiredCash = quantity * priceCents;
            if (availableCash(account) < requiredCash) {
                throw new Error(`Insufficient cash. Need ${formatMoney(requiredCash)} available.`);
            }

            account.reservedCashCents += requiredCash;
        } else if (side === "SELL") {
            if (availableHoldings(account, symbol) < quantity) {
                throw new Error(`Insufficient ${symbol} holdings. Available: ${availableHoldings(account, symbol)} shares.`);
            }

            account.reservedHoldingsBySymbol[symbol] += quantity;
        } else {
            throw new Error("Order side must be BUY or SELL.");
        }

        const order = createOrder(targetState, account.username, symbol, side, quantity, priceCents);
        targetState.orders.push(order);
        targetState.metrics.ordersPlaced += 1;

        if (side === "BUY") {
            matchBuyOrder(targetState, order, account);
        } else {
            matchSellOrder(targetState, order, account);
        }

        updateOrderStatus(order);
        markUpdated(targetState);

        addActivity(
            targetState,
            `${account.username} placed ${side}`,
            `${quantity} ${symbol} @ ${formatMoney(priceCents)}${order.remainingQuantity > 0 ? " is resting on the book." : " was filled immediately."}`,
            side === "BUY" ? "success" : "info");

        return order;
    }

    function cancelOrder(targetState, orderId, userKey) {
        const order = targetState.orders.find((candidate) => candidate.id === orderId);
        if (!order || !isOpenStatus(order.status)) {
            throw new Error("Open order not found.");
        }

        if (makeKey(order.username) !== userKey) {
            throw new Error("You can only cancel your own orders.");
        }

        const account = targetState.accounts[userKey];
        if (order.side === "BUY") {
            account.reservedCashCents = Math.max(0, account.reservedCashCents - order.remainingQuantity * order.priceCents);
        } else {
            account.reservedHoldingsBySymbol[order.symbol] = Math.max(
                0,
                account.reservedHoldingsBySymbol[order.symbol] - order.remainingQuantity);
        }

        order.status = "CANCELLED";
        order.updatedAt = formatClockLabel(new Date().toISOString());
        markUpdated(targetState);

        addActivity(
            targetState,
            `${account.username} cancelled ${order.id}`,
            `${order.symbol} ${order.side.toLowerCase()} order removed with ${order.remainingQuantity} shares remaining.`,
            "info");
    }

    function matchBuyOrder(targetState, incomingOrder, buyerAccount) {
        let matchedUserLiquidity = false;

        while (incomingOrder.remainingQuantity > 0) {
            const restingSell = findBestOpposingOrder(targetState, incomingOrder.symbol, "SELL", incomingOrder.username, incomingOrder.priceCents);
            const systemAsk = getSystemAskCents(incomingOrder.symbol);
            const canMatchUser = Boolean(restingSell);
            const canMatchSystem = !matchedUserLiquidity && systemAsk <= incomingOrder.priceCents;

            if (!canMatchUser && !canMatchSystem) {
                break;
            }

            if (canMatchUser && (!canMatchSystem || restingSell.priceCents <= systemAsk)) {
                // Keep multi-user fills on the real book once this order has started matching another trader.
                matchedUserLiquidity = true;
                executeAgainstRestingSell(targetState, incomingOrder, buyerAccount, restingSell);
            } else {
                executeAgainstSystemAsk(targetState, incomingOrder, buyerAccount, systemAsk);
            }
        }
    }

    function matchSellOrder(targetState, incomingOrder, sellerAccount) {
        let matchedUserLiquidity = false;

        while (incomingOrder.remainingQuantity > 0) {
            const restingBuy = findBestOpposingOrder(targetState, incomingOrder.symbol, "BUY", incomingOrder.username, incomingOrder.priceCents);
            const systemBid = getSystemBidCents(incomingOrder.symbol);
            const canMatchUser = Boolean(restingBuy);
            const canMatchSystem = !matchedUserLiquidity && systemBid >= incomingOrder.priceCents;

            if (!canMatchUser && !canMatchSystem) {
                break;
            }

            if (canMatchUser && (!canMatchSystem || restingBuy.priceCents >= systemBid)) {
                // Keep multi-user fills on the real book once this order has started matching another trader.
                matchedUserLiquidity = true;
                executeAgainstRestingBuy(targetState, incomingOrder, sellerAccount, restingBuy);
            } else {
                executeAgainstSystemBid(targetState, incomingOrder, sellerAccount, systemBid);
            }
        }
    }

    function executeAgainstRestingSell(targetState, incomingBuy, buyerAccount, restingSell) {
        const sellerAccount = targetState.accounts[makeKey(restingSell.username)];
        const executedQuantity = Math.min(incomingBuy.remainingQuantity, restingSell.remainingQuantity);
        const executionPriceCents = restingSell.priceCents;

        incomingBuy.remainingQuantity -= executedQuantity;
        incomingBuy.updatedAt = formatClockLabel(new Date().toISOString());
        applyBuyerFill(buyerAccount, incomingBuy.priceCents, executedQuantity, executionPriceCents);
        buyerAccount.holdingsBySymbol[incomingBuy.symbol] += executedQuantity;

        restingSell.remainingQuantity -= executedQuantity;
        restingSell.updatedAt = formatClockLabel(new Date().toISOString());
        applySellerFill(sellerAccount, incomingBuy.symbol, executedQuantity, executionPriceCents);
        updateOrderStatus(restingSell);

        recordTrade(targetState, incomingBuy.symbol, incomingBuy.username, restingSell.username, executedQuantity, executionPriceCents);
    }

    function executeAgainstRestingBuy(targetState, incomingSell, sellerAccount, restingBuy) {
        const buyerAccount = targetState.accounts[makeKey(restingBuy.username)];
        const executedQuantity = Math.min(incomingSell.remainingQuantity, restingBuy.remainingQuantity);
        const executionPriceCents = restingBuy.priceCents;

        incomingSell.remainingQuantity -= executedQuantity;
        incomingSell.updatedAt = formatClockLabel(new Date().toISOString());
        applySellerFill(sellerAccount, incomingSell.symbol, executedQuantity, executionPriceCents);

        restingBuy.remainingQuantity -= executedQuantity;
        restingBuy.updatedAt = formatClockLabel(new Date().toISOString());
        applyBuyerFill(buyerAccount, restingBuy.priceCents, executedQuantity, executionPriceCents);
        buyerAccount.holdingsBySymbol[incomingSell.symbol] += executedQuantity;
        updateOrderStatus(restingBuy);

        recordTrade(targetState, incomingSell.symbol, restingBuy.username, incomingSell.username, executedQuantity, executionPriceCents);
    }

    function executeAgainstSystemAsk(targetState, incomingBuy, buyerAccount, systemAskCents) {
        const executedQuantity = incomingBuy.remainingQuantity;
        incomingBuy.remainingQuantity = 0;
        incomingBuy.updatedAt = formatClockLabel(new Date().toISOString());

        applyBuyerFill(buyerAccount, incomingBuy.priceCents, executedQuantity, systemAskCents);
        buyerAccount.holdingsBySymbol[incomingBuy.symbol] += executedQuantity;

        recordTrade(targetState, incomingBuy.symbol, incomingBuy.username, "MARKET_MAKER", executedQuantity, systemAskCents);
    }

    function executeAgainstSystemBid(targetState, incomingSell, sellerAccount, systemBidCents) {
        const executedQuantity = incomingSell.remainingQuantity;
        incomingSell.remainingQuantity = 0;
        incomingSell.updatedAt = formatClockLabel(new Date().toISOString());

        applySellerFill(sellerAccount, incomingSell.symbol, executedQuantity, systemBidCents);
        recordTrade(targetState, incomingSell.symbol, "MARKET_MAKER", incomingSell.username, executedQuantity, systemBidCents);
    }

    function recordTrade(targetState, symbol, buyUser, sellUser, quantity, priceCents) {
        const trade = {
            id: issueTradeId(targetState),
            symbol,
            buyUser,
            sellUser,
            quantity,
            priceCents,
            time: formatClockLabel(new Date().toISOString()),
        };

        targetState.trades.push(trade);
        if (targetState.trades.length > MAX_RECENT_TRADES) {
            targetState.trades = targetState.trades.slice(-MAX_RECENT_TRADES);
        }

        targetState.markets[symbol].lastTradeCents = priceCents;
        appendHistoryPoint(targetState.markets[symbol], priceCents, trade.time);
        targetState.metrics.tradesExecuted += 1;

        addActivity(
            targetState,
            `${buyUser} bought ${quantity} ${symbol}`,
            `Matched with ${sellUser} at ${formatMoney(priceCents)}.`,
            "success");
    }

    function applyBuyerFill(account, reservedPriceCents, quantity, executionPriceCents) {
        account.reservedCashCents = Math.max(0, account.reservedCashCents - reservedPriceCents * quantity);
        account.cashCents -= executionPriceCents * quantity;
    }

    function applySellerFill(account, symbol, quantity, executionPriceCents) {
        account.reservedHoldingsBySymbol[symbol] = Math.max(0, account.reservedHoldingsBySymbol[symbol] - quantity);
        account.holdingsBySymbol[symbol] -= quantity;
        account.cashCents += executionPriceCents * quantity;
    }

    function updateOrderStatus(order) {
        if (order.status === "CANCELLED") {
            return;
        }

        if (order.remainingQuantity <= 0) {
            order.remainingQuantity = 0;
            order.status = "FILLED";
        } else if (order.remainingQuantity < order.originalQuantity) {
            order.status = "PARTIAL";
        } else {
            order.status = "OPEN";
        }
    }

    function createOrder(targetState, username, symbol, side, quantity, priceCents) {
        const time = formatClockLabel(new Date().toISOString());
        return {
            id: issueOrderId(targetState),
            username,
            symbol,
            side,
            originalQuantity: quantity,
            remainingQuantity: quantity,
            priceCents,
            status: "OPEN",
            sequence: targetState.orderSequence - 1,
            createdAt: time,
            updatedAt: time,
        };
    }

    function issueOrderId(targetState) {
        const value = targetState.orderSequence;
        targetState.orderSequence += 1;
        return `ORD${String(value).padStart(4, "0")}`;
    }

    function issueTradeId(targetState) {
        const value = targetState.tradeSequence;
        targetState.tradeSequence += 1;
        return `TRD${String(value).padStart(4, "0")}`;
    }

    function getUserOrders(userKey) {
        return state.orders
            .filter((order) => makeKey(order.username) === userKey)
            .sort((left, right) => left.sequence - right.sequence);
    }

    function getTradesForSymbol(symbol) {
        return state.trades.filter((trade) => trade.symbol === symbol);
    }

    function buildLevels(symbol, side) {
        const grouped = new Map();
        const openOrders = getOpenOrders(symbol, side);

        openOrders.forEach((order) => {
            grouped.set(order.priceCents, (grouped.get(order.priceCents) || 0) + order.remainingQuantity);
        });

        const systemPrice = side === "BUY" ? getSystemBidCents(symbol) : getSystemAskCents(symbol);
        grouped.set(systemPrice, (grouped.get(systemPrice) || 0) + SYSTEM_LIQUIDITY);

        const levels = Array.from(grouped.entries()).map(([priceCents, quantity]) => ({ priceCents, quantity }));
        levels.sort((left, right) => side === "BUY" ? right.priceCents - left.priceCents : left.priceCents - right.priceCents);
        return levels.slice(0, 5);
    }

    function getOpenOrders(symbol, side) {
        return state.orders
            .filter((order) => order.symbol === symbol && order.side === side && isOpenStatus(order.status))
            .sort((left, right) => {
                if (side === "BUY" && left.priceCents !== right.priceCents) {
                    return right.priceCents - left.priceCents;
                }

                if (side === "SELL" && left.priceCents !== right.priceCents) {
                    return left.priceCents - right.priceCents;
                }

                return left.sequence - right.sequence;
            });
    }

    function findBestOpposingOrder(targetState, symbol, side, ownUsername, limitPriceCents) {
        const ownKey = makeKey(ownUsername);

        const candidates = targetState.orders
            .filter((order) => order.symbol === symbol
                && order.side === side
                && isOpenStatus(order.status)
                && makeKey(order.username) !== ownKey)
            .sort((left, right) => {
                if (side === "SELL" && left.priceCents !== right.priceCents) {
                    return left.priceCents - right.priceCents;
                }

                if (side === "BUY" && left.priceCents !== right.priceCents) {
                    return right.priceCents - left.priceCents;
                }

                return left.sequence - right.sequence;
            });

        return candidates.find((order) => side === "SELL"
            ? order.priceCents <= limitPriceCents
            : order.priceCents >= limitPriceCents) || null;
    }

    function getBestBid(symbol) {
        const userBid = getOpenOrders(symbol, "BUY")[0];
        return Math.max(getSystemBidCents(symbol), userBid ? userBid.priceCents : 0);
    }

    function getBestAsk(symbol) {
        const userAsk = getOpenOrders(symbol, "SELL")[0];
        return Math.min(getSystemAskCents(symbol), userAsk ? userAsk.priceCents : Number.MAX_SAFE_INTEGER);
    }

    function getSystemBidCents(symbol) {
        return Math.max(1, state.markets[symbol].lastTradeCents - SPREAD_CENTS);
    }

    function getSystemAskCents(symbol) {
        return state.markets[symbol].lastTradeCents + SPREAD_CENTS;
    }

    function getMarketChangeCents(symbol) {
        const history = state.markets[symbol].history;
        return history[history.length - 1].priceCents - history[0].priceCents;
    }

    function computeTotalEquityCents(account) {
        const positionsValue = SYMBOLS.reduce(
            (total, symbol) => total + holdingsForSymbol(account, symbol.key) * state.markets[symbol.key].lastTradeCents,
            0);
        return account.cashCents + positionsValue;
    }

    function availableCash(account) {
        return account.cashCents - account.reservedCashCents;
    }

    function holdingsForSymbol(account, symbol) {
        return account.holdingsBySymbol[symbol] || 0;
    }

    function reservedHoldingsForSymbol(account, symbol) {
        return account.reservedHoldingsBySymbol[symbol] || 0;
    }

    function availableHoldings(account, symbol) {
        return holdingsForSymbol(account, symbol) - reservedHoldingsForSymbol(account, symbol);
    }

    function addActivity(targetState, title, body, tone) {
        const item = {
            id: targetState.activitySequence,
            title,
            body,
            tone,
            time: formatClockLabel(new Date().toISOString()),
        };

        targetState.activitySequence += 1;
        targetState.activity.push(item);
        if (targetState.activity.length > MAX_ACTIVITY_ITEMS) {
            targetState.activity = targetState.activity.slice(-MAX_ACTIVITY_ITEMS);
        }
    }

    function markUpdated(targetState) {
        targetState.lastUpdatedAt = new Date().toISOString();
    }

    function appendHistoryPoint(market, priceCents, label) {
        market.history.push({ priceCents, label });
        if (market.history.length > MAX_HISTORY_POINTS) {
            market.history = market.history.slice(-MAX_HISTORY_POINTS);
        }
    }

    function generateHistory(basePriceCents, phase) {
        const points = [];
        for (let index = 0; index < MAX_HISTORY_POINTS; index += 1) {
            const waveA = Math.sin((index + phase) * 0.53) * (basePriceCents * 0.032);
            const waveB = Math.cos((index + phase * 2) * 0.29) * (basePriceCents * 0.016);
            const drift = (index - MAX_HISTORY_POINTS / 2) * (basePriceCents * 0.0011);
            const priceCents = Math.max(100, Math.round(basePriceCents + waveA + waveB + drift));
            points.push({ priceCents, label: syntheticTimeLabel(index) });
        }

        return points;
    }

    function syntheticTimeLabel(index) {
        const hour = 9 + Math.floor(index / 2);
        const minute = (index % 2) * 30;
        return `${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}`;
    }

    function renderBookLevels(levels, topQuantity, sideClass) {
        if (levels.length === 0) {
            return `<div class="book-level ${sideClass}"><span class="book-bar" style="width: 0%"></span><strong>No depth</strong><span></span><small></small></div>`;
        }

        return levels.map((level) => {
            const width = (level.quantity / topQuantity) * 100;
            return `
                <div class="book-level ${sideClass}">
                    <span class="book-bar" style="width: ${width}%"></span>
                    <strong>${formatMoney(level.priceCents)}</strong>
                    <span>${level.quantity} sh</span>
                    <small>${width.toFixed(0)}%</small>
                </div>`;
        }).join("");
    }

    function toast(title, body, tone) {
        const toastElement = document.createElement("div");
        toastElement.className = `toast ${tone}`;
        toastElement.innerHTML = `
            <span class="toast-title">${title}</span>
            <div>${body}</div>`;
        dom.toastLayer.appendChild(toastElement);

        window.setTimeout(() => {
            toastElement.remove();
        }, 3200);
    }

    function isOpenStatus(status) {
        return status === "OPEN" || status === "PARTIAL";
    }

    function sanitizeUsername(value) {
        return value
            .trim()
            .replace(/[^a-zA-Z0-9_-]+/g, " ")
            .trim()
            .replace(/\s+/g, "_")
            .slice(0, 24);
    }

    function makeKey(value) {
        return sanitizeUsername(value).toUpperCase();
    }

    function renderEmptyTableRow(columns, message) {
        return `<tr><td colspan="${columns}" class="muted">${message}</td></tr>`;
    }

    function formatMoney(cents) {
        return new Intl.NumberFormat("en-US", {
            style: "currency",
            currency: "USD",
            minimumFractionDigits: 2,
            maximumFractionDigits: 2,
        }).format((cents || 0) / 100);
    }

    function formatCompactMoney(cents) {
        return `$${((cents || 0) / 100).toFixed(2)}`;
    }

    function formatSignedMoney(cents) {
        const sign = cents >= 0 ? "+" : "-";
        return `${sign}${formatCompactMoney(Math.abs(cents))}`;
    }

    function formatClockLabel(isoString) {
        const date = new Date(isoString);
        return date.toLocaleTimeString("en-US", {
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit",
        }).toLowerCase();
    }
})();
