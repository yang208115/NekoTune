#pragma once
#include "domain/kugou_backend.h"
#include <QUrl>
namespace nekotune {
struct KugouSong : KugouSearchItem {
    QString audioId;
    QUrl coverUrl;
};

} // namespace nekotune
