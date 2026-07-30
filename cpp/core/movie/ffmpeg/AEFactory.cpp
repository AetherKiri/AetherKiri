#include "AEFactory.h"
#include "AEStream.h"
#include "WaveMixer.h"
#include "Clock.h"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

#include "Timer.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

NS_KRMOVIE_BEGIN

static bool VideoTraceEnabled() {
    const char *value = std::getenv("AETHERKIRI_VIDEO_TRACE");
    return value && *value && std::strcmp(value, "0") != 0;
}

static int64_t GetLayoutByChannels(int nChannel) {
    switch(nChannel) {
        case 1:
            return AV_CH_LAYOUT_MONO;
        case 2:
            return AV_CH_LAYOUT_STEREO;
        case 3:
            return AV_CH_LAYOUT_2POINT1;
        case 4:
            return AV_CH_LAYOUT_QUAD;
        case 5:
            return AV_CH_LAYOUT_5POINT0;
        case 6:
            return AV_CH_LAYOUT_5POINT1;
        case 7:
            return AV_CH_LAYOUT_6POINT1;
        case 8:
            return AV_CH_LAYOUT_7POINT1;
        default:
            return 0;
    }
}

class CAEStreamAL : public IAEStream {
    iTVPSoundBuffer *m_impl = nullptr;
    AEAudioFormat m_format;
    IAEClockCallback *m_cbClock;
    double m_playbackStartPts = 0;
    tjs_uint m_playbackStartSample = 0;
    bool m_hasPts = false;
    double m_syncError = 0;
    unsigned int m_syncErrorTime = 0;
    Timer m_syncTimer;
    struct SwrContext *swr_ctx = nullptr;
    AVSampleFormat swr_tgtFormat;
    unsigned int src_buffer_count = 0;
    unsigned int tgt_frameSize = 0;
    uint8_t *audio_buf = nullptr;
    unsigned int audio_buf_size = 0;
    AVFilterGraph *tempo_graph = nullptr;
    AVFilterContext *tempo_source = nullptr;
    AVFilterContext *tempo_sink = nullptr;
    double playback_rate = 1.0;
    int64_t tempo_input_pts = 0;
    uint64_t tempo_trace_input_frames = 0;
    uint64_t tempo_trace_output_frames = 0;
    uint64_t tempo_trace_baseline_input_frames = 0;
    uint64_t tempo_trace_baseline_output_frames = 0;
    bool tempo_trace_has_baseline = false;
    bool tempo_trace_logged = false;
    std::mutex _mutex;
    std::condition_variable _cond;
    Timer _timer;

