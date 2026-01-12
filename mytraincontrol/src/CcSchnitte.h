/*
MyTrainControl - CC-Schnitte Interface
Basierend auf RailControl von Teddy / Dominik Mahrer

CAN Protokoll Spezifikation: http://streaming.maerklin.de/public-media/cs2/cs2CAN-Protokoll-2_0.pdf
Weitere Info: http://www.mbernstein.de/modellbahn/can/bem.htm
*/

#pragma once

#include <thread>
#include <atomic>
#include <functional>
#include <cstdlib>
#include <cstring>

#include "DataTypes.h"
#include "Logger.h"
#include "Serial.h"
#include "Utils.h"

// OS X definiert B500000 nicht
#ifdef __APPLE__
#ifndef B500000
#define B500000 500000
#endif
#endif

namespace Hardware
{
    // Callback-Typen für Events
    using BoosterCallback = std::function<void(BoosterState)>;
    using LocoSpeedCallback = std::function<void(Protocol, Address, Speed)>;
    using LocoOrientationCallback = std::function<void(Protocol, Address, Orientation)>;
    using LocoFunctionCallback = std::function<void(Protocol, Address, FunctionNr, FunctionState)>;
    using AccessoryCallback = std::function<void(Protocol, Address, AccessoryState)>;
    using FeedbackCallback = std::function<void(uint8_t, uint8_t, uint16_t, bool)>; // device, bus, pin, state

    class CcSchnitte
    {
    public:
        static const unsigned char CANCommandBufferLength = 13;

        CcSchnitte() = delete;
        CcSchnitte(const CcSchnitte&) = delete;
        CcSchnitte& operator=(const CcSchnitte&) = delete;

        CcSchnitte(const std::string& serialPort, const std::string& name = "CC-Schnitte")
            : logger(new Logger::Logger(name)),
              serialLine(logger, serialPort, B500000, 8, 'N', 1, true),
              run(false),
              uid(0xAFFEAFFE),
              hash(CalcHash(uid))
        {
            logger->Info("CC-Schnitte initialisiert auf " + serialPort);
            logger->Debug("UID: " + std::to_string(uid) + ", Hash: " + std::to_string(hash));
        }

        ~CcSchnitte()
        {
            Stop();
            delete logger;
        }

        // Startet die Kommunikation
        void Start()
        {
            if (run)
            {
                return;
            }
            run = true;
            receiverThread = std::thread(&CcSchnitte::Receiver, this);
            pingThread = std::thread(&CcSchnitte::PingSender, this);
            logger->Info("CC-Schnitte gestartet");
        }

        // Stoppt die Kommunikation
        void Stop()
        {
            if (!run)
            {
                return;
            }
            run = false;
            if (receiverThread.joinable())
            {
                receiverThread.join();
            }
            if (pingThread.joinable())
            {
                pingThread.join();
            }
            logger->Info("CC-Schnitte gestoppt");
        }

        // Prüft ob Verbindung aktiv ist
        bool IsConnected() const
        {
            return serialLine.IsConnected() && run;
        }

        // Setzt den Log-Level
        void SetLogLevel(Logger::LogLevel level)
        {
            logger->SetLevel(level);
        }

        // ===== Steuerungsbefehle =====

        // Booster ein/ausschalten
        void Booster(BoosterState status)
        {
            unsigned char buffer[CANCommandBufferLength];
            logger->Info(status ? "Booster AN" : "Booster AUS");
            CreateCommandHeader(buffer, CanCommandSystem, CanResponseCommand, 5);
            buffer[9] = status;
            Send(buffer);
        }

        // Lok-Geschwindigkeit setzen (0-1023)
        void LocoSpeed(Protocol protocol, Address address, Speed speed)
        {
            unsigned char buffer[CANCommandBufferLength];
            logger->Info("Setze Geschwindigkeit: Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Speed=" + std::to_string(speed));
            CreateCommandHeader(buffer, CanCommandLocoSpeed, CanResponseCommand, 6);
            CreateLocalIDLoco(buffer, protocol, address);
            Utils::ShortToDataBigEndian(speed, buffer + 9);
            Send(buffer);
        }

        // Lok-Fahrtrichtung setzen
        void LocoOrientation(Protocol protocol, Address address, Orientation orientation)
        {
            unsigned char buffer[CANCommandBufferLength];
            logger->Info("Setze Fahrtrichtung: Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Richtung=" + (orientation ? "Vorwärts" : "Rückwärts"));
            CreateCommandHeader(buffer, CanCommandLocoDirection, CanResponseCommand, 5);
            CreateLocalIDLoco(buffer, protocol, address);
            buffer[9] = (orientation ? 1 : 2);
            Send(buffer);
        }

