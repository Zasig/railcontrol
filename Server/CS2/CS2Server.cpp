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
#include "Server/CS2/CS2HttpServer.h"
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
		runUdp(false),
		udpServerSocket(-1),
		udpBroadcastSocket(-1),
		httpServer(nullptr)
	{
	}

	CS2Server::~CS2Server()
	{
		CleanUpClients();
		delete httpServer;
		httpServer = nullptr;
		logger->Info(Languages::TextCS2ServerStopped);
	}

	void CS2Server::Start()
	{
		StartTcpServer();
		StartUdpServer();

		// Start HTTP server for CS2 config files on port 80
		httpServer = new CS2HttpServer(manager);
		httpServer->Start();

		logger->Info(Languages::TextCS2ServerStarted);
	}

	void CS2Server::Stop()
	{
		if (httpServer)
		{
			httpServer->Stop();
		}
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

		// Start broadcast thread to send periodic ping broadcasts
		udpBroadcastThread = std::thread(&Server::CS2::CS2Server::UdpBroadcastWorker, this);
	}

	void CS2Server::TerminateUdpServer()
	{
		runUdp = false;
		if (udpBroadcastThread.joinable())
		{
			udpBroadcastThread.join();
		}
		if (udpServerThread.joinable())
		{
			udpServerThread.join();
		}
		if (udpBroadcastSocket >= 0)
		{
			close(udpBroadcastSocket);
			udpBroadcastSocket = -1;
		}
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

            // Parse CAN command from buffer
            // Byte 0-1: Priority (4 bits) + Command (8 bits) + Response bit (1 bit)
            // Command is in bits 7-0 of the combined bytes 0-1
            unsigned char command = ((buffer[0] & 0x01) << 7) | (buffer[1] >> 1);
            bool isResponse = buffer[1] & 0x01;

            // Check for Ping command (0x18)
            if (command != 0x18)
            {
                udpLogger->Debug("Ignoring non-ping command {0}", static_cast<int>(command));
                continue;
            }

            // Check if this is a discovery request (Response with device type 0xEEEE)
            // CS2.exe sends this to discover CS2 masters on the network
            bool isDiscoveryRequest = isResponse && (buffer[11] == 0xEE) && (buffer[12] == 0xEE);
            bool isPingRequest = !isResponse;

            if (!isPingRequest && !isDiscoveryRequest)
            {
                udpLogger->Debug("Ignoring ping response (not discovery)");
                continue;
            }

            udpLogger->Debug("Received {0}, sending response", isDiscoveryRequest ? "discovery request" : "ping request");

            // Build reply based on request type
            unsigned char reply[CANCommandBufferLength];
            
            if (isDiscoveryRequest)
            {
                // For discovery requests (0x31 with 0xEEEE), respond with a simple Ping Request (0x30)
                // This tells the client "I'm here, please send me a proper ping"
                // According to can2lan: M_PING_RESPONSE = { 0x00, 0x30, 0x00, 0x00, 0x00 }
                reply[0] = 0x00;
                reply[1] = 0x30; // Ping command WITHOUT response bit
                reply[2] = 0x00;
                reply[3] = 0x00;
                reply[4] = 0x00; // Length = 0 (empty ping)
                // Rest is zeroed
                for (int i = 5; i < CANCommandBufferLength; ++i)
                {
                    reply[i] = 0x00;
                }
            }
            else
            {
                // For normal ping requests, respond with full device info (Ping Response 0x31)
                reply[0] = 0x00;
                reply[1] = 0x31; // Ping command WITH response bit
                // Use own hash for the response (calculate from UID)
                uint16_t serverHash = ((ServerUid >> 16) ^ ServerUid) & 0x03FF;
                serverHash |= 0x0300;
                reply[2] = (serverHash >> 8) & 0xFF;
                reply[3] = serverHash & 0xFF;
                // length of payload = 8 (device info + version + device type)
                reply[4] = 0x08;
                // UID bytes
                reply[5] = (ServerUid >> 24) & 0xFF;
                reply[6] = (ServerUid >> 16) & 0xFF;
                reply[7] = (ServerUid >> 8) & 0xFF;
                reply[8] = ServerUid & 0xFF;
                // Software version: Major.Minor (4.3 for CS2 compatibility)
                reply[9] = 0x04;
                reply[10] = 0x03;
                // device type = CanDeviceCs2Main (0xFFFF)
                reply[11] = 0xFF;
                reply[12] = 0xFF;
            }

            // Send response as broadcast on port 15730 (clients listen for broadcasts)
            struct sockaddr_in broadcastAddr;
            memset(&broadcastAddr, 0, sizeof(broadcastAddr));
            broadcastAddr.sin_family = AF_INET;
            broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            broadcastAddr.sin_port = htons(CS2SenderPort);

            // Enable broadcast on the socket for this send
            int broadcastEnable = 1;
            setsockopt(udpServerSocket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));

            int sendRes = sendto(udpServerSocket, reply, sizeof(reply), 0, 
                reinterpret_cast<struct sockaddr*>(&broadcastAddr), sizeof(broadcastAddr));
            if (sendRes < 0)
            {
                udpLogger->Debug("Unable to send UDP broadcast response: {0}", strerror(errno));
            }
            else
            {
                udpLogger->Debug("UDP broadcast response sent to port {0}", CS2SenderPort);
                udpLogger->HexOut(reply, CANCommandBufferLength);
            }
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

	void CS2Server::UdpBroadcastWorker()
	{
		Utils::Utils::SetThreadName("CS2 UDP Broadcast");
		Logger::Logger* broadcastLogger = Logger::Logger::GetLogger("CS2 UDP Broadcast");

		// Create broadcast socket
		udpBroadcastSocket = socket(AF_INET, SOCK_DGRAM, 0);
		if (udpBroadcastSocket < 0)
		{
			broadcastLogger->Debug("Unable to create broadcast socket");
			return;
		}

		int on = 1;
		setsockopt(udpBroadcastSocket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
		setsockopt(udpBroadcastSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

		// Bind to sender port (15730) so responses come back correctly
		struct sockaddr_in bindAddr;
		memset(&bindAddr, 0, sizeof(bindAddr));
		bindAddr.sin_family = AF_INET;
		bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		bindAddr.sin_port = htons(CS2SenderPort);
		if (bind(udpBroadcastSocket, reinterpret_cast<struct sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0)
		{
			broadcastLogger->Debug("Unable to bind broadcast socket to port {0}: {1}", CS2SenderPort, strerror(errno));
			// Continue anyway, just won't be able to receive responses
		}

		broadcastLogger->Debug("Starting periodic ping broadcasts");

		// Wait a bit before first broadcast
		for (int i = 0; i < 10 && runUdp; ++i)
		{
			Utils::Utils::SleepForMilliseconds(100);
		}

		while (runUdp)
		{
			SendPingBroadcast();

			// Wait 10 seconds between broadcasts
			for (int i = 0; i < 100 && runUdp; ++i)
			{
				Utils::Utils::SleepForMilliseconds(100);
			}
		}

		broadcastLogger->Debug("Stopping ping broadcasts");
	}

	void CS2Server::SendPingBroadcast()
	{
		if (udpBroadcastSocket < 0)
		{
			return;
		}

		static const int CANCommandBufferLength = 13;
		unsigned char buffer[CANCommandBufferLength];

		// Build Ping response (with response bit set)
		// Prio = 0, Command = 0x18 (Ping), Response = 1
		// Format: Byte 0-1 = (Prio << 12) | (Command << 1) | Response
		// Byte 0 = 0x00, Byte 1 = (0x18 << 1) | 1 = 0x31
		buffer[0] = 0x00;
		buffer[1] = 0x31; // Command 0x18 << 1 = 0x30, response bit = 1 -> 0x31

		// Calculate hash from UID
		uint16_t serverHash = ((ServerUid >> 16) ^ ServerUid) & 0x03FF;
		serverHash |= 0x0300;
		buffer[2] = (serverHash >> 8) & 0xFF;
		buffer[3] = serverHash & 0xFF;

		// Length = 8 (full device info)
		buffer[4] = 0x08;

		// UID
		buffer[5] = (ServerUid >> 24) & 0xFF;
		buffer[6] = (ServerUid >> 16) & 0xFF;
		buffer[7] = (ServerUid >> 8) & 0xFF;
		buffer[8] = ServerUid & 0xFF;

		// Version 4.3 (like real CS2)
		buffer[9] = 0x04;
		buffer[10] = 0x03;

		// Device type = CS2 Main (0xFFFF)
		buffer[11] = 0xFF;
		buffer[12] = 0xFF;

		// Send to broadcast address on CS2 receiver port
		struct sockaddr_in broadcastAddr;
		memset(&broadcastAddr, 0, sizeof(broadcastAddr));
		broadcastAddr.sin_family = AF_INET;
		broadcastAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		broadcastAddr.sin_port = htons(CS2SenderPort); // Send to 15730, where clients listen

		int ret = sendto(udpBroadcastSocket, buffer, CANCommandBufferLength, 0,
			reinterpret_cast<struct sockaddr*>(&broadcastAddr), sizeof(broadcastAddr));

		if (ret < 0)
		{
			logger->Debug("Failed to send ping broadcast: {0}", strerror(errno));
		}
		else
		{
			logger->Debug("Sent ping broadcast");
		}
	}
}} // namespace Server::CS2
