#include "AudioCodecFFmpeg.h"

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#ifdef TARGET_POSIX
#include "XMemUtils.h"
#endif

#include "StreamInfo.h"
#include "Codecs.h"

extern "C" {
#include "libavutil/opt.h"
}

#if defined(TARGET_DARWIN)
#include "cores/AudioEngine/Utils/AEUtil.h"
#endif

#include "Clock.h"
#include "AEUtil.h"
#include "AEAudioFormat.h"

NS_KRMOVIE_BEGIN
CDVDAudioCodecFFmpeg::CDVDAudioCodecFFmpeg(CProcessInfo &processInfo) :
    CDVDAudioCodec(processInfo) {
    m_pCodecContext = nullptr;

    m_channels = 0;
    m_layout = 0;

    m_pFrame1 = nullptr;
    m_iSampleFormat = AV_SAMPLE_FMT_NONE;
    m_gotFrame = 0;
}

CDVDAudioCodecFFmpeg::~CDVDAudioCodecFFmpeg() { Dispose(); }

bool CDVDAudioCodecFFmpeg::Open(CDVDStreamInfo &hints,
                                CDVDCodecOptions &options) {
    const AVCodec *pCodec = nullptr;
    bool allowdtshddecode = true;

    // set any special options
    for(auto &m_key : options.m_keys)
        if(m_key.m_name == "allowdtshddecode")
            allowdtshddecode = atoi(m_key.m_value.c_str());

    if(hints.codec == AV_CODEC_ID_DTS && allowdtshddecode)
        pCodec = avcodec_find_decoder_by_name("dcadec");

    if(!pCodec)
        pCodec = avcodec_find_decoder(hints.codec);

    if(!pCodec) {
        //    CLog::Log(LOGDEBUG,"CDVDAudioCodecFFmpeg::Open() Unable
        //    to find codec %d", hints.codec);
        return false;
    }

    m_pCodecContext = avcodec_alloc_context3(pCodec);
    if(!m_pCodecContext)
        return false;

    m_pCodecContext->debug = 0;
    m_pCodecContext->workaround_bugs = 1;
    m_pCodecContext->pkt_timebase = AV_TIME_BASE_Q;

    m_matrixEncoding = AV_MATRIX_ENCODING_NONE;
    m_channels = 0;
    if(hints.channellayout != 0)
        av_channel_layout_from_mask(&m_pCodecContext->ch_layout,
                                    hints.channellayout);
    else if(hints.channels > 0)
        av_channel_layout_default(&m_pCodecContext->ch_layout, hints.channels);
    m_pCodecContext->sample_rate = hints.samplerate;
    m_pCodecContext->block_align = hints.blockalign;
    m_pCodecContext->bit_rate = hints.bitrate;
    m_pCodecContext->bits_per_coded_sample = hints.bitspersample;

    if(m_pCodecContext->bits_per_coded_sample == 0)
        m_pCodecContext->bits_per_coded_sample = 16;

    if(hints.extradata && hints.extrasize > 0) {
        m_pCodecContext->extradata = (uint8_t *)av_mallocz(
            hints.extrasize + AV_INPUT_BUFFER_PADDING_SIZE);
        if(m_pCodecContext->extradata) {
            m_pCodecContext->extradata_size = hints.extrasize;
            memcpy(m_pCodecContext->extradata, hints.extradata,
                   hints.extrasize);
        }
    }

    //   if (g_advancedSettings.m_audioApplyDrc >= 0.0)
    //     av_opt_set_double(m_pCodecContext, "drc_scale",
    //     g_advancedSettings.m_audioApplyDrc,
    //     AV_OPT_SEARCH_CHILDREN);

    if(avcodec_open2(m_pCodecContext, pCodec, nullptr) < 0) {
        //    CLog::Log(LOGDEBUG,"CDVDAudioCodecFFmpeg::Open() Unable
        //    to open codec");
        Dispose();
        return false;
    }

    m_pFrame1 = av_frame_alloc();
    if(!m_pFrame1) {
        Dispose();
        return false;
    }

    m_iSampleFormat = AV_SAMPLE_FMT_NONE;
    m_matrixEncoding = AV_MATRIX_ENCODING_NONE;

    m_processInfo.SetAudioDecoderName(m_pCodecContext->codec->name);
    return true;
}

