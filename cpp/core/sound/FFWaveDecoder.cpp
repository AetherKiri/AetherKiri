#include "FFWaveDecoder.h"
#include "WaveIntf.h"
#include "StorageIntf.h"
#include "DebugIntf.h"
#include "SysInitIntf.h"
#include "BinaryStream.h"
#include "krffmpeg.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

extern "C" {
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#ifndef UINT64_C
#define UINT64_C(x) (x##ULL)
#endif
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
};

class FFWaveDecoder : public tTVPWaveDecoder // decoder interface
{
    bool IsPlanar;

    int StreamIdx;
    int audio_buf_index;
    int audio_buf_samples;
    int64_t audio_frame_next_pts;
    int64_t stream_start_time;
    tTVPWaveFormat Format; // output PCM format
    AVSampleFormat AVFmt;
    AVStream *AudioStream;
    AVCodecContext *CodecCtx;
    bool DecoderDraining;
    bool PacketPending;

    // release in Clear()
    AVPacket Packet;
    tTJSBinaryStream *InputStream; // input stream
    AVFormatContext *FormatCtx;
    AVFrame *frame;

    int audio_decode_frame();

    void Clear() {
        av_packet_unref(&Packet);
        PacketPending = false;
        if(frame)
            av_frame_free(&frame), frame = nullptr;
        avcodec_free_context(&CodecCtx);
        if(FormatCtx) {
            AVIOContext *ioContext = FormatCtx->pb;
            FormatCtx->pb = nullptr;
            avformat_close_input(&FormatCtx);
            if(ioContext) {
                av_freep(&ioContext->buffer);
                avio_context_free(&ioContext);
            }
        }
        if(InputStream)
            delete InputStream, InputStream = nullptr;
    }

    bool ReadPacket();

public:
    FFWaveDecoder() :
        AudioStream(nullptr),
        CodecCtx(nullptr),
        DecoderDraining(false),
        PacketPending(false),
        InputStream(nullptr),
        FormatCtx(nullptr),
        frame(nullptr) {
        memset(&Packet, 0, sizeof(Packet));
    }

    ~FFWaveDecoder() override { Clear(); }

public:
    // ITSSWaveDecoder
    void GetFormat(tTVPWaveFormat &format) override { format = Format; }

    bool Render(void *buf, tjs_uint bufsamplelen, tjs_uint &rendered) override;

    bool SetPosition(tjs_uint64 samplepos) override;

    // others
    bool SetStream(const ttstr &storagename);
};

static int AVReadFunc(void *opaque, uint8_t *buf, int buf_size) {
    TJS::tTJSBinaryStream *stream = (TJS::tTJSBinaryStream *)opaque;
    return stream->Read(buf, buf_size);
}

static int64_t AVSeekFunc(void *opaque, int64_t offset, int whence) {
    TJS::tTJSBinaryStream *stream = (TJS::tTJSBinaryStream *)opaque;
    switch(whence) {
        case AVSEEK_SIZE:
            return stream->GetSize();
        default:
            return stream->Seek(offset, whence & 0xFF);
    }
}

tTVPWaveDecoder *FFWaveDecoderCreator::Create(const ttstr &storagename,
                                              const ttstr &extension) {
    TVPInitLibAVCodec();

    FFWaveDecoder *decoder = new FFWaveDecoder();
    if(!decoder->SetStream(storagename)) {
        delete decoder;
        decoder = nullptr;
    }
    return decoder;
}

template <typename T>
static unsigned char *_CopySmaples(unsigned char *dst, AVFrame *frame,
                                   int samples, int buf_index) {
    int buf_pos = buf_index * sizeof(T);
    T *pDst = (T *)dst;
    for(int i = 0; i < samples; ++i, buf_pos += sizeof(T)) {
        for(int j = 0; j < frame->ch_layout.nb_channels; ++j) {
            *pDst++ = *(T *)(frame->data[j] + buf_pos);
        }
    }
    return (unsigned char *)pDst;
}

