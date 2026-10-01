#pragma once
#include "domain/lyrics_types.h"
#include <QJsonObject>
namespace nekotune {
QJsonObject toJson(const LyricsDocument &document);
QJsonObject toJson(const LyricsCandidate &candidate);
QJsonObject toJson(const LyricsSnapshot &snapshot);
} // namespace nekotune