void CDVDAudioCodecFFmpeg::Dispose() {
    av_frame_free(&m_pFrame1);
    avcodec_free_context(&m_pCodecContext);
}

int CDVDAudioCodecFFmpeg::Decode(uint8_t *pData, int iSize, double dts,
                                 double pts) {
    if(!m_pCodecContext)
        return -1;

    auto receiveFrame = [this]() {
        m_gotFrame = 0;
        av_frame_unref(m_pFrame1);
        const int result =
            avcodec_receive_frame(m_pCodecContext, m_pFrame1);
        if(result == 0)
            m_gotFrame = 1;
        return result;
    };

    // Drain every frame produced by the previous packet before accepting a new
    // packet. Returning zero tells the legacy caller that the current input has
    // not been consumed yet.
    int receiveResult = receiveFrame();
    if(receiveResult == 0)
        return 0;
    if(receiveResult != AVERROR(EAGAIN) && receiveResult != AVERROR_EOF)
        return receiveResult;

    AVPacket packet{};
    packet.data = pData;
    packet.size = iSize;
    packet.dts = (dts == DVD_NOPTS_VALUE)
        ? AV_NOPTS_VALUE
        : static_cast<int64_t>(dts / DVD_TIME_BASE * AV_TIME_BASE);
    packet.pts = (pts == DVD_NOPTS_VALUE)
        ? AV_NOPTS_VALUE
        : static_cast<int64_t>(pts / DVD_TIME_BASE * AV_TIME_BASE);

    const int sendResult =
        avcodec_send_packet(m_pCodecContext, pData ? &packet : nullptr);
    if(sendResult < 0 && sendResult != AVERROR_EOF)
        return sendResult;

    receiveResult = receiveFrame();
    if(receiveResult != 0 && receiveResult != AVERROR(EAGAIN) &&
       receiveResult != AVERROR_EOF) {
        return receiveResult;
    }

    if(m_gotFrame && m_pFrame1->nb_side_data) {
        for(int i = 0; i < m_pFrame1->nb_side_data; i++) {
            AVFrameSideData *sd = m_pFrame1->side_data[i];
            if(sd->data) {
                if(sd->type == AV_FRAME_DATA_MATRIXENCODING) {
                    m_matrixEncoding = *(enum AVMatrixEncoding *)sd->data;
                }
            }
        }
    }

    if(m_gotFrame) {
        m_format.m_dataFormat = GetDataFormat();
        m_format.m_channelLayout = GetChannelMap();
        m_format.m_sampleRate = GetSampleRate();
        m_format.m_frameSize = m_format.m_channelLayout.Count() *
                CAEUtil::DataFormatToBits(m_format.m_dataFormat) >>
            3;
    }
    return pData ? iSize : 0;
}

