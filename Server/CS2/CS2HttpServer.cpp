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

#include <cstring>
#include <sstream>

#include "Server/CS2/CS2HttpServer.h"
#include "Utils/Utils.h"

using std::string;
using std::to_string;

namespace Server { namespace CS2
{
	CS2HttpServer::CS2HttpServer(Manager& manager)
	:	Network::TcpServer("0.0.0.0", CS2HttpPort, "CS2HttpServer"),
		logger(Logger::Logger::GetLogger("CS2HttpServer")),
		manager(manager)
	{
	}

	CS2HttpServer::~CS2HttpServer()
	{
		logger->Info(Languages::TextCS2ServerStopped);
	}

	void CS2HttpServer::Start()
	{
		StartTcpServer();
		logger->Info(Languages::TextCS2ServerStarted);
	}

	void CS2HttpServer::Stop()
	{
		TerminateTcpServer();
	}

	void CS2HttpServer::Work(Network::TcpConnection* connection)
	{
		HandleClient(connection);
		delete connection;
	}

	void CS2HttpServer::HandleClient(Network::TcpConnection* connection)
	{
		const int BufferSize = 4096;
		char buffer[BufferSize];
		memset(buffer, 0, BufferSize);

		int ret = connection->Receive(buffer, BufferSize - 1, 0);
		if (ret <= 0)
		{
			return;
		}

		string request(buffer);
		logger->Debug("HTTP Request: {0}", request.substr(0, request.find("\r\n")));

		// Parse HTTP request - extract path
		size_t methodEnd = request.find(' ');
		if (methodEnd == string::npos)
		{
			return;
		}

		size_t pathEnd = request.find(' ', methodEnd + 1);
		if (pathEnd == string::npos)
		{
			return;
		}

		string path = request.substr(methodEnd + 1, pathEnd - methodEnd - 1);

		// Strip query string if present
		size_t queryStart = path.find('?');
		if (queryStart != string::npos)
		{
			path = path.substr(0, queryStart);
		}

		logger->Debug("Requested path: {0}", path);

		// Generate response
		string body = GenerateResponse(path);

		if (body.empty())
		{
			// 404 Not Found
			string response = "HTTP/1.1 404 Not Found\r\n";
			response += "Content-Length: 0\r\n";
			response += "Connection: close\r\n";
			response += "\r\n";
			connection->Send(response);
			return;
		}

		// 200 OK
		string response = "HTTP/1.1 200 OK\r\n";
		response += "Content-Type: text/plain; charset=utf-8\r\n";
		response += "Content-Length: " + to_string(body.size()) + "\r\n";
		response += "Cache-Control: no-cache, must-revalidate\r\n";
		response += "Pragma: no-cache\r\n";
		response += "Connection: close\r\n";
		response += "\r\n";
		response += body;

		connection->Send(response);
	}

	string CS2HttpServer::GenerateResponse(const string& path)
	{
		// Handle CS2 config file requests
		if (path == "/config/lokomotive.cs2")
		{
			string result = manager.GetCs2Lokomotive();
			logger->Debug("lokomotive.cs2 response length: {0}", result.size());
			return result;
		}
		else if (path == "/config/magnetartikel.cs2")
		{
			return manager.GetCs2Magnetartikel();
		}
		else if (path == "/config/fahrstrassen.cs2")
		{
			return manager.GetCs2Fahrstrassen();
		}
		else if (path == "/config/gleisbild.cs2")
		{
			return manager.GetCs2GBS();
		}
		else if (path.rfind("/config/gleisbilder/", 0) == 0)
		{
			// Handle individual track layout pages: /config/gleisbilder/<page>.cs2
			string pageName = path.substr(20); // Remove "/config/gleisbilder/"
			// Remove .cs2 extension if present
			if (pageName.size() > 4 && pageName.substr(pageName.size() - 4) == ".cs2")
			{
				pageName = pageName.substr(0, pageName.size() - 4);
			}
			// Parse page number, default to layer 1
			signed char pageNr = 1;
			try
			{
				pageNr = static_cast<signed char>(std::stoi(pageName));
			}
			catch (...)
			{
				// Keep default
			}
			return manager.GetCs2GBS(pageNr);
		}
		else if (path == "/config/geraet.vrs")
		{
			// Device information for CS2 discovery
			// Format according to can2udp (known working implementation)
			string body = "[geraet]\n";
			body += "version\n";
			body += " .minor=1\n";
			body += "geraet\n";
			body += " .sernum=1\n";
			body += " .hardvers=RailControl,1\n";
			return body;
		}
		else if (path == "/config/lokomotive.sr2")
		{
			// Locomotive status file
			string body = "[lokstatus]\n";
			body += "version\n";
			body += " .minor=1\n";
			return body;
		}
		else if (path == "/config/magnetartikel.sr2")
		{
			// Accessory status file
			string body = "[magnetartikel]\n";
			body += "version\n";
			body += " .minor=1\n";
			return body;
		}
		else if (path == "/config/fahrstrassen.sr2")
		{
			// Routes status file
			string body = "[fahrstrassen]\n";
			body += "version\n";
			body += " .minor=1\n";
			return body;
		}
		else if (path == "/config/gleisbild.sr2")
		{
			// Track layout status file
			string body = "[gleisbild]\n";
			body += "version\n";
			body += " .minor=1\n";
			return body;
		}

		// Unknown path
		return "";
	}
}} // namespace Server::CS2
