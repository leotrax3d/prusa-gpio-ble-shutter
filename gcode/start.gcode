; Am ENDE des vorhandenen Start-G-Codes ergaenzen.
; Standardprofil: GPIO27, OUT6 aktiv HIGH, 30 ms Entprellung.
M264 P6 B0 ; Ausgangslatch vor Richtungswechsel auf Ruhe setzen
M262 P6 B0 ; OUT6 als Ausgang
M264 P6 B0 ; Ruhepegel LOW
G4 P150   ; Ruhepegel stabilisieren
