#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file main.cpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 trading server program entry point.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#include <iostream>
#include <string>

#include "SocketSupport.hpp"
#include "TradingServer.hpp"

int main(int argc, char* argv[])
{
    unsigned short port = 5000;
    if (argc > 1)
    {
        try
        {
            port = static_cast<unsigned short>(std::stoi(argv[1]));
        }
        catch (...)
        {
            std::cerr << "Usage: TradingPlatformCpp.Server.exe [port]\n";
            return 1;
        }
    }

    trading::WinsockSession winsock;
    if (!winsock.IsValid())
    {
        std::cerr << "Failed to initialize Winsock.\n";
        return 1;
    }

    try
    {
        trading::server::TradingServer server(port);
        server.Run();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Server error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
