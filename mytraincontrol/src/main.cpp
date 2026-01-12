/*
MyTrainControl - Beispielprogramm für CC-Schnitte
Demonstriert die Grundfunktionen der Modellbahn-Steuerung
*/

#include <iostream>
#include <thread>
#include <chrono>

#include "CcSchnitte.h"

using namespace Hardware;
using namespace std;

// Callback-Funktionen für empfangene Events

void OnBoosterChange(BoosterState state)
{
    cout << "==> Booster-Zustand geändert: " << (state ? "EIN" : "AUS") << endl;
}

void OnLocoSpeed(Protocol protocol, Address address, Speed speed)
{
    cout << "==> Lok " << address << " (" << ProtocolToString(protocol)
         << "): Geschwindigkeit = " << speed << endl;
}

void OnLocoOrientation(Protocol protocol, Address address, Orientation orientation)
{
    cout << "==> Lok " << address << " (" << ProtocolToString(protocol)
         << "): Richtung = " << (orientation ? "Vorwärts" : "Rückwärts") << endl;
}

void OnLocoFunction(Protocol protocol, Address address, FunctionNr function, FunctionState state)
{
    cout << "==> Lok " << address << " (" << ProtocolToString(protocol)
         << "): F" << static_cast<int>(function) << " = " << (state ? "AN" : "AUS") << endl;
}

void OnAccessory(Protocol protocol, Address address, AccessoryState state)
{
    cout << "==> Weiche/Signal " << address << " (" << ProtocolToString(protocol)
         << "): " << (state ? "Grün/Gerade" : "Rot/Abzweig") << endl;
}

void OnFeedback(uint8_t device, uint8_t bus, uint16_t pin, bool state)
{
    cout << "==> Rückmelder Device " << static_cast<int>(device)
         << ", Bus " << static_cast<int>(bus)
         << ", Pin " << pin
         << ": " << (state ? "BELEGT" : "FREI") << endl;
}

void PrintMenu()
{
    cout << "\n=== MyTrainControl Steuerung ===" << endl;
    cout << "1 - Booster EIN" << endl;
    cout << "2 - Booster AUS" << endl;
    cout << "3 - Lok Geschwindigkeit setzen" << endl;
    cout << "4 - Lok Richtung ändern" << endl;
    cout << "5 - Lok Funktion schalten" << endl;
    cout << "6 - Weiche/Signal schalten" << endl;
    cout << "q - Beenden" << endl;
    cout << "Auswahl: ";
}

