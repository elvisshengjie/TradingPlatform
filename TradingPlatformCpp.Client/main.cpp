#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file main.cpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 trading client program entry point.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#include <iostream>
#include <string>

#include "SocketSupport.hpp"
#include "TradingClient.hpp"

int main(int argc, char* argv[])
{
    std::string host = "127.0.0.1";
    unsigned short port = 5000;
    std::string username;

    if (argc > 1)
    {
        host = argv[1];
    }

    if (argc > 2)
    {
        try
        {
            port = static_cast<unsigned short>(std::stoi(argv[2]));
        }
        catch (...)
        {
            std::cerr << "Usage: TradingPlatformCpp.Client.exe [host] [port] [username]\n";
            return 1;
        }
    }

    if (argc > 3)
    {
        username = argv[3];
    }

    trading::WinsockSession winsock;
    if (!winsock.IsValid())
    {
        std::cerr << "Failed to initialize Winsock.\n";
        return 1;
    }

    trading::client::TradingClient client(host, port, username);
    client.Run();
    return 0;
}