void CDVDAudioCodecFFmpeg::GetData(DVDAudioFrame &frame) {
    frame.passthrough = false;
    frame.nb_frames = 0;
    frame.format.m_dataFormat = m_format.m_dataFormat;
    frame.format.m_channelLayout = m_format.m_channelLayout;
    frame.framesize =
        (CAEUtil::DataFormatToBits(frame.format.m_dataFormat) >> 3) *
        frame.format.m_channelLayout.Count();
    if(frame.framesize == 0)
        return;
    frame.nb_frames = GetData(frame.data) / frame.framesize;
    frame.planes = AE_IS_PLANAR(frame.format.m_dataFormat)
        ? frame.format.m_channelLayout.Count()
        : 1;
    frame.bits_per_sample =
        CAEUtil::DataFormatToBits(frame.format.m_dataFormat);
    frame.format.m_sampleRate = m_format.m_sampleRate;
    frame.matrix_encoding = GetMatrixEncoding();
    frame.audio_service_type = GetAudioServiceType();
    frame.profile = GetProfile();
    // compute duration.
    if(frame.format.m_sampleRate)
        frame.duration = ((double)frame.nb_frames * DVD_TIME_BASE) /
            frame.format.m_sampleRate;
    else
        frame.duration = 0.0;

    int64_t bpts = m_pFrame1->best_effort_timestamp;
    if(bpts != AV_NOPTS_VALUE)
        frame.pts = (double)bpts * DVD_TIME_BASE / AV_TIME_BASE;
    else
        frame.pts = DVD_NOPTS_VALUE;
}

int CDVDAudioCodecFFmpeg::GetData(uint8_t **dst) {
    if(m_gotFrame) {
        const int channels = m_pFrame1->ch_layout.nb_channels;
        const auto sampleFormat =
            static_cast<AVSampleFormat>(m_pFrame1->format);
        int planes = av_sample_fmt_is_planar(sampleFormat)
            ? channels
            : 1;
        for(int i = 0; i < planes; i++)
            dst[i] = m_pFrame1->extended_data[i];
        m_gotFrame = 0;
        return m_pFrame1->nb_samples * channels *
            av_get_bytes_per_sample(sampleFormat);
    }

    return 0;
}

void CDVDAudioCodecFFmpeg::Reset() {
    if(m_pCodecContext)
        avcodec_flush_buffers(m_pCodecContext);
    m_gotFrame = 0;
}

int CDVDAudioCodecFFmpeg::GetChannels() {
    return m_pCodecContext->ch_layout.nb_channels;
}

int CDVDAudioCodecFFmpeg::GetSampleRate() {
    if(m_pCodecContext)
        return m_pCodecContext->sample_rate;
    return 0;
}

enum AEDataFormat CDVDAudioCodecFFmpeg::GetDataFormat() {
    const auto sampleFormat = m_gotFrame
        ? static_cast<AVSampleFormat>(m_pFrame1->format)
        : m_pCodecContext->sample_fmt;
    switch(sampleFormat) {
        case AV_SAMPLE_FMT_U8:
            return AE_FMT_U8;
        case AV_SAMPLE_FMT_U8P:
            return AE_FMT_U8P;
        case AV_SAMPLE_FMT_S16:
            return AE_FMT_S16NE;
        case AV_SAMPLE_FMT_S16P:
            return AE_FMT_S16NEP;
        case AV_SAMPLE_FMT_S32:
            return AE_FMT_S32NE;
        case AV_SAMPLE_FMT_S32P:
            return AE_FMT_S32NEP;
        case AV_SAMPLE_FMT_FLT:
            return AE_FMT_FLOAT;
        case AV_SAMPLE_FMT_FLTP:
            return AE_FMT_FLOATP;
        case AV_SAMPLE_FMT_DBL:
            return AE_FMT_DOUBLE;
        case AV_SAMPLE_FMT_DBLP:
            return AE_FMT_DOUBLEP;
        case AV_SAMPLE_FMT_NONE:
        default:
            //      CLog::Log(LOGERROR,
            //      "CDVDAudioCodecFFmpeg::GetDataFormat - invalid
            //      data format");
            return AE_FMT_INVALID;
    }
}

int CDVDAudioCodecFFmpeg::GetBitRate() {
    if(m_pCodecContext)
        return m_pCodecContext->bit_rate;
    return 0;
}

enum AVMatrixEncoding CDVDAudioCodecFFmpeg::GetMatrixEncoding() {
    return m_matrixEncoding;
}

enum AVAudioServiceType CDVDAudioCodecFFmpeg::GetAudioServiceType() {
    if(m_pCodecContext)
        return m_pCodecContext->audio_service_type;
    return AV_AUDIO_SERVICE_TYPE_MAIN;
}

