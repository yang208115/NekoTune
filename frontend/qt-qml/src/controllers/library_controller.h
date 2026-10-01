#pragma once
#include "controllers/feature_controller.h"
#include "models/record_model.h"
class LibraryController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(RecordModel *songs READ songs CONSTANT)
    Q_PROPERTY(RecordModel *tags READ tags CONSTANT)
    Q_PROPERTY(RecordModel *filteredSongs READ filteredSongs CONSTANT)
    Q_PROPERTY(QVariantList selectedTagIds READ selectedTagIds WRITE setSelectedTagIds NOTIFY filterChanged)
    Q_PROPERTY(QVariantList selectedSongIds READ selectedSongIds NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool metadataReady READ metadataReady NOTIFY editingChanged)
  public:
    explicit LibraryController(IpcClient &client);
    RecordModel *songs() { return &m_songs; }
    RecordModel *tags() { return &m_tags; }
    RecordModel *filteredSongs() { return &m_filtered; }
    QVariantList selectedTagIds() const { return m_tagIds; }
    QVariantList selectedSongIds() const { return m_songIds; }
    QVariantMap editing() const { return m_editing; }
    bool metadataReady() const { return m_metadataReady; }
    void setSelectedTagIds(const QVariantList &ids);
    Q_INVOKABLE void toggleTag(int id);
    Q_INVOKABLE void toggleSelection(int id);
    Q_INVOKABLE void toggleSelectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void importLibraryPath(const QString &path) {
        send("library.import", {{"path", IpcClient::normalizePath(path)}});
    }
    Q_INVOKABLE void playLibrary(const QVariantList &tagIds, int songId = 0);
    Q_INVOKABLE void deleteLibrarySongs(const QVariantList &ids);
    Q_INVOKABLE void loadMetadata(int id);
    Q_INVOKABLE void saveMetadata(const QVariantMap &patch);
  signals:
    void filterChanged();
    void selectionChanged();
    void editingChanged();
    void songMetadataSaved(int id);
    void libraryPlaybackSkipped(int count);

  private:
    void apply(const QJsonObject &data);
    void filter();
    RecordModel m_songs{"song_id"}, m_tags{"id"}, m_filtered{"song_id"};
    QVariantList m_tagIds, m_songIds;
    QVariantMap m_editing;
    bool m_metadataReady = false;
    int m_editingId = 0;
    quint64 m_generation = 0;
    bool m_loading = false, m_reload = false;
};
