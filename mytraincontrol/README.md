# MyTrainControl - Modellbahn Steuerungssoftware

Eine eigenständige C++ Bibliothek zur Steuerung von Modellbahnen über die **CAN-Digital-Bahn CC-Schnitte**.

## Funktionen

- **Lok-Steuerung**: Geschwindigkeit, Fahrtrichtung, Funktionen (F0-F31)
- **Zubehör-Steuerung**: Weichen und Signale
- **Rückmelder**: S88-Feedback-Unterstützung
- **Protokolle**: Märklin Motorola (MM), Märklin mfx, DCC
- **Callbacks**: Event-basierte Architektur für eingehende Befehle

## Voraussetzungen

- Linux (getestet auf Ubuntu/Debian)
- CMake 3.14+
- GCC/Clang mit C++17 Unterstützung
- CC-Schnitte 2.1 von CAN-Digital-Bahn

## Kompilieren

```bash
mkdir build
cd build
cmake ..
make
```

## Verwendung

### Grundlegendes Beispiel

```cpp
#include "CcSchnitte.h"

int main()
{
    // Verbindung zur CC-Schnitte herstellen
    Hardware::CcSchnitte ccSchnitte("/dev/ttyUSB0", "MeineAnlage");
    
    // Callbacks für Events registrieren
    ccSchnitte.SetLocoSpeedCallback([](Protocol p, Address a, Speed s) {
        std::cout << "Lok " << a << " Speed: " << s << std::endl;
    });
    
    // Kommunikation starten
    ccSchnitte.Start();
    
    // Booster einschalten
    ccSchnitte.Booster(BoosterStateGo);
    
    // Lok steuern (DCC, Adresse 3, Geschwindigkeit 500)
    ccSchnitte.LocoSpeed(ProtocolDCC, 3, 500);
    
    // Funktion F0 (Licht) einschalten
    ccSchnitte.LocoFunction(ProtocolDCC, 3, 0, FunctionStateOn);
    
    // Weiche schalten (MM, Adresse 1, Gerade)
    ccSchnitte.Accessory(ProtocolMM, 1, AccessoryStateOn);
    
    // ... Programm läuft ...
    
    ccSchnitte.Stop();
    return 0;
}
```

### Interaktives Beispiel ausführen

```bash
./mytraincontrol /dev/ttyUSB0
```

## Serielle Schnittstelle einrichten

### USB-Gerät finden

```bash
ls /dev/ttyUSB*
```

### Berechtigungen

```bash
sudo usermod -a -G dialout $USER
# Danach neu anmelden
```

### Udev-Regel (optional)

Für einen konstanten Gerätenamen `/dev/CC-Schnitte`:

```bash
# Serial-ID herausfinden
udevadm info -a /dev/ttyUSB0 | grep serial

# /etc/udev/rules.d/99-cc-schnitte.rules erstellen:
SUBSYSTEM=="tty", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", ATTRS{serial}=="IHRE_SERIAL_ID", SYMLINK+="CC-Schnitte"

# Udev-Regeln neu laden
sudo udevadm control --reload-rules
```

## API-Referenz

### Konstruktor

```cpp
CcSchnitte(const std::string& serialPort, const std::string& name = "CC-Schnitte")
```

### Steuerungsmethoden

| Methode | Beschreibung |
|---------|--------------|
| `Start()` | Startet die Kommunikation |
| `Stop()` | Beendet die Kommunikation |
| `IsConnected()` | Prüft ob Verbindung aktiv ist |
| `Booster(state)` | Booster ein/ausschalten |
| `LocoSpeed(protocol, address, speed)` | Lok-Geschwindigkeit (0-1023) |
| `LocoOrientation(protocol, address, dir)` | Fahrtrichtung |
| `LocoFunction(protocol, address, nr, state)` | Funktion F0-F31 |
| `Accessory(protocol, address, state)` | Weiche/Signal schalten |

### Protokolle

| Konstante | Beschreibung |
|-----------|--------------|
| `ProtocolMM` | Märklin Motorola |
| `ProtocolMFX` | Märklin mfx |
| `ProtocolDCC` | Digital Command Control |

### Callbacks

```cpp
void SetBoosterCallback(std::function<void(BoosterState)>);
void SetLocoSpeedCallback(std::function<void(Protocol, Address, Speed)>);
void SetLocoOrientationCallback(std::function<void(Protocol, Address, Orientation)>);
void SetLocoFunctionCallback(std::function<void(Protocol, Address, FunctionNr, FunctionState)>);
void SetAccessoryCallback(std::function<void(Protocol, Address, AccessoryState)>);
void SetFeedbackCallback(std::function<void(uint8_t device, uint8_t bus, uint16_t pin, bool state)>);
```

## Dateien

| Datei | Beschreibung |
|-------|--------------|
| `CcSchnitte.h` | Hauptklasse für CC-Schnitte Kommunikation |
| `DataTypes.h` | Datentypen und Enumerationen |
| `Serial.h` | Serielle Kommunikation |
| `Logger.h` | Einfaches Logging |
| `Utils.h` | Hilfsfunktionen (Big-Endian, etc.) |

## Basiert auf

Diese Bibliothek basiert auf dem CC-Schnitte-Modul von [RailControl](https://www.railcontrol.org/) 
von Teddy / Dominik Mahrer und verwendet das Märklin CAN-Protokoll.

CAN Protokoll Spezifikation: http://streaming.maerklin.de/public-media/cs2/cs2CAN-Protokoll-2_0.pdf

## Lizenz

Basierend auf RailControl (GPL v3). Bitte beachten Sie die Lizenzbedingungen.

