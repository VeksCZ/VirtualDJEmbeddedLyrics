#include "Lyrics.hpp"

LyricsLoadResult LoadLyricsForTrack(const std::filesystem::path& audioPath) {
    auto embedded = LoadEmbeddedTimedLyrics(audioPath);
    if (!embedded.document.empty()) return embedded;

    auto lrcPath = audioPath;
    lrcPath.replace_extension(L".lrc");
    auto sidecar = LoadLrc(lrcPath);
    if (!sidecar.document.empty()) return sidecar;

    auto textPath = audioPath;
    textPath.replace_extension(L".txt");
    auto plainText = LoadPlainTextLyrics(textPath);
    if (!plainText.document.empty() && plainText.document.synchronized) return plainText;

    auto untimed = LoadEmbeddedUntimedLyrics(audioPath);
    if (!untimed.document.empty()) return untimed;
    if (!plainText.document.empty()) return plainText;

    plainText.error = L"Timed embedded: " + embedded.error + L"; LRC: " + sidecar.error +
                      L"; untimed embedded: " + untimed.error + L"; TXT: " + plainText.error;
    return plainText;
}
