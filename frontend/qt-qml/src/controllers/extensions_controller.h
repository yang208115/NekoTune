#pragma once
#include "controllers/feature_controller.h"
#include <QVariantList>
#include <QVariantMap>
class ExtensionsController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed)
    Q_PROPERTY(QVariantList settingsPages READ settingsPages NOTIFY changed)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QVariantList browserSources READ browserSources NOTIFY changed)
    Q_PROPERTY(QVariantList menus READ menus NOTIFY changed)
    Q_PROPERTY(QVariantList toolbars READ toolbars NOTIFY changed)
    Q_PROPERTY(QVariantMap activeSlots READ activeSlots NOTIFY changed)
    Q_PROPERTY(QVariantMap selections READ selections NOTIFY changed)
    Q_PROPERTY(QVariantMap themeTokens READ themeTokens NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  public:
    explicit ExtensionsController(IpcClient &client);
    QVariantList items() const { return m_items; }
    QVariantList pages() const;
    QVariantList settingsPages() const { return contributions("settings"); }
    QVariantList commands() const;
    QVariantList sources() const;
    QVariantList browserSources() const;
    QVariantList menus() const { return contributions("menus"); }
    QVariantList toolbars() const { return contributions("toolbars"); }
    QVariantMap activeSlots() const;
    QVariantMap selections() const { return m_selections; }
    QVariantMap themeTokens() const;
    QString error() const { return m_error; }
    Q_INVOKABLE int request(const QString &method, const QVariantMap &params = {});
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantList contributions(const QString &kind) const;
    Q_INVOKABLE QVariantMap selected(const QString &slot) const;
    Q_INVOKABLE QString label(const QVariant &value, const QString &language) const;
    Q_INVOKABLE QVariantList menuActions(const QString &context, const QString &language) const;
    Q_INVOKABLE void execute(const QString &id, const QVariantMap &params = {});
    Q_INVOKABLE void openManager() { emit managerRequested(); }
    Q_INVOKABLE void resetInterface();
  signals:
    void changed();
    void errorChanged();
    void completed(int id, const QVariantMap &data, const QString &error);
    void eventReceived(const QVariantMap &event);
    void managerRequested();

  private:
    void apply(const QJsonObject &data);
    QVariantList registrations(const QString &kind) const;
    QVariantList m_items;
    QVariantMap m_selections;
    QString m_error;
    int m_nextId = 1;
};
