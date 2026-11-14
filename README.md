# Prusa Shutter 3

**CORE One + Prusa GPIO Hackerboard + ESP32-WROOM-32**: Bluetooth-Fotoauslöser für Android und iOS mit lokalem Dashboard und PrusaSlicer-G-Code-Generator.

![Dashboard](docs/dashboard-1440.png)

## Was ist neu in 3.0?

- **Neue Plattform:** Arduino-ESP32 3.3 (pioarduino), NimBLE statt Bluedroid, C++17. Die Firmware ist rund 400 KB kleiner.
- **Foto-Ausgang jetzt OUT6** (vorher OUT7). G-Code neu erzeugen und neu slicen.
- **Servo entfernt.** Keine Servo-Seite, kein OUT7-Befehlskanal mehr.
- **Aufgeräumter Code:** `src/` ist in Bluetooth, Steuerung, Einstellungen und Web-API aufgeteilt.
- **Neues Dashboard-Design:** Karten, Kennzahlen-Kacheln, Pill-Navigation, weiterhin Orange/Schwarz/Weiß und Hell/Dunkel.

[Performance-Notizen 2.0.2](docs/performance.md) · [Ready-Taster anschließen](docs/ready-button.md)

| Bereich | Möglichkeiten |
|---|---|
| Fotos | Lauter, Leiser oder abwechselnd; manueller Test; GPIO- und Intervallmodus |
| Aufnahmeprogramme | Jede n-te Schicht, Verzögerung, 1–50 Befehle pro Serie, Serienabstand, Countdown |
| Grenzen | Mindestabstand, Tastendauer, Sitzungsgrenze, zeitgesteuerter Stopp |
| Eingang | GPIO-Auswahl, HIGH/LOW-Auslösung, Entprellung, interner Pull-up, Verhalten bei BLE-Ausfall |
| G-Code-Generator | Start-/Schicht-/End-Code aus den gespeicherten Einstellungen, kopieren oder herunterladen |
| Ready-Taster | GPIO32 gegen GND, Open-Drain-Ausgang GPIO25 → IN4, USB-Makro `M1200`, Entprellung und Sperrzeit |
| Verbindungen | Eigenes WPA2-WLAN, zusätzlich Router-WLAN, mDNS, eigene Geräte-/Bluetooth-Namen und Passwörter |
| Diagnose | HID-Bereitschaft, Eingangspegel, Zähler, Heap/Flash/WLAN, 48 Ereignisse mit inkrementellem Abruf, Diagnose-Export |
| Verwaltung | Foto-Einstellungen importieren/exportieren, Kopplungen löschen, Foto-Reset, Neustart, authentifiziertes WLAN-Update |
| Oberfläche | Schlichtes HTML, wenig CSS, Lucide-SVGs, Orange/Schwarz/Weiß, Mobil/Desktop, Hell/Dunkel |
| Installation | Einfache HTML-Flashseite mit ESP Web Tools, passendem ESP32-Image und Prüfsummen |

Die Firmware sendet Tastenbefehle, keine Kamera-API-Aufträge. Die Kamera-App muss geöffnet sein und Lautstärketasten als Fotoauslöser unterstützen. Ein gesendeter Befehl ist keine Bestätigung für ein gespeichertes Foto.

## Installation in wenigen Schritten

Die veröffentlichte Flashseite: **https://leotrax3d.github.io/prusa-gpio-ble-shutter/**

1. Klassisches **ESP32-WROOM-32 / DevKit mit mindestens 4 MB Flash** über USB-Datenkabel an Desktop-Chrome oder Edge anschließen. Druckerleitungen zunächst trennen.
2. Die bereitgestellte Flashseite öffnen und **ESP32 installieren** wählen. Die Website liegt als schlichte HTML-Datei in [`site/index.html`](site/index.html); Hosting benötigt HTTPS, lokal genügt localhost.
3. Beim Umstieg von Version 1 oder 2 per USB flashen und **Gerät löschen / Erase device** wählen. Version 3 nutzt einen neuen Bluetooth-Stack; alte Kopplungen gelten nicht mehr. Foto-Einstellungen vorher im Dashboard exportieren, danach importieren. Auf dem Handy den alten Eintrag „Prusa Shutter“ vergessen.
4. Nach Installation im ESP-Web-Tools-Dialog **Logs & Console** öffnen, 115200 Baud, Board mit EN/RESET neu starten. Dort stehen WLAN- und Dashboard-Passwort.
5. WLAN **Prusa-Shutter** verbinden, „kein Internet“ akzeptieren, **http://192.168.4.1** öffnen. Benutzer **admin**, individuell erzeugtes Passwort aus dem seriellen Protokoll.
6. Smartphone mit **Prusa Shutter** in den Bluetooth-Einstellungen koppeln. Kamera öffnen und Testfoto prüfen. Erst danach Aufnahme aktivieren.

