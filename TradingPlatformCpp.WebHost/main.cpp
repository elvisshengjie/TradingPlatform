//*****************************************************************
// Start Header
//*****************************************************************
/*!
\file main.cpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 28 Mar, 2026
\brief Lightweight localhost web host for the browser demo. It serves the
TradingPlatformCpp.WebDemo static files over HTTP so multiple browser tabs share
the same origin and can observe the same local-storage-backed exchange state.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
// End Header
//*******************************************************************

#include "..\TradingPlatformCpp.Common\SocketSupport.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    struct Options
    {
        int port{ 8080 };
        fs::path root;
    };

    std::string TrimCarriageReturn(std::string value)
    {
        if (!value.empty() && value.back() == '\r')
        {
            value.pop_back();
        }

        return value;
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
        return value;
    }

    bool SendAll(SOCKET socketHandle, const char* data, size_t size)
    {
        size_t sent = 0;
        while (sent < size)
        {
            const int chunk = send(socketHandle, data + sent, static_cast<int>(size - sent), 0);
            if (chunk == SOCKET_ERROR || chunk == 0)
            {
                return false;
            }

            sent += static_cast<size_t>(chunk);
        }

        return true;
    }

    std::string ReasonPhrase(int statusCode)
    {
        switch (statusCode)
        {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        default: return "Internal Server Error";
        }
    }

    std::string MimeTypeFor(const fs::path& path)
    {
        const std::string ext = ToLower(path.extension().string());
        if (ext == ".html" || ext == ".htm")
        {
            return "text/html; charset=utf-8";
        }

        if (ext == ".css")
        {
            return "text/css; charset=utf-8";
        }

        if (ext == ".js")
        {
            return "application/javascript; charset=utf-8";
        }

        if (ext == ".json")
        {
            return "application/json; charset=utf-8";
        }

        if (ext == ".svg")
        {
            return "image/svg+xml";
        }

        if (ext == ".png")
        {
            return "image/png";
        }

        if (ext == ".jpg" || ext == ".jpeg")
        {
            return "image/jpeg";
        }

        if (ext == ".ico")
        {
            return "image/x-icon";
        }

        return "application/octet-stream";
    }

    std::string UrlDecode(std::string_view encoded)
    {
        std::string decoded;
        decoded.reserve(encoded.size());

        for (size_t index = 0; index < encoded.size(); ++index)
        {
            const char ch = encoded[index];
            if (ch == '%' && index + 2 < encoded.size())
            {
                const auto hexValue = [](char hexDigit) -> int
                {
                    if (hexDigit >= '0' && hexDigit <= '9')
                    {
                        return hexDigit - '0';
                    }

                    if (hexDigit >= 'a' && hexDigit <= 'f')
                    {
                        return 10 + (hexDigit - 'a');
                    }

                    if (hexDigit >= 'A' && hexDigit <= 'F')
                    {
                        return 10 + (hexDigit - 'A');
                    }

                    return -1;
                };

                const int hi = hexValue(encoded[index + 1]);
                const int lo = hexValue(encoded[index + 2]);
                if (hi >= 0 && lo >= 0)
                {
                    decoded.push_back(static_cast<char>((hi << 4) | lo));
                    index += 2;
                    continue;
                }
            }

            decoded.push_back(ch == '+' ? ' ' : ch);
        }

        return decoded;
    }

    bool TryResolveRequestPath(const fs::path& webRoot, std::string target, fs::path& filePath)
    {
        const size_t queryStart = target.find_first_of("?#");
        if (queryStart != std::string::npos)
        {
            target = target.substr(0, queryStart);
        }

        if (target.empty() || target == "/")
        {
            target = "/index.html";
        }

        target = UrlDecode(target);
        std::replace(target.begin(), target.end(), '\\', '/');

        fs::path relativePath;
        // Rebuild the requested path segment-by-segment so "../" never escapes the web root.
        std::stringstream stream(target);
        std::string segment;
        while (std::getline(stream, segment, '/'))
        {
            if (segment.empty() || segment == ".")
            {
                continue;
            }

            if (segment == "..")
            {
                return false;
            }

            relativePath /= segment;
        }

        if (relativePath.empty())
        {
            relativePath = "index.html";
        }

        filePath = webRoot / relativePath;
        if (fs::is_directory(filePath))
        {
            filePath /= "index.html";
        }

        return true;
    }

    std::string LoadFileBytes(const fs::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }

    bool SendResponse(SOCKET socketHandle, int statusCode, std::string_view contentType, std::string_view body, bool sendBody)
    {
        std::ostringstream response;
        response << "HTTP/1.1 " << statusCode << ' ' << ReasonPhrase(statusCode) << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: close\r\n\r\n";

        const std::string headerText = response.str();
        if (!SendAll(socketHandle, headerText.data(), headerText.size()))
        {
            return false;
        }

        if (sendBody && !body.empty())
        {
            return SendAll(socketHandle, body.data(), body.size());
        }

        return true;
    }

    bool ReadRequest(SOCKET socketHandle, std::string& requestText)
    {
        std::array<char, 4096> buffer{};
        requestText.clear();

        // The host only needs a small request header for static GET/HEAD requests.
        while (requestText.find("\r\n\r\n") == std::string::npos)
        {
            const int received = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received == SOCKET_ERROR || received == 0)
            {
                return false;
            }

            requestText.append(buffer.data(), static_cast<size_t>(received));
            if (requestText.size() > 16384)
            {
                return false;
            }
        }

        return true;
    }

    fs::path FindWebRoot(const fs::path& executablePath)
    {
        // Probe from the current working directory and from the executable location so the host
        // still finds the demo whether launched from Visual Studio or from the built output folder.
        const std::vector<fs::path> searchStarts = {
            fs::current_path(),
            executablePath.parent_path(),
        };

        for (const fs::path& start : searchStarts)
        {
            fs::path probe = start;
            for (int depth = 0; depth < 8; ++depth)
            {
                if (fs::exists(probe / "index.html"))
                {
                    return probe;
                }

                if (fs::exists(probe / "TradingPlatformCpp.WebDemo" / "index.html"))
                {
                    return probe / "TradingPlatformCpp.WebDemo";
                }

                if (!probe.has_parent_path())
                {
                    break;
                }

                const fs::path parent = probe.parent_path();
                if (parent == probe)
                {
                    break;
                }

                probe = parent;
            }
        }

        return {};
    }

    Options ParseOptions(int argc, char* argv[])
    {
        Options options;

        if (argc >= 2)
        {
            options.port = std::stoi(argv[1]);
        }

        if (argc >= 3)
        {
            options.root = fs::path(argv[2]);
        }

        return options;
    }

    void HandleClient(SOCKET clientSocket, const fs::path& webRoot)
    {
        std::string requestText;
        if (!ReadRequest(clientSocket, requestText))
        {
            SendResponse(clientSocket, 400, "text/plain; charset=utf-8", "Unable to read request.", true);
            return;
        }

        std::istringstream requestStream(requestText);
        std::string requestLine;
        if (!std::getline(requestStream, requestLine))
        {
            SendResponse(clientSocket, 400, "text/plain; charset=utf-8", "Malformed request.", true);
            return;
        }

        requestLine = TrimCarriageReturn(requestLine);
        std::istringstream lineStream(requestLine);
        std::string method;
        std::string target;
        std::string version;
        lineStream >> method >> target >> version;

        if (method != "GET" && method != "HEAD")
        {
            SendResponse(clientSocket, 405, "text/plain; charset=utf-8", "Only GET and HEAD are supported.", method == "GET");
            return;
        }

        fs::path filePath;
        if (!TryResolveRequestPath(webRoot, target, filePath))
        {
            SendResponse(clientSocket, 400, "text/plain; charset=utf-8", "Invalid path.", method == "GET");
            return;
        }

        if (!fs::exists(filePath) || !fs::is_regular_file(filePath))
        {
            SendResponse(clientSocket, 404, "text/plain; charset=utf-8", "File not found.", method == "GET");
            return;
        }

        const std::string body = LoadFileBytes(filePath);
        SendResponse(clientSocket, 200, MimeTypeFor(filePath), body, method == "GET");
    }
}

int main(int argc, char* argv[])
{
    try
    {
        const Options options = ParseOptions(argc, argv);
        trading::WinsockSession winsock;
        if (!winsock.IsValid())
        {
            std::cerr << "WSAStartup failed.\n";
            return 1;
        }

        const fs::path executablePath = fs::absolute(argv[0]);
        const fs::path webRoot = !options.root.empty() ? options.root : FindWebRoot(executablePath);
        if (webRoot.empty() || !fs::exists(webRoot / "index.html"))
        {
            std::cerr << "Unable to locate TradingPlatformCpp.WebDemo.\n";
            std::cerr << "Usage: TradingPlatformCpp.WebHost.exe [port] [web-root]\n";
            return 1;
        }

        SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listenSocket == INVALID_SOCKET)
        {
            std::cerr << "Failed to create listen socket.\n";
            return 1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(options.port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        {
            trading::CloseSocket(listenSocket);
            std::cerr << "Failed to bind http://localhost:" << options.port << ".\n";
            return 1;
        }

        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
        {
            trading::CloseSocket(listenSocket);
            std::cerr << "Failed to listen for incoming connections.\n";
            return 1;
        }

        std::cout << "Mercury Lane web host running.\n";
        std::cout << "Serving: " << fs::absolute(webRoot).string() << '\n';
        std::cout << "Open: http://localhost:" << options.port << '\n';
        std::cout << "Press Ctrl+C to stop.\n";

        // A single-threaded loop is enough here because the web demo only serves a few small static files.
        while (true)
        {
            sockaddr_in clientAddress{};
            int clientLength = sizeof(clientAddress);
            SOCKET clientSocket = accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
            if (clientSocket == INVALID_SOCKET)
            {
                continue;
            }

            HandleClient(clientSocket, webRoot);
            trading::CloseSocket(clientSocket);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "Web host failed: " << error.what() << '\n';
        return 1;
    }
}
