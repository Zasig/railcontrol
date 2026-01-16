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

#include <atomic>
#include <thread>

#include "Logger/Logger.h"
#include "Manager.h"
#include "Network/TcpServer.h"

namespace Server { namespace CS2
{
	class CS2HttpServer : private Network::TcpServer
	{
		public:
			static const unsigned short CS2HttpPort = 80;

			CS2HttpServer() = delete;
			CS2HttpServer(const CS2HttpServer&) = delete;
			CS2HttpServer& operator=(const CS2HttpServer&) = delete;

			CS2HttpServer(Manager& manager);
			~CS2HttpServer();

			void Start();
			void Stop();

			void Work(Network::TcpConnection* connection) override;

		private:
			Logger::Logger* logger;
			Manager& manager;

			void HandleClient(Network::TcpConnection* connection);
			std::string GenerateResponse(const std::string& path);
	};
}} // namespace Server::CS2
