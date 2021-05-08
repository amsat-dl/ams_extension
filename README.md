# ams_extension
Private AMSAT-DL Erweiterung für die MM Bake

Diese Dateien sind eine Erweiterung zu HSmodem um dieses in den Bakenbetrieb zu bringen.

! Diese Dateien sind nicht für die Öffentlichkeit bestimmt !

ams_config.txt ... Konfigurationsdatei der Bake (bei der Installation manuell zu editieren)

ams_config.cpp ... lese Elemente aus ams_config.txt

ams_extension.cpp ... Emuliert das GUI, womit ein GUI.loser Betrieb möglich ist. Enthält auf den DXcluster.

Um diese Dateien in HSmodem einzubinden ist HSmodem mit Makefile_ams zu kompilieren.