    int InitResample(AEAudioFormat &audioFormat) {
        uint64_t layout =
            GetLayoutByChannels(audioFormat.m_channelLayout.Count());
        AVSampleFormat srcFormat;
        switch(audioFormat.m_dataFormat) {
            case AE_FMT_U8:
                srcFormat = AV_SAMPLE_FMT_U8;
                break;
            case AE_FMT_S16LE:
                srcFormat = AV_SAMPLE_FMT_S16;
                break;
            case AE_FMT_S16NE:
                srcFormat = AV_SAMPLE_FMT_S16;
                break;
            case AE_FMT_S32LE:
                srcFormat = AV_SAMPLE_FMT_S32;
                break;
            case AE_FMT_S32NE:
                srcFormat = AV_SAMPLE_FMT_S32;
                break;
            case AE_FMT_DOUBLE:
                srcFormat = AV_SAMPLE_FMT_DBL;
                break;
            case AE_FMT_FLOAT:
                srcFormat = AV_SAMPLE_FMT_FLT;
                break;
            case AE_FMT_U8P:
                srcFormat = AV_SAMPLE_FMT_U8P;
                break;
            case AE_FMT_S16NEP:
                srcFormat = AV_SAMPLE_FMT_S16P;
                break;
            case AE_FMT_S32NEP:
                srcFormat = AV_SAMPLE_FMT_S32P;
                break;
            case AE_FMT_DOUBLEP:
                srcFormat = AV_SAMPLE_FMT_DBLP;
                break;
            case AE_FMT_FLOATP:
                srcFormat = AV_SAMPLE_FMT_FLTP;
                break;
            default:
                throw new std::invalid_argument("unknown sample format");
        }
        switch(audioFormat.m_dataFormat) {
            case AE_FMT_U8P:
            case AE_FMT_S16NEP:
            case AE_FMT_S32NEP:
            case AE_FMT_DOUBLEP:
            case AE_FMT_FLOATP:
                src_buffer_count = audioFormat.m_channelLayout.Count();
                break;
            default:
                src_buffer_count = 1;
                break;
        }
        swr_tgtFormat = AV_SAMPLE_FMT_S16;
        AVChannelLayout channelLayout{};
        const int layoutResult =
            av_channel_layout_from_mask(&channelLayout, layout);
        const int allocResult = layoutResult < 0
            ? layoutResult
            : swr_alloc_set_opts2(
            &swr_ctx, &channelLayout, swr_tgtFormat, audioFormat.m_sampleRate,
            &channelLayout, srcFormat, audioFormat.m_sampleRate, 0, nullptr);
        av_channel_layout_uninit(&channelLayout);
        if(allocResult < 0 || !swr_ctx)
            throw new std::runtime_error("could not allocate audio resampler");
        const int result = swr_init(swr_ctx);
        if(result < 0) {
            swr_free(&swr_ctx);
            throw new std::runtime_error("could not initialize audio resampler");
        }
        tgt_frameSize = av_get_bytes_per_sample(swr_tgtFormat) *
            m_format.m_channelLayout.Count();
        return 16;
    }

    void ResetTempoGraph() {
        if(tempo_graph)
            avfilter_graph_free(&tempo_graph);
        tempo_source = nullptr;
        tempo_sink = nullptr;
        tempo_input_pts = 0;
        tempo_trace_input_frames = 0;
        tempo_trace_output_frames = 0;
        tempo_trace_baseline_input_frames = 0;
        tempo_trace_baseline_output_frames = 0;
        tempo_trace_has_baseline = false;
        tempo_trace_logged = false;
    }

    bool WaitForBufferSlot() {
        _timer.Set(1000);
        while(m_impl && !m_impl->IsBufferValid()) {
            std::unique_lock<std::mutex> lk(_mutex);
            _cond.wait_for(lk, std::chrono::milliseconds(10));
            if(_timer.IsTimePast())
                return false;
        }
        return m_impl != nullptr;
    }

    bool InitTempoGraph() {
        ResetTempoGraph();
        if(std::abs(playback_rate - 1.0) < 0.0001)
            return true;

        tempo_graph = avfilter_graph_alloc();
        const AVFilter *source_filter = avfilter_get_by_name("abuffer");
        const AVFilter *tempo_filter = avfilter_get_by_name("atempo");
        const AVFilter *sink_filter = avfilter_get_by_name("abuffersink");
        if(!tempo_graph || !source_filter || !tempo_filter || !sink_filter) {
            ResetTempoGraph();
            return false;
        }

        const uint64_t layout =
            GetLayoutByChannels(m_format.m_channelLayout.Count());
        char source_args[256];
        std::snprintf(
            source_args, sizeof(source_args),
            "time_base=1/%u:sample_rate=%u:sample_fmt=s16:"
            "channel_layout=0x%llx",
            m_format.m_sampleRate, m_format.m_sampleRate,
            static_cast<unsigned long long>(layout));
        char tempo_args[64];
        std::snprintf(tempo_args, sizeof(tempo_args), "tempo=%.6f",
                      playback_rate);

        AVFilterContext *tempo = nullptr;
        int result = avfilter_graph_create_filter(
            &tempo_source, source_filter, "aether_tempo_source", source_args,
            nullptr, tempo_graph);
        if(result >= 0)
            result = avfilter_graph_create_filter(
                &tempo, tempo_filter, "aether_tempo", tempo_args, nullptr,
                tempo_graph);
        if(result >= 0)
            result = avfilter_graph_create_filter(
                &tempo_sink, sink_filter, "aether_tempo_sink", nullptr,
                nullptr, tempo_graph);
        if(result >= 0)
            result = avfilter_link(tempo_source, 0, tempo, 0);
        if(result >= 0)
            result = avfilter_link(tempo, 0, tempo_sink, 0);
        if(result >= 0)
            result = avfilter_graph_config(tempo_graph, nullptr);
        if(result < 0) {
            ResetTempoGraph();
            return false;
        }
        return true;
    }

