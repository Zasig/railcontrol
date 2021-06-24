/*
RailControl - Model Railway Control Software

Copyright (c) 2017-2021 Dominik (Teddy) Mahrer - www.railcontrol.org

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

#include <map>

#include "DataTypes.h"

namespace Hardware
{
	class ProtocolZ21TurnoutCacheEntry
	{
		public:
			ProtocolZ21TurnoutCacheEntry(const ProtocolZ21TurnoutCacheEntry&) = delete;

			ProtocolZ21TurnoutCacheEntry()
			:	protocol(ProtocolNone)
			{
			}

			ProtocolZ21TurnoutCacheEntry(const Protocol protocol)
			:	protocol(protocol)
			{
			}

			Protocol protocol;
	};

	class ProtocolZ21TurnoutCache
	{
		public:
			ProtocolZ21TurnoutCache& operator=(const ProtocolZ21TurnoutCache&) = delete;

			void SetProtocol(const Address address, const Protocol protocol)
			{
				ProtocolZ21TurnoutCacheEntry entry(protocol);
				cache[address] = entry;
			}

			Protocol GetProtocol(const Address address)
			{
				if (cache.count(address) == 0)
				{
					return ProtocolDCC;
				}
				return cache[address].protocol;
			}

		private:
			std::map<Address, ProtocolZ21TurnoutCacheEntry> cache;
	};
} // namespace

