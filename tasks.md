# Aufgaben: Sliverbar als generisches X11-Panel

## Zielbild

Sliverbar soll auf Linux-Systemen mit X11 und einem weitgehend
EWMH-kompatiblen Window Manager lauffaehig sein. Window-Manager-spezifische
Integrationen duerfen Zusatzfunktionen bereitstellen, aber nicht Voraussetzung
fuer den Start oder den grundlegenden Panelbetrieb sein.

"Jedes Linux-System mit Xorg" ist dabei nicht als Garantie fuer beliebige,
nicht EWMH-kompatible Window Manager zu verstehen. Unterstuetzte
Laufzeitbibliotheken, Architekturen und getestete Umgebungen muessen explizit
dokumentiert werden.

## Bugfix: Unplausible WLAN-Signalstaerke

Beobachtete Reproduktion am 20.07.2026:

- Sliverbar verwendet bevorzugt `nmcli` und erhaelt fuer das aktive WLAN 63 %.
- `/proc/net/wireless` meldet fuer `wlo1` eine Linkqualitaet von 51/70, also
  gerundet etwa 73 %.
- Conky zeigt `${wireless_link_qual_perc wlo1}` an und liegt damit in der
  plausibleren Groessenordnung des Kernelwerts.
- Sliverbars vorhandener Kernel-Fallback berechnet ebenfalls aus 70, wird aber
  nur benutzt, wenn `nmcli` oder dessen Parser fehlschlaegt. Dadurch werden im
  Normalbetrieb zwei unterschiedlich skalierte Messwerte verglichen.

- [x] Signalquelle und Bedeutung des angezeigten Prozentwerts eindeutig
  festlegen und dokumentieren: Linkqualitaet des aktiven Interfaces oder die
  von NetworkManager normalisierte Signalstaerke.
- [x] Fuer eine mit Conky vergleichbare Anzeige die Kernel-Linkqualitaet des
  aktiven WLAN-Interfaces bevorzugen, sofern sie verfuegbar und gueltig ist.
  `nmcli` kann weiterhin SSID und Verbindungsstatus liefern sowie als Fallback
  dienen.
- [x] Bei der Umrechnung von Kernelqualitaet in Prozent korrekt runden statt
  den Gleitkommawert nur abzuschneiden und das Ergebnis auf 0 bis 100
  begrenzen.
- [x] Nicht einfach das erste aktive WLAN-Interface aus `/sys/class/net`
  verwenden. Bei mehreren WLAN-Adaptern das tatsaechlich verbundene
  beziehungsweise fuer die Standardroute verwendete Interface bestimmen.
- [x] Beruecksichtigen, dass `/proc/net/wireless` nicht mit jedem Treiber eine
  brauchbare Qualitaet liefert. Eine priorisierte Backend-Reihenfolge mit
  sauberem Fallback fuer Kernel/nl80211, NetworkManager und "nicht verfuegbar"
  definieren.
- [x] In `sliverbar --diagnose` WLAN-Interface, verwendetes Signal-Backend,
  Rohwert und daraus berechneten Prozentwert ausgeben.
- [x] Parser- und Berechnungstests mit festen Beispielen ergaenzen, darunter
  51/70 -> 73 %, ungueltige Werte, Grenzwerte, mehrere Access Points mit
  gleicher SSID und mehrere WLAN-Interfaces.
- [ ] Einen Laufzeittest vorsehen, der Sliverbars Wert mit der ausgewaehlten
  Referenzquelle vergleicht, ohne einen bestimmten Prozentwert fuer alle
  Treiber vorauszusetzen.

Akzeptanzkriterium: Sliverbar zeigt fuer das tatsaechlich aktive WLAN-Interface
einen korrekt gerundeten Wert aus der dokumentierten, besten verfuegbaren
Quelle. Auf dem reproduzierenden System entspricht die Anzeige bei 51/70 rund
73 % statt dem abweichend skalierten NetworkManager-Wert von 63 %.

## Prioritaet 1: Window Manager entkoppeln

- [x] Eine interne Schnittstelle fuer Window-Manager-Funktionen definieren:
  - Arbeitsflaechen ermitteln und aktualisieren;
  - aktive Arbeitsflaeche wechseln;
  - belegte, fokussierte und dringende Arbeitsflaechen darstellen;
  - Titel des aktiven Fensters verfolgen.
- [x] Ein generisches EWMH-Backend als Standard implementieren.
  - Relevante Eigenschaften sind mindestens `_NET_CURRENT_DESKTOP`,
    `_NET_NUMBER_OF_DESKTOPS`, `_NET_DESKTOP_NAMES`, `_NET_WM_DESKTOP`,
    `_NET_ACTIVE_WINDOW` und die Dringlichkeitsinformationen der Fenster.
- [x] Die bestehende `bspc subscribe report`-Integration in ein optionales
  bspwm-Backend verschieben.
- [x] Backend-Auswahl ueber automatische Erkennung und eine explizite
  Konfigurationsoption erlauben.
- [x] Sliverbar ohne `bspc` starten und dauerhaft betreiben koennen.
- [x] Einen eingeschraenkten Betrieb ohne verfuegbares Workspace-Backend
  ermoeglichen; das Workspace-Modul wird dann ausgeblendet, statt das Panel zu
  beenden.
- [x] Tests fuer das EWMH-Backend, das bspwm-Backend und den Betrieb ohne
  Workspace-Backend hinzufuegen.

Akzeptanzkriterium: Sliverbar startet unter Xvfb ohne installiertes `bspc`,
bleibt aktiv und zeigt alle vom Window Manager unabhaengigen Module an.

## Prioritaet 2: Neutrale Konfiguration und optionale Module

- [x] Benutzerspezifische Standardwerte entfernen oder neutralisieren:
  - keine Pfade unter `~/.config/bspwm` automatisch einsetzen;
  - keinen bestimmten Terminalemulator voraussetzen;
  - Wetterort und Sprache nicht fest auf Muenchen und Deutsch setzen;
  - robuste Standardschrift mit dokumentierter Font-Fallback-Strategie
    verwenden;
  - Icon-Schrift optional machen, damit fehlende Nerd Fonts den Betrieb nicht
    unbrauchbar machen.
- [x] Module einzeln per Konfiguration aktivieren, deaktivieren oder automatisch
  erkennen lassen.
- [x] Fehlende optionale Programme wie `pactl`, `amixer`, `nmcli`, `xrandr`,
  `curl`, `notify-send` und `xdg-open` als normale Laufzeitsituation behandeln.
- [x] Fuer jedes Modul festlegen, welche Backends und externen Programme
  optional beziehungsweise zwingend sind.
- [x] Eine Diagnoseoption wie `sliverbar --diagnose` vorsehen, die erkannte
  Backends, fehlende optionale Programme, Display, Window-Manager-Backend,
  Schriften und Konfigurationspfad ausgibt.
- [x] Tests sicherstellen, dass fehlende optionale Programme weder Absturz noch
  Beendigung des Panels verursachen.

Akzeptanzkriterium: Eine minimale Konfiguration startet nur Uhr und Fenstertitel
und benoetigt ausser den grafischen Laufzeitbibliotheken keine externen
Hilfsprogramme.

## Querschnittsaufgabe: Standardanwendungen und Anwendungsrollen

Aktuell sind mehrere Klickziele fest verdrahtet: CPU startet `btop`, Netzwerk
startet `nmtui`, Lautstaerke startet `pulsemixer` und Terminalprogramme werden
mit dem fest konfigurierten `alacritty -e` ausgefuehrt. Das soll durch eine
einheitliche, portable Startlogik ersetzt werden.

Freedesktop/XDG definiert Standardanwendungen fuer MIME-Typen und URI-Schemata.
Es gibt jedoch keine allgemeine Standardzuordnung fuer die semantischen Rollen
"Systemmonitor" oder "Netzwerkkonfiguration". Desktop-Kategorien wie `System`,
`Monitor`, `Network` und `Settings` klassifizieren Programme fuer Menues, legen
aber keine bevorzugte Anwendung fest.