Android/iOS funktionieren als Kamera und für das Dashboard. Die USB-Installation benötigt einen Desktop-Browser mit Web Serial; Safari/Firefox/iOS eignen sich für diesen Installer nicht. Ein rein lokaler, nicht über localhost/HTTPS geöffneter HTML-Download genügt nicht.

### Selbst bauen / lokal flashen

```sh
pip install platformio==6.2.0
pio run
pio run --target upload
pio device monitor --baud 115200
python scripts/package_firmware.py
python -m http.server 8080 --directory site
```

Dann `http://localhost:8080` auf dem Rechner mit angeschlossenem ESP32 öffnen. `package_firmware.py` übernimmt das von pioarduino erzeugte `firmware.factory.bin` als `merged.bin` für USB, `firmware.bin` für OTA, Manifest und SHA-256-Prüfsummen. Die Dateien werden ebenfalls als GitHub-Actions-Artefakt gebaut. Die Flashseite enthält keine Benutzerpasswörter.

Plattform fixiert: pioarduino platform-espressif32 55.03.312 / Arduino-ESP32 3.3.12, NimBLE-Arduino 2.5.2, ArduinoJson 7.4.3, C++17. Die offizielle PlatformIO-Plattform `espressif32` endet bei Arduino-ESP32 2.0.x; deshalb wird pioarduino per URL eingebunden. `scripts/embed.py` komprimiert das Dashboard beim Build in PROGMEM; kein Dateisystem-Upload nötig. Firmware ist ausschließlich für das klassische ESP32 gebaut. C3/S3 benötigen eigene Builds; S2 hat kein Bluetooth.

## Ohne Löten: Fotoanschluss

| Hackerboard | ESP32 |
|---|---|
| OUT6 | GPIO27 (Standard, im Dashboard änderbar) |
| GND | GND |

ESP32 separat per USB versorgen. **JP6 darf offen bleiben**, die Firmware nutzt `INPUT_PULLUP`. Ein zusätzlicher 1-kΩ-Serienwiderstand ist optional. Ohne Löten braucht man montierte Stiftleisten oder zuverlässige passende Prüfkontakte. Lose Drähte in Lötlöchern sind ungeeignet.

Zum Verkabeln beide Geräte ausschalten. Keine Verbindung zwischen 3V3 / 5V / VIN der Platinen herstellen; GPIOs sind nicht 5-V-tolerant. Der Aufbau hat gemeinsame Masse und ist nicht galvanisch getrennt. Bei längerer einseitiger Versorgung die Signalleitung trennen, um Rückspeisung über Schutzdioden zu vermeiden.

