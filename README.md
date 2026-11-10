# Prusa GPIO BLE Shutter

Bluetooth-Fotoauslöser für **Prusa CORE One + GPIO Hackerboard + klassisches ESP32 Dev Board (ESP32-WROOM-32 / DevKit)**. Ein OUT7-Impuls sendet genau einen Lauter- oder Leiser-Tastendruck an ein gekoppeltes Android-Smartphone oder iPhone. Die Kamera-App muss diese Taste als Fotoauslöser unterstützen und geöffnet sein.

![Anschlussplan](docs/wiring.svg)

## Hardware und Verdrahtung

Benötigt: ESP32 DevKit mit BLE, Prusa GPIO Hackerboard samt Original-I²C-Kabel, zwei passende Steckleitungen, USB-Netzteil. Diese Firmware ist für das klassische ESP32 DevKit kompiliert. ESP32-S2 hat kein Bluetooth; C3/S3 benötigen eigene Boardkonfiguration und Pinwahl. GPIO27 ist die **GPIO-Nummer**, keine Steckleistenposition und keine pauschale „D27“-Zuordnung.

1. Drucker ausschalten, abkühlen lassen, ESP32 von USB trennen.
2. Hackerboard gemäß [Prusa-Anleitung](https://help.prusa3d.com/article/gpio-module_734695) am vorgesehenen I²C-Anschluss des CORE One installieren.
3. JP7 kann offen bleiben: Die Firmware aktiviert am ESP32 `INPUT_PULLUP`. Eine Lötbrücke ist für diesen Aufbau nicht erforderlich.
4. OUT7 → ESP32 GPIO27. Optional kann ein 1-kΩ-Serienwiderstand ergänzt werden; er ist für die normale 3,3-V-Verbindung nicht erforderlich. GND → ESP32 GND. Für Rev02 BOM05 ist OUT7 J6/9 und GND J6/10 (alternativ J6/1). Boardbeschriftung und Revision prüfen.
5. ESP32 separat über USB versorgen. Keine Verbindung zu 3V3, VIN, 5 V oder 24 V des Druckers herstellen. ESP32-Eingänge sind nicht 5-V-tolerant. Leitungen kurz halten und von Motor-/Heizleitungen fernhalten.

Ohne Löten funktioniert der Aufbau nur mit bereits montierten Stiftleisten oder geeigneten zuverlässigen Prüfkontakten. Lose Drähte in Lötlöchern sind ungeeignet.

Die Masse ist gemeinsam; diese Schaltung ist nicht galvanisch getrennt. Ein optionaler Serienwiderstand begrenzt Fehlerströme, ersetzt aber keinen Pegelwandler. Beide Geräte im Betrieb versorgen; Signalleitung bei längerem Betrieb mit nur einer versorgten Platine trennen, um Rückspeisung über Schutzdioden zu vermeiden.

## Warum OUT7 und welche Polarität?

Die Prusa-Hilfeseite enthält widersprüchliche Aussagen zur Ausgangspolarität. Der [Originalschaltplan FDM-MK4-Gpio Rev02 BOM05](https://www.prusa3d.com/downloads/Electronics_drawings/FDM-MK4-Gpio-02-(BOM05).pdf) zeigt: OUT0–3 führen über invertierende MOSFET-Stufen, **OUT4–7 dagegen über 330 Ω direkt zum TCA6408A**. OUT7 wird deshalb hier mit B0 als LOW-Ruhepegel und B1 als HIGH-Auslösepegel verwendet. Der Pull-up ist für definierte Pegel bei hochohmigem Ausgang vorgesehen.

Vor der ersten Nutzung bei deaktiviertem Auslöser prüfen:

```gcode
M262 P7 B0
M264 P7 B0
; im Webinterface GPIO27 LOW / mit Multimeter ungefähr 0 V
M264 P7 B1
; im Webinterface GPIO27 HIGH / mit Multimeter ungefähr 3,3 V
M264 P7 B0
```

G-Code über eine geeignete Drucker-Konsole oder eine kleine G-Code-Datei auf USB ausführen; jede Stufe einzeln prüfen. Bei anderer Hardware-Revision und umgekehrtem Ergebnis aktive Polarität sowie Ruhe-/Impulswerte im G-Code gemeinsam anpassen. Bei unerwartetem Spannungspegel Verdrahtung prüfen. Keine 24-V-Ausgänge an den ESP32 anschließen.

## Bauen und flashen

[PlatformIO](https://platformio.org/) installieren (z. B. VS Code-Erweiterung):

```sh
pio run
pio run --target upload
pio device monitor --baud 115200
```

Bei Bedarf Upload-Port mit `--upload-port /dev/ttyUSB0` angeben (Windows z. B. COM3). Zum Flashen die Signalleitung vom Drucker trennen. ESP32 ggf. mit BOOT-Taste in den Downloadmodus bringen. Plattform ist auf `espressif32@6.10.0` fixiert (Arduino-ESP32 2.0.17); keine weiteren Firmware-Bibliotheken erforderlich. Die große App-Partition lässt keinen Platz für OTA; Upload erfolgt per USB.

## Einrichten

1. Serienmonitor öffnen, ESP32 neu starten. Das pro Gerät erzeugte 16-stellige WLAN-Passwort wird ausgegeben. Es wird lokal im NVS gespeichert und ist nicht im Repository enthalten.
2. Mit WLAN **Prusa-Shutter** verbinden, Hinweis „kein Internet“ akzeptieren. `http://192.168.4.1` öffnen. Benutzer **admin**, Passwort wie WLAN. HTTP bleibt auf das eigene WPA2-WLAN beschränkt; kein Router-/Cloudzugang wird eingerichtet.
3. In den Bluetooth-Einstellungen des Smartphones **Prusa Shutter** koppeln. Das Gerät nutzt BLE HID mit Bonding. Das erstmalige Pairing hat keine PIN-Bestätigung (Just Works); in eigener Nähe koppeln. Nach Trennung wird wieder geworben.
4. Kamera-App öffnen. Android: Lautstärketastenfunktion „Foto aufnehmen“ wählen. Unter iOS mit der gewählten Kamera-App prüfen. Lauter oder Leiser im Webinterface auswählen, speichern, **Testfoto** drücken.
5. G-Code-Pegeltest durchführen, danach Eingang aktivieren. Nach Aktivierung muss der Eingang erst einen stabilen Ruhepegel sehen, bevor ein Impuls auslösen kann.

Das Smartphone kann Bluetooth und WLAN gleichzeitig nutzen; WLAN ist nach Einrichtung nicht für den Druckbetrieb nötig. Passwortänderung/kompletter Reset über vollständiges Flash-Löschen (`pio run --target erase`) und erneutes Flashen. Das löscht Einstellungen und BLE-Bonds; Smartphone-Eintrag danach ebenfalls vergessen und neu koppeln. Das WLAN-Passwort wird über USB ausgegeben: Serienprotokolle nicht öffentlich teilen.

## PrusaSlicer

Unter Drucker → Benutzerdefinierter G-Code die Dateien aus [gcode/](gcode/) ergänzen; vorhandenen Drucker-Code beibehalten:

- `start.gcode`: ans Ende des Start-Codes.
- `after-layer-change.gcode`: ans Ende von „Nach dem Schichtwechsel“.
- `end.gcode`: End-Code, ggf. auch Abbruch-Code.

Die Beispiele enthalten bewusst keine Parkbewegung. Es entstehen Fotos an der jeweiligen Schichtwechselposition; für einen festen Bildausschnitt ist ein separat geprüftes Park-/Retraktionskonzept erforderlich. Der Impuls dauert 150 ms, Entprellung standardmäßig 30 ms. Bei Entprellung ≥150 ms muss der Impuls verlängert werden. Mindestabstand standardmäßig 1000 ms; schnellere Schichten werden übersprungen. Kameraaufnahmezeit und Wartezeit im G-Code auf das Telefon abstimmen. Es gibt keine Rückmeldung vom Telefon, ob ein Foto gespeichert wurde.

## Webinterface und Verhalten

- Live-Status für BLE, GPIO-Pegel, Aktivierung und Zähler.
- Testauslösung auch bei deaktiviertem Eingang; BLE-Verbindung und Mindestabstand gelten weiterhin.
- Lauter/Leiser, aktive Polarität, Entprellung (5–200 ms), Mindestabstand (300–10000 ms), Tastendauer (30–300 ms).
- Einstellungen bleiben erhalten; Aktivierung und Zähler werden bei Neustart zurückgesetzt. Speichern deaktiviert den Eingang.
- Ein stabiler Impuls erzeugt maximal einen Tastendruck. Keine Warteschlange: während Trennung oder Sperrzeit verworfene Impulse werden später nicht nachgeholt.
- „Verbunden“ heißt BLE-Verbindung, nicht nachgewiesene HID-Bereitschaft. „Gesendet“ zählt Notification-Versuche, keine bestätigten Bilder.
- BLE-Tastenfreigabe erfolgt ohne `delay()`-Wartezeit. Der synchrone Arduino-Webserver kann bei langsamen Clients trotzdem Schleifen verzögern; Webinterface während einer Aufnahme möglichst nicht bedienen.

API ist per HTTP Basic Auth geschützt. POST benötigt zusätzlich `X-Shutter: 1` (Schutz gegen einfache fremde Formularaufrufe). Routen: `GET /api/status`, `POST /api/arm` (`armed=0/1`), `POST /api/test`, `POST /api/config` (`activeHigh`, `volumeDown`, `debounce`, `cooldown`, `hold`). POST-Parameter sind form-url-encoded; Werte werden vor Speichern geprüft.

## Validierung

CI baut die Firmware und prüft Entprellung, gehaltene Pegel, Startunterdrückung, erneutes Auslösen und Millisekundenüberlauf des Eingangsfilters:

```sh
g++ -std=c++11 -Iinclude test/trigger.cpp -o /tmp/trigger-test
/tmp/trigger-test
pio run
```

Ein realer Test mit CORE One, ESP32 und Smartphone ist zusätzlich erforderlich. Vor einem langen Druck: zehn Impulse prüfen, BLE trennen/wiederverbinden, ESP32 neu starten (Eingang muss deaktiviert bleiben), Lauter/Leiser separat prüfen und kontrollieren, dass jedes Mal genau ein Foto entsteht.

## Quellen

Gelesen am 10.11.2026:

- [Prusa GPIO Module – Installation, Grenzwerte, JP7, G-Code](https://help.prusa3d.com/article/gpio-module_734695)
- [Prusa-Timelapse-Anleitung – Smartphone via BLE-Lautstärketaste](https://blog.prusa3d.com/create_3d_print_timelapses_using_prusa_gpio_hackerboard_117339/)
- [Prusa Open Source – Originalschaltplan-Verzeichnis](https://www.prusa3d.com/page/open-source-at-prusa-research_236812/)
- [Originalschaltplan Rev02 BOM05](https://www.prusa3d.com/downloads/Electronics_drawings/FDM-MK4-Gpio-02-(BOM05).pdf)

Eigenständiges Community-Projekt, nicht von Prusa unterstützt. Code und eigener Anschlussplan stehen unter MIT; verlinkte Prusa-Unterlagen haben eigene Lizenzen.