- [x] Eine zentrale Launcher-Schnittstelle definieren, die folgende Ziele ohne
  Shell-Auswertung starten kann:
  - Standardanwendung fuer einen MIME-Typ oder ein URI-Schema;
  - Anwendung ueber ihre Desktop-Datei-ID mit korrekter Beruecksichtigung von
    `Exec`, `TryExec`, `Terminal` und D-Bus-Aktivierung;
  - Terminalprogramm ueber den bevorzugten Terminal-Launcher;
  - expliziten Befehl als gepruefte argv-Liste.
- [x] Desktop-Dateien nicht mit einem vereinfachten eigenen `Exec`-Parser
  starten. Dafuer GIO oder eine andere standardkonforme Desktop-Entry-
  Implementierung verwenden.
- [x] Fuer automatisch aufgeloeste Anwendungsrollen folgende Reihenfolge
  verwenden und dokumentieren:
  1. explizite Sliverbar-Konfiguration als Benutzer-Override;
  2. standardisierte XDG-Zuordnung, falls fuer das Ziel ein MIME-Typ oder
     URI-Schema existiert;
  3. standardisierte oder Desktop-spezifische Praeferenz, falls tatsaechlich
     vorhanden;
  4. dokumentierte automatische Kandidatenerkennung;
  5. keine Klickaktion, wenn kein passendes Ziel existiert.
- [x] Keine kuenstlichen MIME-Typen oder URI-Schemata erfinden, um eine nicht
  vorhandene XDG-Standardrolle vorzuspiegeln.

### Bevorzugtes Terminal

- [x] Einen portablen Terminal-Launcher untersuchen und implementieren. Dabei
  `xdg-terminal-exec` verwenden, wenn es installiert und nutzbar ist, sowie
  sinnvolle Desktop- und Umgebungs-Fallbacks pruefen.
- [x] `terminal=alacritty` durch `terminal=auto` beziehungsweise eine optionale
  explizite Terminalkonfiguration ersetzen.
- [x] Unterschiede der Ausfuehrungsoptionen verschiedener Terminals nicht durch
  die Annahme abbilden, dass jedes Terminal `-e` gleich interpretiert.
- [x] Falls kein Terminal ermittelt werden kann, terminalgebundene Klickziele
  deaktivieren, ohne den Panelbetrieb zu beeintraechtigen.

### Systemmonitor

- [x] Den fest codierten CPU-Klick auf `btop` entfernen.
- [x] Eine Konfigurationsrolle `system_monitor` vorsehen, die eine Desktop-ID
  oder argv-Liste akzeptiert und standardmaessig auf `auto` steht.
- [x] Bei `auto` zunaechst eine tatsaechlich konfigurierte Desktop-Praeferenz
  nutzen, falls die Umgebung eine solche bereitstellt. Da XDG keine
  Standardrolle hierfuer definiert, danach nur dokumentierte installierte
  Kandidaten wie grafische Systemmonitore oder `btop`/`htop` pruefen.
- [x] Terminalbasierte Kandidaten ueber den ermittelten Standard-Terminal-
  Launcher starten; grafische Desktop-Anwendungen direkt aktivieren.

### Netzwerkkonfiguration

- [x] Den fest codierten Netzwerk-Klick auf `nmtui` entfernen.
- [x] Eine Konfigurationsrolle `network_settings` vorsehen, die eine Desktop-ID
  oder argv-Liste akzeptiert und standardmaessig auf `auto` steht.
- [x] Bei `auto` die aktive Netzwerkverwaltung beruecksichtigen. Fuer
  NetworkManager koennen beispielsweise ein installierter grafischer Editor,
  ein passendes Desktop-Control-Center oder als letzter Fallback `nmtui` im
  bevorzugten Terminal verwendet werden.
- [x] Keine beliebige Anwendung allein aufgrund der allgemeinen Kategorie
  `Network` starten; darunter fallen auch Browser und andere unpassende
  Programme.

### Weitere Klickziele

- [x] Dieselbe Launcher-Schnittstelle fuer Kalender, Wetterbild,
  Lautstaerkemixer, Launcher und Power-Menue verwenden.
- [x] Fuer Bilddateien weiterhin die XDG-Standardanwendung des MIME-Typs
  verwenden.
- [x] Fuer Lautstaerkekonfiguration eine eigene optionale Rolle
  `volume_settings` einfuehren, statt `pulsemixer` vorauszusetzen.
- [x] Erkannte Ziele, Aufloesungsweg und fehlende Rollen in
  `sliverbar --diagnose` anzeigen.
- [ ] Tests fuer Desktop-IDs, MIME-Handler, Terminal- und GUI-Anwendungen,
  Overrides, Fallback-Reihenfolge, fehlende Anwendungen und fehlerhafte Starts
  hinzufuegen.

Akzeptanzkriterium: Kein Panelblock setzt `alacritty`, `btop`, `nmtui` oder
`pulsemixer` fest voraus. Wo das System eine standardisierte Zuordnung kennt,
wird diese verwendet. Fuer nicht standardisierte Rollen greift eine explizite,
dokumentierte Konfiguration oder eine transparente automatische Erkennung; ein
fehlendes Ziel beeintraechtigt Sliverbar nicht.

## Feature: Integrierter Anwendungsstarter mit Suche

Der linke Launcher-Block soll optional einen nativen Anwendungsstarter von
Sliverbar oeffnen. Der erste Umfang entspricht Rofis `drun`-Anwendungsmodus:
installierte Desktop-Anwendungen anzeigen, durchsuchen, auswaehlen und starten.
Weitere Rofi-Modi wie freie Shellbefehle, Fensterwechsel, SSH oder Plugins sind
nicht Teil des ersten Umfangs.

- [x] Eine Konfigurationsoption wie
  `application_launcher=internal|external|auto|disabled` definieren.
- [x] Den internen Starter als bevorzugte generische Variante anbieten, ohne
  die bestehende Moeglichkeit eines externen Launchers wie Rofi zu entfernen.
- [x] Bei `external` eine Desktop-ID oder gepruefte argv-Liste aus der zentralen
  Launcher-Schnittstelle verwenden; keine Shellauswertung einfuehren.
- [x] Linksklick auf den ganz linken Launcher-Block oeffnet beziehungsweise
  schliesst den internen Starter.

### Anwendungen ermitteln und starten

- [x] Installierte Anwendungen ueber GIO `GAppInfo` ermitteln, statt die
  XDG-Verzeichnisse und Desktop-Dateien mit einem unvollstaendigen eigenen
  Parser auszuwerten.
- [x] Nur Anwendungen anzeigen, fuer die `g_app_info_should_show()` wahr ist.
  Damit muessen unter anderem `Hidden`, `NoDisplay`, `OnlyShowIn` und
  `NotShowIn` entsprechend der Desktopumgebung beruecksichtigt werden.
- [x] Pro Eintrag mindestens lokalisierter Anzeigename, generischer Name,
  Beschreibung, Desktop-ID und ausfuehrbares Programm als Suchdaten erfassen.
- [x] Die ausgewaehlte Anwendung ueber `g_app_info_launch()` und einen passenden
  Launch-Kontext starten. Desktop-Entry-Feldcodes, D-Bus-Aktivierung und
  Terminalanwendungen nicht selbst nachimplementieren.
- [x] Veraenderungen durch neu installierte oder entfernte Anwendungen erkennen
  oder den Anwendungscache beim naechsten Oeffnen sicher aktualisieren.
- [x] Fehlerhafte oder nicht mehr vorhandene Desktop-Eintraege ueberspringen;
  ein fehlgeschlagener Start darf Sliverbar nicht beenden.

### Suche und Sortierung

- [x] Beim Oeffnen den Tastaturfokus direkt in das Suchfeld setzen.
- [x] Unicode-faehige, von Gross-/Kleinschreibung unabhaengige Suche vorsehen,
  beispielsweise mit GLib-Casefolding.
- [x] Fuer die erste Version eine nachvollziehbare tokenbasierte Teilstring-
  Suche implementieren: Namensanfang vor Wortanfang, Wortanfang vor sonstigem
  Treffer.
- [x] Fuzzy Matching erst als spaetere, getrennt testbare Verbesserung
  ergaenzen; Trefferreihenfolge muss stabil und erklaerbar bleiben.
- [x] Ohne Suchtext Anwendungen alphabetisch oder optional nach zuletzt
  verwendeten Anwendungen sortieren.
