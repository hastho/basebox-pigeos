# BaseBox Fork: LPT-Drucker, Host-Zeitsync und das CI/CD-Desaster

**Datum:** 4. Mai 2026  
**Tags:** GEOS, BaseBox, PC-GEOS, CI/CD, GitHub Actions, Retro-Computing  
**Kategorie:** Entwickler-Journal

---

Es begann alles so harmlos. Ich wollte eigentlich nur zwei Features zu meinem BaseBox-Fork hinzufügen – einer spezialisierten Distribution von DOSBox-Staging, die ich für den Betrieb von PC-GEOS auf dem Raspberry Pi bestimmt ist. Drucken auf angeschlossenen (Netzwerk-)Druckern zu ermöglichen, das war die erste Aufgabe. Pi/GEOS Anwender sollen einfach aus den Applikationen heraus auf modernen Druckern drucken können. Und die DOS-Uhr? Die vergaß immer die Zeit, wenn der Host in den Schlafmodus ging. Also Host-Zeitsynchronisation, damit GEOS die Host-Uhr persistent tracken. Einfach, dachte ich.

Einen Monat später sitze ich hier und schreibe diesen Bericht. Was als Feature-Update begann, wurde zu einem zweigleisigen Projekt: Neue Funktionalität implementieren und gleichzeitig eine CI/CD-Infrastruktur modernisieren, die teilweise noch aus dem Jahr 2023 stammte. Über 60 Iterationen der GitHub Actions Workflows später und ich habe eine Geschichte zu erzählen...

## Zwei Features, eine Mission

Zuerst die Druckerumleitung. Die alte `device_LPT1`-Stub-Implementierung in `src/dos/dos_devices.cpp` war unzureichend, also habe ich `src/dos/dev_lpt.cpp` und `dev_lpt.h` von Grund auf neu geschrieben. Konfigurierbare Druckbefehle pro Port (LPT1 bis LPT4) in der `[printer]`-Sektion, Print-Timeout, Spool-Directory – und ein Hintergrund-Worker-Thread für asynchrone Druckaufträge. Cross-Platform Support war natürlich Pflicht: Windows nutzt `notepad /p %s`, Linux `lp %s`, macOS `lpr %s`. Die Integration in die BIOS-Interrupts (INT 17h) erforderte noch einen Patch in `src/ints/bios.cpp`, um die LPT-Ports korrekt zu initialisieren. Aber das Ergebnis zahlt sich aus: PC-GEOS Anwendungen drucken jetzt nahtlos auf Host-Druckern. Ein kritisches Feature für die produktive Entwicklung, das endlich funktioniert.

Dann die Host-Zeitsynchronisation. Das alte `UpdateDateTimeFromHost()`-Konzept war veraltet und plattformabhängig, also bin ich auf C++11 `std::chrono` umgestiegen. Die Konfiguration ist simpel: `[dosbox] hosttime=true` in der Config. `BIOS_HostTimeSync()` sorgt für sofortige Synchronisation, der INT 8 Handler hält die Zeit kontinuierlich aktuell. `std::chrono::system_clock::now()` ersetzt plattform-spezifische APIs, `duration_cast<milliseconds>` berechnet die Ticks präzise, und direkte Speicherzugriffe auf `BIOS_TIMER` sorgen für maximale Performance. Selbst nach Sleep- oder Hibernate-Zyklen verliert DOS die Zeit-Referenz nicht mehr. Plattformunabhängig, dank `cross::localtime_r()`.

## Das CI/CD-Desaster

Mit der Repository-Struktur beginnt das eigentliche Abenteuer: DOSBox-Staging ist der Upstream, daraus wurde BaseBox (bluewaysw/pcgeos-basebox) ubgeleitet und mein Fork wiederum ist hastho/basebox-pigeos. Drei Stufen Hierarchie, ständige Synchronisation zwischen Upstream-Neuerungen und eigenen Features. Aktueller Stand? 1429 Commits vor BaseBox Upstream, aber 207 hinter deren main Branch. Eigene Features ja, aber eine veraltete Basis – das würde später noch wehtun.

Ein weiterer Albtraum war die Build-Infrastruktur. BaseBox Upstream nutzt teilweise veraltete Workflows aus 2023/2024, während sie in DOSBox-Staging schon überarbeitet wurden. Die Divergenz war massiv: BaseBox nutzt teilweise noch CMake, DOSBox-Staging ist längst Meson-only. Mein Fork folgt der aktuellen Konvention und ist komplett auf Meson umgestellt – aber die Workflows stammen von BasBox mussten trotzdem komplett überarbeitet werden.

### macOS: Von Rosetta-Hacks zu nativen Runnern

Das `macos.yml` Workflow hat 8 Iterationen durchlaufen (von v0.82.0-pigeos.56 bis .63). Erst das Architektur-Chaos: `macos-15` Runner sind nur ARM64 (Apple Silicon), x86_64 Builds brauchten Rosetta 2 Cross-Compile Hacks. Die Lösung? `macos-15-intel` Runner für native x86_64 Builds. Dann kamen die Python-Symlink-Konflikte: GitHub Runner haben präinstallierte Python-Versionen, die mit Homebrew kollidieren. `brew link --overwrite python@3.14` war der Workaround.

Der echte Showstopper war aber dylibbundler. Das Tool zum Bündeln dynamischer Bibliotheken hing sich auf, weil es interaktive Prompts erwartete, wenn Bibliotheken nicht gefunden wurden. Mehrstufige Lösung: Suchpfade für beide Homebrew-Präfixe (`/usr/local` für Intel, `/opt/homebrew` für Apple Silicon) hinzufügen, `yes quit |` Pipe um die Hänger zu verhindern, `timeout-minutes: 10` als Sicherheitsnetz, und Force-Linking von glib und gettext vor dem Bundling.

