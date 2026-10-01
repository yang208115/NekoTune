#pragma once
#include "domain/kugou_backend.h"
#include "domain/result.h"

namespace nekotune {
class KugouService final : public QObject {
    Q_OBJECT
  public:
    explicit KugouService(IKugouBackend &backend) : m_backend(backend) {
        connect(&backend, &IKugouBackend::eventReady, this, &KugouService::eventReady);
        connect(&backend, &IKugouBackend::audioReady, this, &KugouService::audioReady);
    }
    KugouStatus status() const { return m_backend.status(); }
    Result<void> saveAccountKey(const QString &key) { return result(m_backend.saveAccountKey(key)); }
    Result<void> clearAccountKey() { return result(m_backend.clearAccountKey()); }
    Result<void> startCodeRequest(const QString &mobile) {
        return result(m_backend.startCodeRequest(mobile));
    }
    Result<void> startLogin(const QString &mobile, const QString &code) {
        return result(m_backend.startLogin(mobile, code));
    }
    Result<void> startSearch(const QString &keywords, int page) {
        return result(m_backend.startSearch(keywords, page));
    }
    Result<void> startDownload(const QString &hash) { return result(m_backend.startDownload(hash)); }
    Result<void> cancelDownload() { return result(m_backend.cancelDownload()); }
  signals:
    void eventReady(const nekotune::KugouEvent &event);
    void audioReady(const QString &path, const QString &lyricStatus, const QString &coverStatus,
                    const QString &title, const QString &artist);

  private:
    static Result<void> result(const QString &error) {
        return error.isEmpty() ? Result<void>{} : Result<void>{failure(error)};
    }
    IKugouBackend &m_backend;
};
} // namespace nekotune
