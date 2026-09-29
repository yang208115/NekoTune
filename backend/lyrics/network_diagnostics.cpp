#include "lyrics/network_diagnostics.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace nekotune {
namespace {
QString safeToken(const QString &value, const QString &secret)
{
    static const QRegularExpression token(QStringLiteral("^[A-Za-z0-9_.:-]{1,128}$"));
    if (!token.match(value).hasMatch() || value.startsWith(QStringLiteral("sk-"), Qt::CaseInsensitive) ||
        (!secret.isEmpty() && value.contains(secret)))
        return QStringLiteral("unavailable");
    return value;
}
} // namespace
QString providerDiagnostics(const QJsonObject &object, const QString &secret)
{
    return QStringLiteral("upstream_code=%1 request_id=%2")
        .arg(safeToken(object.value(QStringLiteral("code")).toString(), secret),
             safeToken(object.value(QStringLiteral("request_id")).toString(), secret));
}
QString networkDiagnostics(const QNetworkReply *reply, const QByteArray &body, const QString &secret)
{
    if (!reply)
        return QStringLiteral("http=0 network=NoReply");
    const auto network = QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(reply->error());
    QString result = QStringLiteral("http=%1 network=%2")
                         .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                         .arg(network ? QString::fromLatin1(network) : QString::number(reply->error()));
    if (!body.isEmpty() && body.size() <= 2 * 1024 * 1024) {
        auto object = QJsonDocument::fromJson(body).object();
        if (object.isEmpty() && body.trimmed().startsWith('<')) {
            QXmlStreamReader xml(body);
            while (!xml.atEnd()) {
                xml.readNext();
                if (!xml.isStartElement())
                    continue;
                if (xml.name() == QLatin1String("Code"))
                    object.insert(QStringLiteral("code"), xml.readElementText());
                else if (xml.name() == QLatin1String("RequestId"))
                    object.insert(QStringLiteral("request_id"), xml.readElementText());
            }
        }
        result += QLatin1Char(' ') + providerDiagnostics(object, secret);
    }
    return result;
}
} // namespace nekotune
