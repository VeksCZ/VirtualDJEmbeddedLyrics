LRC Lyrics plugin pro VirtualDJ (Windows 64-bit)
================================================

Obsah
-----

- EmbeddedLyricsMaster.dll  -> ve VirtualDJ se zobrazí jako "LRC Master"
- EmbeddedLyricsDeck.dll    -> ve VirtualDJ se zobrazí jako "LRC Deck"
- Blackout.dll              -> ve VirtualDJ se zobrazí jako "LRC BlackOut"

Instalace
---------

1. Ukončete VirtualDJ.
2. Stiskněte Win+R a otevřete:

   %LOCALAPPDATA%\VirtualDJ\Plugins64\VideoEffect

3. Do této složky zkopírujte:

   EmbeddedLyricsMaster.dll
   Blackout.dll

4. Pro použití LRC Deck jako efektu konkrétního decku zkopírujte do stejné
   složky také EmbeddedLyricsDeck.dll.

5. Pro zobrazení LRC Deck dole v nabídce "Source for audio-only tracks"
   zkopírujte EmbeddedLyricsDeck.dll také do:

   %LOCALAPPDATA%\VirtualDJ\Plugins64\Visualisations

   Pokud složka Visualisations neexistuje, vytvořte ji.

6. Spusťte VirtualDJ znovu.

Použití
-------

- LRC Master: aktivujte v Master Video FX v sekci Overlays.
- LRC Deck: vyberte jako Source for audio-only tracks, případně jej aktivujte
  ve Video FX konkrétního decku.
- LRC BlackOut: volitelný černý podklad v Master Video FX. Běží pod dalšími
  efekty, například slideshow a shadery.

Plugin čte časované titulky z ID3 tagů SYLT, SYNCEDLYRICS a také timestampy
uložené v UNSYNCEDLYRICS nebo USLT. Podporuje rovněž stejně pojmenované .lrc
a .txt soubory vedle skladby. Text bez časových značek lze posouvat ručně.
