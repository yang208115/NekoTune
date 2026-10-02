#pragma once
#include "controllers/feature_controller.h"
#include "models/record_model.h"
/// Owns library filtering, selection and the full-metadata editing session.
/// Songs/tags use stable record models reconciled by backend identities.
/// Filtered playback sends explicit visible song order to the backend.
/// Selection changes alone never issue a playback command.
/// Metadata loads are generation-guarded across repeated editor opens.
/// The controller stores editing data separately from lightweight list rows.
class LibraryController final : public FeatureController {
    Q_OBJECT
    Q_PROPERTY(RecordModel *songs READ songs CONSTANT)
    Q_PROPERTY(RecordModel *tags READ tags CONSTANT)
    Q_PROPERTY(RecordModel *filteredSongs READ filteredSongs CONSTANT)
    Q_PROPERTY(QVariantList selectedTagIds READ selectedTagIds WRITE setSelectedTagIds NOTIFY filterChanged)
    Q_PROPERTY(QVariantList selectedSongIds READ selectedSongIds NOTIFY selectionChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    Q_PROPERTY(QVariantMap editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool metadataReady READ metadataReady NOTIFY editingChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadStateChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadStateChanged)
  public:
    explicit LibraryController(IpcClient &client);
    RecordModel *songs() { return &m_songs; }
    RecordModel *tags() { return &m_tags; }
    RecordModel *filteredSongs() { return &m_filtered; }
    QVariantList selectedTagIds() const { return m_tagIds; }
    QVariantList selectedSongIds() const { return m_songIds; }
    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    QVariantMap editing() const { return m_editing; }
    bool metadataReady() const { return m_metadataReady; }
    bool loading() const { return m_loading; }
    bool loaded() const { return m_loaded; }
    void setSelectedTagIds(const QVariantList &ids);
    Q_INVOKABLE void toggleTag(int id);
    Q_INVOKABLE void toggleSelection(int id);
    /// @param id Song identity in the current filtered model, not a source-model row number.
    /// @param extend Preserve the existing selection for toggle/range operations.
    /// @param range Select the inclusive visible range from the current song-ID anchor.
    /// A hidden song ID is ignored; selection never issues a backend playback request.
    Q_INVOKABLE void selectRow(int id, bool extend = false, bool range = false);
    /// Select every currently visible song, including unavailable records.
    /// If the visible list is already fully selected, clear its selection instead.
    /// The result is a view action rather than an instruction to enqueue the selected rows.
    Q_INVOKABLE void toggleSelectAll();
    Q_INVOKABLE void clearSelection();
    /// Refresh songs and catalog tags through one library snapshot request.
    /// Concurrent refresh triggers coalesce into a follow-up request after the active one.
    /// loaded records whether any snapshot succeeded; loading describes current request activity.
    /// A failure retains the previous displayed records instead of replacing them with emptiness.
    Q_INVOKABLE void refreshLibrary();
    Q_INVOKABLE void importLibraryPath(const QString &path) {
        send("library.import", {{"path", IpcClient::normalizePath(path)}});
    }
    /// @param tagIds Intersection to submit for backend playback validation.
    /// @param songId Requested starting song, or zero for the first playable match.
    /// When tagIds equals the current filter, submit the current visible search order too.
    /// Otherwise the backend derives the collection from those tags without inheriting this view.
    Q_INVOKABLE void playLibrary(const QVariantList &tagIds, int songId = 0);
    /// @param songIds Explicit ordered song context owned by the caller, such as Home.
    /// @param startSongId Starting identity within that context, or zero for its first playable song.
    /// This path deliberately submits no selected-tag constraints from the library view.
    /// Skipped unavailable songs are reported through libraryPlaybackSkipped after success.
    Q_INVOKABLE void playSongs(const QVariantList &songIds, int startSongId = 0);
    /// @param ids Frozen song-ID batch chosen by the confirmation dialog.
    /// @param cleanFiles Explicitly request managed-file cleanup in addition to collection removal.
    /// Clear selection only after backend success; failure leaves the retry context available.
    /// Post-commit cleanup warnings are delivered separately from a rejected deletion.
    Q_INVOKABLE void deleteLibrarySongs(const QVariantList &ids, bool cleanFiles = false);
    /// Begin a new editing generation even when the same song is reopened.
    /// Clear editing data until the full metadata response for this generation arrives.
    /// A late response from another opening must not enable saving the wrong draft.
    Q_INVOKABLE void loadMetadata(int id);
    /// @param patch Only the fields intentionally changed by the editor.
    /// The current editing song ID is supplied by the controller, not trusted from the patch.
    /// Saving is refused until full metadata for that editing session is ready.
    /// Omitting lyrics preserves them; an explicit empty lyric string requests clearing.
    Q_INVOKABLE void saveMetadata(const QVariantMap &patch);
  signals:
    void filterChanged();
    void selectionChanged();
    void editingChanged();
    void songMetadataSaved(int id);
    void libraryPlaybackSkipped(int count);
    void libraryDeletionFinished(const QStringList &cleanupErrors);
    void loadStateChanged();

  private:
    void apply(const QJsonObject &data);
    void filter();
    RecordModel m_songs{"song_id"}, m_tags{"id"}, m_filtered{"song_id"};
    QVariantList m_tagIds, m_songIds;
    QString m_searchText;
    int m_selectionAnchor = 0;
    QVariantMap m_editing;
    bool m_metadataReady = false;
    int m_editingId = 0;
    quint64 m_generation = 0;
    bool m_loading = false, m_loaded = false, m_reload = false;
};