    bool AppendPcm(const uint8_t *data, unsigned int bytes) {
        if(bytes == 0)
            return true;
        if(!WaitForBufferSlot())
            return false;
        m_impl->AppendBuffer(data, bytes);
        return true;
    }

    bool AppendTempoOutput(const uint8_t *data, unsigned int frames) {
        if(std::abs(playback_rate - 1.0) < 0.0001)
            return AppendPcm(data, frames * tgt_frameSize);
        if(!tempo_graph && !InitTempoGraph())
            return AppendPcm(data, frames * tgt_frameSize);

        AVFrame *input = av_frame_alloc();
        if(!input)
            return false;
        input->format = AV_SAMPLE_FMT_S16;
        input->sample_rate = m_format.m_sampleRate;
        input->nb_samples = static_cast<int>(frames);
        input->pts = tempo_input_pts;
        tempo_input_pts += frames;
        const uint64_t layout =
            GetLayoutByChannels(m_format.m_channelLayout.Count());
        int result =
            av_channel_layout_from_mask(&input->ch_layout, layout);
        if(result >= 0)
            result = av_frame_get_buffer(input, 0);
        if(result >= 0) {
            std::memcpy(input->data[0], data, frames * tgt_frameSize);
            result = av_buffersrc_add_frame_flags(
                tempo_source, input, AV_BUFFERSRC_FLAG_KEEP_REF);
        }
        av_frame_free(&input);
        if(result < 0) {
            ResetTempoGraph();
            return AppendPcm(data, frames * tgt_frameSize);
        }
        tempo_trace_input_frames += frames;

        while(true) {
            AVFrame *output = av_frame_alloc();
            if(!output)
                return false;
            result = av_buffersink_get_frame(tempo_sink, output);
            if(result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                av_frame_free(&output);
                break;
            }
            if(result < 0 || output->format != AV_SAMPLE_FMT_S16 ||
               av_sample_fmt_is_planar(
                   static_cast<AVSampleFormat>(output->format))) {
                av_frame_free(&output);
                return false;
            }
            const unsigned int output_bytes =
                output->nb_samples * tgt_frameSize;
            tempo_trace_output_frames += output->nb_samples;
            const bool appended = AppendPcm(output->data[0], output_bytes);
            av_frame_free(&output);
            if(!appended)
                return false;
        }
        if(VideoTraceEnabled() && !tempo_trace_has_baseline &&
           tempo_trace_input_frames >= 8192) {
            tempo_trace_baseline_input_frames = tempo_trace_input_frames;
            tempo_trace_baseline_output_frames = tempo_trace_output_frames;
            tempo_trace_has_baseline = true;
        } else if(
            VideoTraceEnabled() && tempo_trace_has_baseline &&
            !tempo_trace_logged &&
            tempo_trace_input_frames - tempo_trace_baseline_input_frames >=
                16384) {
            const uint64_t measured_input =
                tempo_trace_input_frames - tempo_trace_baseline_input_frames;
            const uint64_t measured_output =
                tempo_trace_output_frames - tempo_trace_baseline_output_frames;
            spdlog::info(
                "MoviePlayer audio tempo rate={} input_frames={} "
                "output_frames={} output_ratio={}",
                playback_rate, measured_input, measured_output,
                static_cast<double>(measured_output) / measured_input);
            tempo_trace_logged = true;
        }
        return true;
    }

public:
    CAEStreamAL(AEAudioFormat &audioFormat, unsigned int options,
                IAEClockCallback *clock) {
        m_format = audioFormat;
        m_cbClock = clock;
        tTVPWaveFormat format;
        format.SamplesPerSec = audioFormat.m_sampleRate;
        format.Channels = audioFormat.m_channelLayout.Count();
        format.BitsPerSample = 0;
        switch(audioFormat.m_dataFormat) {
            case AE_FMT_S16LE:
                format.BitsPerSample = 16;
                break;
            default:
                format.BitsPerSample = InitResample(audioFormat);
                break;
        }
        tgt_frameSize =
            (format.BitsPerSample / 8) * audioFormat.m_channelLayout.Count();
        format.BytesPerSample = format.BitsPerSample / 8;
        format.TotalSamples = 0;
        format.TotalTime = 0;
        format.SpeakerConfig = 0;
        format.IsFloat = false;
        format.Seekable = false;
        m_impl = TVPCreateSoundBuffer(format, 8);
    }

