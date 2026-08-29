LRC Lyrics pro VirtualDJ 0.3.0 (Windows 64-bit)
================================================

Instalace
---------

1. Ukoncete VirtualDJ.
2. Otevrete %LOCALAPPDATA%\VirtualDJ\Plugins64.
3. Vytvorte slozky VideoOverlay a Visualisations, pokud neexistuji.
4. Do VideoOverlay zkopirujte:
   - LRC Master.dll
   - LRC BlackOut.dll
   - EmbeddedLyricsTagWriter.py
5. Do Visualisations zkopirujte:
   - LRC Deck.dll
   - EmbeddedLyricsTagWriter.py
6. Spustte VirtualDJ.

Pouziti
-------

- LRC Master a LRC BlackOut jsou v horni sekci Overlays.
- LRC Deck vyberte jako Source for audio-only tracks. Jde o jediny automaticky
  videoAudioOnlyVisualisation slot VirtualDJ. Pri dvou audio skladbach ho muze
  VirtualDJ vykreslovat na Masteru; SDK nema samostatny automaticky slot pro
  kazdy deck.
- Text ma cerny obrys pro citelnost pres fotografie a video.
- Tlacitko Advanced otevre dialog LRC Presets s volbou fontu, obrysu/stinu
  a barev textu.

Titulky se ctou z tagu SYLT, SYNCEDLYRICS, UNSYNCEDLYRICS a USLT i ze stejne
pojmenovanych souboru .lrc a .txt. Timestampy nalezene v TXT nebo unsynced tagu
se pouziji jako casovane titulky.

Pro zapis rucne natukaneho casovani je potreba Python 3 a Mutagen:
  py -m pip install mutagen
