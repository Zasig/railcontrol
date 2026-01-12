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

		// Route: /layout - serve the layout UI
		if (uri == "/layout" || uri == "/layout.html")
		{
			if (method == "GET")
			{
				ServeLayoutUI();
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
            justify-content: center;
            align-items: center;
            color: #fff;
        }
        .container {
            background: rgba(255, 255, 255, 0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 40px;
            text-align: center;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        h1 { margin-bottom: 10px; font-size: 1.8rem; }
        .subtitle { color: rgba(255, 255, 255, 0.6); margin-bottom: 30px; font-size: 0.9rem; }
        .status-container { margin-bottom: 30px; }
        .status-label { font-size: 0.9rem; color: rgba(255, 255, 255, 0.7); margin-bottom: 10px; }
        .status-indicator {
            display: inline-flex;
            align-items: center;
            gap: 12px;
            padding: 15px 30px;
            border-radius: 50px;
            font-size: 1.4rem;
            font-weight: bold;
            transition: all 0.3s ease;
        }
        .status-indicator.stop {
            background: linear-gradient(135deg, #ff4757 0%, #c0392b 100%);
            box-shadow: 0 4px 20px rgba(255, 71, 87, 0.4);
        }
        .status-indicator.go {
            background: linear-gradient(135deg, #2ed573 0%, #27ae60 100%);
            box-shadow: 0 4px 20px rgba(46, 213, 115, 0.4);
        }
        .status-indicator.loading {
            background: linear-gradient(135deg, #ffa502 0%, #e67e22 100%);
            box-shadow: 0 4px 20px rgba(255, 165, 2, 0.4);
        }
        .status-dot {
            width: 16px; height: 16px;
            border-radius: 50%;
            background: #fff;
            animation: pulse 2s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; transform: scale(1); }
            50% { opacity: 0.7; transform: scale(1.1); }
        }
        .buttons { display: flex; gap: 20px; justify-content: center; }
        button {
            padding: 15px 40px;
            font-size: 1.1rem;
            font-weight: bold;
            border: none;
            border-radius: 12px;
            cursor: pointer;
            transition: all 0.2s ease;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        button:hover { transform: translateY(-2px); }
        button:active { transform: translateY(0); }
        button:disabled { opacity: 0.5; cursor: not-allowed; transform: none; }
        .btn-go {
            background: linear-gradient(135deg, #2ed573 0%, #27ae60 100%);
            color: #fff;
            box-shadow: 0 4px 15px rgba(46, 213, 115, 0.3);
        }
        .btn-go:hover:not(:disabled) { box-shadow: 0 6px 20px rgba(46, 213, 115, 0.5); }
        .btn-stop {
            background: linear-gradient(135deg, #ff4757 0%, #c0392b 100%);
            color: #fff;
            box-shadow: 0 4px 15px rgba(255, 71, 87, 0.3);
        }
        .btn-stop:hover:not(:disabled) { box-shadow: 0 6px 20px rgba(255, 71, 87, 0.5); }
        .error {
            margin-top: 20px;
            padding: 10px 20px;
            background: rgba(255, 71, 87, 0.2);
            border-radius: 8px;
            color: #ff6b7a;
            display: none;
        }
        .error.show { display: block; }
        .refresh-info { margin-top: 20px; font-size: 0.8rem; color: rgba(255, 255, 255, 0.4); }
    </style>
</head>
<body>
    <div class="container">
        <h1>&#128642; RailControl</h1>
        <p class="subtitle">Booster-Steuerung</p>
        <div class="status-container">
            <div class="status-label">Aktueller Status</div>
            <div id="status" class="status-indicator loading">
                <span class="status-dot"></span>
                <span id="status-text">Laden...</span>
            </div>
        </div>
        <div class="buttons">
            <button class="btn-stop" id="btn-stop" onclick="setBooster('stop')">Stop</button>
            <button class="btn-go" id="btn-go" onclick="setBooster('go')">Go</button>
        </div>
        <div id="error" class="error"></div>
        <div class="refresh-info">Status wird alle 2 Sekunden aktualisiert</div>
    </div>
    <script>
        const API_BASE = window.location.origin;
        async function getBoosterStatus() {
            try {
                const response = await fetch(API_BASE + '/api/v1/booster');
                if (!response.ok) throw new Error('Server nicht erreichbar');
                const data = await response.json();
                updateStatusDisplay(data.state);
                hideError();
            } catch (error) {
                showError('Verbindungsfehler: ' + error.message);
            }
        }
        async function setBooster(state) {
            const btnGo = document.getElementById('btn-go');
            const btnStop = document.getElementById('btn-stop');
            btnGo.disabled = true;
            btnStop.disabled = true;
            try {
                const response = await fetch(API_BASE + '/api/v1/booster', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ state: state })
                });
                if (!response.ok) throw new Error('Fehler beim Senden');
                const data = await response.json();
                updateStatusDisplay(data.state);
                hideError();
            } catch (error) {
                showError('Fehler: ' + error.message);
            } finally {
                btnGo.disabled = false;
                btnStop.disabled = false;
            }
        }
        function updateStatusDisplay(state) {
            const statusEl = document.getElementById('status');
            const statusText = document.getElementById('status-text');
            statusEl.classList.remove('stop', 'go', 'loading');
            if (state === 'go') {
                statusEl.classList.add('go');
                statusText.textContent = 'GO';
            } else if (state === 'stop') {
                statusEl.classList.add('stop');
                statusText.textContent = 'STOP';
            } else {
                statusEl.classList.add('loading');
                statusText.textContent = 'Unbekannt';
            }
        }
        function showError(message) {
            const errorEl = document.getElementById('error');
            errorEl.textContent = message;
            errorEl.classList.add('show');
        }
        function hideError() {
            document.getElementById('error').classList.remove('show');
        }
        getBoosterStatus();
        setInterval(getBoosterStatus, 2000);
    </script>
</body>
</html>)HTML";
		SendHtmlResponse(html);
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
				<< ",\"blocked\":" << (blocked ? "true" : "false");
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

	void ApiClient::ServeLayoutUI()
	{
		static const string html = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>RailControl - Gleisbild</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #1a1a2e;
            color: #fff;
            overflow: hidden;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 10px 20px;
            background: rgba(255,255,255,0.1);
            border-bottom: 1px solid rgba(255,255,255,0.1);
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
        .layout-container {
            position: relative;
            width: 100%;
            height: calc(100vh - 60px);
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
    <div class="layout-container">
        <div id="layout" class="layout"></div>
    </div>
    <div id="tooltip" class="tooltip"></div>

    <script>
        const API_BASE = window.location.origin;
        const CELL_SIZE = 36;
        let currentLayer = 1;

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

                // Calculate translate for rotation (like original RailControl)
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
                        stateClass = item.state ? 'switch-thrown' : '';
                        svg = renderSwitch(item, item.rotation, translate);
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

                // Add loco name for reserved tracks
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
            const transform = 'transform="rotate('+rot+') translate('+translate+','+translate+')" transform-origin="center"';
            switch(item.trackType) {
                case 1: // Turn
                    return '<svg viewBox="0 0 36 36" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="0,21 0,15 21,36 15,36"/></svg>';
                case 2: // End
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,5 21,5 21,'+h+' 15,'+h+'"/><polygon class="track" points="4,10 4,5 32,5 32,10"/></svg>';
                case 3: // Bridge
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,0 21,0 21,'+h+' 15,'+h+'"/><polygon class="track" points="10,3 12,5 12,'+(h-5)+' 10,'+(h-3)+'"/><polygon class="track" points="26,3 24,5 24,'+(h-5)+' 26,'+(h-3)+'"/></svg>';
                case 6: // Link
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,22 21,22 21,'+h+' 15,'+h+'"/><polygon class="track" points="18,1 6,22 30,22"/></svg>';
                default: // Straight
                    return '<svg viewBox="0 0 36 '+h+'" width="'+w+'" height="'+h+'" style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"><polygon class="track" points="15,0 21,0 21,'+h+' 15,'+h+'"/></svg>';
            }
        }

        function renderSwitch(item, rotation, translate) {
            const rot = rotation * 90;
            const type = item.switchType || 0;
            const style = 'style="transform:rotate('+rot+'deg) translate('+translate+'px,'+translate+'px);"';
            if (type === 1) { // Right
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
        loadLayout();
        getBoosterStatus();
        setInterval(() => { loadLayout(); getBoosterStatus(); }, 2000);
    </script>
</body>
</html>)HTML";
		SendHtmlResponse(html);
	}
}} // namespace Server::Api

