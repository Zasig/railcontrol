/*
RailControl - Model Railway Control Software

Copyright (c) 2017-2025 by Teddy / Dominik Mahrer - www.railcontrol.org

RailControl is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

RailControl is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with RailControl; see the file LICENCE. If not see
<http://www.gnu.org/licenses/>.
*/

#include <arpa/inet.h>
#include <netinet/in.h>

#include "DataModel/LocoBase.h"
#include "DataModel/LocoFunctions.h"
#include "Manager.h"
#include "Network/Select.h"
#include "Network/UdpConnection.h"
#include "Server/CS2/CS2Client.h"
#include "Server/CS2/CS2Server.h"
#include "Utils/Network.h"

using DataModel::Loco;
using DataModel::LocoBase;
using DataModel::LocoFunctionNr;
using DataModel::LocoFunctionState;

namespace Network
{
	class UdpClient;
}

namespace Server { namespace CS2
{
	CS2Server::CS2Server(Manager& manager)
	:	ControlInterface(ControlTypeCS2Server),
		Network::TcpServer("0.0.0.0", CS2ReceiverPort, "CS2Server"),
		logger(Logger::Logger::GetLogger("CS2Server")),
		manager(manager),
		lastClientID(0),
		runUdp(false)
	{
	}

	CS2Server::~CS2Server()
	{
		CleanUpClients();
		logger->Info(Languages::TextCS2ServerStopped);
	}

	void CS2Server::Start()
	{
		StartTcpServer();
		StartUdpServer();
		logger->Info(Languages::TextCS2ServerStarted);
	}

	void CS2Server::Stop()
	{
		TerminateUdpServer();
		TerminateTcpServer();
		// stopping all clients
		for (auto client : clients)
		{
			client->Stop();
		}
	}

	void CS2Server::StartUdpServer()
	{
		runUdp = true;

		// Only IPv4 is used by clients
		struct sockaddr_in serverAddr4;
		memset(reinterpret_cast<char*>(&serverAddr4), 0, sizeof(serverAddr4));
		serverAddr4.sin_family = AF_INET;
		serverAddr4.sin_addr.s_addr = htonl(INADDR_ANY);
		serverAddr4.sin_port = htons(CS2ReceiverPort);
		UdpSocketCreateBindListen(serverAddr4.sin_family, reinterpret_cast<struct sockaddr*>(&serverAddr4));
	}

	void CS2Server::TerminateUdpServer()
	{
		runUdp = false;
		udpServerThread.join();
	}

	void CS2Server::UdpSocketCreateBindListen(int family, struct sockaddr* address)
	{
		udpServerSocket = socket(family, SOCK_DGRAM, 0);
		if (udpServerSocket < 0)
		{
//			error = "Unable to create socket for udp server. Unable to serve clients.";
			return;
		}

		int on = 1;
		int intResult = setsockopt(udpServerSocket, SOL_SOCKET, SO_REUSEADDR, (const void*) &on, sizeof(on));
		if (intResult < 0)
		{
//			error = "Unable to set tcp server socket option SO_REUSEADDR.";
			close(udpServerSocket);
			return;
		}

		intResult = bind(udpServerSocket, address, sizeof(struct sockaddr_in));
		if (intResult < 0)
		{
//			error = "Unable to bind socket for udp server to port. Unable to serve clients.";
			close(udpServerSocket);
			return;
		}

        // allow sending broadcast replies
        setsockopt(udpServerSocket, SOL_SOCKET, SO_BROADCAST, (const void*)&on, sizeof(on));

		if (!runUdp)
		{
			close(udpServerSocket);
			return;
		}
		udpServerThread = std::thread(&Server::CS2::CS2Server::UdpWorker, this);
	}

