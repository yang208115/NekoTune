#pragma once
#include "domain/krc_parser.h"
#include <QJsonObject>
namespace nekotune {
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