        // Lok-Funktion setzen
        void LocoFunction(Protocol protocol, Address address, FunctionNr function, FunctionState on)
        {
            unsigned char buffer[CANCommandBufferLength];
            logger->Info("Setze Funktion F" + std::to_string(function)
                        + ": Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Zustand=" + (on ? "AN" : "AUS"));
            CreateCommandHeader(buffer, CanCommandLocoFunction, CanResponseCommand, 6);
            CreateLocalIDLoco(buffer, protocol, address);
            buffer[9] = function;
            buffer[10] = on ? 1 : 0;
            Send(buffer);
        }

        // Weiche/Signal schalten
        void Accessory(Protocol protocol, Address address, AccessoryState state, bool on = true)
        {
            unsigned char buffer[CANCommandBufferLength];
            logger->Info("Schalte Zubehör: Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Zustand=" + (state ? "Grün/Gerade" : "Rot/Abzweig"));
            CreateCommandHeader(buffer, CanCommandAccessory, CanResponseCommand, 6);
            CreateLocalIDAccessory(buffer, protocol, address);
            buffer[9] = state & 0x03;
            buffer[10] = static_cast<unsigned char>(on);
            Send(buffer);
        }

        // Ping senden
        void Ping()
        {
            unsigned char buffer[CANCommandBufferLength];
            CreateCommandHeader(buffer, CanCommandPing, CanResponseCommand, 0);
            Send(buffer);
        }

        // ===== Callback-Registrierung =====

        void SetBoosterCallback(BoosterCallback callback) { boosterCallback = callback; }
        void SetLocoSpeedCallback(LocoSpeedCallback callback) { locoSpeedCallback = callback; }
        void SetLocoOrientationCallback(LocoOrientationCallback callback) { locoOrientationCallback = callback; }
        void SetLocoFunctionCallback(LocoFunctionCallback callback) { locoFunctionCallback = callback; }
        void SetAccessoryCallback(AccessoryCallback callback) { accessoryCallback = callback; }
        void SetFeedbackCallback(FeedbackCallback callback) { feedbackCallback = callback; }

    private:
        // CAN Befehle
        enum CanCommand : unsigned char
        {
            CanCommandSystem = 0x00,
            CanCommandLocoSpeed = 0x04,
            CanCommandLocoDirection = 0x05,
            CanCommandLocoFunction = 0x06,
            CanCommandReadConfig = 0x07,
            CanCommandWriteConfig = 0x08,
            CanCommandAccessory = 0x0B,
            CanCommandS88Event = 0x11,
            CanCommandPing = 0x18
        };

        enum CanSubCommand : unsigned char
        {
            CanSubCommandStop = 0x00,
            CanSubCommandGo = 0x01
        };

        enum CanResponse : unsigned char
        {
            CanResponseCommand = 0x00,
            CanResponseResponse = 0x01
        };

        typedef unsigned char CanPrio;
        typedef unsigned char CanLength;
        typedef uint32_t CanUid;
        typedef uint16_t CanHash;

        // Logger
        Logger::Logger* logger;

        // Serielle Verbindung
        Network::Serial serialLine;

        // Threading
        std::atomic<bool> run;
        std::thread receiverThread;
        std::thread pingThread;

        // Protokoll-Daten
        CanUid uid;
        CanHash hash;

        // Callbacks
        BoosterCallback boosterCallback;
        LocoSpeedCallback locoSpeedCallback;
        LocoOrientationCallback locoOrientationCallback;
        LocoFunctionCallback locoFunctionCallback;
        AccessoryCallback accessoryCallback;
        FeedbackCallback feedbackCallback;

        // Hash-Berechnung für CAN-Protokoll
        static CanHash CalcHash(const CanUid uid)
        {
            const CanHash calc = (uid >> 16) ^ (uid & 0xFFFF);
            CanHash hash = ((calc << 3) | 0x0300) & 0xFF00;
            hash |= (calc & 0x007F);
            return hash;
        }

        // Befehlsheader erstellen
        void CreateCommandHeader(unsigned char* buffer, CanCommand command,
                                  CanResponse response, CanLength length)
        {
            const CanPrio prio = 0;
            buffer[0] = (prio << 1) | (command >> 7);
            buffer[1] = (command << 1) | (response & 0x01);
            Utils::ShortToDataBigEndian(hash, buffer + 2);
            buffer[4] = length;
            std::memset(buffer + 5, 0, 8);
        }