int CDVDAudioCodecFFmpeg::GetProfile() {
    if(m_pCodecContext)
        return m_pCodecContext->profile;
    return 0;
}

static unsigned count_bits(int64_t value) {
    unsigned bits = 0;
    for(; value; ++bits)
        value &= value - 1;
    return bits;
}

void CDVDAudioCodecFFmpeg::BuildChannelMap() {
    const AVChannelLayout &channelLayout =
        m_gotFrame && m_pFrame1->ch_layout.nb_channels > 0
        ? m_pFrame1->ch_layout
        : m_pCodecContext->ch_layout;
    const int channels = channelLayout.nb_channels;
    const uint64_t channelMask =
        channelLayout.order == AV_CHANNEL_ORDER_NATIVE
        ? channelLayout.u.mask
        : 0;

    if(m_channels == channels && m_layout == channelMask)
        return; // nothing to do here

    m_channels = channels;
    m_layout = channelMask;

    uint64_t layout = channelMask;

    int bits = count_bits(channelMask);
    if(bits != channels) {
        //    CLog::Log(LOGINFO, "CDVDAudioCodecFFmpeg::GetChannelMap
        //    - FFmpeg reported %d channels, but the layout contains
        //    %d ignoring", m_pCodecContext->channels, bits);
        AVChannelLayout defaultLayout{};
        av_channel_layout_default(&defaultLayout, channels);
        if(defaultLayout.order == AV_CHANNEL_ORDER_NATIVE)
            layout = defaultLayout.u.mask;
        av_channel_layout_uninit(&defaultLayout);
    }

    m_channelLayout.Reset();

    if(layout & AV_CH_FRONT_LEFT)
        m_channelLayout += AE_CH_FL;
    if(layout & AV_CH_FRONT_RIGHT)
        m_channelLayout += AE_CH_FR;
    if(layout & AV_CH_FRONT_CENTER)
        m_channelLayout += AE_CH_FC;
    if(layout & AV_CH_LOW_FREQUENCY)
        m_channelLayout += AE_CH_LFE;
    if(layout & AV_CH_BACK_LEFT)
        m_channelLayout += AE_CH_BL;
    if(layout & AV_CH_BACK_RIGHT)
        m_channelLayout += AE_CH_BR;
    if(layout & AV_CH_FRONT_LEFT_OF_CENTER)
        m_channelLayout += AE_CH_FLOC;
    if(layout & AV_CH_FRONT_RIGHT_OF_CENTER)
        m_channelLayout += AE_CH_FROC;
    if(layout & AV_CH_BACK_CENTER)
        m_channelLayout += AE_CH_BC;
    if(layout & AV_CH_SIDE_LEFT)
        m_channelLayout += AE_CH_SL;
    if(layout & AV_CH_SIDE_RIGHT)
        m_channelLayout += AE_CH_SR;
    if(layout & AV_CH_TOP_CENTER)
        m_channelLayout += AE_CH_TC;
    if(layout & AV_CH_TOP_FRONT_LEFT)
        m_channelLayout += AE_CH_TFL;
    if(layout & AV_CH_TOP_FRONT_CENTER)
        m_channelLayout += AE_CH_TFC;
    if(layout & AV_CH_TOP_FRONT_RIGHT)
        m_channelLayout += AE_CH_TFR;
    if(layout & AV_CH_TOP_BACK_LEFT)
        m_channelLayout += AE_CH_BL;
    if(layout & AV_CH_TOP_BACK_CENTER)
        m_channelLayout += AE_CH_BC;
    if(layout & AV_CH_TOP_BACK_RIGHT)
        m_channelLayout += AE_CH_BR;

    m_channels = channels;
}

CAEChannelInfo CDVDAudioCodecFFmpeg::GetChannelMap() {
    BuildChannelMap();
    return m_channelLayout;
}
NS_KRMOVIE_END