    ~CAEStreamAL() override {
        {
            //			std::unique_lock<std::mutex> lk(_mutex);
            ResetTempoGraph();
            if(swr_ctx) {
                swr_free(&swr_ctx);
            }
            if(audio_buf) {
                av_freep(&audio_buf);
            }
            if(m_impl) {
                delete m_impl;
                m_impl = nullptr;
            }
        }
        _cond.notify_all();
    }

    unsigned int AddData(const uint8_t *const *data, unsigned int offset,
                         unsigned int frames, double pts) override {
        if(!WaitForBufferSlot())
            return 0;
        const bool startPlaybackClock = !m_hasPts;
        const tjs_uint startSample = startPlaybackClock
            ? m_impl->GetCurrentPlaySamples()
            : 0;

        if(swr_ctx) {
            uint32_t srcoff =
                offset * (m_format.m_frameSize / src_buffer_count);
            const uint8_t *in[8];
            for(unsigned int i = 0; i < src_buffer_count; ++i) {
                in[i] = data[i] + srcoff;
            }
            int out_count = frames + 256;
            int out_size = av_samples_get_buffer_size(
                nullptr, m_format.m_channelLayout.Count(), out_count,
                swr_tgtFormat, 0);
            av_fast_malloc(&audio_buf, &audio_buf_size, out_size);
            int len2 = swr_convert(swr_ctx, &audio_buf, out_count, in, frames);
            if(len2 == out_count) {
                av_log(nullptr, AV_LOG_WARNING,
                       "audio buffer is probably too small\n");
                swr_init(swr_ctx);
            }
            if(!AppendTempoOutput(audio_buf, len2))
                return 0;
        } else {
            const auto *input =
                data[0] + offset * m_format.m_frameSize;
            if(!AppendTempoOutput(input, frames))
                return 0;
        }

        if(!m_impl->IsPlaying()) { // out of buffer
            m_impl->Play();
        }

        if(startPlaybackClock) {
            m_playbackStartPts = pts;
            m_playbackStartSample = startSample;
            m_hasPts = true;
            m_syncTimer.Set(250);
        }
        return frames;
    }

    double GetDelay() override { return (double)m_impl->GetLatencySeconds(); }

    CAESyncInfo GetSyncInfo() override {
        CAESyncInfo info{};
        info.state = CAESyncInfo::SYNC_OFF;
        if(!m_hasPts || !m_impl || !m_impl->IsPlaying() || !m_cbClock)
            return info;

        const double delay = std::max(0.0, GetDelay());
        if(m_syncTimer.IsTimePast()) {
            const tjs_uint sampleRate = m_impl->GetPlaybackSampleRate();
            const tjs_uint currentSample = m_impl->GetCurrentPlaySamples();
            const tjs_uint playedSamples = currentSample >= m_playbackStartSample
                ? currentSample - m_playbackStartSample
                : 0;
            const double playingPts = sampleRate > 0
                ? m_playbackStartPts +
                    static_cast<double>(playedSamples) * playback_rate *
                        1000.0 / sampleRate
                : m_playbackStartPts;
            const double error = playingPts - m_cbClock->GetClock();
            if(std::isfinite(error)) {
                m_syncError = std::clamp(error, -20.0, 20.0);
                if(++m_syncErrorTime == 0)
                    ++m_syncErrorTime;
                m_syncTimer.Set(250);
            }
        }
        if(m_syncErrorTime == 0)
            return info;

        info.delay = delay * 1000.0;
        info.error = m_syncError;
        info.rr = 1.0;
        info.errortime = m_syncErrorTime;
        info.state = CAESyncInfo::SYNC_INSYNC;
        return info;
    }

