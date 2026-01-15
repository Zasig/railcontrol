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
#include <deque>
#include <sstream>

#include "DataModel/Accessory.h"
#include "DataModel/Feedback.h"
#include "DataModel/Loco.h"
#include "DataModel/Route.h"
#include "DataModel/Signal.h"
#include "DataModel/Switch.h"
#include "DataModel/Track.h"
#include "Languages.h"
#include "Server/Api/ApiClient.h"
#include "Server/Api/ApiServer.h"
#include "Utils/Utils.h"

using std::deque;
using std::string;
using std::stringstream;
using std::to_string;

namespace Server { namespace Api
{
	// Helper function to escape strings for JSON
	static string JsonEscape(const string& input)
	{
		string output;
		output.reserve(input.size() + 10);
		for (char c : input)
		{
			switch (c)
			{
				case '"': output += "\\\""; break;
				case '\\': output += "\\\\"; break;
				case '\b': output += "\\b"; break;
				case '\f': output += "\\f"; break;
				case '\n': output += "\\n"; break;
				case '\r': output += "\\r"; break;
				case '\t': output += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						// Control characters
						char buf[8];
						snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
						output += buf;
					}
					else
					{
						output += c;
					}
					break;
			}
		}
		return output;
	}

	ApiClient::ApiClient(const unsigned int id,
		Network::TcpConnection* connection,
		ApiServer& server,
		Manager& manager)
	:	logger(Logger::Logger::GetLogger("ApiClient # " + to_string(id))),
		id(id),
		connection(connection),
		run(false),
		terminated(false),
		server(server),
		clientThread(&ApiClient::Worker, this),
		manager(manager)
	{
	}

	ApiClient::~ApiClient()
	{
		run = false;
		clientThread.join();
		delete connection;
	}

	void ApiClient::Worker()
	{
		Utils::Utils::SetThreadName("ApiClient");
		logger->Debug(Languages::TextTcpConnectionEstablished, connection->AddressAsString());
		WorkerImpl();
		logger->Debug(Languages::TextTcpConnectionClosed, connection->AddressAsString());
		terminated = true;
	}

	void ApiClient::WorkerImpl()
	{
		run = true;

		while (run)
		{
			const int BufferSize = 8192;
			char buffer[BufferSize];
			memset(buffer, 0, BufferSize);

			int pos = 0;
			string requestData;
			while (pos < BufferSize - 1 && requestData.find("\r\n\r\n") == string::npos && run)
			{
				int ret = connection->Receive(buffer + pos, BufferSize - 1 - pos, 0);
				if (ret == -1)
				{
					if (errno != ETIMEDOUT)
					{
						return;
					}
					if (run == false)
					{
						return;
					}
					continue;
				}
				if (ret == 0)
				{
					return; // Connection closed
				}
				pos += ret;
				requestData = string(buffer);
			}

			if (requestData.empty())
			{
				return;
			}

			// Parse HTTP request
			deque<string> lines;
			Utils::Utils::SplitString(requestData, string("\r\n"), lines);

			if (lines.empty())
			{
				return;
			}

			// Parse request line: "GET /api/v1/booster HTTP/1.1"
			deque<string> requestLine;
			Utils::Utils::SplitString(lines[0], string(" "), requestLine);

			if (requestLine.size() < 2)
			{
				SendResponse(400, "Bad Request", "{\"error\":\"Invalid request\"}");
				return;
			}

			string method = requestLine[0];
			string uri = requestLine[1];

			// Find body (after empty line)
			string body;
			size_t bodyStart = requestData.find("\r\n\r\n");
			if (bodyStart != string::npos)
			{
				body = requestData.substr(bodyStart + 4);
			}

			logger->Info(Languages::TextHttpRequest, method, uri);

			HandleRequest(method, uri, body);

			// HTTP/1.0 style: close after response
			return;
		}
	}

	void ApiClient::SendResponse(int statusCode, const std::string& statusText, const std::string& body, const std::string& contentType)
	{
		stringstream response;
		response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
		response << "Content-Type: " << contentType << "\r\n";
		response << "Access-Control-Allow-Origin: *\r\n";
		response << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
		response << "Access-Control-Allow-Headers: Content-Type\r\n";
		response << "Content-Length: " << body.length() << "\r\n";
		response << "Connection: close\r\n";
		response << "\r\n";
		response << body;

		string responseStr = response.str();
		connection->Send(responseStr.c_str(), responseStr.length());
	}

	void ApiClient::SendHtmlResponse(const std::string& html)
	{
		SendResponse(200, "OK", html, "text/html; charset=utf-8");
	}

