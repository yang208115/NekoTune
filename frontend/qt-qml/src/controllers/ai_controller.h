#pragma once
#include "controllers/feature_controller.h"
class AiController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QVariantMap configuration READ configuration NOTIFY configurationChanged)
    Q_PROPERTY(bool configBusy READ configBusy NOTIFY configBusyChanged)
    Q_PROPERTY(bool generating READ generating NOTIFY generatingChanged)
    Q_PROPERTY(QString suggestionError READ suggestionError NOTIFY suggestionErrorChanged)
  public:
    explicit AiController(IpcClient &client);
    QVariantMap configuration() const { return m_configuration; }
    bool configBusy() const { return m_configBusy; }
    bool generating() const { return m_generating; }
    QString suggestionError() const { return m_suggestionError; }
    Q_INVOKABLE void refreshConfiguration();
    Q_INVOKABLE void saveConfiguration(const QVariantMap &config);
    Q_INVOKABLE void clearKey();
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void suggest(int songId, const QVariantMap &draft);
    Q_INVOKABLE void discardSuggestion();
  signals:
    void configurationChanged();
    void configBusyChanged();
    void generatingChanged();
    void suggestionErrorChanged();
    void suggestionReady(const QVariantMap &suggestion);

  private:
    void configRequest(const QString &method, const QJsonObject &params = {});
    QVariantMap m_configuration{{"configured", false}, {"key_saved", false}};
    bool m_configBusy = false, m_generating = false;
    QString m_suggestionError;
    quint64 m_generation = 0;
};
