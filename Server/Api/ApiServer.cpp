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

#include <algorithm>

#include "Languages.h"
#include "Server/Api/ApiClient.h"
#include "Server/Api/ApiServer.h"

using std::string;
using std::vector;

namespace Server { namespace Api
{
	ApiServer::ApiServer(Manager& manager, const std::string& address, const unsigned short port)
	:	ControlInterface(ControlTypeApiServer),
		Network::TcpServer(address, port, "ApiServer"),
		logger(Logger::Logger::GetLogger("ApiServer")),
		lastClientID(0),
		manager(manager)
	{
		logger->Info(Languages::TextApiServerStarted);
	}

	ApiServer::~ApiServer()
	{
		// delete all client memory
		while (clients.size())
		{
			ApiClient* client = clients.back();
			clients.pop_back();
			delete client;
		}
		logger->Info(Languages::TextApiServerStopped);
	}

	void ApiServer::Start()
	{
		StartTcpServer();
		logger->Info(Languages::TextApiServerStarted);
	}

	void ApiServer::Stop()
	{
		TerminateTcpServer();
		// stopping all clients
		for (auto client : clients)
		{
			client->Stop();
		}
	}

	void ApiServer::Work(Network::TcpConnection* connection)
	{
		// delete terminated clients
		clients.erase(std::remove_if(clients.begin(), clients.end(), [](ApiClient* client) -> bool
		{
			if (client->IsTerminated())
			{
				delete client;
				return true;
			}
			return false;
		}), clients.end());

		// create new client
		clients.push_back(new ApiClient(++lastClientID, connection, *this, manager));
	}

	void ApiServer::Booster(const ControlType controlType, const BoosterState status)
	{
		if (controlType == ControlTypeApiServer)
		{
			return;
		}
		// Could be used to push updates to connected WebSocket clients in future
		logger->Debug(status == BoosterStateGo ? Languages::TextBoosterIsTurnedOn : Languages::TextBoosterIsTurnedOff);
	}
}} // namespace Server::Api