// optimized for stereo
template <typename T>
static unsigned char *_CopySmaples2(unsigned char *dst, AVFrame *frame,
                                    int samples, int buf_index) {
    int buf_pos = buf_index * sizeof(T);
    T *pDst = (T *)dst;
    for(int i = 0; i < samples; ++i, buf_pos += sizeof(T)) {
        *pDst++ = *(T *)(frame->data[0] + buf_pos);
        *pDst++ = *(T *)(frame->data[1] + buf_pos);
    }
    return (unsigned char *)pDst;
}

static unsigned char *CopySmaples(unsigned char *dst, AVFrame *frame,
                                  int samples, int buf_index) {
    switch(frame->format) {
        case AV_SAMPLE_FMT_FLTP:
        case AV_SAMPLE_FMT_S32P:
            if(frame->ch_layout.nb_channels == 2)
                return _CopySmaples2<tjs_uint32>(dst, frame, samples,
                                                 buf_index);
            else
                return _CopySmaples<tjs_uint32>(dst, frame, samples, buf_index);
            break;
        case AV_SAMPLE_FMT_S16P:
            if(frame->ch_layout.nb_channels == 2)
                return _CopySmaples2<tjs_uint16>(dst, frame, samples,
                                                 buf_index);
            else
                return _CopySmaples<tjs_uint16>(dst, frame, samples, buf_index);
            break;
    }
    return nullptr;
}

bool FFWaveDecoder::Render(void *buf, tjs_uint bufsamplelen,
                           tjs_uint &rendered) {
    // render output PCM
    if(!InputStream)
        return false; // InputFile is yet not inited
    int remain = bufsamplelen; // remaining PCM (in sample)
    int sample_size =
        av_samples_get_buffer_size(nullptr, Format.Channels, 1, AVFmt, 1);
    unsigned char *stream = (unsigned char *)buf;
    while(remain) {
        if(audio_buf_index >= audio_buf_samples) {
            int decoded_samples = audio_decode_frame();
            if(decoded_samples < 0) {
                break;
            }
            audio_buf_samples = decoded_samples;
            audio_buf_index = 0;
        }
        int samples = audio_buf_samples - audio_buf_index;
        if(samples > remain)
            samples = remain;

        if(!IsPlanar || Format.Channels == 1) {
            memcpy(stream, (frame->data[0] + audio_buf_index * sample_size),
                   samples * sample_size);
            stream += samples * sample_size;
        } else {
            stream = CopySmaples(stream, frame, samples, audio_buf_index);
        }
        remain -= samples;
        audio_buf_index += samples;
    }

    rendered = bufsamplelen - remain; // return rendered PCM samples

    return !remain; // if the decoding is ended
}

bool FFWaveDecoder::SetPosition(tjs_uint64 samplepos) {
    // set PCM position (seek)
    if(!InputStream)
        return false;
    if(samplepos && !Format.Seekable)
        return false;

    int64_t seek_target =
        samplepos / av_q2d(AudioStream->time_base) / Format.SamplesPerSec;
    if(AudioStream->start_time != AV_NOPTS_VALUE) {
        seek_target += AudioStream->start_time;
    }
    if(Packet.duration <= 0) {
        if(PacketPending) {
            av_packet_unref(&Packet);
            PacketPending = false;
        }
        if(!ReadPacket()) {
            int ret = avformat_seek_file(FormatCtx, StreamIdx, 0, 0, 0,
                                         AVSEEK_FLAG_BACKWARD);
            if(ret < 0)
                return false;
            if(!ReadPacket())
                return false;
        }
    }
    int64_t seek_temp = seek_target - Packet.duration;
    for(;;) {
        if(seek_temp < 0)
            seek_temp = 0;
        int ret = avformat_seek_file(FormatCtx, StreamIdx, seek_temp, seek_temp,
                                     seek_temp, AVSEEK_FLAG_BACKWARD);
        if(ret < 0)
            return false;
        if(PacketPending) {
            av_packet_unref(&Packet);
            PacketPending = false;
        }
        avcodec_flush_buffers(CodecCtx);
        DecoderDraining = false;
        if(!ReadPacket())
            return false;
        if(seek_target < Packet.dts) {
            seek_temp -= Packet.duration;
            continue;
        }
        do {
            audio_buf_samples = audio_decode_frame();
            if(audio_buf_samples < 0) {
                return false;
            }
        } while(samplepos > audio_frame_next_pts);
        audio_buf_index =
            (samplepos - frame->pts) /*/ av_q2d(AudioStream->time_base) /
                                        Format.SamplesPerSec*/
            ;
        if(audio_buf_index < 0)
            audio_buf_index = 0;
        return true;
    }
    return false;
}

