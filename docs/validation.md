# Prüfstand · Version 2.0.1 · 10.11.2026

## Erfolgreich geprüft

Version 2.0.1: englische Oberfläche und Gerätemeldungen, kurze Texte, keine Eyebrows. Navigation als Desktop-Links und mobile Seitenauswahl; Direktlinks und Browser-Zurück im Browsertest geprüft.

- Firmware für klassisches ESP32 DevKit/WROOM-32, espressif32 6.10.0 / Arduino-ESP32 2.0.17 gebaut. Zwei OTA-Partitionen mit jeweils 1.875 MiB; Anwendung passt mit Reserve.
- Nativer C++-Test des Eingangsfilters: Entprellung, gehaltenes Signal, Start bei aktivem Pegel, erneute Flanke und Millisekundenüberlauf.
- Nativer C++-Test der tatsächlichen Aufnahme-Engine: Einzelaufnahme/Freigabe, begrenzte Serie, Mindestabstand, Countdown, Intervall, zeitgesteuerter Stopp, jeder n-te Impuls, BLE-Ausfall, verworfene Aufträge, konfigurierbares Deaktivieren und Überlauf.
- Nativer C++-Test des Servo-Protokolls: gültiger Slot, keine Wiederholung, Unterdrückung von Start-HIGH, ungültigen/zu langen Pulsen und Millisekundenüberlauf.
- Browserprüfung in Chromium bei 390 und 1440 Pixel Breite: alle acht Dashboard-Bereiche ohne horizontales Überlaufen, Status, Testfoto, Vorlagen, Einstellungs-POST, visueller Servo-Aktionsplan, G-Code für konkrete Schicht, separate Kamera-Auswahl, manuelle Servo-Kommandos, Hell/Dunkel und sichere Darstellung von Logtext.
- Installer-Zustände bereit/nicht unterstützter Browser mit simuliertem Custom Element geprüft.
- Zusätzlich den **echten lokal mitgelieferten ESP-Web-Tools-Build 10.1.0** geladen: Custom Element registriert, Installationsbutton freigegeben, keine JavaScript-Fehler. USB-Verbindung wurde nicht angefordert.
- Firmware-Paketierung prüft ESP32-App-Kennung und Partitionsgröße; USB-Gesamtimage, OTA-App, Manifest und SHA-256-Prüfsummen erzeugt.
- ESP-Web-Tools-Paket gegen die SHA-512-Integritätsangabe des npm-Pakets geprüft; Quell-/Lizenzinformationen werden mitgeliefert.

## Grenzen

Keine physische Hardware angeschlossen. Nicht geprüft: Flashen über USB, tatsächlicher ESP32-Webserver, OTA-Vorgang, BLE-Pairing mit Android/iOS, Kamera-App, physische GPIO-Pegel, Miuzei-Signalverträglichkeit und Servo-Endlagen. Der zusätzliche G-Code-Signalkanal und seine Zeitfenster müssen mit der konkreten Druckerfirmware getestet werden. Der erzeugte G-Code wurde im Browser auf seine Inhalte geprüft, nicht durch einen realen PrusaSlicer-Drucklauf validiert.

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
4. Servo ohne Mechanik und mit geeigneter externer Versorgung vorsichtig kalibrieren; erste Bewegung kann direkt zum Ziel erfolgen.
5. Jeden gespeicherten Servo-Slot mit erzeugtem G-Code einzeln aufrufen, danach mit einem kurzen Testdruck prüfen. Slicer-Vorschau und Schichtindex kontrollieren.
6. Nach Plan-/Timingänderungen immer neuen G-Code erzeugen und neu slicen.