- [ ] Nutzungshistorie nur lokal und optional unter
  `$XDG_STATE_HOME/sliverbar` speichern; eine Deaktivierung und ein einfaches
  Loeschen der Historie vorsehen.

### Native X11-Oberflaeche

- [x] Ein eigenes X11-Fenster auf dem Monitor des Panels erzeugen, mit Cairo
  und Pango im bestehenden Farbschema darstellen und am Launcher-Block
  verankern oder konfigurierbar zentrieren.
- [x] Suchfeld, markierten Treffer und eine begrenzte scrollbare Ergebnisliste
  darstellen. Textdarstellung ist fuer die erste Version ausreichend.
- [ ] Anwendungssymbole in einer zweiten Ausbaustufe ueber `GIcon` und eine
  standardkonforme Icon-Theme-Aufloesung ergaenzen; fehlende Icons duerfen
  keinen Eintrag ausblenden.
- [x] Navigation mit Pfeiltasten, Page Up/Down, Home/End, Enter und Escape sowie
  Auswahl, Scrollen und Schliessen per Maus unterstuetzen.
- [x] X11-Tastatureingaben mit `xkbcommon-x11` und Compose-Unterstuetzung
  auswerten, damit Tastaturlayouts, Umlaute und tote Tasten korrekt
  funktionieren.
- [x] Tastatur- oder Pointer-Grabs nur waehrend des geoeffneten Starters halten
  und auf allen Fehler-, Fokusverlust- und Beendigungspfaden sicher freigeben.
- [x] Den Starter bei Klick ausserhalb, Escape, Panel-Ausblendung,
  Monitorwechsel oder erfolgreichem Anwendungsstart schliessen.
- [x] Positionierung und Groesse an kleinen Bildschirmen, mehreren Monitoren und
  Bildschirmraendern begrenzen.

### Abhaengigkeiten und Fallback

- [x] GIO und `xkbcommon-x11` als direkte Build- und Laufzeitabhaengigkeiten des
  nativen X11-Starters pruefen, in CMake erkennen und dokumentieren.
- [x] Den vorgeschriebenen CLI-only-Build ohne XCB-Entwicklungsheader weiterhin
  erhalten; der native Starter darf dort nicht mitgebaut werden muessen.
- [x] Wenn die Abhaengigkeiten des internen Starters fehlen, bei `auto` einen
  konfigurierten externen Launcher verwenden oder den Block ohne Fehler
  deaktivieren.
- [x] In `sliverbar --diagnose` Launcher-Modus, erkannte Abhaengigkeiten,
  Anzahl sichtbarer Anwendungen und externen Fallback ausgeben.

### Tests und Dokumentation

- [ ] Tests mit einem isolierten `XDG_DATA_HOME` und kontrollierten
  Desktop-Dateien erstellen: sichtbare, versteckte, lokalisierte, doppelte,
  ungueltige und nicht mehr vorhandene Anwendungen.
- [ ] Suche, Ranking, Unicode, leere Trefferliste und stabile Sortierung als
  reine Logiktests abdecken.
- [ ] Unter Xvfb Oeffnen, Texteingabe, Tastaturnavigation, Mauswahl, Scrollen,
  Abbruch, Fokusverlust und erfolgreichen Start einer Testanwendung pruefen.
- [x] Native Popups duerfen Tastatur und Maus nicht serverweit exklusiv
  greifen. Unter Xvfb pruefen, dass ein zweiter X11-Client waehrend des offenen
  Menues weiterhin Eingaben erhalten kann und der vorherige Fokus beim
  Schliessen wiederhergestellt wird.
- [x] Ueber GIO gestartete Desktop-Anwendungen duerfen Sliverbars fuer die
  Ereignisschleife blockierte Signalmaske nicht erben. Insbesondere muss ein
  Terminal nach dem Ende von Vim, Neovim oder einer anderen Terminalanwendung
  selbststaendig schliessen.
- [ ] Sicherstellen, dass der Suchtext niemals als Befehl ausgefuehrt wird und
  nur ein ausgewaehlter registrierter Desktop-Eintrag gestartet werden kann.
- [x] Internen und externen Launcher-Modus sowie Konfigurationsbeispiele in der
  README dokumentieren.

Akzeptanzkriterium: Ein Linksklick auf den linken Launcher-Block oeffnet einen
nativen, per Tastatur durchsuchbaren Katalog der installierten und fuer das
Anwendungsmenue sichtbaren Programme. Enter oder Linksklick startet den
markierten Desktop-Eintrag standardkonform und schliesst das Fenster. Rofi oder
ein anderer externer Launcher bleibt als konfigurierbare Alternative erhalten.

## Feature: Integriertes System- und Power-Menue

Der ganz rechte Power-Block soll optional ein natives Sliverbar-Menue oeffnen
und das bestehende Rofi-Power-Menue ersetzen. Nur Aktionen, die auf dem System
tatsaechlich vorhanden und fuer den Benutzer zulaessig sind, werden angezeigt.

Das bestehende Rofi-Menue bezeichnet eine Aktion als `Suspend`, fuehrt aber
`systemctl suspend-then-hibernate` aus. Im neuen Menue muessen Suspend,
Hibernate und Suspend-then-Hibernate getrennte, korrekt benannte Aktionen sein.

- [x] Eine Konfigurationsoption wie
  `power_menu=internal|external|auto|disabled` definieren.
- [x] Linksklick auf den ganz rechten Power-Block oeffnet beziehungsweise
  schliesst das interne Menue.
- [x] Das Menue als wiederverwendbares natives X11-Popup umsetzen und am rechten
  Panelrand verankern. Tastatur- und Mausbedienung aus dem Anwendungsstarter
  wiederverwenden.
- [x] Rofi oder ein anderes externes Menue als optionale Alternative behalten,
  aber keine benutzerspezifischen bspwm-/Rofi-Pfade als Standard einsetzen.

### Aktionen und Glyphs

Die Laufzeit-Glyphen stammen aus der Private Use Area von Font Awesome
beziehungsweise Nerd Fonts. Da GitHub diese Icon-Schriften nicht laedt, werden
hier statt der nicht portabel darstellbaren Zeichen ihre eindeutigen Codepoints
und eine semantische Unicode-Vorschau gezeigt.

| Aktion | Laufzeit-Glyph | Unicode-Vorschau | Bedeutung |
| --- | --- | --- | --- |
| Bildschirm sperren | Font Awesome `U+F023` | 🔒 | Lock |
| Abmelden | Font Awesome `U+F08B` | ⇥ | Logout |
| Standby/Suspend | Font Awesome `U+F186` | 🌙 | Schlafmodus im RAM |
| Hibernate | Font Awesome `U+F236` | 🛏 | Ruhezustand auf Datentraeger |
| Suspend, dann Hibernate | Font Awesome `U+F186`, `U+F236` | 🌙 → 🛏 | zweistufiger Schlafmodus |
| Hybrid Sleep | Font Awesome `U+F2DC` | ❄ | Suspend und Hibernate kombiniert |
| Neustart | Font Awesome `U+F01E` | ↻ | Reboot |
| Ausschalten | Font Awesome `U+F011` | ⏻ | Poweroff |

- [x] Im Menue neben dem Glyph immer eine lokalisierbare Textbezeichnung
  anzeigen; gefaehrliche Aktionen duerfen nicht nur anhand eines Symbols
  unterschieden werden muessen.
- [x] Reihenfolge und Sichtbarkeit der grundsaetzlich erlaubten Aktionen
  konfigurierbar machen. Die Laufzeitpruefung darf eine nicht unterstuetzte oder
  nicht erlaubte Aktion trotzdem ausblenden.
- [ ] Fehlende Glyphs ueber die allgemeine Icon-Fallback-Strategie abfangen.

### Faehigkeiten sicher erkennen

- [x] Als primaeres Backend die D-Bus-Schnittstelle
  `org.freedesktop.login1.Manager` verwenden, vorzugsweise ueber das bereits
  geplante GIO/GDBus.
- [x] Beim Oeffnen des Menues mindestens `CanPowerOff`, `CanReboot`,
  `CanSuspend`, `CanHibernate`, `CanHybridSleep` und
  `CanSuspendThenHibernate` abfragen, soweit die laufende logind-Version die
  jeweilige Methode anbietet.
