; Am ENDE von "Nach dem Schichtwechsel" ergaenzen.
; Standardprofil: 1 Bild, 0 ms Verzoegerung, 1000 ms Mindestabstand.
; Fuer andere Einstellungen den Generator im Dashboard verwenden.
M400        ; Bewegungen abwarten
G4 P150     ; Mechanik beruhigen
M264 P7 B1  ; Fotoimpuls HIGH
G4 P150     ; laenger als Entprellzeit
M264 P7 B0  ; Ruhepegel LOW
G4 P1580    ; Taste (80), Kamera (500), Mindestabstand (1000)
