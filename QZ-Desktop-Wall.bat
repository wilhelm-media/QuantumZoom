@echo off
REM ── QuantumZoom, Desktop-Fassung fuer die Probe ─────────────────────────────
REM Wall-Bild, kein Cluster: ohne nDisplay-Konfiguration meldet der Pawn
REM bInCluster = false, uebernimmt die Kamera selbst und die Titelkarte faellt
REM auf ihre Wall-Fassung zurueck. Es gibt dafuer keinen eigenen Modus - es ist
REM derselbe Build, nur ohne den Cluster-Start.
REM
REM Aufloesung hier setzen; das MELINDA-Menue im Stueck regelt zusaetzlich die
REM interne Renderaufloesung (r.ScreenPercentage) und ueberlebt einen Neustart.

set PROJ=C:\Users\WILHELM\WILHELM-MEDIA\PROJEKTE\aktiv\QuantumZOOM\DeepSpaceStarter\Project
set EXE=%PROJ%\Binaries\Win64\QuantumZoom.exe

"%EXE%" "%PROJ%\QuantumZoom.uproject" -game ^
  -windowed -resx=2560 -resy=1440 ^
  -notexturestreaming ^
  -log

REM Vollbild statt Fenster:      -fullscreen  (statt -windowed -resx -resy)
REM Anderer Startpunkt im Stueck: nichts noetig, das Stueck startet im Titel.
