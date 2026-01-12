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

#pragma once

#include <string>
#include <thread>

#include "Logger/Logger.h"
#include "Manager.h"
#include "Network/TcpConnection.h"

namespace Server { namespace Api
{
	class ApiServer;

	class ApiClient
	{
		public:
			ApiClient() = delete;
			ApiClient(const ApiClient&) = delete;
			ApiClient& operator=(const ApiClient&) = delete;

			ApiClient(const unsigned int id,
				Network::TcpConnection* connection,
				ApiServer& server,
				Manager& manager);

			~ApiClient();

			void Worker();

			inline void Stop()
			{
				run = false;
			}

			inline bool IsTerminated()
			{
				return terminated;
			}

		private:
			void WorkerImpl();
			void SendResponse(int statusCode, const std::string& statusText, const std::string& body, const std::string& contentType = "application/json");
			void SendJsonResponse(int statusCode, const std::string& json);
			void SendHtmlResponse(const std::string& html);
			void HandleRequest(const std::string& method, const std::string& uri, const std::string& body);
			void HandleBoosterGet();
			void HandleBoosterPost(const std::string& body);
			void HandleLayersGet();
			void HandleLayoutGet(const std::string& uri);
			void HandleLocosGet();
			void ServeHtmlUI();
			void ServeLayoutUI();

			Logger::Logger* logger;
			unsigned int id;
			Network::TcpConnection* connection;
			volatile bool run;
			bool terminated;
			ApiServer& server;
			std::thread clientThread;
			Manager& manager;
	};
}} // namespace Server::Api

