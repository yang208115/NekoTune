#pragma once
#include "domain/kugou_backend.h"
#include <QJsonObject>
namespace nekotune {
QJsonObject toJson(const KugouStatus &status);
QJsonObject toJson(const KugouEvent &event);
} // namespace nekotune
