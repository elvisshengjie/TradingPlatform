# Trading Platform

A Windows C++17 trading simulator built for a networking assignment. Multiple console clients connect to a TCP server to trade fictional shares, view their portfolios, and receive market updates. The repository also includes a browser dashboard for presenting the same trading concepts.

All balances, shares, and trades are simulated. The browser demo runs its own JavaScript trading engine and stores its state in the browser; it does not connect to the C++ TCP server.

## Features

- Username-based accounts with $10,000 in starting cash and 50 shares each of `ACME`, `BETA`, and `GAMMA`.
- Buy and sell limit orders with price/time priority, partial fills, and cancellation.
- Separate order books for each symbol and built-in market maker quotes.
- Cash and share reservations, with insufficient-funds and insufficient-holdings checks.
- Account, open-order, and trade-history views with live TCP updates.
- Server state saved to `trading_state.txt` for reconnects and restarts.
- A concurrent stress client for generating busy network traffic.
- A browser dashboard with order entry, portfolios, order history, and recent trades.

## Requirements

- Windows 10 or 11. The C++ programs use Windows Winsock.
- Visual Studio 2022 with the **Desktop development with C++** workload, **MSVC v143** tools, and a **Windows 10/11 SDK**.
- Git to clone the repository.
- A modern browser for the optional web demo.

No third-party C++ libraries, database, Node.js, or Python installation is required.

## Get the project

Run in PowerShell:

```powershell
git clone https://github.com/elvisshengjie/TradingPlatform.git
cd TradingPlatform
```

For the existing local copy, use:

```powershell
Set-Location 'C:\portfolio\Trading platform'
```

Run the commands below from the repository root unless stated otherwise.

## Build

### Visual Studio 2022

1. Open `TradingPlatformCpp.sln`.
2. Select **Release** and **x64** in the solution toolbar.
3. Choose **Build > Build Solution** (`Ctrl+Shift+B`).

This builds the server, console client, stress client, and web host. Executables are written to:

```text
build\TradingPlatformCpp.Server\Release\TradingPlatformCpp.Server.exe
build\TradingPlatformCpp.Client\Release\TradingPlatformCpp.Client.exe
build\TradingPlatformCpp.Stress\Release\TradingPlatformCpp.Stress.exe
build\TradingPlatformCpp.WebHost\Release\TradingPlatformCpp.WebHost.exe
```

### Command line

Open **Developer PowerShell for VS 2022**, change to the repository root, and run:

```powershell
msbuild .\TradingPlatformCpp.sln /m /p:Configuration=Release /p:Platform=x64
```

## Run the C++ trading application

Keep each program running in its own PowerShell window, with all windows opened at the repository root.

**Window 1 - start the server:**

```powershell
.\build\TradingPlatformCpp.Server\Release\TradingPlatformCpp.Server.exe 5000
```

**Window 2 - connect as Alice:**

```powershell
.\build\TradingPlatformCpp.Client\Release\TradingPlatformCpp.Client.exe 127.0.0.1 5000 alice
```

**Window 3 - connect as Bob:**

```powershell
.\build\TradingPlatformCpp.Client\Release\TradingPlatformCpp.Client.exe 127.0.0.1 5000 bob
```

The server defaults to port `5000`. The client defaults to `127.0.0.1:5000` and prompts for a username if none is provided. Use different usernames for simultaneous clients; a duplicate active login is rejected.

To launch through Visual Studio, set the server as the startup project and use **Project > Properties > Debugging > Command Arguments** with `5000`, then press `Ctrl+F5`. Launch clients separately from PowerShell, or use another Visual Studio instance with the client as its startup project and arguments `127.0.0.1 5000 alice`.

### Try a trade

With fresh accounts, enter these commands in Alice's console:

```text
buy BETA 5 75.00
orders
account
```

Then enter these in Bob's console:

```text
sell BETA 3 75.00
symbol BETA
history
account
```

Three shares trade at $75.00. Alice now holds 53 BETA shares with 2 shares left on her buy order and $150 reserved. Bob holds 47 BETA shares and $10,225 cash. In Alice's console, use `orders` to find the remaining order ID, then cancel it with `cancel <orderId>`.

### Console commands

| Command | Action |
| --- | --- |
| `help` | List supported commands. |
| `symbol <sym>` | Select `ACME`, `BETA`, or `GAMMA`. |
| `market [sym]` | View the order book. |
| `account` | View cash, reservations, and holdings. |
| `orders` | View your orders. |
| `history` | View recent trades for the selected symbol. |
| `buy <qty> <price>` | Place a buy limit order on the selected symbol. |
| `buy <sym> <qty> <price>` | Place a buy limit order on a specific symbol. |
| `sell <qty> <price>` | Place a sell limit order on the selected symbol. |
| `sell <sym> <qty> <price>` | Place a sell limit order on a specific symbol. |
| `cancel <orderId>` | Cancel one of your open orders. |
| `refresh [sym]` | Request a fresh market and account snapshot. |
| `quit` | Disconnect. |

