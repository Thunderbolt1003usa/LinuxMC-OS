📘 LinuxMC OS – Handbuch & Entwicklerdokumentation

Für Arduino Nano (ATmega328P) – Stand 27.04.2026
1. Systemüberblick

LinuxMC OS ist ein minimalistisches, mehrbenutzerfähiges Betriebssystem für den Arduino Nano. Es läuft über die serielle USB-Schnittstelle (115200 Baud). Nach dem Login erscheint eine Shell, die ein flaches Dateisystem im EEPROM, LED‑Steuerung, Benutzerverwaltung und eine konfigurierbare Message‑of‑the‑Day (MOTD) bietet.

Kernmerkmale:
Feature	Beschreibung
Anmeldung	=== LINUXMC OS ===, Benutzername + Passwort (Eingabe unsichtbar)
Mehrbenutzer	Max. 5 Nutzer; ADMIN ist Root, nicht löschbar
Dateisystem	Flach, je Benutzer eigene Dateien; Zugriff über Indexnummern
EEPROM-Speicher	1 kB; Verwaltung dynamisch, keine künstlichen Limits
MOTD	Individuell pro Benutzer
LED	LED ON, OFF, BLINK <ms>
Reset	Befehl RESET sowie Taster an Pin 2 (Werkseinstellungen)
Erweiterbarkeit	Befehlstabelle als Array, neue Befehle leicht hinzufügbar
2. Funktionen im Detail
2.1 Login-Bildschirm

    Nach jedem Start oder nach EXIT wird der Bildschirm gelöscht (40 Leerzeilen).

    Überschrift === LINUXMC OS ===

    USERNAME: (mit Echo, Großbuchstaben)

    PASSWORD: (ohne Echo, Großbuchstaben intern)

    Drei Fehlversuche → NO CARRIER + 2 Sekunden Pause

2.2 Benutzerverwaltung

    ADMIN (ID 0) ist fest, kann nicht gelöscht werden und hat Zugriff auf Admin‑Befehle.

    ADDUSER <name> <password> (nur ADMIN) legt einen neuen Nutzer an.

    DELUSER <name> (nur ADMIN) löscht einen Nutzer und alle seine Dateien.

    PASSWD ändert das eigene Passwort (altes muss bestätigt werden).

2.3 Dateisystem

    Flach – keine Ordner.

    Jeder Nutzer sieht nur seine eigenen Dateien.

    EDIT <dateiname> – interaktiver Editor; beenden mit einer Zeile, die nur . enthält.

    LS – zeigt alle eigenen Dateien mit Indexnummer (0, 1, 2 …)

    READ <index> – zeigt den Inhalt der Datei mit dem Index an.

    RM <index> – löscht die Datei mit dem Index.

2.4 Motd (Message of the Day)

    Standard-MOTD für ADMIN: === WELCOME ADMIN ===

    Neue Nutzer erhalten === WELCOME <NAME> ===

    MOTD (ohne Argument) zeigt die aktuelle MOTD.

    MOTD <text> setzt eine neue MOTD (max. 31 Zeichen).

2.5 LED-Steuerung

    LED ON / LED OFF schaltet die eingebaute LED (Pin 13).

    LED BLINK 500 lässt sie im 500 ms‑Takt blinken (nicht blockierend).

2.6 Systembefehle

    HELP – Liste aller verfügbaren Befehle + Lizenzinfo.

    CLEAR – löscht den Terminalbildschirm.

    RESET (nur ADMIN) – setzt das gesamte System auf Werkseinstellungen zurück und meldet ab.

    EXIT – meldet ab, löscht den Bildschirm und startet den Login‑Bildschirm neu.

3. Speicherlayout im EEPROM
Adresse	Größe	Inhalt
0	1	Magic Byte (0x4F)
1	1	Anzahl Benutzer
2–321	je 64 B × 5	Benutzerdatensätze (Name, Passwort, MOTD)
322–711	je 26 B × 15	Dateitabelle (Name, Besitzer, Typ, Größe, Startadresse)
712–1023	312 B	Datenbereich für Dateiinhalte
4. So fügst du neue Befehle hinzu

Das System ist modular aufgebaut. Ein neuer Befehl erfordert drei Schritte:
Schritt 1 – Handlerfunktion schreiben
cpp

void cmdMeinBefehl(char* arg) {
  // Deine Logik hier
  Serial.println(F("MEIN BEFEHL AUSGEFÜHRT"));
}

Schritt 2 – In die Befehlstabelle eintragen

Das Array commands[] enthält alle Befehle. Ergänze einen neuen Eintrag vor der Zeile {NULL, NULL, false}:
cpp

Command commands[] = {
  ...
  {"MEINBEF", cmdMeinBefehl, false},  // false = jeder darf, true = nur ADMIN
  {NULL, NULL, false}
};

Schritt 3 – Funktionsdeklaration hinzufügen

Füge ganz oben, wo die anderen void cmd... stehen, deine Deklaration ein:
cpp

void cmdMeinBefehl(char*);

Fertig! Der neue Befehl wird automatisch von HELP aufgelistet und von der Shell erkannt.
5. Hinweise zur Kompilierung

    Board: Arduino Nano

    Prozessor: ATmega328P (Old Bootloader)

    Programmer: AVRISP mkII (oder Standard)

    Serieller Monitor: 115200 Baud, Zeilenende Neue Zeile

Benötigte Bibliotheken:

    EEPROM.h (Arduino-Standard)

    string.h (für strcasecmp – wird normalerweise automatisch gelinkt)

6. Lizenz
text

LinuxMC OS ist lizenziert unter der GNU General Public License v3.0.
Copyright (c) 2025 LinuxMC IPV& INF
Quellcode: github.com/Thunderbolt1003USA/LinuxMC-OS