        // Lok-ID für CAN-Protokoll erstellen
        void CreateLocalIDLoco(unsigned char* buffer, Protocol protocol, Address address) const
        {
            uint32_t localID = address;
            if (protocol == ProtocolDCC)
            {
                localID |= 0xC000;
            }
            else if (protocol == ProtocolMFX)
            {
                localID |= 0x4000;
            }
            // MM: kein Offset
            Utils::IntToDataBigEndian(localID, buffer + 5);
        }

        // Zubehör-ID für CAN-Protokoll erstellen
        void CreateLocalIDAccessory(unsigned char* buffer, Protocol protocol, Address address) const
        {
            uint32_t localID = address - 1; // GUI ist 1-basiert, Protokoll 0-basiert
            if (protocol == ProtocolDCC)
            {
                localID |= 0x3800;
            }
            else // ProtocolMM
            {
                localID |= 0x3000;
            }
            Utils::IntToDataBigEndian(localID, buffer + 5);
        }

        // Daten senden
        void Send(const unsigned char* buffer)
        {
            if (!serialLine.IsConnected())
            {
                logger->Error("Keine Verbindung zur CC-Schnitte");
                return;
            }
            logger->HexOut(buffer, CANCommandBufferLength);
            if (serialLine.Send(buffer, CANCommandBufferLength) == -1)
            {
                logger->Error("Fehler beim Senden");
            }
        }

        // Empfänger-Thread
        void Receiver()
        {
            logger->Info("Empfänger-Thread gestartet");
            while (run)
            {
                if (!serialLine.IsConnected())
                {
                    logger->Error("Verbindung verloren");
                    return;
                }
                unsigned char buffer[CANCommandBufferLength];
                ssize_t datalen = serialLine.ReceiveExact(buffer, CANCommandBufferLength);
                if (!run)
                {
                    break;
                }
                if (datalen == 0)
                {
                    continue;
                }
                if (datalen != CANCommandBufferLength)
                {
                    logger->Error("Ungültige Daten empfangen");
                    continue;
                }
                Parse(buffer);
            }
            logger->Info("Empfänger-Thread beendet");
        }

