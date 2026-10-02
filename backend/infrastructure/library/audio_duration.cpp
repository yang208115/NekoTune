#include "infrastructure/library/audio_duration.h"
#include <QElapsedTimer>
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

namespace nekotune {
namespace {
struct ProbeContext {
  const std::atomic_bool &cancelled;
  QElapsedTimer elapsed;
  static int interrupt(void *opaque) {
    const auto &probe = *static_cast<ProbeContext *>(opaque);
    return probe.cancelled.load() || probe.elapsed.hasExpired(5000);
  }
};
} // namespace

qint64 readAudioDuration(const QString &path,
                         const std::atomic_bool &cancelled) {
  if (cancelled.load())
    return 0;
  ProbeContext probe{cancelled, {}};
  probe.elapsed.start();
  auto *format = avformat_alloc_context();
  if (!format)
    return 0;
  format->interrupt_callback = {&ProbeContext::interrupt, &probe};
  AVDictionary *options = nullptr;
  // Imported files are local; probing must never follow a network playlist.
  av_dict_set(&options, "protocol_whitelist", "file", 0);
  av_dict_set(&options, "format_whitelist", "mp3,mov,aac,wav,flac,ogg", 0);
  av_dict_set(&options, "probesize", "1048576", 0);
  av_dict_set(&options, "analyzeduration", "2000000", 0);
  const auto filename = path.toUtf8();
  const auto opened =
      avformat_open_input(&format, filename.constData(), nullptr, &options);
  av_dict_free(&options);
  if (opened < 0)
    return 0; // avformat_open_input frees the context on failure.
  qint64 duration = 0;
  if (avformat_find_stream_info(format, nullptr) >= 0 &&
      !ProbeContext::interrupt(&probe)) {
    for (unsigned int index = 0; index < format->nb_streams; ++index) {
      const auto *stream = format->streams[index];
      if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        continue;
      if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
        duration = av_rescale_q(stream->duration, stream->time_base,
                                AVRational{1, 1000});
      else if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
        duration =
            av_rescale_q(format->duration, AV_TIME_BASE_Q, AVRational{1, 1000});
      if (duration > 0)
        break;
    }
  }
  avformat_close_input(&format);
  return qMax(qint64(0), duration);
}
} // namespace nekotune