- [x] Eine Aktion nur bei Rueckgabe `yes` oder `challenge` anzeigen. `challenge`
  bedeutet, dass die Aktion verfuegbar ist, aber eine interaktive
  Polkit-Autorisierung verlangen kann. Bei `no`, `na`, fehlender Methode oder
  nicht erreichbarem Dienst bleibt sie unsichtbar.
- [x] Power-Aktionen ueber die zugehoerigen logind-Methoden mit erlaubter
  interaktiver Autorisierung ausloesen, nicht durch fest codierte Aufrufe von
  `systemctl`.
- [ ] Einen optionalen Backend-/Command-Fallback nur explizit ueber gepruefte
  argv-Konfiguration erlauben. Seine Faehigkeiten muessen ebenfalls explizit
  konfiguriert sein und duerfen nicht aus einem vorhandenen Programmnamen
  geraten werden.
- [x] Den gesamten Power-Block ausblenden, wenn weder internes Backend noch
  externer Fallback mindestens eine sichere Aktion bereitstellt.

### Lock und Logout

- [x] Bildschirm-Sperren getrennt von den Power-Faehigkeiten erkennen, da ein
  vorhandener logind-Aufruf nicht garantiert, dass die X11-Sitzung einen
  Lock-Screen bereitstellt.
- [x] Vorzugsweise die registrierte Sitzung beziehungsweise eine geeignete
  ScreenSaver-/Session-Schnittstelle verwenden und einen konfigurierten
  Lock-Befehl als Fallback erlauben.
- [x] Logout ueber ein Window-Manager-/Session-Backend ausfuehren. Fuer bspwm
  kann dies das optionale bspwm-Backend uebernehmen; ein erzwungenes
  `TerminateUser` darf nicht als allgemeiner, harmloser Fallback verwendet
  werden.
- [x] Lock und Logout ausblenden, wenn keine passende Integration sicher
  erkannt oder konfiguriert wurde.

### Bestaetigung und Fehlerbehandlung

- [x] Lock standardmaessig sofort ausfuehren. Fuer Logout, Suspend, Hibernate,
  Hybrid Sleep, Suspend-then-Hibernate, Reboot und Poweroff eine native
  Bestaetigungsansicht anzeigen.
- [x] In der Bestaetigung `Nein` beziehungsweise Abbrechen vorselektieren und
  Enter, Escape sowie eindeutige Mausziele unterstuetzen.
- [x] Bestaetigung pro Aktion konfigurierbar machen, aber fuer Reboot und
  Poweroff standardmaessig eingeschaltet lassen.
- [x] D-Bus-, Polkit- und Backend-Fehler sichtbar melden, ohne Sliverbar zu
  beenden. Nach einem Fehler muss das Menue erneut benutzbar sein.
- [ ] Mehrfachklicks und wiederholte Tasteneingaben entprellen, damit keine
  Aktion zweimal angefordert wird.

### Zusammenspiel mit dem Inhibitor

- [x] Wenn Sliverbars eigener Standby-Inhibitor aktiv ist und der Benutzer im
  Power-Menue bewusst eine Schlafaktion waehlt, in der Bestaetigung explizit
  auf den Konflikt hinweisen.
- [x] Nach Bestaetigung den eigenen Inhibitor vor der Schlafanforderung
  freigeben. Schlaegt die Anforderung fehl, den vorherigen Zustand
  wiederherstellen.
- [ ] Festlegen und testen, ob ein vor dem Schlaf aktiver Inhibitor nach dem
  Aufwachen derselben Sliverbar-Instanz automatisch wieder aufgenommen werden
  soll. Dies darf nicht mit einer Wiederherstellung nach einem Neustart von
  Sliverbar verwechselt werden.

### Vorbereitende Aktionen

- [x] Die benutzerspezifischen `mpc`-/`playerctl`-Vorbereitungen des bisherigen
  Rofi-Skripts nicht als allgemeines Standardverhalten fest einbauen.
- [ ] Optional wiederholbare, argv-basierte Hooks pro Aktion erlauben oder ein
  generisches MPRIS-Pause-Verhalten als spaetere Erweiterung untersuchen.
- [ ] Fehler optionaler Hooks protokollieren und festlegen, ob sie die
  bestaetigte Power-Aktion abbrechen oder nur eine Warnung erzeugen.

### Tests und Dokumentation

- [ ] Ein austauschbares Test-Backend verwenden, das alle Kombinationen aus
  `yes`, `challenge`, `no`, `na`, fehlender Methode und D-Bus-Fehler simuliert,
  ohne den Testrechner herunterzufahren oder schlafen zu legen.
- [ ] Unter Xvfb Menueposition, Glyphs und Beschriftungen, Tastatur- und
  Mauswahl, sichere Vorbelegung, Abbruch und Bestaetigung testen.
- [x] Beim direkten Wiederverwenden des Popup-Fensters fuer die
  Power-Bestaetigung ein verspaetetes `FocusOut` des vorherigen Menues
  ignorieren, solange das neu geoeffnete Popup bereits wieder fokussiert ist.
  Einen echten Fokusverlust weiterhin als Abbruch behandeln.
- [ ] Sicherstellen, dass eine nicht angebotene Aktion weder sichtbar noch ueber
  manipulierte interne Action-Strings ausloesbar ist.
- [ ] Inhibitor-Konflikt, Freigabe, fehlgeschlagene Schlafanforderung und
  Verhalten nach simuliertem Resume testen.
- [x] Verfuegbare Power-Aktionen, Backend, Autorisierungsstatus und externe
  Fallbacks in `sliverbar --diagnose` ausgeben.
- [x] Aktionen, Glyphs, Bestaetigungsregeln und Konfigurationsbeispiele in der
  README dokumentieren.

Akzeptanzkriterium: Ein Linksklick auf den rechten Power-Block oeffnet ein
natives Menue mit Glyph und Text fuer jede aktuell verfuegbare Aktion.
Insbesondere erscheint Hibernate nur, wenn das Backend die Funktion als
verfuegbar meldet. Gefaehrliche Aktionen benoetigen eine sichere Bestaetigung
und werden ueber das erkannte Sitzungs-/Power-Backend statt ueber fest codierte
Rofi- oder `systemctl`-Aufrufe ausgefuehrt.

## Feature: Mehrere Wetterorte und Ortsauswahl

- [x] In der Konfiguration eine Liste von maximal vier Wetterorten erlauben. Im
  bestehenden `key=value`-Format denselben Schluessel mehrfach zulassen und
  sichere ID, Anzeigename und Suchwert getrennt angeben, zum Beispiel:

  ```ini
  weather_location=munich|Muenchen|Munich
  weather_location=berlin|Berlin|Berlin, Germany
  weather_location=london|London|London, United Kingdom
  weather_default=berlin
  ```

- [x] Optional getrennte Anzeigenamen und Suchwerte ermoeglichen, falls der
  Wetterdienst fuer einen eindeutigen Ort einen ausfuehrlicheren Suchbegriff
  benoetigt.
- [x] Leere, doppelte, zu lange oder unsichere Ortseintraege sowie einen
  fuenften Wetterort bei `--check-config` ablehnen. Die Grenze von vier Orten
  und die zulaessigen sicheren IDs dokumentieren.
- [x] `weather_interval` in Sekunden auf minimal 1800 Sekunden (30 Minuten) und
  maximal 14400 Sekunden (240 Minuten) begrenzen.
- [x] Syntaktisch gueltige Intervallwerte ausserhalb dieses Bereichs nicht als
  Konfigurationsfehler behandeln, sondern auf die naechste Grenze setzen und
  den konfigurierten sowie den verwendeten Wert als `WARNING` protokollieren,
  beispielsweise 900 -> 1800 und 42000 -> 14400 Sekunden.
- [x] Den aktiven Wetterort als Laufzeitzustand fuehren, statt die eingelesene
  Konfiguration beim Umschalten zu veraendern.
- [x] Den zuletzt ausgewaehlten Ort optional unter
  `$XDG_STATE_HOME/sliverbar` speichern und beim naechsten Start
  wiederherstellen. Die Konfigurationsdatei darf dabei nicht automatisch
  umgeschrieben werden.
