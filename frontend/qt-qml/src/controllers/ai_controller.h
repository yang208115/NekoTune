#pragma once
#include "controllers/feature_controller.h"
/// Separates configuration busy state from metadata generation state.
/// Generation belongs to one editor session identified by a local counter.
/// Discarding a suggestion makes late replies irrelevant to that editor.
/// Successful results emit advisory data rather than calling metadata save.
/// Connection loss completes pending IPC work and releases busy controls.
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
    /// Submit endpoint/model edits and an optional newly typed API key.
    /// The response updates only public configuration and releases configBusy on failure too.
    /// Existing key bytes are never reconstructed from the returned configuration map.
    Q_INVOKABLE void saveConfiguration(const QVariantMap &config);
    Q_INVOKABLE void clearKey();
    Q_INVOKABLE void testConnection();
    /// @param songId Persistent song being edited; nonpositive identities are ignored.
    /// @param draft Current unsaved field values to use as advisory generation input.
    /// Only one suggestion is active for this local editor generation at a time.
    /// Result delivery checks both the generation and echoed song identity before emitting readiness.
    Q_INVOKABLE void suggest(int songId, const QVariantMap &draft);
    /// Invalidate local interest in the active generation and release its UI busy state.
    /// The backend request may still finish; its reply is ignored by the generation check.
    /// This is editor-session cancellation rather than a metadata write or remote abort request.
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