	void CS2Server::UdpWorker()
	{
		Utils::Utils::SetThreadName("CS2 UDP Server");
		Logger::Logger* udpLogger = Logger::Logger::GetLogger("CS2 UDP Server");
		fd_set set;
		struct timeval tv;
		struct sockaddr_storage clientAddress;
		socklen_t clientAddressLength = sizeof(clientAddress);
		while (runUdp)
		{
			// wait for data and abort on shutdown
			int ret;
			do
			{
				FD_ZERO(&set);
				FD_SET(udpServerSocket, &set);
				tv.tv_sec = 1;
				tv.tv_usec = 0;
				ret = TEMP_FAILURE_RETRY(select(FD_SETSIZE, &set, NULL, NULL, &tv));

				if (!runUdp)
				{
					return;
				}
			} while (ret == 0);

			if (ret < 0)
			{
				continue;
			}

			static const int CANCommandBufferLength = 13;
			unsigned char buffer[CANCommandBufferLength];
            memset(reinterpret_cast<char*>(&clientAddress), 0, sizeof(clientAddress));
            ssize_t size = recvfrom(udpServerSocket, buffer, sizeof(buffer), 0, reinterpret_cast<struct sockaddr*>(&clientAddress), &clientAddressLength);
            struct sockaddr_in* clientAddress4_log = reinterpret_cast<struct sockaddr_in*>(&clientAddress);
            char clientIpLog[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddress4_log->sin_addr, clientIpLog, sizeof(clientIpLog));
            udpLogger->Debug("UDP received from {0}:{1} size {2}", std::string(clientIpLog), ntohs(clientAddress4_log->sin_port), static_cast<int>(size));
            udpLogger->HexIn(buffer, size);
			if (size != CANCommandBufferLength)
			{
				continue;
			}

            // Build Ping response using the hash from the received packet so the client recognizes it
            unsigned char reply[CANCommandBufferLength];
            // set prio/command byte - echo command header but set response bit in byte 1
            reply[0] = buffer[0];
            reply[1] = buffer[1] | 0x01; // set response bit
            // copy hash from requester so their Parse() matches
            reply[2] = buffer[2];
            reply[3] = buffer[3];
            // length of payload = 8 (device info + version + device type)
            reply[4] = 0x08;
            // copy bytes 5..10 (reserved, device id, major, minor) from requester to keep versions/device id
            memcpy(reply + 5, buffer + 5, 6);
            // device type = CanDeviceCs2Main (0xFFFF)
            reply[11] = 0xFF;
            reply[12] = 0xFF;

            // Send response to client's CS2 sender port (15730) — clients expect replies on that port
            struct sockaddr_in* clientAddress4 = reinterpret_cast<struct sockaddr_in*>(&clientAddress);
            clientAddress4->sin_port = htons(CS2SenderPort);
            char clientIp[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddress4->sin_addr, clientIp, sizeof(clientIp));
            int sendRes = sendto(udpServerSocket, reply, sizeof(reply), 0, reinterpret_cast<struct sockaddr*>(clientAddress4), sizeof(struct sockaddr_in));
            if (sendRes < 0)
            {
                udpLogger->Debug("Unable to send UDP response to {0}:{1}: {2}", std::string(clientIp), ntohs(clientAddress4->sin_port), strerror(errno));
            }
            else
            {
                udpLogger->Debug("UDP response sent to {0}:{1}", std::string(clientIp), ntohs(clientAddress4->sin_port));
                udpLogger->HexOut(reply, CANCommandBufferLength);
            }
//			sockaddr_in* clientAddress4 = reinterpret_cast<struct sockaddr_in*>(&clientAddress);
//			clientAddress4->sin_port = htons(CS2SenderPort);
//			Network::UdpConnection udpConnection(logger, reinterpret_cast<struct sockaddr*>(clientAddress4));
//			udpConnection.Bind();
//			udpConnection.Send(buffer, sizeof(buffer));
//			udpConnection.Terminate();
		}
	}

	void CS2Server::Booster(__attribute__((unused)) const ControlType controlType,
		const BoosterState status)
	{
		if (status == BoosterStateGo)
		{
			for (auto client : clients)
			{
				client->SendPowerOn();
			}
		}
		else
		{
			for (auto client : clients)
			{
				client->SendPowerOff();
			}
		}
	}

	void CS2Server::LocoBaseSpeed(__attribute__((unused)) const ControlType controlType,
		const LocoBase* locoBase,
		__attribute__((unused)) const Speed speed)
	{
		if (nullptr == locoBase)
		{
			return;
		}
		for (auto client : clients)
		{
			client->SendLocoInfo(locoBase);
		}
	}

	void CS2Server::LocoBaseOrientation(__attribute__((unused)) const ControlType controlType,
		const LocoBase* locoBase,
		__attribute__((unused)) const Orientation orientation)
	{
		if (nullptr == locoBase)
		{
			return;
		}
		for (auto client : clients)
		{
			client->SendLocoInfo(locoBase);
		}
	}

	void CS2Server::LocoBaseFunction(__attribute__((unused)) const ControlType controlType,
		const LocoBase* locoBase,
		__attribute__((unused)) const LocoFunctionNr function,
		__attribute__((unused)) const LocoFunctionState state)
	{
		if (!locoBase)
		{
			return;
		}
		for (auto client : clients)
		{
			client->SendLocoInfo(locoBase);
		}
	}

	void CS2Server::AccessoryState(__attribute__((unused)) const ControlType controlType,
		const DataModel::Accessory* accessory)
	{
		AccessoryBaseState(accessory);
	}

	void CS2Server::SwitchState(__attribute__((unused)) const ControlType controlType,
		const DataModel::Switch* mySwitch)
	{
		AccessoryBaseState(mySwitch);
	}

	void CS2Server::SignalState(__attribute__((unused)) const ControlType controlType,
		const DataModel::Signal* signal)
	{
		AccessoryBaseState(signal);
	}

	void CS2Server::AccessoryBaseState(const DataModel::AccessoryBase* accessoryBase)
	{
		if (!accessoryBase)
		{
			return;
		}
		for (auto client : clients)
		{
			client->SendAccessoryInfo(accessoryBase);
		}
	}

	void CS2Server::Work(Network::TcpConnection* connection)
	{
		clients.push_back(new CS2Client(++lastClientID, connection, manager));
		CleanUpClients();
	}

	void CS2Server::CleanUpClients()
	{
		for (auto iterator = clients.begin(); iterator != clients.end();)
		{
			CS2Client* client = *iterator;
			if (client->IsTerminated())
			{
				iterator = clients.erase(iterator);
				delete client;
			}
			else
			{
				++iterator;
			}
		}
	}
}} // namespace Server::CS2
