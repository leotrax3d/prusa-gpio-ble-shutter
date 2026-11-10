# Prüfstand 10.11.2026

- PlatformIO `esp32dev`, espressif32 6.10.0 / Arduino-ESP32 2.0.17: Firmware erfolgreich kompiliert.
- C++-Eingangsfiltertest: Entprellung, dauerhaft aktiver Pegel, Neustart bei aktivem Pegel, erneuter Impuls, Millisekundenüberlauf bestanden.
- Chromium/Playwright mit simulierten API-Antworten: mobile Darstellung bei 390 × 844, Statusanzeige, Aktivierung, Konfigurations-POST, Testauslösung und zusätzlicher POST-Header bestanden; keine JavaScript-Fehler, kein horizontales Überlaufen.
- [Screenshot des mobilen Webinterfaces](webinterface-mobile.png) mit simuliertem Verbindungsstatus.

Keine physische Hardware angeschlossen. BLE-Pairing/HID-Kompatibilität, reale Pegel, Kameraauslösung und Timing während des Drucks sind noch am Gerät zu prüfen. Der Browsertest belegt die Oberfläche mit einer API-Simulation, nicht die HTTP-Implementierung auf dem ESP32. Der C++-Test prüft den Eingangsfilter, nicht den Bluetooth-Stack.
