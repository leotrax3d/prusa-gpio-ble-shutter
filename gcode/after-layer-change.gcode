; Am ENDE von "Nach dem Schichtwechsel" ergänzen.
; Keine Parkbewegung: kein Eingriff in Extrusion/Koordinatenmodus.
M400        ; Bewegungsplaner abwarten
G4 P150     ; Mechanik beruhigen
M264 P7 B1  ; OUT7 HIGH: steigende Flanke
G4 P150     ; länger als Entprellung (Standard 30 ms)
M264 P7 B0  ; zurück auf LOW
G4 P350     ; Zeit für Smartphone-Aufnahme (bei Bedarf erhöhen)
