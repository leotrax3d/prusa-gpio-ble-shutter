; Am ENDE des vorhandenen Start-G-Codes ergaenzen.
; Standardprofil: GPIO27, OUT7 aktiv HIGH, 30 ms Entprellung.
M264 P7 B0 ; Ausgangslatch vor Richtungswechsel auf Ruhe setzen
M262 P7 B0 ; OUT7 als Ausgang
M264 P7 B0 ; Ruhepegel LOW
G4 P150   ; Ruhepegel stabilisieren