Der [Originalschaltplan Rev02 BOM05](https://www.prusa3d.com/downloads/Electronics_drawings/FDM-MK4-Gpio-02-(BOM05).pdf) zeigt OUT4–7 direkt über 330 Ω am TCA6408A (3,3-V-Pegel). OUT0–3 besitzen dagegen invertierende MOSFET-Stufen für bis zu 24 V. Die pauschalen Open-Drain-Aussagen der Hilfeseite sind für OUT6 missverständlich. Standard: **B0 = LOW-Ruhepegel, B1 = HIGH-Auslösung**. Revision und gemessenen Pegel vor dem ersten Druck prüfen. Für Rev02 ist OUT6 J6/8, GND J6/10 oder J6/1. OUT4 bleibt für den optionalen Ready-Taster frei.

```gcode
M264 P6 B0
M262 P6 B0
M264 P6 B0 ; Dashboard muss LOW zeigen
; einzeln pruefen:
M264 P6 B1 ; Dashboard muss HIGH zeigen
M264 P6 B0
```

Die Ausgabe `M264` bezeichnet den logischen Zustand am Expander. Keinen OUT0–3-Anschluss anstelle von OUT6 verwenden, ohne die elektrische Schaltung anzupassen.

## G-Code-Generator

1. Foto-Einstellungen unter Aufnahme/Eingang speichern.
2. Im Dashboard **G-code** öffnen.
3. Die drei erzeugten Blöcke unter PrusaSlicer → Drucker → Benutzerdefinierter G-Code an das Ende von **Start G-Code**, **Nach dem Schichtwechsel**, **End G-Code** ergänzen. Vorhandene Inhalte beibehalten.
4. Neu slicen. Nach Änderungen an Polarität oder Timing immer erneut erzeugen und neu slicen.

Der Generator benutzt **gespeicherte** Einstellungen. Keine Parkbewegungen und keine Änderung des Extrusions-/Koordinatenmodus; der Kopf bleibt an der jeweiligen Schichtwechselposition. Foto-Code wartet konservativ auf Verzögerung, Serie, Kamera und Mindestabstand. Dadurch verlängert sich der Druck.

Die Dateien in [`gcode/`](gcode/) sind fertige Standardvorlagen für OUT6. End-/Abbruch-Code setzt das Signal in Ruhe, stoppt aber nicht das ESP32-Programm. Nach dem Druck im Dashboard stoppen.

## Verhalten und Architektur

Die Aufnahme-Engine und der Ready-Taster laufen in einem eigenen FreeRTOS-Task (`src/Control.cpp`). Bluetooth-HID liegt in `src/Ble.cpp` (NimBLE), Validierung und NVS in `src/Settings.cpp`, die HTTP-API in `src/WebApi.cpp`; die zeitkritische Logik in `include/` ist ohne Arduino-Abhängigkeit nativ testbar. Statuswerte werden unter dem Mutex nur kopiert; JSON entsteht danach. Ein fester Log-Ring verhindert dynamische Logtexte im schnellen Task. Der Task läuft aktiv mit 2 ms und im Leerlauf mit 10 ms Periode. HTTP-Antworten und NVS-Schreibvorgänge werden außerhalb des Engine-Mutex ausgeführt; kurze Konfigurationswechsel sind synchronisiert. Tastenfreigabe wartet nicht auf einen `delay()` im Webserver. BLE-/Flash-Operationen und das ESP32-Betriebssystem erlauben trotzdem keine harte Echtzeitgarantie.

- BLE-Bereitschaft bedeutet Verbindung, erfolgreiche Verschlüsselung und aktivierte HID-Notifications. Das bestätigt keine Bereitschaft der Kamera-App.
- Jede n-te Aufnahme bedeutet Impuls 1, n+1, 2n+1 …; Neustart einer Sitzung beginnt neu zu zählen.
- Nur ein ausstehender Auftrag; keine unbegrenzte Warteschlange. Während Trennung oder Sperrzeit verworfene Impulse werden nicht nachgeholt.
- Bei BLE-Trennung wird eine ausstehende Serie verworfen. Standardmäßig wird zusätzlich die Aufnahme deaktiviert. Wird dieses Verhalten ausgeschaltet, bleibt das Programm aktiv, puffert aber keine Fotos.
- Neustart deaktiviert die Aufnahme. Es gibt bewusst keine gespeicherte automatische Freigabe.
- Start bei bereits aktivem Eingang erzeugt kein Foto: erst stabiler Ruhepegel, dann aktive Flanke.
- Sitzungs-/Gesamtzähler und die letzten 48 Ereignisse mit inkrementellem Abruf liegen im RAM und werden beim Neustart gelöscht. Konfiguration liegt im NVS.
- „Gesendet“ zählt Notification-Versuche. „Gefiltert“ zählt durch jeden-n-ten-Impuls ausgelassene Signale; Impulszählung erfolgt im aktiven GPIO-Aufnahmemodus.
- „Aufnahme stoppen“ betrifft nur Fotos; **Alles stoppen** beendet auch Ready-Impulse. Ein bereits gesetzter Ready-Zustand des Druckers wird dadurch nicht gelöscht.

## Netzwerk, Passwort und Updates

Eigenes WPA2-WLAN bleibt aktiv, optional zusätzlich Router-WLAN. Bei AP+STA folgt der AP dem Router-Kanal. `hostname.local` benötigt mDNS-Unterstützung im Netzwerk; IP-Adresse steht im Dashboard und seriellen Status. Maximal vier AP-Clients. Keine Portfreigabe oder Cloud-Anbindung nötig.

Beim ersten Start erzeugt der ESP32 ein individuelles Passwort. WLAN- und Admin-Passwort lassen sich später unabhängig ändern; Änderungen gelten erst nach Neustart. Passwortfelder liefern gespeicherte Geheimnisse niemals an den Browser zurück. Lokale HTTP-Basic-Authentifizierung schützt API und Dashboard, `X-Shutter: 1` schützt POST-Routen gegen einfache fremde Formularaufrufe. Die HTTP-Verbindung selbst ist nicht TLS-verschlüsselt; nur in vertrauenswürdigem Netz verwenden, keine Internet-Portfreigabe einrichten. BLE (NimBLE) verwendet Bonding mit Just Works und LE Secure Connections; beim ersten Koppeln gibt es keine PIN-Prüfung.

OTA über System → **firmware.bin** auswählen. Upload benötigt Authentifizierung und POST-Header, stoppt die Aufnahme und prüft ESP32-App-Header. `merged.bin`/Bootloader-Dateien werden abgelehnt. Firmware ist nicht kryptografisch signiert; nur vertrauenswürdige Projekt-Releases installieren. Für den Umstieg von Version 1 oder 2 auf 3 USB verwenden (neuer Bootloader, neuer Bluetooth-Stack). Ein fehlgeschlagenes Update startet nicht automatisch neu; Fehleranzeige prüfen. Bei erfolgreichem Update erfolgt Neustart.

Foto-JSON-Export enthält keine Netzwerkgeheimnisse. Die Diagnose enthält lokale IP und Ereignisse; vor Weitergabe prüfen. Vollständiges Löschen mit `pio run --target erase` entfernt Konfiguration und Kopplungen; danach neu flashen und alten Smartphone-Eintrag vergessen.

## Tests und Stand

```sh
g++ -std=c++17 -Iinclude test/trigger.cpp -o /tmp/trigger-test && /tmp/trigger-test
g++ -std=c++17 -Iinclude test/engine.cpp -o /tmp/engine-test && /tmp/engine-test
g++ -std=c++17 -Iinclude test/log.cpp -o /tmp/log-test && /tmp/log-test
g++ -std=c++17 -Iinclude test/ready.cpp -o /tmp/ready-test && /tmp/ready-test
pio run
python scripts/package_firmware.py
pip install playwright==1.56.0
python -m playwright install chromium
python test/browser.py
python test/polling.py
```

[Prüfprotokoll](docs/validation.md). C++-Tests prüfen den tatsächlichen Aufnahme-Scheduler, Eingangsfilter, Logpuffer und Ready-Taster. Browserprüfungen simulieren Geräteantworten und das Installer-Element, prüfen also keine echte ESP32-HTTP-/BLE-/USB-Kommunikation. **Hardwaretests mit CORE One, ESP32 und Smartphone stehen für 3.0 aus.** Vor einem langen Druck: Pegel prüfen, zehn Fotoimpulse testen, BLE trennen/neu verbinden und Neustartverhalten prüfen.

CI baut Firmware/Installer und führt C++- und Browsertests aus. GitHub Pages veröffentlicht den eingecheckten Installer inklusive Firmware-Dateien aus **main / (root)**. Die Startseite leitet nach `site/` weiter. Für ein neues Release zuerst die Firmware bauen, `scripts/package_firmware.py` ausführen und die aktualisierten Dateien unter `site/firmware/` mit dem dazugehörigen Quellcode committen. CI stellt zusätzlich ein Firmware-Artefakt bereit.

## Quellen

- [Prusa GPIO Module](https://help.prusa3d.com/article/gpio-module_734695)
- [Prusa Timelapse mit ESP32/Bluetooth](https://blog.prusa3d.com/create_3d_print_timelapses_using_prusa_gpio_hackerboard_117339/)
- [Originalschaltplan Rev02 BOM05](https://www.prusa3d.com/downloads/Electronics_drawings/FDM-MK4-Gpio-02-(BOM05).pdf)
- [Prusa-Firmware: M262–M268](https://github.com/prusa3d/Prusa-Firmware-Buddy/blob/master/src/marlin_stubs/M262-M268.cpp)
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/)

Gestaltung: native HTML-Elemente, wenig CSS, Orange/Schwarz/Weiß. Icons ausschließlich aus Lucide; keine selbst gezeichneten Grafiken.

Eigenständiges Community-Projekt, nicht von Prusa unterstützt. Eigener Code und Anschlussplan: MIT. Externe Unterlagen/Bibliotheken haben eigene Lizenzen. Lucide-SVGs 0.468.0: ISC, siehe `web/lucide-LICENSE`. ESP Web Tools und dessen Lizenzen liegen unter `site/vendor/`.