- [x] Wetterdaten und Vorhersagebilder pro Ort getrennt cachen. Fuer Dateinamen
  eine sichere ID oder einen Hash verwenden, nicht den ungeprueften Ortsnamen
  als Pfadbestandteil.
- [x] Beim Wechsel sofort vorhandene Cache-Daten des neuen Orts anzeigen und
  anschliessend eine Aktualisierung im Hintergrund anstossen.
- [x] Laufzeitsignale vor der Initialisierung von GIO-/D-Bus-Hilfsthreads
  blockieren, damit das Ende des Wetter-Workers sicher ueber `signalfd`
  verarbeitet, der neue Cache eingelesen und der Wetterblock neu gezeichnet
  wird.
- [x] Sicherstellen, dass eine noch laufende Anfrage fuer den vorherigen Ort
  nach einem Wechsel nicht die Anzeige oder den Cache des neuen Orts
  ueberschreibt.
- [x] Bei nur einem konfigurierten Ort weiterhin ohne unnoetiges Auswahlmenue
  funktionieren.

### Bedienung

- [x] Linksklick auf den Wetterbereich oeffnet ein am Wetterblock verankertes
  Auswahlmenue mit allen konfigurierten Orten und einer Markierung des aktiven
  Orts.
- [x] Das Auswahlmenue nativ mit X11 umsetzen, damit weder `rofi` noch `dmenu`
  eine zwingende Laufzeitabhaengigkeit werden.
- [x] Auswahl per Maus vorsehen; Tastaturbedienung mit Pfeiltasten, Enter und
  Escape sowie Schliessen bei einem Klick ausserhalb des Menues ergaenzen.
- [x] Die bisherige Aktion zum Oeffnen des Vorhersagebilds von Linksklick auf
  Rechtsklick verschieben.
- [x] Das Vorhersagebild weiterhin ueber `xdg-open` oeffnen, damit die fuer den
  MIME-Typ `image/png` konfigurierte Standardanwendung verwendet wird. Keinen
  Bildbetrachter wie `sxiv` direkt in Sliverbar fest eintragen.
- [x] Einen Test ergaenzen, der fuer die Vorschau den Aufruf des
  Standardprogramm-Dispatchers prueft, ohne eine konkrete Desktop-Anwendung
  vorauszusetzen.
- [x] Festlegen, ob der Mittelklick weiterhin eine Wetterbenachrichtigung zeigt
  oder stattdessen eine sofortige Aktualisierung ausloest.
- [x] Das Menue bei ausgeblendetem Panel, Monitorwechsel und Beendigung von
  Sliverbar ebenfalls ausblenden beziehungsweise zerstoeren.

### Tests und Dokumentation

- [x] Parser-Tests fuer einen Ort, mehrere Orte, Standardort, ungueltige Listen,
  doppelte Orte, die Grenze von vier Orten und einen abgelehnten fuenften Ort
  ergaenzen.
- [x] Parser-Tests fuer Wetterintervalle unterhalb und oberhalb des erlaubten
  Bereichs ergaenzen und die Begrenzung auf 1800 beziehungsweise 14400 Sekunden
  pruefen.
- [ ] Tests fuer Auswahl, Zustandswiederherstellung, getrennte Cache-Dateien und
  den Wechsel waehrend einer laufenden Wetteranfrage hinzufuegen.
- [ ] Das X11-Menue unter Xvfb testen: Oeffnen, Auswahl, Abbruch, Klick
  ausserhalb und korrekte Positionierung am Bildschirmrand.
- [x] Konfigurationsbeispiel, Vier-Orte-Grenze, Intervallgrenzen,
  Warn-/Clamp-Verhalten und Mausbelegung in der README dokumentieren.

Akzeptanzkriterium: Bis zu vier Orte koennen in der Konfiguration hinterlegt
werden. Ein Linksklick oeffnet ohne externen Menueprozess die Ortsliste; nach
der Auswahl zeigt die Leiste den gewaehlten Ort an, aktualisiert dessen Daten
asynchron und stellt die Auswahl beim naechsten Start optional wieder her. Das
Aktualisierungsintervall liegt wirksam immer zwischen 30 und 240 Minuten;
abweichende Konfigurationswerte werden mit einer Warnung auf die naechste
Grenze gesetzt.

## Feature: Aufziehbarer Timer im Panel

Ein optionaler Timer-Block soll unmittelbar links vom Inhibitor-Block
erscheinen. Der Timer wird direkt mit dem Mausrad eingestellt und anschliessend
per Linksklick gestartet.

### Darstellung und Bedienung

- [x] Einen Timer-Block mit einer Uhr- oder Wecker-Glyphe einfuegen.
- [x] Den Block unmittelbar links vom Inhibitor-Block positionieren.
- [x] Ohne eingestellte Zeit nur die Glyphe in `color_clock` beziehungsweise
  Dracula-Gruen anzeigen.
- [x] Sobald eine Zeit eingestellt ist, Timeranzeige und Glyphe in
  `color_urgent` beziehungsweise Dracula-Rot darstellen.
- [x] Die eingestellte beziehungsweise noch verbleibende Zeit in Minuten links
  von der Timer-Glyphe anzeigen.
- [x] Vorwaerts gerichtete Mausradbewegungen erhoehen den Timer in
  Ein-Minuten-Schritten.
- [x] Rueckwaerts gerichtete Mausradbewegungen reduzieren den Timer in
  Ein-Minuten-Schritten, jedoch nie unter null.
- [x] Mausradbewegungen nur vor dem ersten Start des eingestellten Timers
  auswerten.
- [x] Das Mausrad bei einem laufenden oder pausierten Timer ignorieren; die
  eingestellte Restzeit darf dadurch nicht veraendert werden.
- [x] Einen eingestellten Timer durch Linksklick auf den Timer-Block starten.
- [x] Einen laufenden Countdown durch einen weiteren Linksklick pausieren, ohne
  die Restzeit zu veraendern.
- [x] Einen pausierten Countdown durch erneuten Linksklick fortsetzen.
- [x] Laufende und pausierte Timer weiterhin mit Minutenanzeige und in
  `color_urgent` beziehungsweise Dracula-Rot darstellen.
- [x] Waehrend des Ablaufs die verbleibende Zeit aktualisieren und angefangene
  Minuten nachvollziehbar darstellen, beispielsweise auf die naechste volle
  Minute aufgerundet.
- [x] Einen eingestellten, laufenden oder pausierten Timer durch Rechtsklick
  sofort auf null zuruecksetzen.
- [x] Beim manuellen Zuruecksetzen weder den Ablaufklang abspielen noch eine
  Ablaufbenachrichtigung erzeugen.
- [x] Nach dem Zuruecksetzen oder regulaeren Ablauf die Minutenanzeige
  ausblenden und die Glyphe wieder in `color_clock` beziehungsweise
  Dracula-Gruen darstellen.

### Zeitmessung und Lebenszyklus

- [x] Fuer den Countdown eine monotone Zeitquelle verwenden, damit Aenderungen
  der Systemuhr den Timer nicht beeinflussen.
- [x] Den Countdown in die vorhandene ereignisgesteuerte `poll`-/`timerfd`-
  Architektur integrieren, ohne blockierende Wartezeiten oder einen
  sekundenweise gestarteten Hilfsprozess einzufuehren.
- [x] Einen laufenden oder pausierten Timer beim Beenden oder Neustarten von
  Sliverbar standardmaessig nicht wiederherstellen.
- [x] Einen sinnvollen Maximalwert festlegen und Ueberlaeufe auch bei vielen
  schnellen Mausradbewegungen verhindern.

### Tonsignal und Konfiguration

- [x] Eine Konfigurationsoption `module_timer=auto|enabled|disabled`
  hinzufuegen.
- [x] Eine Konfigurationsoption `timer_sound` fuer den Pfad zur abzuspielenden
  Audiodatei hinzufuegen.
- [x] `timer_sound` mit einem sinnvollen Systemklang vorbelegen, beispielsweise
  `/usr/share/sounds/freedesktop/stereo/alarm-clock-elapsed.oga`.
- [x] Fuer die Wiedergabe verfuegbare Audio-Backends wie `pw-play`, `paplay`,
  `canberra-gtk-play` oder `aplay` in einer dokumentierten Reihenfolge
  erkennen. Befehle als argv-Liste und ohne Shell-Auswertung starten.