int main(int argc, char* argv[])
{
    // Serielle Schnittstelle konfigurieren
    string serialPort = "/dev/ttyUSB0";
    if (argc > 1)
    {
        serialPort = argv[1];
    }

    cout << "MyTrainControl - CC-Schnitte Beispiel" << endl;
    cout << "======================================" << endl;
    cout << "Verwende serielle Schnittstelle: " << serialPort << endl;
    cout << endl;

    // CC-Schnitte initialisieren
    CcSchnitte ccSchnitte(serialPort, "MeineTrain");

    // Log-Level setzen (Debug für detaillierte Ausgabe)
    // ccSchnitte.SetLogLevel(Logger::LogLevelDebug);

    // Callbacks registrieren
    ccSchnitte.SetBoosterCallback(OnBoosterChange);
    ccSchnitte.SetLocoSpeedCallback(OnLocoSpeed);
    ccSchnitte.SetLocoOrientationCallback(OnLocoOrientation);
    ccSchnitte.SetLocoFunctionCallback(OnLocoFunction);
    ccSchnitte.SetAccessoryCallback(OnAccessory);
    ccSchnitte.SetFeedbackCallback(OnFeedback);

    // Verbindung prüfen
    if (!ccSchnitte.IsConnected())
    {
        cerr << "FEHLER: Kann keine Verbindung zur CC-Schnitte herstellen!" << endl;
        cerr << "Bitte prüfen Sie:" << endl;
        cerr << "  - Ist die CC-Schnitte angeschlossen?" << endl;
        cerr << "  - Ist die serielle Schnittstelle korrekt? (aktuell: " << serialPort << ")" << endl;
        cerr << "  - Haben Sie Leserechte? (ggf. 'sudo usermod -a -G dialout $USER' ausführen)" << endl;
        return 1;
    }

    // Kommunikation starten
    ccSchnitte.Start();

    cout << "Verbindung hergestellt!" << endl;
    cout << "Drücken Sie ENTER, um das Menü anzuzeigen..." << endl;

    // Interaktive Steuerung
    char choice;
    bool running = true;

    while (running)
    {
        PrintMenu();
        cin >> choice;

        switch (choice)
        {
            case '1':
                ccSchnitte.Booster(BoosterStateGo);
                break;

            case '2':
                ccSchnitte.Booster(BoosterStateStop);
                break;

            case '3':
            {
                int addr, spd;
                char proto;
                cout << "Protokoll (m=MM, x=MFX, d=DCC): ";
                cin >> proto;
                cout << "Lok-Adresse: ";
                cin >> addr;
                cout << "Geschwindigkeit (0-1023): ";
                cin >> spd;

                Protocol p = ProtocolMM;
                if (proto == 'x' || proto == 'X') p = ProtocolMFX;
                else if (proto == 'd' || proto == 'D') p = ProtocolDCC;

                ccSchnitte.LocoSpeed(p, static_cast<Address>(addr),
                                     static_cast<Speed>(spd));
                break;
            }

            case '4':
            {
                int addr;
                char proto, dir;
                cout << "Protokoll (m=MM, x=MFX, d=DCC): ";
                cin >> proto;
                cout << "Lok-Adresse: ";
                cin >> addr;
                cout << "Richtung (v=Vorwärts, r=Rückwärts): ";
                cin >> dir;

                Protocol p = ProtocolMM;
                if (proto == 'x' || proto == 'X') p = ProtocolMFX;
                else if (proto == 'd' || proto == 'D') p = ProtocolDCC;

                Orientation o = (dir == 'v' || dir == 'V') ? OrientationRight : OrientationLeft;
                ccSchnitte.LocoOrientation(p, static_cast<Address>(addr), o);
                break;
            }

            case '5':
            {
                int addr, func;
                char proto, state;
                cout << "Protokoll (m=MM, x=MFX, d=DCC): ";
                cin >> proto;
                cout << "Lok-Adresse: ";
                cin >> addr;
                cout << "Funktionsnummer (0-31): ";
                cin >> func;
                cout << "Zustand (1=AN, 0=AUS): ";
                cin >> state;

                Protocol p = ProtocolMM;
                if (proto == 'x' || proto == 'X') p = ProtocolMFX;
                else if (proto == 'd' || proto == 'D') p = ProtocolDCC;

                ccSchnitte.LocoFunction(p, static_cast<Address>(addr),
                                        static_cast<FunctionNr>(func),
                                        state == '1' ? FunctionStateOn : FunctionStateOff);
                break;
            }

            case '6':
            {
                int addr;
                char proto, state;
                cout << "Protokoll (m=MM, d=DCC): ";
                cin >> proto;
                cout << "Weichen/Signal-Adresse: ";
                cin >> addr;
                cout << "Zustand (g=Grün/Gerade, r=Rot/Abzweig): ";
                cin >> state;

                Protocol p = (proto == 'd' || proto == 'D') ? ProtocolDCC : ProtocolMM;
                AccessoryState s = (state == 'g' || state == 'G') ? AccessoryStateOn : AccessoryStateOff;

                ccSchnitte.Accessory(p, static_cast<Address>(addr), s);
                break;
            }

            case 'q':
            case 'Q':
                running = false;
                break;

            default:
                cout << "Ungültige Auswahl!" << endl;
                break;
        }
    }

    cout << "Beende Programm..." << endl;
    ccSchnitte.Stop();

    return 0;
}