        // Ping-Sender-Thread
        void PingSender()
        {
            logger->Info("Ping-Thread gestartet");
            while (run)
            {
                for (int i = 0; i < 100 && run; ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (run)
                {
                    Ping();
                }
            }
            logger->Info("Ping-Thread beendet");
        }

        // Parser-Hilfsfunktionen
        static CanCommand ParseCommand(const unsigned char* buffer)
        {
            return static_cast<CanCommand>((buffer[0] << 7) | (buffer[1] >> 1));
        }

        static CanResponse ParseResponse(const unsigned char* buffer)
        {
            return static_cast<CanResponse>(buffer[1] & 0x01);
        }

        static CanLength ParseLength(const unsigned char* buffer)
        {
            return buffer[4];
        }

        static CanSubCommand ParseSubCommand(const unsigned char* buffer)
        {
            return static_cast<CanSubCommand>(buffer[9]);
        }

        static Address ParseAddress(const unsigned char* buffer)
        {
            return static_cast<Address>(Utils::DataBigEndianToInt(buffer + 5));
        }

        // Adresse und Protokoll extrahieren
        static void ParseAddressProtocol(const unsigned char* buffer,
                                          Address& address, Protocol& protocol)
        {
            Address input = ParseAddress(buffer);
            address = input;
            Address maskedAddress = address & 0xFC00;

            if ((maskedAddress == 0x0000) || (maskedAddress == 0x1000) ||
                (maskedAddress == 0x2000) || (maskedAddress == 0x3000))
            {
                protocol = ProtocolMM;
                address &= 0x03FF;
                return;
            }

            if ((maskedAddress == 0x3800) || (maskedAddress == 0x3C00))
            {
                protocol = ProtocolDCC;
                address &= 0x03FF;
                return;
            }

            maskedAddress = address & 0xC000;
            address &= 0x3FFF;
            if (maskedAddress == 0x4000)
            {
                protocol = ProtocolMFX;
                return;
            }
            if (maskedAddress == 0xC000)
            {
                protocol = ProtocolDCC;
                return;
            }

            protocol = ProtocolNone;
            address = 0;
        }

        // Empfangene Daten parsen
        void Parse(const unsigned char* buffer)
        {
            CanResponse response = ParseResponse(buffer);
            CanCommand command = ParseCommand(buffer);
            CanLength length = ParseLength(buffer);

            logger->HexIn(buffer, 5 + length);

            if (response)
            {
                switch (command)
                {
                    case CanCommandS88Event:
                        ParseResponseS88Event(buffer);
                        break;
                    case CanCommandLocoSpeed:
                        ParseResponseLocoSpeed(buffer);
                        break;
                    case CanCommandLocoDirection:
                        ParseResponseLocoDirection(buffer);
                        break;
                    case CanCommandLocoFunction:
                        ParseResponseLocoFunction(buffer);
                        break;
                    case CanCommandAccessory:
                        ParseResponseAccessory(buffer);
                        break;
                    default:
                        break;
                }
            }
            else
            {
                switch (command)
                {
                    case CanCommandSystem:
                        ParseCommandSystem(buffer);
                        break;
                    default:
                        break;
                }
            }
        }

        // System-Befehle parsen (Booster)
        void ParseCommandSystem(const unsigned char* buffer)
        {
            if (ParseLength(buffer) != 5)
            {
                return;
            }
            CanSubCommand subcmd = ParseSubCommand(buffer);
            switch (subcmd)
            {
                case CanSubCommandStop:
                    logger->Info("Booster STOP empfangen");
                    if (boosterCallback)
                    {
                        boosterCallback(BoosterStateStop);
                    }
                    break;
                case CanSubCommandGo:
                    logger->Info("Booster GO empfangen");
                    if (boosterCallback)
                    {
                        boosterCallback(BoosterStateGo);
                    }
                    break;
            }
        }

        // Lok-Geschwindigkeit parsen
        void ParseResponseLocoSpeed(const unsigned char* buffer)
        {
            if (ParseLength(buffer) != 6)
            {
                return;
            }
            Address address;
            Protocol protocol;
            ParseAddressProtocol(buffer, address, protocol);
            Speed speed = Utils::DataBigEndianToShort(buffer + 9);
            logger->Info("Geschwindigkeit empfangen: Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Speed=" + std::to_string(speed));
            if (locoSpeedCallback)
            {
                locoSpeedCallback(protocol, address, speed);
            }
        }

        // Lok-Richtung parsen
        void ParseResponseLocoDirection(const unsigned char* buffer)
        {
            if (ParseLength(buffer) != 5)
            {
                return;
            }
            Address address;
            Protocol protocol;
            ParseAddressProtocol(buffer, address, protocol);
            Orientation orientation = (buffer[9] == 1 ? OrientationRight : OrientationLeft);
            logger->Info("Fahrtrichtung empfangen: Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Richtung=" + (orientation ? "Vorwärts" : "Rückwärts"));
            if (locoOrientationCallback)
            {
                locoOrientationCallback(protocol, address, orientation);
            }
        }

        // Lok-Funktion parsen
        void ParseResponseLocoFunction(const unsigned char* buffer)
        {
            if (ParseLength(buffer) != 6)
            {
                return;
            }
            Address address;
            Protocol protocol;
            ParseAddressProtocol(buffer, address, protocol);
            FunctionNr function = buffer[9];
            FunctionState on = (buffer[10] != 0 ? FunctionStateOn : FunctionStateOff);
            logger->Info("Funktion empfangen: F" + std::to_string(function)
                        + ", Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Zustand=" + (on ? "AN" : "AUS"));
            if (locoFunctionCallback)
            {
                locoFunctionCallback(protocol, address, function, on);
            }
        }

        // Zubehör parsen
        void ParseResponseAccessory(const unsigned char* buffer)
        {
            if (ParseLength(buffer) != 6 || buffer[10] != 1)
            {
                return;
            }
            Address address;
            Protocol protocol;
            ParseAddressProtocol(buffer, address, protocol);
            AccessoryState state = (buffer[9] ? AccessoryStateOn : AccessoryStateOff);
            ++address; // GUI ist 1-basiert
            logger->Info("Zubehör empfangen: Protokoll=" + std::string(ProtocolToString(protocol))
                        + ", Adresse=" + std::to_string(address)
                        + ", Zustand=" + (state ? "Grün" : "Rot"));
            if (accessoryCallback)
            {
                accessoryCallback(protocol, address, state);
            }
        }

        // S88 Feedback parsen
        void ParseResponseS88Event(const unsigned char* buffer)
        {
            if (ParseLength(buffer) < 4)
            {
                return;
            }
            uint32_t pin = Utils::DataBigEndianToInt(buffer + 5);
            uint8_t device = (pin >> 16) & 0x000000FF;
            uint8_t bus = 0;
            pin &= 0x0000FFFF;
            while (pin > 1000)
            {
                ++bus;
                pin -= 1000;
            }
            bool state = (buffer[10] != 0);
            logger->Info("Rückmelder empfangen: Device=" + std::to_string(device)
                        + ", Bus=" + std::to_string(bus)
                        + ", Pin=" + std::to_string(pin)
                        + ", Zustand=" + (state ? "belegt" : "frei"));
            if (feedbackCallback)
            {
                feedbackCallback(device, bus, static_cast<uint16_t>(pin), state);
            }
        }
    };
}

