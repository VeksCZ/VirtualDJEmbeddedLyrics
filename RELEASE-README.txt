LRC Lyrics pro VirtualDJ 0.2.0 (Windows 64-bit)
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
- LRC Deck vyberte dole jako Source for audio-only tracks.
- VDJ muze visualisation zdroj LRC Deck soucasne ukazat i mezi overlays; je to
  bezne chovani nabidky VirtualDJ.
- Tri tlacitka barev oteviraji systemovy Windows vyber barvy.
- Text ma adaptivne silny cerny obrys pro citelnost pres fotografie a video.

Titulky se ctou z tagu SYLT, SYNCEDLYRICS, UNSYNCEDLYRICS a USLT i ze stejne
pojmenovanych souboru .lrc a .txt. Timestampy nalezene v TXT nebo unsynced tagu
se pouziji jako casovane titulky.

Pro zapis rucne natukaneho casovani je potreba Python 3 a Mutagen:
  py -m pip install mutagen