    double GetCacheTime() override { return GetDelay(); }

    double GetCacheTotal() override {
        // return std::max(GetDelay(), (double)TVPAL_BUFFER_COUNT);
        return GetDelay();
    }

    void Pause() override { m_impl->Pause(); }

    void Resume() override { m_impl->Play(); }

    bool IsSuspended() override { return !m_impl->IsPlaying(); }

    void Drain(bool wait) override {} // TODO
    void Flush() override {
        ResetTempoGraph();
        m_impl->Reset();
        m_hasPts = false;
        m_syncError = 0;
        m_syncErrorTime = 0;
    }

    void SetPlaybackRate(double rate) override {
        if(!std::isfinite(rate) || rate < 0.5 || rate > 2.0)
            return;
        if(std::abs(rate - playback_rate) < 0.0001)
            return;
        playback_rate = rate;
        ResetTempoGraph();
        if(m_impl) {
            m_impl->Reset();
            m_hasPts = false;
            m_syncError = 0;
            m_syncErrorTime = 0;
        }
    }

    iTVPSoundBuffer *GetNativeImpl() override { return m_impl; }
};

bool CAEFactory::SupportsRaw(AEAudioFormat &format) {
    // check if passthrough is enabled
//   if
//   (!CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_PASSTHROUGH))
//     return false;
#if 0
        // fixed config disabled passthrough
        if (CSettings::GetInstance().GetInt(CSettings::SETTING_AUDIOOUTPUT_CONFIG) == AE_CONFIG_FIXED)
          return false;

        // check if the format is enabled in settings
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_AC3 && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_AC3PASSTHROUGH))
          return false;
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_DTS_512 && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_DTSPASSTHROUGH))
          return false;
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_DTS_1024 && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_DTSPASSTHROUGH))
          return false;
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_DTS_2048 && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_DTSPASSTHROUGH))
          return false;
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_DTSHD_CORE && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_DTSPASSTHROUGH))
          return false;
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_EAC3 && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_EAC3PASSTHROUGH))
          return false;
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_TRUEHD && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_TRUEHDPASSTHROUGH))
          return false;
        if (format.m_streamInfo.m_type == CAEStreamInfo::STREAM_TYPE_DTSHD && !CSettings::GetInstance().GetBool(CSettings::SETTING_AUDIOOUTPUT_DTSHDPASSTHROUGH))
          return false;
        if(AE)
          return AE->SupportsRaw(format);
#endif
    // refer to the format in TVPALSoundWrap::Init
    switch(format.m_dataFormat) {
        case AE_FMT_U8:
        case AE_FMT_S16LE:
            break;
        default:
            return false;
    }

    // if (format.m_channelLayout.Count() > 2) return false;
    // if (format.m_sampleRate > 48000) return false;

    return true;
}

IAEStream *CAEFactory::MakeStream(AEAudioFormat &audioFormat,
                                  unsigned int options,
                                  IAEClockCallback *clock) {
    //   if(AE)
    //     return AE->MakeStream(audioFormat, options, clock);
    return new CAEStreamAL(audioFormat, options, clock);
    return nullptr;
}

bool CAEFactory::FreeStream(IAEStream *stream) {
    //   if(AE)
    //     return AE->FreeStream(stream);
    delete static_cast<CAEStreamAL *>(stream);
    return true;
}

NS_KRMOVIE_END
