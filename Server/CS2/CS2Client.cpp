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

#include <cstring>		//memset

#include "DataModel/AccessoryBase.h"
#include "DataModel/LocoBase.h"
#include "DataModel/ObjectIdentifier.h"
#include "Manager.h"
#include "Server/CS2/CS2Client.h"
#include "Utils/Integer.h"
#include "Utils/Utils.h"

using DataModel::ObjectIdentifier;

namespace Server { namespace CS2
{
	// worker is the thread that handles client requests
	void CS2Client::Receiver()
	{
		Utils::Utils::SetThreadName("CS2Client # " + std::to_string(id) + " receiver");
		logger->Debug(Languages::TextTcpConnectionEstablished, connection->AddressAsString());
		ReceiverImpl();
		logger->Debug(Languages::TextTcpConnectionClosed, connection->AddressAsString());
		terminated = true;
	}

	void CS2Client::ReceiverImpl()
	{
		while (run)
		{
			unsigned char buffer[CANCommandBufferLength];
			int ret = connection->ReceiveExact(buffer, sizeof(buffer), 0);

			if (!run)
			{
				return;
			}

			if (ret == -1)
			{
				if (errno != ETIMEDOUT)
				{
					logger->Error(Languages::TextErrorReadingData, strerror(errno));
					return;
				}
				continue;
			}

			if (ret == 0)
			{
				continue;
			}

			if (ret != 13)
			{
				logger->Error(Languages::TextErrorReadingData, strerror(errno));
			}

			Parse(buffer);
		}
	}

	void CS2Client::Send(const unsigned char* buffer)
	{
		if (connection->Send(buffer, CANCommandBufferLength) == -1)
		{
			logger->Error(Languages::TextUnableToSendDataToControl);
		}
	}

	void CS2Client::SendPowerOn()
	{
		unsigned char buffer[CANCommandBufferLength];
		CreateCommandHeader(buffer, CanCommandSystem, CanResponseResponse, 5);
		buffer[5] = 0x00;
		buffer[6] = 0x00;
		buffer[7] = 0x00;
		buffer[8] = 0x00;
		buffer[9] = CanSubCommandGo; // System Go
		logger->HexOut(buffer, CANCommandBufferLength);
		Send(buffer);
	}

	void CS2Client::SendPowerOff()
	{
		unsigned char buffer[CANCommandBufferLength];
		CreateCommandHeader(buffer, CanCommandSystem, CanResponseResponse, 5);
		buffer[5] = 0x00;
		buffer[6] = 0x00;
		buffer[7] = 0x00;
		buffer[8] = 0x00;
		buffer[9] = CanSubCommandStop; // System Stop
		logger->HexOut(buffer, CANCommandBufferLength);
		Send(buffer);
	}

	void CS2Client::SendLocoInfo(const DataModel::LocoBase* loco)
	{
		if (loco == nullptr)
		{
			return;
		}

		const Protocol protocol = loco->GetProtocol();
		const Address address = loco->GetAddress();

		// Send speed
		{
			unsigned char buffer[CANCommandBufferLength];
			CreateCommandHeader(buffer, CanCommandLocoSpeed, CanResponseResponse, 6);
			CreateLocalIDLoco(buffer, protocol, address);
			Utils::Integer::ShortToDataBigEndian(loco->GetSpeed(), buffer + 9);
			logger->HexOut(buffer, CANCommandBufferLength);
			Send(buffer);
		}

		// Send direction
		{
			unsigned char buffer[CANCommandBufferLength];
			CreateCommandHeader(buffer, CanCommandLocoDirection, CanResponseResponse, 5);
			CreateLocalIDLoco(buffer, protocol, address);
			buffer[9] = (loco->GetOrientation() == OrientationRight ? 1 : 2);
			logger->HexOut(buffer, CANCommandBufferLength);
			Send(buffer);
		}

		// Send function states
		std::vector<DataModel::LocoFunctionEntry> functions = loco->GetFunctionStates();
		for (const DataModel::LocoFunctionEntry& function : functions)
		{
			unsigned char buffer[CANCommandBufferLength];
			CreateCommandHeader(buffer, CanCommandLocoFunction, CanResponseResponse, 6);
			CreateLocalIDLoco(buffer, protocol, address);
			buffer[9] = function.nr;
			buffer[10] = function.state;
			logger->HexOut(buffer, CANCommandBufferLength);
			Send(buffer);
		}
	}

	void CS2Client::SendAccessoryInfo(const DataModel::AccessoryBase* accessory)
	{
		if (accessory == nullptr)
		{
			return;
		}

		unsigned char buffer[CANCommandBufferLength];
		CreateCommandHeader(buffer, CanCommandAccessory, CanResponseResponse, 6);
		CreateLocalIDAccessory(buffer, accessory->GetProtocol(), accessory->GetAddress());
		buffer[9] = accessory->GetAccessoryState() & 0x03;
		buffer[10] = 1; // Power on
		logger->HexOut(buffer, CANCommandBufferLength);
		Send(buffer);
	}

}} // namespace Server::CS2
