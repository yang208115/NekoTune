#pragma once
#include "controllers/feature_controller.h"
class SettingsController final : public FeatureController {
    Q_OBJECT
  public:
    using FeatureController::FeatureController;
};
