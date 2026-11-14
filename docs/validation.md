# Prüfstand · Version 3.0.0 · 14.11.2026

## Erfolgreich geprüft

Version 3.0.0: Firmware mit pioarduino 55.03.312 / Arduino-ESP32 3.3.12, NimBLE-Arduino 2.5.2, ArduinoJson 7.4.3 und C++17 fehlerfrei gebaut (RAM 69.180 B, Flash 1.301.744 B von 1.966.080 B). Servo-Funktion entfernt, Foto-Ausgang auf OUT6. C++-Tests (Eingangsfilter, Engine, Log, Ready) und beide Browserprüfungen erneut bestanden.

Version 2.0.2: [Performance-Prüfung](performance.md) mit Request-Zählung, Log-Deltas, DOM-Wiederverwendung und Tab-Pause. Native Tests für den festen Logpuffer und Ready-Taster ergänzt. Die Ready-Oberfläche einschließlich Makro-Download wurde mit simulierten API-Antworten geprüft. Physische Ready-Impulse, Makroausführung und Prusa-Connect-Warteschlange wurden nicht am Gerät getestet.

Version 2.0.1: englische Oberfläche und Gerätemeldungen, kurze Texte, keine Eyebrows. Navigation als Desktop-Links und mobile Seitenauswahl; Direktlinks und Browser-Zurück im Browsertest geprüft.

- Version 2.x: Firmware für klassisches ESP32 DevKit/WROOM-32, espressif32 6.10.0 / Arduino-ESP32 2.0.17 gebaut. Zwei OTA-Partitionen mit jeweils 1.875 MiB; Anwendung passt mit Reserve.
- Nativer C++-Test des Eingangsfilters: Entprellung, gehaltenes Signal, Start bei aktivem Pegel, erneute Flanke und Millisekundenüberlauf.
- Nativer C++-Test der tatsächlichen Aufnahme-Engine: Einzelaufnahme/Freigabe, begrenzte Serie, Mindestabstand, Countdown, Intervall, zeitgesteuerter Stopp, jeder n-te Impuls, BLE-Ausfall, verworfene Aufträge, konfigurierbares Deaktivieren und Überlauf.
- Browserprüfung in Chromium bei 390 und 1440 Pixel Breite: alle acht Dashboard-Bereiche ohne horizontales Überlaufen, Status, Testfoto, Vorlagen, Einstellungs-POST, erzeugter Foto-G-Code für OUT6, Ready-Taster, Hell/Dunkel und sichere Darstellung von Logtext.
- Installer-Zustände bereit/nicht unterstützter Browser mit simuliertem Custom Element geprüft.
- Zusätzlich den **echten lokal mitgelieferten ESP-Web-Tools-Build 10.1.0** geladen: Custom Element registriert, Installationsbutton freigegeben, keine JavaScript-Fehler. USB-Verbindung wurde nicht angefordert.
- Firmware-Paketierung prüft ESP32-App-Kennung und Partitionsgröße; USB-Gesamtimage, OTA-App, Manifest und SHA-256-Prüfsummen erzeugt.
- ESP-Web-Tools-Paket gegen die SHA-512-Integritätsangabe des npm-Pakets geprüft; Quell-/Lizenzinformationen werden mitgeliefert.

## Grenzen

Keine physische Hardware angeschlossen. Nicht geprüft: Flashen über USB, tatsächlicher ESP32-Webserver, OTA-Vorgang, BLE-Pairing mit Android/iOS, Kamera-App, physische GPIO-Pegel sowie das Verhalten des NimBLE-Stacks mit Android/iOS. Der erzeugte G-Code wurde im Browser auf seine Inhalte geprüft, nicht durch einen realen PrusaSlicer-Drucklauf validiert.

Browserprüfungen verwenden explizit simulierte API-Antworten. Sie ersetzen keinen Hardwaretest. Screenshots zeigen simulierte Statuswerte.

## Screenshots

- [Dashboard Desktop](dashboard-1440.png)
- [Dashboard Mobil](dashboard-390.png)
- [G-Code-Konfigurator Desktop](configurator-1440.png)
- [G-Code-Konfigurator Mobil](configurator-390.png)
- [Flashseite Mobil](installer-mobile.png)

## Vor der ersten echten Nutzung

1. Getrennte Stromversorgungen und gemeinsame Masse prüfen, GPIO-Pegel zunächst ohne aktive Aufnahme kontrollieren.
2. Smartphone koppeln, HID-Bereitschaft abwarten, Kamera öffnen, zehn einzelne Testfotos prüfen.
3. BLE trennen/wiederverbinden und ESP32 neu starten: keine nachgeholten Fotos, keine automatische Aktivierung.
4. `M264 P6 B1` / `M264 P6 B0` in der Druckerkonsole: Dashboard muss HIGH/LOW zeigen, Handy muss auslösen.
5. Kurzen Testdruck mit erzeugtem G-Code; nach Timingänderungen immer neu erzeugen und neu slicen.
