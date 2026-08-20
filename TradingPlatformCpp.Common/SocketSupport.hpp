#/*****************************************************************
/* Start Header
*****************************************************************/
/*!
\file SocketSupport.hpp
\author Erika Ishii, Yimo Kong, Elvis Lim
\par email: erika.ishii@digipen.edu; yimo.kong@digipen.edu; elvisshengjie.lim@digipen.edu
\date 26 Mar, 2026
\brief Assignment 5 Winsock setup and socket utility helpers for the trading platform.
Copyright (C) 2026 DigiPen Institute of Technology.
Reproduction or disclosure of this file or its contents without the prior
written consent of DigiPen Institute of Technology is prohibited.
*/
/* End Header
*******************************************************************/

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

namespace trading
{
    class WinsockSession
    {
    public:
        WinsockSession()
        {
            valid_ = (WSAStartup(MAKEWORD(2, 2), &data_) == 0);
        }

        ~WinsockSession()
        {
            if (valid_)
            {
                WSACleanup();
            }
        }

        bool IsValid() const
        {
            return valid_;
        }

    private:
        WSADATA data_{};
        bool valid_{ false };
    };

    inline void CloseSocket(SOCKET& socketHandle)
    {
        if (socketHandle != INVALID_SOCKET)
        {
            shutdown(socketHandle, SD_BOTH);
            closesocket(socketHandle);
            socketHandle = INVALID_SOCKET;
        }
    }
}