- [x] Eine fehlende oder unlesbare Sounddatei sowie ein fehlendes
  Wiedergabeprogramm als normale Laufzeitsituation behandeln. Der Timer muss
  trotzdem korrekt ablaufen und Sliverbar darf nicht beendet werden.
- [x] Konfigurierten Soundpfad und erkanntes Wiedergabe-Backend in
  `sliverbar --diagnose` ausgeben.

### Benachrichtigungen

- [x] Beim Start des Countdowns ueber `notify-send` eine kurze
  Benachrichtigung ausgeben, beispielsweise
  `Timer gestartet - 15 Minuten`.
- [x] Beim Fortsetzen eines pausierten Timers keine weitere
  Startbenachrichtigung erzeugen.
- [x] Nach regulaerem Ablauf zusaetzlich zum Tonsignal eine
  Ablaufbenachrichtigung anzeigen, beispielsweise `Timer abgelaufen`.
- [x] Benachrichtigungstexte entsprechend der konfigurierten Sprache auf
  Deutsch oder Englisch ausgeben.
- [x] Fehlendes `notify-send` und fehlgeschlagene Benachrichtigungen als
  normale Laufzeitsituation behandeln; Timer und Panelbetrieb duerfen dadurch
  nicht beeintraechtigt werden.

### Tests und Dokumentation

- [x] Zustandswechsel zwischen leer, eingestellt, laufend, pausiert und
  abgelaufen testen.
- [x] Mausradrichtung, Ein-Minuten-Schritte, Untergrenze, Maximalwert und
  schnelle aufeinanderfolgende Eingaben testen.
- [x] Sicherstellen, dass das Mausrad einen laufenden oder pausierten Timer
  nicht veraendert.
- [x] Blockreihenfolge, Minutenanzeige, Glyphe und Farbwechsel testen.
- [x] Start, Pause, Fortsetzung und Ablauf mit einer kontrollierten monotonen
  Testzeit pruefen, ohne reale Minuten warten zu muessen.
- [ ] Sound- und Benachrichtigungsausloesung mit Test-Backends pruefen, ohne auf
  dem Entwicklungsrechner einen echten Ton oder eine Desktopbenachrichtigung
  auszugeben.
- [x] Sicherstellen, dass ein Rechtsklick den Timer in jedem Zustand auf null
  setzt und weder Ton noch Ablaufbenachrichtigung ausloest.
- [ ] Fehlende Sounddatei, fehlende Wiedergabe- und Benachrichtigungsprogramme
  sowie fehlgeschlagene Starts testen.
- [x] Timer-Konfiguration, Darstellung und Mausbelegung in der README
  dokumentieren.

Akzeptanzkriterium: Der Benutzer kann den links vom Inhibitor angeordneten
Timer mit dem Mausrad minutengenau aufziehen und wieder reduzieren. Nach einem
Linksklick laeuft die angezeigte Restzeit ab und kann mit weiteren Linksklicks
pausiert und fortgesetzt werden; das Mausrad veraendert sie dann nicht mehr.
Der eingestellte, laufende oder pausierte Timer erscheint rot. Ein Rechtsklick
setzt ihn jederzeit still auf null zurueck. Nur nach regulaerem Ablauf werden
der konfigurierte Klang und eine Benachrichtigung ausgeloest; danach kehrt der
Block in seinen gruenen Grundzustand zurueck. Fehler bei Ton oder
Benachrichtigung beeintraechtigen weder Timer noch Panelbetrieb.

## Feature: Zeitabhaengige Uhr- und zustandsabhaengige Timer-Glyphen

Die folgenden Nerd-Font-Glyphen sollen die bisherige Uhr-Glyphe sowie die
statische Timer-Glyphe ersetzen. Die vorhandenen Farben, Texte und Mausaktionen
der beiden Bloecke bleiben dabei erhalten.

### Uhr-Glyphe

- [x] Die zwoelf Uhr-Glyphen wie folgt den Stunden 1 Uhr bis 12 Uhr zuordnen:

  | Stunde | Glyph |
  | ---: | :---: |
  | 1 Uhr | 󱑋 |
  | 2 Uhr | 󱑌 |
  | 3 Uhr | 󱑍 |
  | 4 Uhr | 󱑎 |
  | 5 Uhr | 󱑏 |
  | 6 Uhr | 󱑐 |
  | 7 Uhr | 󱑑 |
  | 8 Uhr | 󱑒 |
  | 9 Uhr | 󱑓 |
  | 10 Uhr | 󱑔 |
  | 11 Uhr | 󱑕 |
  | 12 Uhr | 󱑖 |

- [x] Links neben der Uhrzeit die zur aktuellen lokalen Stunde passende Glyphe
  anzeigen. Minuten und Sekunden beeinflussen die Auswahl nicht; fuer 0 Uhr
  und 12 Uhr wird die Glyph fuer 12 Uhr verwendet.
- [x] Die bisherige Kalender-Glyphe links vom Datum sowie die Klickaktion des
  gesamten Datums- und Uhrzeitblocks unveraendert lassen.
- [x] Ohne konfigurierte Icon-Schrift weiterhin einen darstellbaren
  Unicode-Fallback fuer die Uhr verwenden.

### Timer-Glyphen und Zustaende

- [x] Fuer einen leeren Timer die Standard-Glyph `󰀠` anzeigen.
- [x] Fuer einen eingestellten, laufenden oder pausierten Timer die Glyph `󰀡`
  anzeigen. Minutenanzeige und bestehende Farbgebung bleiben unveraendert.
- [x] Nach regulaerem Ablauf die Ablauf-Glyph `󰀢` anzeigen, solange der
  erfolgreich gestartete Timerklang wiedergegeben wird. Nach Ende des
  Wiedergabeprozesses wieder zur Standard-Glyph `󰀠` wechseln.
- [x] Kann der Timerklang wegen einer fehlenden oder unlesbaren Datei, eines
  fehlenden Wiedergabe-Backends oder eines fehlgeschlagenen Prozessstarts nicht
  gestartet werden, die Ablauf-Glyph fuer 1,5 Sekunden anzeigen und danach zur
  Standard-Glyph wechseln.
- [x] Nach einem Rechtsklick auf einen zuvor eingestellten, laufenden oder
  pausierten Timer die Ruecksetz-Glyph `󰀣` fuer 1,5 Sekunden anzeigen. Ein
  Rechtsklick auf einen bereits leeren Timer zeigt keine Ruecksetz-Glyph.
- [x] Wird der Timer waehrend der 1,5-sekuendigen Ruecksetzanzeige erneut mit
  dem Mausrad aufgezogen, die Ruecksetz-Glyph sofort durch die Glyph fuer den
  eingestellten Timer ersetzen.
- [x] Fuer Installationen ohne konfigurierte Icon-Schrift die bisherigen
  darstellbaren Unicode-Fallbacks fuer alle Timerzustaende beibehalten.

### Zeitsteuerung und Tests

- [x] Die 1,5-sekuendigen Anzeigen mit einer monotonen Zeitquelle und ohne
  blockierende Wartezeit in die bestehende `poll`-/`timerfd`-Architektur
  integrieren.
- [x] Die Zuordnung aller zwoelf Uhr-Glyphen einschliesslich 0 Uhr, 12 Uhr und
  des Wechsels zwischen zwei Stunden automatisiert testen.
- [x] Standard-, eingestellt-, laufend-, pausiert-, abgelaufen- und
  zurueckgesetzt-Glyph sowie die unveraenderten Farben und Minutenangaben
  testen.
- [x] Die Ablaufanzeige sowohl mit einem kontrolliert laufenden
  Wiedergabeprozess als auch bei nicht startbarem Klang testen.
- [x] Testen, dass die Ruecksetz-Glyph nur bei einem zuvor aufgezogenen Timer
  erscheint, nach 1,5 Sekunden verschwindet und beim erneuten Aufziehen sofort
  ersetzt wird.
- [x] Die neuen Uhr- und Timer-Glyphen sowie deren Zustaende in der README
  dokumentieren.

