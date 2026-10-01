#pragma once
#include "controllers/feature_controller.h"
class TagController final : public FeatureController {
    Q_OBJECT
  public:
    using FeatureController::FeatureController;
    Q_INVOKABLE void manageTag(const QString &action, const QVariantMap &params) {
        send("tag." + action, QJsonObject::fromVariantMap(params));
    }
};