### Saved state

The server reads and writes `trading_state.txt` in its **process working directory**. Launching it from the repository root keeps that file in the root. Restart from the same working directory to restore accounts, orders, and trade history.

For a fresh demonstration, stop the server and rename or remove that state file before restarting. This resets the saved simulation. When launching through Visual Studio, check the project's debugging working directory to locate the file. Runtime state is excluded from Git.

## Run the browser demo

After building, run:

```powershell
.\build\TradingPlatformCpp.WebHost\Release\TradingPlatformCpp.WebHost.exe
```

1. Open **http://localhost:8080**.
2. Open a second tab at the same address in the same browser profile.
3. Log in as `alice` in one tab and `bob` in the other.
4. Place a buy order for 5 ACME shares at $100.00 as Alice, then sell 5 ACME shares at $100.00 as Bob.
5. Review the filled orders, trade history, and updated portfolios.

The web host only serves the HTML, CSS, and JavaScript files. You can run this demo without starting the TCP trading server. Exchange state is shared through `localStorage`; each tab's login is stored in `sessionStorage`. Use the same origin and browser profile for both tabs. Use **Reset Demo** to clear the browser simulation and start again.

Serve the demo over HTTP rather than opening `index.html` directly, so tabs share the same browser storage. To change the HTTP port or specify the web folder explicitly:

```powershell
.\build\TradingPlatformCpp.WebHost\Release\TradingPlatformCpp.WebHost.exe 8081 .\TradingPlatformCpp.WebDemo
```

Then open **http://localhost:8081**.

## Run the stress client

With the TCP server running, open another PowerShell window:

```powershell
.\build\TradingPlatformCpp.Stress\Release\TradingPlatformCpp.Stress.exe 127.0.0.1 5000 10 40 20 demo
```

Arguments are `host`, `port`, `clients`, `operations per client`, `maximum delay in milliseconds`, and `username prefix`. This example starts 10 clients with 40 operations each and random delays up to 20 ms. It prints command, accepted-order, trade-notice, snapshot, error, and client-failure counts.

Stress accounts are saved in the server state. Use a fresh username prefix for independent runs, or reset the saved simulation before a clean demonstration.

## Project structure

| Path | Purpose |
| --- | --- |
| `TradingPlatformCpp.sln` | Visual Studio solution containing four executable projects. |
| `TradingPlatformCpp.Server/` | TCP server, matching engine, account state, and persistence. |
| `TradingPlatformCpp.Client/` | Interactive console client. |
| `TradingPlatformCpp.Common/` | Shared protocol and Winsock helper headers. |
| `TradingPlatformCpp.Stress/` | Concurrent traffic generator. |
| `TradingPlatformCpp.WebHost/` | C++ HTTP server for the browser demo. |
| `TradingPlatformCpp.WebDemo/` | HTML, CSS, and JavaScript dashboard. |
| `Assignment_5_Design_Report_2403446.pdf` | Assignment design report. |
| `CSD2161_Group_Self-Eva.xlsx` | Group self-evaluation document. |

The TCP application uses newline-delimited messages such as `LOGIN|alice`, `BUY|BETA|5|75.00`, and `CANCEL|ORD0001`. The server responds with `INFO`, `ERROR`, and structured `SNAPSHOT_*` messages.

## Troubleshooting

- **MSVC v143 or SDK missing:** Modify your Visual Studio installation and install the C++ workload, v143 build tools, and a Windows SDK.
- **Executable missing:** Build the solution in `Release | x64` and check the output paths above.
- **Client cannot connect:** Start the TCP server first and use the same port for server and client.
- **Port already in use:** Stop the previous instance or choose another port and update the client arguments or browser URL.
- **Web host cannot find the demo:** Launch from the repository root or pass `TradingPlatformCpp.WebDemo` as the web-root argument.
- **Browser tabs show different state:** Use the same `http://localhost:8080` address and browser profile for both tabs.
- **Previous orders or balances reappear:** Reset the relevant C++ state file or use the browser's **Reset Demo** button.

## Credits

Developed by Erika Ishii, Yimo Kong, and Elvis Lim for Assignment 5, Option 3. Existing source files contain DigiPen Institute of Technology copyright notices; see the individual file headers.