Akzeptanzkriterium: Die Uhr zeigt links neben der Uhrzeit fuer jede lokale
Stunde die oben festgelegte Glyph. Der Timer verwendet fuer seinen leeren,
aufgezogenen, regulaer abgelaufenen und manuell zurueckgesetzten Zustand die
jeweils vorgesehene Glyph. Die Ablauf-Glyph bleibt bei erfolgreicher Wiedergabe
bis zum Ende des Timerklangs sichtbar und erscheint bei nicht startbarem Klang
1,5 Sekunden. Die Ruecksetz-Glyph erscheint nur nach dem Zuruecksetzen eines
zuvor aufgezogenen Timers fuer 1,5 Sekunden und wird durch erneutes Aufziehen
sofort beendet.

## Feature: Standby- und Hibernation-Inhibitor

- [x] Einen eigenen Inhibitor-Block links vom Wetterblock einfuegen; Wetter und
  Batterie- beziehungsweise AC-Anzeige folgen rechts davon.
- [x] Als Glyph die Kaffeetasse aus Font Awesome/Nerd Font (`U+F0F4`,
  portable Unicode-Vorschau: ☕) verwenden. Fuer Installationen ohne
  Icon-Schrift einen darstellbaren Text- oder Unicode-Fallback vorsehen.
- [x] Inaktiven Zustand mit `color_free` und aktive Inhibition mit
  `color_warning` darstellen. Spaeter optional eigene konfigurierbare Farben
  `color_inhibit_inactive` und `color_inhibit_active` anbieten.
- [x] Linksklick schaltet die Inhibition fuer Standby und Hibernation ein
  beziehungsweise aus.
- [x] Nach erfolgreichem Umschalten per Desktop-Notification erklaeren, ob
  automatischer Standby und Hibernation blockiert oder wieder erlaubt sind.
- [x] Den Zustand aus dem tatsaechlichen Inhibitor-Prozess beziehungsweise
  dessen gehaltenem Lock ableiten, nicht nur aus einem internen booleschen
  Schalter.
- [x] Beim unerwarteten Ende des Inhibitor-Backends sofort auf den inaktiven
  Zustand wechseln und den Benutzer optional benachrichtigen.
- [x] Beim normalen Beenden von Sliverbar den eigenen Inhibitor sauber
  freigeben.
- [x] Die Inhibition nach einem Neustart von Sliverbar standardmaessig nicht
  automatisch wieder aktivieren.

### Backend und Portabilitaet

- [x] Eine kleine Backend-Schnittstelle fuer Inhibition definieren, damit die
  Paneldarstellung nicht direkt von systemd abhaengt.
- [x] Als erstes Backend `systemd-inhibit` beziehungsweise die logind-Inhibit-
  Schnittstelle fuer `sleep` untersuchen und implementieren. Festlegen, ob auch
  `handle-lid-switch` blockiert werden soll oder nur explizite
  Standby-/Hibernation-Anforderungen.
- [x] Das Backend zur Laufzeit erkennen. Wenn insbesondere `systemd-inhibit`
  beziehungsweise kein anderes kompatibles Inhibit-Backend verfuegbar ist,
  den Block vollstaendig ausblenden. Kein inaktives Fehlersymbol oder
  Platzhalter darf sichtbar bleiben und der Panelstart darf nicht scheitern.
- [ ] Optional einen konfigurierbaren argv-basierten Inhibit-Befehl als
  alternatives Backend vorsehen; keine Shell-Auswertung einfuehren.
- [x] Status und gewaehltes Backend in der geplanten Ausgabe von
  `sliverbar --diagnose` anzeigen.

### Tests und Dokumentation

- [x] Zustandswechsel, Farbwechsel, Blockreihenfolge und Klickaktion testen.
- [x] Start, kontrolliertes Beenden und unerwarteten Ausfall des Backends mit
  einem Test-Backend pruefen, ohne den Entwicklungsrechner wirklich am Schlafen
  zu hindern.
- [x] Den Betrieb ohne verfuegbares Inhibit-Backend testen.
- [x] Bedeutung des Icons, Mausbelegung, blockierte Schlafarten und
  Backend-Voraussetzungen dokumentieren.

Akzeptanzkriterium: Bei vorhandenem Backend aktiviert ein Linksklick einen
echten Inhibitor fuer Standby und Hibernation und faerbt die Kaffeetasse gelb.
Ein weiterer Klick oder das Beenden von Sliverbar gibt den Inhibitor frei und
stellt das Icon wieder grau dar. Fehlt ein kompatibles Backend, ist der gesamte
Block unsichtbar und der restliche Panelbetrieb bleibt unbeeintraechtigt.

## Feature: Standardkalender ueber Datum und Uhrzeit oeffnen

- [x] Den gesamten Datums- und Uhrzeitblock mit einer Linksklickaktion
  versehen, die die auf dem System als Standard registrierte Kalenderanwendung
  oeffnet.
- [x] Den Standardhandler fuer den MIME-Typ `text/calendar` ueber eine
  standardkonforme GIO-/XDG-Schnittstelle ermitteln und die Anwendung ohne
  Shell-Auswertung starten.
- [x] Nicht blind `xdg-open calendar:` verwenden: Der Handler des URI-Schemas
  `calendar:` kann von der Standardanwendung fuer `text/calendar` abweichen.
- [x] Keine leere oder kuenstliche `.ics`-Datei erzeugen, nur um den
  MIME-Handler zu starten; dies koennte ungewollt einen Importdialog oder einen
  Termineintrag oeffnen.
- [x] Optional einen expliziten `calendar_command` als argv-basierte
  Konfigurationsalternative vorsehen. Dieser hat Vorrang vor der automatischen
  Erkennung und darf nicht durch eine Shell ausgewertet werden.
- [x] Wenn kein Kalenderhandler und kein konfigurierter Befehl verfuegbar ist,
  Datum und Uhrzeit weiterhin normal anzeigen. Ein Klick darf weder den
  Panelbetrieb beeintraechtigen noch ein beliebiges Ersatzprogramm starten.
- [x] Den erkannten Kalenderhandler in `sliverbar --diagnose` ausgeben.
- [x] Tests fuer Standardhandler, konfigurierten Fallback, fehlenden Handler,
  fehlerhaften Start und korrekt gerouteten Linksklick hinzufuegen.
- [x] Klickfunktion und Konfigurationsalternative in der README dokumentieren.

Akzeptanzkriterium: Ein Linksklick auf Datum oder Uhrzeit startet die fuer
`text/calendar` registrierte Standardanwendung. Fehlt diese, bleibt der
Zeitblock sichtbar und Sliverbar laeuft unveraendert weiter.

## Prioritaet 3: Standardkonforme Konfigurationssuche

- [x] Folgende Suchreihenfolge implementieren und dokumentieren:
  1. `--config PATH`;
  2. `SLIVERBAR_CONFIG`;
  3. `$XDG_CONFIG_HOME/sliverbar/panel.conf`;
  4. `$HOME/.config/sliverbar/panel.conf`, falls `XDG_CONFIG_HOME` fehlt;
  5. installierte Systemkonfiguration, beispielsweise
     `/etc/sliverbar/panel.conf`.
- [x] Festlegen, ob ohne vorhandene Konfigurationsdatei interne Defaults
  verwendet werden oder mit einer klaren Fehlermeldung abgebrochen wird.
- [x] Den tatsaechlich verwendeten Pfad in `--diagnose` ausgeben.
- [x] Tests fuer Prioritaet, Fallback und fehlende Umgebungsvariablen anlegen.

Akzeptanzkriterium: Ein ueber CMake installiertes Sliverbar kann ohne Wechsel in
das Quell- oder Buildverzeichnis gestartet werden.

## Feature: Automatische Systemsprache Deutsch/Englisch

- [x] Mit `language=auto` die bevorzugte Systemsprache aus `LANGUAGE`,
  `LC_MESSAGES`, `LC_ALL` und `LANG` bestimmen. Technische Werte wie `C` und
  `POSIX` duerfen eine darunter konfigurierte Sprache nicht verdecken.
- [x] Bei einer deutschen Locale deutsche Texte verwenden; fuer jede andere
  Locale aus Platz- und Wartungsgruenden auf Englisch zurueckfallen.
- [x] Die Sprachwahl auf Launcher-Suche, leere Trefferlisten, Power-Aktionen,
  Bestaetigungen, Inhibitor-Benachrichtigungen, Datumsabkuerzungen und
  Wetterdienst anwenden.
