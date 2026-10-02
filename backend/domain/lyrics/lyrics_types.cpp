#include "domain/lyrics/lyrics_types.h"
#include "domain/lyrics/krc_parser.h"
#include "domain/lyrics/lrc_parser.h"

namespace nekotune {

void LyricsDocument::validate() {
    if (!LrcParser::looksLikeLrc(syncedLyrics))
        syncedLyrics.clear();
    if (KrcParser::parse(krcLyrics).isEmpty())
        krcLyrics.clear();
    if (instrumental) {
        syncedLyrics.clear();
        krcLyrics.clear();
        plainLyrics.clear();
    }
}

} // namespace nekotune
