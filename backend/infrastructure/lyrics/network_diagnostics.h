#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QString>

namespace nekotune {
// Only emit structured diagnostic fields; Qt error strings can contain signed
// URLs.
QString networkDiagnostics(const QNetworkReply *reply, const QByteArray &body = {}, const QString &secret = {});
QString providerDiagnostics(const QJsonObject &object, const QString &secret = {});
} // namespace nekotune