- [x] Explizite Overrides `language=de` und `language=en` weiterhin erlauben
  und andere Werte bei `--check-config` ablehnen.
- [x] Automatische Erkennung sowie deutsche und englische Power-Texte testen
  und das Verhalten in der README dokumentieren.

Akzeptanzkriterium: Auf einem deutsch konfigurierten System verwendet Sliverbar
deutsche Oberflaechentexte. Auf allen anderen Systemsprachen erscheinen
englische Texte, ohne dass weitere Uebersetzungen eingebaut werden muessen.

## Bugfix: Battery-Block ohne erkannte Batterie

- [x] Im automatischen Modus den Battery-Block anzeigen, sobald das Linux-
  Power-Supply-Subsystem vorhanden ist.
- [x] Ohne `BAT*`-Eintrag den vorhandenen AC-Zustand anzeigen, statt den
  gesamten Block auszublenden.
- [x] Den Block nur dann automatisch ausblenden, wenn das Power-Supply-
  Subsystem auf dem System nicht verfuegbar ist.
- [x] Das Verhalten mit und ohne lesbares Power-Supply-Subsystem testen.

Akzeptanzkriterium: Auf einem Desktop ohne Batterie steht `AC` im Battery-
Block; auf einem Notebook werden Ladezustand und Ladestatus angezeigt.

## Prioritaet 4: Mehrmonitor- und Multi-Screen-Unterstuetzung

- [x] Den von `xcb_connect` gewaehlten X-Screen korrekt beruecksichtigen, statt
  immer den ersten Root-Screen zu verwenden.
- [x] RandR-Monitore beziehungsweise Ausgaenge erkennen.
- [x] Eine Monitorwahl konfigurieren koennen, beispielsweise `primary`, Name,
  Index oder `all`.
- [x] Panelposition, Breite und `_NET_WM_STRUT_PARTIAL` auf den ausgewaehlten
  Monitor und dessen Koordinaten begrenzen.
- [x] Aenderungen der Monitoranordnung zur Laufzeit behandeln.
- [x] Die System-Tray-Selection passend zum X-Screen bilden, statt
  `_NET_SYSTEM_TRAY_S0` fest einzutragen.
- [x] Tests fuer einen und mehrere virtuelle Monitore beziehungsweise Screens
  unter Xvfb ergaenzen.

Akzeptanzkriterium: Das Panel kann gezielt auf einem ausgewaehlten Monitor
erscheinen und reserviert nur dort den korrekten Bildschirmbereich.

## Prioritaet 5: Distribution und Laufzeitkompatibilitaet

- [x] Die zwingenden dynamischen Laufzeitbibliotheken dokumentieren: XCB,
  Cairo, Pango, GLib, Fontconfig und deren transitive Anforderungen.
- [x] Eine Mindestversion fuer glibc und weitere relevante Bibliotheken
  festlegen oder Pakete jeweils in einer kompatiblen Zielumgebung bauen.
- [x] Unterstuetzte CPU-Architekturen definieren und Builds nicht als universell
  portabel bezeichnen, wenn sie nur fuer x86-64 erzeugt wurden.
- [x] Paketgenerierung ueber bestehende CMake-Installationsmetadaten und CPack
  vorbereiten, zunaechst beispielsweise fuer `.deb` und spaeter optional `.rpm`.
- [x] Ein Debian-Paket nur dann tatsaechlich erzeugen, wenn der Benutzer den
  Paketbau ausdruecklich anfordert. Allgemeine Builds, Tests, Deployments oder
  Release-Vorbereitungen gelten nicht als implizite Anforderung zum Paketbau.
- [ ] Vor jeder Paketproduktion Binary, Konfigurationsdateien,
  Laufzeitbibliotheken, Paketinhalt und Versionsmetadaten pruefen.
- [ ] Vor jeder Paketproduktion die laut Projektanweisung eingeschraenkte,
  rein statische Sicherheitspruefung ohne Trusted Access durchfuehren.

Akzeptanzkriterium: Ein erzeugtes Paket installiert Binary und
Beispielkonfiguration an standardkonforme Orte, deklariert alle
Laufzeitabhaengigkeiten und laesst sich auf einer sauberen Zielinstallation
starten.

## Verbindliche Funktions- und Produktionsvalidierung

- [ ] Die gesamte sichtbare und klickbare Funktionalitaet gruendlich testen;
  automatisierte Logik- und Xvfb-Tests durch manuelle Integrationspruefungen
  ergaenzen, wo Benutzerinteraktion, Desktop-Integration oder Darstellung
  betroffen sind.
- [ ] Sliverbar nach Moeglichkeit direkt mit dem bisherigen `lemonbar-panel`
  vergleichen. Dabei insbesondere Blockreihenfolge, Inhalte, Aktualisierung,
  Klickaktionen, Menues, Farben, Abstaende, Monitorwahl, Tray und reservierten
  Bildschirmbereich pruefen.
- [ ] Fuer Integrations- und Darstellungspruefungen darf das Produktivsystem
  beziehungsweise die laufende grafische Sitzung verwendet werden, sofern die
  jeweilige Aktion fuer das System sicher ist.
- [ ] Vorher-/Nachher-Screenshots oder parallel aufgenommene Referenzbilder von
  `lemonbar-panel` und Sliverbar vergleichen, um visuelle Abweichungen,
  Regressionen und noch nicht erkannte Fehler systematisch zu finden.
- [ ] Gefundene funktionale oder visuelle Abweichungen reproduzierbar
  dokumentieren und als konkrete offene Punkte in dieser Datei nachtragen.

Akzeptanzkriterium: Die neue Sliverbar ist nicht nur durch automatisierte Tests
abgedeckt, sondern wurde in einer realen X11-Sitzung funktional und visuell
gegen das bisherige Panel geprueft. Relevante Abweichungen sind behoben oder als
offene Aufgaben dokumentiert.

## Prioritaet 6: Portabilitaets- und Kompatibilitaetsmatrix

- [x] Builds mindestens mit GCC und Clang pruefen.
- [x] Container-Tests fuer mehrere Distributionsfamilien vorsehen, zum Beispiel
  Debian/Ubuntu, Fedora und Arch Linux.
- [ ] X11-Integrationstests mit mehreren EWMH-kompatiblen Window Managern
  ergaenzen.
- [ ] Folgende Szenarien automatisiert testen:
  - fehlende optionale Programme;
  - kein bspwm und kein `bspc`;
  - fehlende konfigurierte Schriften;
  - ein Monitor, mehrere Monitore und mehrere X-Screens;
  - bereits belegte System-Tray-Selection;
  - unterbrochene oder neu gestartete Backend-Prozesse;
  - fehlendes `XDG_RUNTIME_DIR`, `XDG_CONFIG_HOME` oder `HOME`.
- [x] README um eine ehrliche Supportmatrix aus "unterstuetzt", "getestet" und
  "Best Effort" erweitern.

Akzeptanzkriterium: Die dokumentierte Supportmatrix wird durch reproduzierbare
Build- und Integrationstests abgedeckt; nicht getestete Kombinationen werden
nicht als garantiert kompatibel bezeichnet.

## Dokumentation

- [x] Die aktuelle Formulierung "Linux with X11 and bspwm" nach Einfuehrung des
  generischen Backends durch das neue Zielbild ersetzen.
- [x] Zwingende grafische Laufzeitbibliotheken von optionalen Modul-Backends
  getrennt auffuehren.
- [x] Konfigurationsbeispiele fuer einen minimalen generischen Betrieb und fuer
  die erweiterte bspwm-Integration bereitstellen.
- [x] Grenzen durch nicht EWMH-kompatible Window Manager erklaeren.
- [x] `--version`, `--check-config` und die geplante Diagnoseoption gemeinsam
  dokumentieren.

## Empfohlene Reihenfolge

1. Window-Manager-Schnittstelle und EWMH-Backend.
2. Betrieb ohne bspwm sowie optionale Module und neutrale Defaults.
3. XDG-Konfigurationssuche.
4. Mehrmonitor- und Multi-Screen-Unterstuetzung.
5. Testmatrix auf weitere Distributionen und Window Manager ausweiten.
6. Paketgenerierung und verifizierte Distributionsartefakte.
