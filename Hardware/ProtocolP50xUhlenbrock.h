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
#include <string>

#include "Hardware/ProtocolP50xSerial.h"
#include "Languages.h"

namespace Hardware
{
	class HardwareParams;

	class ProtocolP50xUhlenbrock : public ProtocolP50xSerial
	{
		public:
			ProtocolP50xUhlenbrock() = delete;
			ProtocolP50xUhlenbrock(const ProtocolP50xUhlenbrock&) = delete;
			ProtocolP50xUhlenbrock& operator=(const ProtocolP50xUhlenbrock&) = delete;

			ProtocolP50xUhlenbrock(const HardwareParams* params, const std::string& controlName)
			:	ProtocolP50xSerial(params, controlName, TypeUhlenbrock)
			{
			}

			virtual ~ProtocolP50xUhlenbrock()
			{
			}
	};
} // namespace

