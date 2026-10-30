# LRC Lyrics for VirtualDJ 0.8.7

## Changes

- macOS: LRC Master now shows the track intro (artist, title and the first lyric line or countdown) before the lyrics start, like the Windows plugin.
- New **Edit lyrics** tab in LyricsTools for Windows and macOS: edit timed (SYLT) and untimed (USLT) embedded lyrics of any MP3. Double-click an MP3 in **Problems** to open it there.
- Online lyrics download is much faster: lookups run in parallel (adjustable, default 4), LRCLIB requests share one rate limit and retry temporary errors, and duplicate tracks are looked up once. MP3 writes remain sequential and in library order.
- Diagnostic ZIPs now also redact Linux and home-relative paths.

## Download

Extract the complete ZIP for your system, open LyricsTools and choose Install / update plugin. On Windows keep the _internal folder beside the EXE.

The in-VirtualDJ lyrics editor button (Edit text / Edit browser) is still Windows-only; macOS users edit lyrics in the LyricsTools app.