bool FFWaveDecoder::SetStream(const ttstr &url) {
    Clear();
    InputStream = TVPCreateBinaryStreamForRead(url, TJS_W(""));
    if(!InputStream)
        return false;
    int bufSize = 32 * 1024;
    unsigned char *ioBuffer =
        (unsigned char *)av_malloc(bufSize + AVPROBE_PADDING_SIZE);
    if(!ioBuffer)
        return false;
    AVIOContext *pIOCtx = avio_alloc_context(
        ioBuffer,
        bufSize, // internal Buffer and its size
        0, // bWriteable (1=true,0=false)
        InputStream, // user data ; will be passed to our callback
                     // functions
        AVReadFunc,
        0, // Write callback function (not used in this example)
        AVSeekFunc);
    if(!pIOCtx) {
        av_free(ioBuffer);
        return false;
    }

    const AVInputFormat *fmt = nullptr;
    tTJSNarrowStringHolder holder(url.c_str());
    if(av_probe_input_buffer2(pIOCtx, &fmt, holder, nullptr, 0, 0) < 0) {
        av_freep(&pIOCtx->buffer);
        avio_context_free(&pIOCtx);
        return false;
    }
    FormatCtx = avformat_alloc_context();
    if(!FormatCtx) {
        av_freep(&pIOCtx->buffer);
        avio_context_free(&pIOCtx);
        return false;
    }
    FormatCtx->pb = pIOCtx;
    if(avformat_open_input(&FormatCtx, "", fmt, nullptr) < 0) {
        av_freep(&pIOCtx->buffer);
        avio_context_free(&pIOCtx);
        return false;
    }
    if(avformat_find_stream_info(FormatCtx, nullptr) < 0) {
        return false;
    }

    if(FormatCtx->pb)
        FormatCtx->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use
                                 // url_feof() to test for the end

    StreamIdx = av_find_best_stream(FormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1,
                                    nullptr, 0);

    if(StreamIdx < 0) {
        return false;
    }

    AVCodecParameters *codecParameters =
        FormatCtx->streams[StreamIdx]->codecpar;
    if(codecParameters->codec_type != AVMEDIA_TYPE_AUDIO) {
        return false;
    }

    const AVCodec *codec = avcodec_find_decoder(codecParameters->codec_id);
    if(!codec) {
        return false;
    }

    CodecCtx = avcodec_alloc_context3(codec);
    if(!CodecCtx ||
       avcodec_parameters_to_context(CodecCtx, codecParameters) < 0)
        return false;
    CodecCtx->pkt_timebase = FormatCtx->streams[StreamIdx]->time_base;
    CodecCtx->workaround_bugs = /*workaround_bugs*/ 1;
    CodecCtx->error_concealment = 3;

    if(avcodec_open2(CodecCtx, codec, nullptr) < 0) {
        return false;
    }

    Format.SamplesPerSec = CodecCtx->sample_rate;
    Format.Channels = CodecCtx->ch_layout.nb_channels;
    Format.Seekable =
        (FormatCtx->iformat->flags &
         (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) !=
        (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK);
    Format.IsFloat = false;
    // 	Format.BigEndian = false;
    // 	Format.Signed = true;
    switch(AVFmt = CodecCtx->sample_fmt) {
        case AV_SAMPLE_FMT_S16P:
        case AV_SAMPLE_FMT_S16:
            Format.BitsPerSample = 16;
            break;
        case AV_SAMPLE_FMT_FLTP:
        case AV_SAMPLE_FMT_FLT:
            Format.BitsPerSample = 32;
            Format.IsFloat = true;
            break;
        case AV_SAMPLE_FMT_S32P:
        case AV_SAMPLE_FMT_S32:
            Format.BitsPerSample = 32;
            break;
        default:
            return false;
    }
    IsPlanar = false;
    if(AVFmt == AV_SAMPLE_FMT_S16P || AVFmt == AV_SAMPLE_FMT_FLTP ||
       AVFmt == AV_SAMPLE_FMT_S32P)
        IsPlanar = true;
    Format.BytesPerSample = Format.BitsPerSample / 8;
    Format.IsFloat =
        AVFmt == AV_SAMPLE_FMT_FLTP || AVFmt == AV_SAMPLE_FMT_FLT;
    Format.SpeakerConfig = 0;
    AudioStream = FormatCtx->streams[StreamIdx];
    Format.TotalTime =
        av_q2d(AudioStream->time_base) * AudioStream->duration * 1000;
    Format.TotalSamples = av_q2d(AudioStream->time_base) *
        AudioStream->duration * Format.SamplesPerSec;

    audio_buf_index = 0;
    audio_buf_samples = 0;
    audio_frame_next_pts = 0;
    DecoderDraining = false;
    PacketPending = false;

    return true;
}

int FFWaveDecoder::audio_decode_frame() {
    AVStream *audio_st = AudioStream;
    for(;;) {
        if(!frame)
            frame = av_frame_alloc();
        else
            av_frame_unref(frame);
        if(!frame)
            return -1;

        const int receiveResult = avcodec_receive_frame(CodecCtx, frame);
        if(receiveResult == 0) {
            AVRational tb = { 1, frame->sample_rate };

            if(frame->best_effort_timestamp != AV_NOPTS_VALUE)
                frame->pts = av_rescale_q(frame->best_effort_timestamp,
                                          audio_st->time_base, tb);
            else if(audio_frame_next_pts != AV_NOPTS_VALUE) {
                AVRational a = { 1, (int)Format.SamplesPerSec };
                frame->pts = av_rescale_q(audio_frame_next_pts, a, tb);
            }

            if(frame->pts != AV_NOPTS_VALUE)
                audio_frame_next_pts = frame->pts + frame->nb_samples;

            //             int data_size =
            //             av_samples_get_buffer_size(nullptr,
            //             av_frame_get_channels(frame),
            //                 frame->nb_samples,
            //                 (AVSampleFormat)frame->format, 1);

            return frame->nb_samples;
        }
        if(receiveResult != AVERROR(EAGAIN) &&
           receiveResult != AVERROR_EOF)
            return -1;
        if(receiveResult == AVERROR_EOF)
            return -1;

        if(!PacketPending && !ReadPacket()) {
            if(DecoderDraining)
                return -1;
            DecoderDraining = true;
            if(avcodec_send_packet(CodecCtx, nullptr) < 0)
                return -1;
            continue;
        }

        const int sendResult = avcodec_send_packet(CodecCtx, &Packet);
        if(sendResult == AVERROR(EAGAIN))
            continue;
        av_packet_unref(&Packet);
        PacketPending = false;
        if(sendResult < 0)
            continue;
    }
}

bool FFWaveDecoder::ReadPacket() {
    for(;;) {
        int ret = av_read_frame(FormatCtx, &Packet);
        if(ret < 0) {
            return false;
        }
        if(Packet.stream_index == StreamIdx) {
            stream_start_time = AudioStream->start_time;
            PacketPending = true;
            return true;
        }
        av_packet_unref(&Packet);
    }
    return false;
}