	void ApiClient::SendJsonResponse(int statusCode, const std::string& json)
	{
		string statusText;
		switch (statusCode)
		{
			case 200: statusText = "OK"; break;
			case 201: statusText = "Created"; break;
			case 400: statusText = "Bad Request"; break;
			case 404: statusText = "Not Found"; break;
			case 405: statusText = "Method Not Allowed"; break;
			case 500: statusText = "Internal Server Error"; break;
			default: statusText = "Unknown"; break;
		}
		SendResponse(statusCode, statusText, json);
	}

	void ApiClient::HandleRequest(const std::string& method, const std::string& uri, const std::string& body)
	{
		// Handle CORS preflight
		if (method == "OPTIONS")
		{
			SendResponse(200, "OK", "");
			return;
		}

		// Route: / or /ui - serve the web UI
		if (uri == "/" || uri == "/ui" || uri == "/index.html")
		{
			if (method == "GET")
			{
				ServeHtmlUI();
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/booster
		if (uri == "/api/v1/booster")
		{
			if (method == "GET")
			{
				HandleBoosterGet();
			}
			else if (method == "POST")
			{
				HandleBoosterPost(body);
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/status (basic health check)
		if (uri == "/api/v1/status")
		{
			if (method == "GET")
			{
				BoosterState state = manager.Booster();
				stringstream json;
				json << "{";
				json << "\"status\":\"ok\",";
				json << "\"booster\":\"" << (state == BoosterStateGo ? "go" : "stop") << "\"";
				json << "}";
				SendJsonResponse(200, json.str());
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/layers
		if (uri == "/api/v1/layers")
		{
			if (method == "GET")
			{
				HandleLayersGet();
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/layout or /api/v1/layout?layer=X
		if (uri.find("/api/v1/layout") == 0)
		{
			if (method == "GET")
			{
				HandleLayoutGet(uri);
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/locos
		if (uri == "/api/v1/locos")
		{
			if (method == "GET")
			{
				HandleLocosGet();
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/locos/{id}
		if (uri.find("/api/v1/locos/") == 0 && uri.length() > 14)
		{
			if (method == "GET")
			{
				HandleLocoDetailsGet(uri);
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/switches/{id}/toggle - toggle switch state
		if (uri.find("/api/v1/switches/") == 0 && uri.find("/toggle") != string::npos)
		{
			if (method == "POST")
			{
				HandleSwitchToggle(uri);
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/routes - get all routes
		if (uri == "/api/v1/routes")
		{
			if (method == "GET")
			{
				HandleRoutesGet();
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Route: /api/v1/routes/{id}/execute - execute a route
		if (uri.find("/api/v1/routes/") == 0 && uri.find("/execute") != string::npos)
		{
			if (method == "POST")
			{
				HandleRouteExecute(uri);
			}
			else
			{
				SendJsonResponse(405, "{\"error\":\"Method not allowed\"}");
			}
			return;
		}

		// Not found
		SendJsonResponse(404, "{\"error\":\"Endpoint not found\"}");
	}

	void ApiClient::HandleBoosterGet()
	{
		BoosterState state = manager.Booster();
		stringstream json;
		json << "{\"state\":\"" << (state == BoosterStateGo ? "go" : "stop") << "\"}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::HandleBoosterPost(const std::string& body)
	{
		// Simple JSON parsing for {"state":"go"} or {"state":"stop"}
		// Not using external library for minimal demo

		BoosterState newState;

		if (body.find("\"go\"") != string::npos || body.find("\"Go\"") != string::npos)
		{
			newState = BoosterStateGo;
		}
		else if (body.find("\"stop\"") != string::npos || body.find("\"Stop\"") != string::npos)
		{
			newState = BoosterStateStop;
		}
		else
		{
			SendJsonResponse(400, "{\"error\":\"Invalid state. Use 'go' or 'stop'\"}");
			return;
		}

		manager.Booster(ControlTypeApiServer, newState);

		stringstream json;
		json << "{\"state\":\"" << (newState == BoosterStateGo ? "go" : "stop") << "\",\"success\":true}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::HandleLayersGet()
	{
		stringstream json;
		json << "{\"layers\":[";

		const std::map<std::string,LayerID> layers = manager.LayerListByName();
		bool first = true;
		for (const auto& layer : layers)
		{
			if (!first) json << ",";
			first = false;
			json << "{\"id\":" << layer.second << ",\"name\":\"" << JsonEscape(layer.first) << "\"}";
		}
		json << "]}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::HandleLayoutGet(const std::string& uri)
	{
		// Parse layer parameter from URI
		LayerID layerId = 1; // Default layer
		size_t pos = uri.find("layer=");
		if (pos != string::npos)
		{
			layerId = std::stoi(uri.substr(pos + 6));
		}

		stringstream json;
		json << "{\"layer\":" << layerId << ",\"items\":[";

		bool first = true;

		// Tracks
		const std::map<TrackID, DataModel::Track*>& tracks = manager.TrackList();
		for (const auto& track : tracks)
		{
			if (!track.second->IsVisibleOnLayer(layerId)) continue;
			if (!first) json << ",";
			first = false;

			const DataModel::ObjectIdentifier locoId = track.second->GetLocoBase();
			const bool occupied = track.second->GetMainStateDelayed() == DataModel::Feedback::FeedbackStateOccupied;
			const bool reserved = locoId.IsSet();
			const bool blocked = track.second->GetBlocked();
			const bool showName = track.second->GetShowName();

			json << "{\"type\":\"track\""
				<< ",\"id\":" << track.second->GetID()
				<< ",\"name\":\"" << JsonEscape(track.second->GetName()) << "\""
				<< ",\"posX\":" << static_cast<int>(track.second->GetPosX())
				<< ",\"posY\":" << static_cast<int>(track.second->GetPosY())
				<< ",\"width\":" << static_cast<int>(track.second->GetWidth())
				<< ",\"height\":" << static_cast<int>(track.second->GetHeight())
				<< ",\"rotation\":" << static_cast<int>(track.second->GetRotation())
				<< ",\"trackType\":" << static_cast<int>(track.second->GetTrackType())
				<< ",\"occupied\":" << (occupied ? "true" : "false")
				<< ",\"reserved\":" << (reserved ? "true" : "false")
				<< ",\"blocked\":" << (blocked ? "true" : "false")
				<< ",\"showName\":" << (showName ? "true" : "false");
			if (showName)
			{
				const std::string& displayName = track.second->GetMainDisplayName();
				json << ",\"displayName\":\"" << JsonEscape(displayName.size() ? displayName : track.second->GetName()) << "\"";
			}
			if (reserved)
			{
				json << ",\"locoName\":\"" << JsonEscape(manager.GetLocoBaseName(locoId)) << "\"";
			}
			json << "}";
		}

		// Switches
		const std::map<SwitchID, DataModel::Switch*>& switches = manager.SwitchList();
		for (const auto& sw : switches)
		{
			if (!sw.second->IsVisibleOnLayer(layerId)) continue;
			if (!first) json << ",";
			first = false;

			json << "{\"type\":\"switch\""
				<< ",\"id\":" << sw.second->GetID()
				<< ",\"name\":\"" << JsonEscape(sw.second->GetName()) << "\""
				<< ",\"posX\":" << static_cast<int>(sw.second->GetPosX())
				<< ",\"posY\":" << static_cast<int>(sw.second->GetPosY())
				<< ",\"rotation\":" << static_cast<int>(sw.second->GetRotation())
				<< ",\"switchType\":" << static_cast<int>(sw.second->GetAccessoryType())
				<< ",\"state\":" << static_cast<int>(sw.second->GetAccessoryState())
				<< "}";
		}

		// Signals
		const std::map<SignalID, DataModel::Signal*>& signals = manager.SignalList();
		for (const auto& sig : signals)
		{
			if (!sig.second->IsVisibleOnLayer(layerId)) continue;
			if (!first) json << ",";
			first = false;

			json << "{\"type\":\"signal\""
				<< ",\"id\":" << sig.second->GetID()
				<< ",\"name\":\"" << JsonEscape(sig.second->GetName()) << "\""
				<< ",\"posX\":" << static_cast<int>(sig.second->GetPosX())
				<< ",\"posY\":" << static_cast<int>(sig.second->GetPosY())
				<< ",\"height\":" << static_cast<int>(sig.second->GetHeight())
				<< ",\"rotation\":" << static_cast<int>(sig.second->GetRotation())
				<< ",\"signalType\":" << static_cast<int>(sig.second->GetAccessoryType())
				<< ",\"state\":" << static_cast<int>(sig.second->GetAccessoryState())
				<< "}";
		}

		// Feedbacks
		const std::map<FeedbackID, DataModel::Feedback*>& feedbacks = manager.FeedbackList();
		for (const auto& fb : feedbacks)
		{
			if (!fb.second->IsVisibleOnLayer(layerId)) continue;
			if (!first) json << ",";
			first = false;

			json << "{\"type\":\"feedback\""
				<< ",\"id\":" << fb.second->GetID()
				<< ",\"name\":\"" << JsonEscape(fb.second->GetName()) << "\""
				<< ",\"posX\":" << static_cast<int>(fb.second->GetPosX())
				<< ",\"posY\":" << static_cast<int>(fb.second->GetPosY())
				<< ",\"rotation\":" << static_cast<int>(fb.second->GetRotation())
				<< ",\"state\":" << (fb.second->GetState() == DataModel::Feedback::FeedbackStateOccupied ? "true" : "false")
				<< "}";
		}

		// Accessories
		const std::map<AccessoryID, DataModel::Accessory*>& accessories = manager.AccessoryList();
		for (const auto& acc : accessories)
		{
			if (!acc.second->IsVisibleOnLayer(layerId)) continue;
			if (!first) json << ",";
			first = false;

			json << "{\"type\":\"accessory\""
				<< ",\"id\":" << acc.second->GetID()
				<< ",\"name\":\"" << JsonEscape(acc.second->GetName()) << "\""
				<< ",\"posX\":" << static_cast<int>(acc.second->GetPosX())
				<< ",\"posY\":" << static_cast<int>(acc.second->GetPosY())
				<< ",\"rotation\":" << static_cast<int>(acc.second->GetRotation())
				<< ",\"accessoryType\":" << static_cast<int>(acc.second->GetAccessoryType())
				<< ",\"state\":" << static_cast<int>(acc.second->GetAccessoryState())
				<< "}";
		}

		// Routes
		const std::map<RouteID, DataModel::Route*>& routes = manager.RouteList();
		for (const auto& route : routes)
		{
			if (!route.second->IsVisibleOnLayer(layerId)) continue;
			if (!first) json << ",";
			first = false;

			json << "{\"type\":\"route\""
				<< ",\"id\":" << route.second->GetID()
				<< ",\"name\":\"" << JsonEscape(route.second->GetName()) << "\""
				<< ",\"posX\":" << static_cast<int>(route.second->GetPosX())
				<< ",\"posY\":" << static_cast<int>(route.second->GetPosY())
				<< ",\"rotation\":" << static_cast<int>(route.second->GetRotation())
				<< "}";
		}

		json << "]}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::HandleLocosGet()
	{
		stringstream json;
		json << "{\"locos\":[";

		const std::map<std::string,LocoID> locos = manager.LocoIdsByName();
		bool first = true;
		for (const auto& loco : locos)
		{
			DataModel::Loco* l = manager.GetLoco(loco.second);
			if (!l) continue;

			if (!first) json << ",";
			first = false;
			json << "{\"id\":" << loco.second
				<< ",\"name\":\"" << JsonEscape(loco.first) << "\""
				<< ",\"speed\":" << l->GetSpeed()
				<< ",\"orientation\":\"" << (l->GetOrientation() == OrientationRight ? "right" : "left") << "\""
				<< "}";
		}
		json << "]}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::HandleLocoDetailsGet(const std::string& uri)
	{
		// Extract loco ID from URI (/api/v1/locos/123)
		// "/api/v1/locos/" has 14 characters
		std::string idStr = uri.substr(14); // Skip "/api/v1/locos/"
		size_t queryPos = idStr.find('?');
		if (queryPos != std::string::npos)
		{
			idStr = idStr.substr(0, queryPos);
		}

		LocoID locoId;
		try
		{
			locoId = std::stoi(idStr);
		}
		catch (...)
		{
			SendJsonResponse(400, "{\"error\":\"Invalid loco ID\"}");
			return;
		}

		DataModel::Loco* loco = manager.GetLoco(locoId);
		if (!loco)
		{
			SendJsonResponse(404, "{\"error\":\"Loco not found\"}");
			return;
		}

		stringstream json;
		json << "{"
			<< "\"id\":" << locoId
			<< ",\"name\":\"" << JsonEscape(loco->GetName()) << "\""
			<< ",\"speed\":" << loco->GetSpeed()
			<< ",\"orientation\":\"" << (loco->GetOrientation() == OrientationRight ? "right" : "left") << "\""
			<< ",\"functions\":[";

		// Get only configured functions using GetFunctionStates()
		const std::vector<DataModel::LocoFunctionEntry> functionStates = loco->GetFunctionStates();
		bool first = true;
		for (const auto& entry : functionStates)
		{
			if (!first) json << ",";
			first = false;

			json << "{"
				<< "\"nr\":" << static_cast<int>(entry.nr)
				<< ",\"icon\":" << static_cast<int>(entry.icon)
				<< ",\"type\":" << static_cast<int>(entry.type)
				<< ",\"state\":" << (entry.state == DataModel::LocoFunctionStateOn ? "true" : "false")
				<< "}";
		}

		json << "]}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::ServeHtmlUI()
	{
		static const string html = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RailControl API - Booster</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            color: #fff;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 10px 20px;
            background: rgba(255,255,255,0.1);
            border-bottom: 1px solid rgba(255,255,255,0.1);
            flex-shrink: 0;
        }
        .header h1 { font-size: 1.2rem; }
        .header-controls { display: flex; gap: 10px; align-items: center; }
        .booster-btn {
            padding: 8px 20px;
            border: none;
            border-radius: 8px;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.2s;
        }
        .booster-btn.go { background: #2ed573; color: #fff; }
        .booster-btn.stop { background: #ff4757; color: #fff; }
        .booster-btn:hover { transform: scale(1.05); }
        .booster-status {
            padding: 8px 16px;
            border-radius: 8px;
            font-weight: bold;
        }
        .booster-status.on { background: #2ed573; }
        .booster-status.off { background: #ff4757; }
        select {
            padding: 8px 12px;
            border-radius: 8px;
            border: 1px solid rgba(255,255,255,0.2);
            background: rgba(255,255,255,0.1);
            color: #fff;
            cursor: pointer;
        }
        .main-content {
            display: flex;
            flex: 1;
            overflow: hidden;
        }
        .routes-sidebar {
            width: 220px;
            background: rgba(0,0,0,0.3);
            border-right: 1px solid rgba(255,255,255,0.1);
            overflow-y: auto;
            flex-shrink: 0;
        }
        .routes-header {
            padding: 15px;
            background: rgba(255,255,255,0.05);
            border-bottom: 1px solid rgba(255,255,255,0.1);
            font-weight: bold;
            font-size: 0.9rem;
        }
        .route-item {
            padding: 12px 15px;
            border-bottom: 1px solid rgba(255,255,255,0.05);
            cursor: pointer;
            transition: background 0.2s;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .route-item:hover {
            background: rgba(255,255,255,0.1);
        }
        .route-item.executing {
            background: rgba(46, 213, 115, 0.3);
        }
        .route-icon {
            width: 24px;
            height: 24px;
            background: #4a69bd;
            border-radius: 4px;
            display: flex;
            align-items: center;
            justify-content: center;
            font-size: 12px;
        }
        .route-name {
            flex: 1;
            font-size: 0.85rem;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }
        .layout-container {
            flex: 1;
            position: relative;
            overflow: auto;
            background: #16213e;
        }
        .layout {
            position: relative;
            min-width: 2000px;
            min-height: 1500px;
        }
        .layout-item {
            position: absolute;
            width: 36px;
            height: 36px;
        }
        .layout-item svg {
            width: 100%;
            height: 100%;
        }
        .layout-item.switch-item {
            cursor: pointer;
        }
        .layout-item.switch-item:hover {
            filter: brightness(1.3);
        }
        .track { fill: #666; stroke: none; }
        .track-free .track { fill: #888; }
        .track-occupied .track { fill: #ff4757; }
        .track-reserved .track { fill: #ffa502; }
        .track-reserved-occupied .track { fill: #ff6b81; }
        .track-blocked .track { fill: #555; }
        .switch { fill: #888; }
        .switch-thrown .switch { fill: #2ed573; }
        .signal { fill: #888; }
        .signal-stop { fill: #ff4757; }
        .signal-go { fill: #2ed573; }
        .feedback { fill: #444; stroke: #666; stroke-width: 2; }
        .feedback-occupied { fill: #ff4757; }
        .route { fill: #4a69bd; }
        .accessory { fill: #888; }
        .item-name {
            position: absolute;
            font-size: 10px;
            color: #fff;
            white-space: nowrap;
            text-shadow: 1px 1px 2px #000;
            pointer-events: none;
        }
        .loco-name {
            position: absolute;
            font-size: 9px;
            color: #ffa502;
            white-space: nowrap;
            text-shadow: 1px 1px 2px #000;
            pointer-events: none;
        }
        .tooltip {
            position: absolute;
            background: rgba(0,0,0,0.9);
            color: #fff;
            padding: 5px 10px;
            border-radius: 4px;
            font-size: 12px;
            pointer-events: none;
            z-index: 1000;
            display: none;
        }
        .nav-link {
            color: #fff;
            text-decoration: none;
            padding: 8px 16px;
            background: rgba(255,255,255,0.1);
            border-radius: 8px;
        }
        .nav-link:hover { background: rgba(255,255,255,0.2); }
        .notification {
            position: fixed;
            bottom: 20px;
            right: 20px;
            padding: 12px 20px;
            background: #2ed573;
            color: #fff;
            border-radius: 8px;
            font-weight: bold;
            z-index: 2000;
            animation: slideIn 0.3s ease;
        }
        .notification.error { background: #ff4757; }
        @keyframes slideIn {
            from { transform: translateX(100%); opacity: 0; }
            to { transform: translateX(0); opacity: 1; }
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>&#128642; RailControl Gleisbild</h1>
        <div class="header-controls">
            <a href="/" class="nav-link">Booster</a>
            <select id="layer-select" onchange="loadLayout()"></select>
            <span id="booster-status" class="booster-status off">STOP</span>
            <button class="booster-btn stop" onclick="setBooster('stop')">Stop</button>
            <button class="booster-btn go" onclick="setBooster('go')">Go</button>
        </div>
    </div>
    <div class="main-content">
        <div class="routes-sidebar">
            <div class="routes-header">&#128739; Fahrstrassen</div>
            <div id="routes-list"></div>
        </div>
        <div class="layout-container">
            <div id="layout" class="layout"></div>
        </div>
    </div>
    <div id="tooltip" class="tooltip"></div>

    <script>
        const API_BASE = window.location.origin;
        const CELL_SIZE = 36;
        let currentLayer = 1;
        let routes = [];

        async function loadLayers() {
            try {
                const response = await fetch(API_BASE + '/api/v1/layers');
                const data = await response.json();
                const select = document.getElementById('layer-select');
                select.innerHTML = '';
                data.layers.forEach(layer => {
                    const option = document.createElement('option');
                    option.value = layer.id;
                    option.textContent = layer.name;
                    if (layer.id === currentLayer) option.selected = true;
                    select.appendChild(option);
                });
            } catch (e) { console.error('Error loading layers:', e); }
        }

        async function loadRoutes() {
            try {
                const response = await fetch(API_BASE + '/api/v1/routes');
                const data = await response.json();
                routes = data.routes;
                renderRoutes();
            } catch (e) { console.error('Error loading routes:', e); }
        }

        function renderRoutes() {
            const list = document.getElementById('routes-list');
            list.innerHTML = '';
            routes.forEach(route => {
                const item = document.createElement('div');
                item.className = 'route-item';
                item.innerHTML = '<div class="route-icon">&#128739;</div><div class="route-name">' + route.name + '</div>';
                item.onclick = () => executeRoute(route.id, route.name);
                list.appendChild(item);
            });
        }

        async function executeRoute(routeId, routeName) {
            try {
                showNotification('Fahrstrasse "' + routeName + '" wird ausgeführt...');
                const response = await fetch(API_BASE + '/api/v1/routes/' + routeId + '/execute', {
                    method: 'POST'
                });
                const data = await response.json();
                if (data.success) {
                    showNotification('Fahrstrasse "' + routeName + '" aktiviert!');
                    loadLayout();
                } else {
                    showNotification('Fahrstrasse konnte nicht aktiviert werden!', true);
                }
            } catch (e) {
                console.error('Error executing route:', e);
                showNotification('Fehler beim Aktivieren der Fahrstrasse!', true);
            }
        }

        async function toggleSwitch(switchId) {
            try {
                const response = await fetch(API_BASE + '/api/v1/switches/' + switchId + '/toggle', {
                    method: 'POST'
                });
                const data = await response.json();
                if (data.success) {
                    loadLayout();
                }
            } catch (e) {
                console.error('Error toggling switch:', e);
                showNotification('Fehler beim Schalten der Weiche!', true);
            }
        }

        function showNotification(message, isError = false) {
            const existing = document.querySelector('.notification');
            if (existing) existing.remove();

            const notif = document.createElement('div');
            notif.className = 'notification' + (isError ? ' error' : '');
            notif.textContent = message;
            document.body.appendChild(notif);
            setTimeout(() => notif.remove(), 3000);
        }

        async function loadLayout() {
            currentLayer = parseInt(document.getElementById('layer-select').value) || 1;
            try {
                const response = await fetch(API_BASE + '/api/v1/layout?layer=' + currentLayer);
                const data = await response.json();
                renderLayout(data.items);
            } catch (e) { console.error('Error loading layout:', e); }
        }

        function renderLayout(items) {
            const layout = document.getElementById('layout');
            layout.innerHTML = '';

            let maxX = 0, maxY = 0;
            items.forEach(item => {
                maxX = Math.max(maxX, item.posX + (item.width || 1));
                maxY = Math.max(maxY, item.posY + (item.height || 1));
            });
            layout.style.minWidth = (maxX + 5) * CELL_SIZE + 'px';
            layout.style.minHeight = (maxY + 5) * CELL_SIZE + 'px';

            items.forEach(item => {
                const el = document.createElement('div');
                el.className = 'layout-item';
                el.style.left = item.posX * CELL_SIZE + 'px';
                el.style.top = item.posY * CELL_SIZE + 'px';

                const width = item.width || 1;
                const height = item.height || 1;
                el.style.width = (CELL_SIZE * width) + 'px';
                el.style.height = (CELL_SIZE * height) + 'px';

                let translate = 0;
                if (height > 1 || width > 1) {
                    if (item.rotation === 1 || item.rotation === 3) {
                        translate = ((height - width) * CELL_SIZE) / 2;
                    }
                    if (item.rotation === 1) {
                        translate = -translate;
                    }
                }

                let svg = '';
                let stateClass = '';

                switch(item.type) {
                    case 'track':
                        stateClass = getTrackStateClass(item);
                        svg = renderTrack(item, width, height, item.rotation, translate);
                        break;
                    case 'switch':
                        el.classList.add('switch-item');
                        stateClass = item.state ? 'switch-thrown' : '';
                        svg = renderSwitch(item, item.rotation, translate);
                        el.onclick = () => toggleSwitch(item.id);
                        break;
                    case 'signal':
                        stateClass = item.state === 0 ? 'signal-stop' : 'signal-go';
                        svg = renderSignal(item, height, item.rotation, translate);
                        break;
                    case 'feedback':
                        stateClass = item.state ? 'feedback-occupied' : '';
                        svg = renderFeedback(item.rotation, translate);
                        break;
                    case 'route':
                        svg = renderRoute(item.rotation, translate);
                        break;
                    case 'accessory':
                        svg = renderAccessory(item.rotation, translate);
                        break;
                }

                el.className += ' ' + stateClass;
                el.innerHTML = svg;
                el.title = item.name;

                el.addEventListener('mouseenter', (e) => showTooltip(e, item));
                el.addEventListener('mouseleave', hideTooltip);

                layout.appendChild(el);

                if (item.type === 'track' && item.locoName) {
                    const locoLabel = document.createElement('div');
                    locoLabel.className = 'loco-name';
                    locoLabel.textContent = item.locoName;
                    locoLabel.style.left = (item.posX * CELL_SIZE + 2) + 'px';
                    locoLabel.style.top = (item.posY * CELL_SIZE + CELL_SIZE * height - 12) + 'px';
                    layout.appendChild(locoLabel);
                }
            });
        }

        function getTrackStateClass(item) {
            if (item.reserved && item.occupied) return 'track-reserved-occupied';
            if (item.reserved) return 'track-reserved';
            if (item.occupied) return 'track-occupied';
            if (item.blocked) return 'track-blocked';
            return 'track-free';
        }

        function renderTrack(item, width, height, rotation, translate) {
            const w = width * CELL_SIZE;
            const h = height * CELL_SIZE;
            const rot = rotation * 90;
            switch(item.trackType) {
                case 1:
                    return '<svg viewBox="0 0 36 36" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="0,21 0,15 21,36 15,36"/></svg>';
                case 2:
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,5 21,5 21,'+h+' 15,'+h+'"/><polygon class="track" points="4,10 4,5 32,5 32,10"/></svg>';
                case 3:
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,0 21,0 21,'+h+' 15,'+h+'"/><polygon class="track" points="10,3 12,5 12,'+(h-5)+' 10,'+(h-3)+'"/><polygon class="track" points="26,3 24,5 24,'+(h-5)+' 26,'+(h-3)+'"/></svg>';
                case 6:
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,22 21,22 21,'+h+' 15,'+h+'"/><polygon class="track" points="18,1 6,22 30,22"/></svg>';
                default:
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,0 21,0 21,'+h+' 15,'+h+'"/><polygon class="track" points="15,0 7,8 7,'+h+' 15,'+h+'"/><polygon class="track" points="21,0 29,8 29,'+h+' 21,'+h+'"/></svg>';
            }
        }

        function renderSwitch(item, rotation, translate) {
            const rot = rotation * 90;
            const type = item.switchType || 0;
            const style = 'style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"';
            if (type === 1) {
                return '<svg viewBox="0 0 36 36" '+style+'><polygon class="switch" points="15,0 21,0 21,36 15,36"/><polygon class="switch" points="21,15 36,0 36,6 21,21"/></svg>';
            }
            return '<svg viewBox="0 0 36 36" '+style+'><polygon class="switch" points="15,0 21,0 21,36 15,36"/><polygon class="switch" points="15,15 0,0 0,6 15,21"/></svg>';
        }

        function renderSignal(item, height, rotation, translate) {
            const h = height * CELL_SIZE;
            const rot = rotation * 90;
            return '<svg viewBox="0 0 36 '+h+'" width="36" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><rect class="signal" x="14" y="2" width="8" height="'+(h-4)+'" rx="2"/><circle class="signal" cx="18" cy="10" r="5"/></svg>';
        }

        function renderFeedback(rotation, translate) {
            const rot = rotation * 90;
            return '<svg viewBox="0 0 36 36" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><circle class="feedback" cx="18" cy="18" r="12"/></svg>';
        }

        function renderRoute(rotation, translate) {
            const rot = rotation * 90;
            return '<svg viewBox="0 0 36 36" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><rect class="route" x="4" y="4" width="28" height="28" rx="4"/></svg>';
        }

        function renderAccessory(rotation, translate) {
            const rot = rotation * 90;
            return '<svg viewBox="0 0 36 36" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><rect class="accessory" x="6" y="6" width="24" height="24" rx="3"/></svg>';
        }

        function showTooltip(e, item) {
            const tooltip = document.getElementById('tooltip');
            let text = item.name;
            if (item.type === 'switch') text += ' (Klicken zum Schalten)';
            if (item.locoName) text += ' - ' + item.locoName;
            if (item.occupied) text += ' [belegt]';
            if (item.blocked) text += ' [blockiert]';
            tooltip.textContent = text;
            tooltip.style.left = (e.pageX + 10) + 'px';
            tooltip.style.top = (e.pageY + 10) + 'px';
            tooltip.style.display = 'block';
        }

        function hideTooltip() {
            document.getElementById('tooltip').style.display = 'none';
        }

        async function getBoosterStatus() {
            try {
                const response = await fetch(API_BASE + '/api/v1/booster');
                const data = await response.json();
                const status = document.getElementById('booster-status');
                status.textContent = data.state.toUpperCase();
                status.className = 'booster-status ' + (data.state === 'go' ? 'on' : 'off');
            } catch (e) { console.error('Error:', e); }
        }

        async function setBooster(state) {
            try {
                await fetch(API_BASE + '/api/v1/booster', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ state: state })
                });
                getBoosterStatus();
            } catch (e) { console.error('Error:', e); }
        }

        // Initialize
        loadLayers();
        loadRoutes();
        loadLayout();
        getBoosterStatus();
        setInterval(() => { loadLayout(); getBoosterStatus(); }, 2000);
    </script>
</body>
</html>)HTML";
		SendHtmlResponse(html);
	}

	void ApiClient::HandleSwitchToggle(const std::string& uri)
	{
		// Extract switch ID from URI (/api/v1/switches/123/toggle)
		// "/api/v1/switches/" has 17 characters
		std::string idStr = uri.substr(17);
		size_t togglePos = idStr.find("/toggle");
		if (togglePos != std::string::npos)
		{
			idStr = idStr.substr(0, togglePos);
		}

		SwitchID switchId;
		try
		{
			switchId = std::stoi(idStr);
		}
		catch (...)
		{
			SendJsonResponse(400, "{\"error\":\"Invalid switch ID\"}");
			return;
		}

		DataModel::Switch* sw = manager.GetSwitch(switchId);
		if (!sw)
		{
			SendJsonResponse(404, "{\"error\":\"Switch not found\"}");
			return;
		}

		// Toggle switch state
		DataModel::AccessoryState currentState = sw->GetAccessoryState();
		DataModel::AccessoryState newState;

		// Toggle between straight and turnout
		if (currentState == DataModel::SwitchStateTurnout)
		{
			newState = DataModel::SwitchStateStraight;
		}
		else
		{
			newState = DataModel::SwitchStateTurnout;
		}

		manager.SwitchState(ControlTypeWebServer, switchId, newState, false);

		stringstream json;
		json << "{\"success\":true,\"id\":" << switchId << ",\"state\":" << static_cast<int>(newState) << "}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::HandleRoutesGet()
	{
		stringstream json;
		json << "{\"routes\":[";

		const std::map<RouteID, DataModel::Route*>& routes = manager.RouteList();
		bool first = true;
		for (const auto& route : routes)
		{
			if (!first) json << ",";
			first = false;

			json << "{\"id\":" << route.second->GetID()
				<< ",\"name\":\"" << JsonEscape(route.second->GetName()) << "\""
				<< ",\"automode\":" << (route.second->GetAutomode() ? "true" : "false")
				<< "}";
		}
		json << "]}";
		SendJsonResponse(200, json.str());
	}

	void ApiClient::HandleRouteExecute(const std::string& uri)
	{
		// Extract route ID from URI (/api/v1/routes/123/execute)
		// "/api/v1/routes/" has 15 characters
		std::string idStr = uri.substr(15);
		size_t executePos = idStr.find("/execute");
		if (executePos != std::string::npos)
		{
			idStr = idStr.substr(0, executePos);
		}

		RouteID routeId;
		try
		{
			routeId = std::stoi(idStr);
		}
		catch (...)
		{
			SendJsonResponse(400, "{\"error\":\"Invalid route ID\"}");
			return;
		}

		DataModel::Route* route = manager.GetRoute(routeId);
		if (!route)
		{
			SendJsonResponse(404, "{\"error\":\"Route not found\"}");
			return;
		}

		// Execute the route asynchronously
		manager.RouteExecuteAsync(logger, routeId);

		stringstream json;
		json << "{\"success\":true,\"id\":" << routeId << ",\"name\":\"" << JsonEscape(route->GetName()) << "\"}";
		SendJsonResponse(200, json.str());
	}

}} // namespace Server::Api