Dann, nach dem ersten Erfolg (v0.82.0-pigeos.67), crashte die App auf echten Macs: `Library not loaded: /opt/homebrew/*/libintl.8.dylib`. Root Cause? `libglib` hat interne Dependencies auf `libintl` (gettext), die nicht gebündelt wurden. Der manuelle Ansatz mit einer festen LIBS-Liste konnte transitive Dependencies nicht auflösen.

Also habe ich die "Spider"-Lösung (v0.82.0-pigeos.75) gebaut: Ein rekursiver Queue-basierter Algorithmus, der automatisch alle transitiven Dependencies findet. Das Skript startet bei der dosbox Binary, findet alle Homebrew-Abhängigkeiten mit `otool -L`, bündelt sie, und fügt sie zur Queue hinzu, um deren Dependencies auch zu verarbeiten. Keine manuellen Listen mehr, keine Bash-Subshell-Probleme (dank Temp-Dateien statt Arrays), und nach allen Änderungen gibt es ad-hoc Codesigning mit `codesign --force --deep --sign -`. Das Ergebnis? v0.82.0-pigeos.75 läuft erfolgreich auf einem echten Apple Silicon Mac (macOS 26.4.1, Mac16,13). x86_64 Builds dauern ~7 Minuten, ARM64 ~2.5 Minuten, und das Universal Binary entsteht via `lipo` plus DMG-Erstellung.

Ein kleines Problem gab es noch: `permissions: read-all` verhinderte die Release-Erstellung durch `softprops/action-gh-release`. Der Fix war simpel: `contents: write`, `pull-requests: read`, `actions: read` in die Permissions schreiben.

### Windows und Linux: Kleinere Hürden

Das `windows-msvc.yml` Workflow war weniger dramatisch, aber trotzdem Arbeit: Vereinfachung auf x64-only Builds (Wegfall von ARM64-Support), MSVC Version Updates auf das v143 Toolset, und Vcpkg-Baseline-Anpassungen für das Dependency-Management.

Beim `linux.yml` Workflow ging es um Modernisierung: Reduktion auf Ubuntu-only für Core-Dependencies, Deaktivierung von `auto_vec_info` wegen `-march=native` Konflikten, und Umstellung von `pip` auf `apt` für Python-Abhängigkeiten (wegen Systemsicherheit).

## Was kommt als Nächstes?

GitHub-Runner für Raspberry Pi Cross-Builds in Docker-Containern - damit das alles endlich auf dem Pi läuft ;-)

Die mittelfristig größte Herausforderung ist die zunehmende Divergenz zu DOSBox-Staging. Während wir hier an BaseBox-spezifischen Features arbeiten, entwickelt sich Upstream rasant weiter. 207 Commits hinter BaseBox main bedeuten: Wenn wir das nächste Mal mergen wollen, werden wir massive Merge-Konflikte bekommen. Workflow-Divergenz, schwer zu portierende Features, und geplante Verbesserungen in DOSBox-Staging (OpenGL, neue Shader, Audio-Features) – das wird kein Spaziergang.

## Fazit

Die Modernisierung der CI/CD-Pipeline war ein massiver Aufwand, der mehr Zeit in Anspruch nahm als die Feature-Implementierung selbst. Aber am Ende haben wir:

- LPT-Druckerumleitung für Pi/GEOS Weiterentwicklung
- Permanente Host-Zeitsynchronisation
- Eine funktionierende, modernere CI/CD-Pipeline über alle Plattformen
- Native Runner-Strategie für macOS (keine Cross-Compile oder Rosetta-Hacks mehr)

---

## Technische Details (Kurzfassung)

- Repository: `hastho/basebox-pigeos`
- Feature Branch: `feature/combined-upgrade-13-lpt-hosttime`
- Aktuelle Version: `v0.82.0-pigeos.75`
- Build System: Meson (kein CMake für macOS)
- macOS Runner: `macos-15-intel` (x86_64), `macos-15` (ARM64)
- Workflow-Dateien: `macos.yml`, `windows-msvc.yml`, `linux.yml`
- Wichtige Commits: LPT-Drucker (`35285742c`, `38c7c81c1`), Host-Zeitsync (`0f8da5d3b`, `132753ec3`), CI/CD Fixes (`38409905c` u.a.), macOS Spider (`208196d16` u.a.)

---

## Glossar (Fachbegriffe)

- **BaseBox**: Fork von DOSBox-Staging für PC-GEOS Entwicklung
- **DOSBox-Staging**: Aktive Community-Fork von DOSBox mit modernen Features
- **GEOS/PC-GEOS**: Grafisches Betriebssystem aus den 80er/90er Jahren, Basis für PC-GEOS
- **LPT**: Line Print Terminal (Parallelport) für Drucker in DOS
- **Host-Zeitsync**: Synchronisation der DOS-Uhr mit der Host-Systemzeit
- **CI/CD**: Continuous Integration / Continuous Deployment (automatisierte Builds)
- **GitHub Actions**: CI/CD-Plattform von GitHub
- **dylibbundler**: macOS-Tool zum Bündeln dynamischer Bibliotheken (.dylib)
- **Rosetta 2**: Apple-Technologie zur Ausführung von x86_64-Code auf ARM64
- **Homebrew**: Paketmanager für macOS (ähnlich apt unter Linux)
- **Meson**: Modernes Build-System (Alternative zu CMake)
- **INT 8/INT 17h**: BIOS Interrupts für Timer (8) und Drucker (17h)
- **lipo**: macOS-Tool zum Zusammenfügen von FAT-Binaries (Universal Binaries)
