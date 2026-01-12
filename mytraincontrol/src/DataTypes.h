/*
MyTrainControl - Modellbahn Steuerungssoftware
Basierend auf RailControl von Teddy / Dominik Mahrer

Eigene Implementierung für die CC-Schnitte Schnittstelle
*/

#pragma once

#include <cstdint>
#include <string>

// Adress-Typen
typedef uint16_t Address;
typedef uint16_t Speed;
typedef uint8_t FunctionNr;

// Konstanten
static const Address AddressNone = 0;
static const Speed MaxSpeed = 1023;
static const Speed MinSpeed = 0;

// Booster-Zustand
enum BoosterState : bool
{
    BoosterStateStop = false,
    BoosterStateGo = true
};

// Protokoll-Typen
enum Protocol : uint8_t
{
    ProtocolNone = 0,
    ProtocolMM = 9,      // Märklin Motorola
    ProtocolMFX = 4,     // Märklin mfx
    ProtocolDCC = 5      // DCC
};

// Fahrtrichtung
enum Orientation : bool
{
    OrientationLeft = false,
    OrientationRight = true
};

// Weichen-/Signalzustand
enum AccessoryState : uint8_t
{
    AccessoryStateOff = 0,
    AccessoryStateOn = 1
};

// Funktionszustand
enum FunctionState : bool
{
    FunctionStateOff = false,
    FunctionStateOn = true
};

// Hilfsfunktion zur Protokoll-Konvertierung
inline const char* ProtocolToString(Protocol protocol)
{
    switch (protocol)
    {
        case ProtocolMM:
            return "MM";
        case ProtocolMFX:
            return "mfx";
        case ProtocolDCC:
            return "DCC";
        default:
            return "Unknown";
    }
}

