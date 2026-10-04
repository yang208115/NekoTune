#pragma once
#include "domain/lyrics/krc_parser.h"
#include <QJsonObject>
namespace nekotune {
// Provider responses carry base64 rather than a ready text document.
// Validate both binary decoding and parsed lyric usefulness here.
// Keep original binary bytes for an authentic .krc file on disk.
// The decoded text is used by the lyric viewer without another request.
// Returning no payload lets callers use their existing LRC fallback.
inline std::optional<KrcPayload> readKrcResponse(const QJsonObject &response) {
    const auto content = response.value(QStringLiteral("content"));
    if (response.value(QStringLiteral("status")).toInt() != 200 || !content.isString())
        return std::nullopt;
    const auto encoded = content.toString().toLatin1();
    if (encoded.isEmpty() || encoded.size() > 4 * 1024 * 1024)
        return std::nullopt;
    const auto result = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (result.decodingStatus != QByteArray::Base64DecodingStatus::Ok)
        return std::nullopt;
    auto text = KrcParser::decode(result.decoded);
    if (!text || KrcParser::parse(*text).isEmpty())
        return std::nullopt;
    return KrcPayload{result.decoded, *text};
}

} // namespace nekotune
