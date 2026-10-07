#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"
#include "dav1d/dav1d.h"
}

#include "forward_exports.h"

static HMODULE g_self_module = nullptr;
static INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_state_lock;
static bool g_lock_ready = false;

static HMODULE g_orig = nullptr;
static HMODULE g_dav1d = nullptr;
static bool g_ready = false;

static AVCodec g_av1_codec;
static const enum AVPixelFormat g_av1_pix_fmts[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV420P10LE,
    AV_PIX_FMT_YUV420P12LE,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV422P10LE,
    AV_PIX_FMT_YUV422P12LE,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P10LE,
    AV_PIX_FMT_YUV444P12LE,
    AV_PIX_FMT_NONE,
};

struct OrigApi {
    AVCodec *(*avcodec_find_decoder)(enum AVCodecID) = nullptr;
    AVCodec *(*avcodec_find_decoder_by_name)(const char *) = nullptr;
    AVCodecContext *(*avcodec_alloc_context3)(const AVCodec *) = nullptr;
    int (*avcodec_open2)(AVCodecContext *, const AVCodec *, AVDictionary **) = nullptr;
    int (*avcodec_send_packet)(AVCodecContext *, const AVPacket *) = nullptr;
    int (*avcodec_receive_frame)(AVCodecContext *, AVFrame *) = nullptr;
    void (*avcodec_flush_buffers)(AVCodecContext *) = nullptr;
    void (*avcodec_free_context)(AVCodecContext **) = nullptr;
    int (*avcodec_close)(AVCodecContext *) = nullptr;
    const AVCodecHWConfig *(*avcodec_get_hw_config)(const AVCodec *, int) = nullptr;
    int (*av_codec_is_decoder)(const AVCodec *) = nullptr;
    void (*av_frame_unref)(AVFrame *) = nullptr;
    int (*av_frame_get_buffer)(AVFrame *, int) = nullptr;
    int (*av_frame_make_writable)(AVFrame *) = nullptr;
    void (*av_dict_free)(AVDictionary **) = nullptr;
};

struct Dav1dApi {
    void (*dav1d_default_settings)(Dav1dSettings *) = nullptr;
    int (*dav1d_open)(Dav1dContext **, const Dav1dSettings *) = nullptr;
    uint8_t *(*dav1d_data_create)(Dav1dData *, size_t) = nullptr;
    void (*dav1d_data_unref)(Dav1dData *) = nullptr;
    int (*dav1d_send_data)(Dav1dContext *, Dav1dData *) = nullptr;
    int (*dav1d_get_picture)(Dav1dContext *, Dav1dPicture *) = nullptr;
    void (*dav1d_picture_unref)(Dav1dPicture *) = nullptr;
    void (*dav1d_flush)(Dav1dContext *) = nullptr;
    void (*dav1d_close)(Dav1dContext **) = nullptr;
};

static OrigApi g_orig_api;
static Dav1dApi g_dav1d_api;

struct ShimState {
    AVCodecContext *avctx = nullptr;
    Dav1dContext *dav1d = nullptr;
    Dav1dData data = {};
    bool draining = false;
    bool drained = false;
    ShimState *next = nullptr;
};

static ShimState *g_states = nullptr;

template <typename T>
static bool bind(HMODULE mod, const char *name, T &out) {
    out = reinterpret_cast<T>(GetProcAddress(mod, name));
    return out != nullptr;
}

static void dirname_of_self(wchar_t *out, DWORD out_count) {
    DWORD n = GetModuleFileNameW(g_self_module, out, out_count);
    if (n == 0 || n >= out_count) {
        out[0] = 0;
        return;
    }
    for (DWORD i = n; i > 0; --i) {
        if (out[i - 1] == L'\\' || out[i - 1] == L'/') {
            out[i] = 0;
            return;
        }
    }
    out[0] = 0;
}

static bool load_orig_api() {
    wchar_t dir[MAX_PATH];
    dirname_of_self(dir, MAX_PATH);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%slibFFmpeg64_orig.dll", dir);
    g_orig = LoadLibraryW(path);
    if (!g_orig) {
        return false;
    }

    return bind(g_orig, "avcodec_find_decoder", g_orig_api.avcodec_find_decoder) &&
           bind(g_orig, "avcodec_find_decoder_by_name", g_orig_api.avcodec_find_decoder_by_name) &&
           bind(g_orig, "avcodec_alloc_context3", g_orig_api.avcodec_alloc_context3) &&
           bind(g_orig, "avcodec_open2", g_orig_api.avcodec_open2) &&
           bind(g_orig, "avcodec_send_packet", g_orig_api.avcodec_send_packet) &&
           bind(g_orig, "avcodec_receive_frame", g_orig_api.avcodec_receive_frame) &&
           bind(g_orig, "avcodec_flush_buffers", g_orig_api.avcodec_flush_buffers) &&
           bind(g_orig, "avcodec_free_context", g_orig_api.avcodec_free_context) &&
           bind(g_orig, "avcodec_close", g_orig_api.avcodec_close) &&
           bind(g_orig, "avcodec_get_hw_config", g_orig_api.avcodec_get_hw_config) &&
           bind(g_orig, "av_codec_is_decoder", g_orig_api.av_codec_is_decoder) &&
           bind(g_orig, "av_frame_unref", g_orig_api.av_frame_unref) &&
           bind(g_orig, "av_frame_get_buffer", g_orig_api.av_frame_get_buffer) &&
           bind(g_orig, "av_frame_make_writable", g_orig_api.av_frame_make_writable) &&
           bind(g_orig, "av_dict_free", g_orig_api.av_dict_free);
}

static bool load_dav1d_api() {
    wchar_t dir[MAX_PATH];
    dirname_of_self(dir, MAX_PATH);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sdav1d.dll", dir);
    g_dav1d = LoadLibraryW(path);
    if (!g_dav1d) {
        return false;
    }

    return bind(g_dav1d, "dav1d_default_settings", g_dav1d_api.dav1d_default_settings) &&
           bind(g_dav1d, "dav1d_open", g_dav1d_api.dav1d_open) &&
           bind(g_dav1d, "dav1d_data_create", g_dav1d_api.dav1d_data_create) &&
           bind(g_dav1d, "dav1d_data_unref", g_dav1d_api.dav1d_data_unref) &&
           bind(g_dav1d, "dav1d_send_data", g_dav1d_api.dav1d_send_data) &&
           bind(g_dav1d, "dav1d_get_picture", g_dav1d_api.dav1d_get_picture) &&
           bind(g_dav1d, "dav1d_picture_unref", g_dav1d_api.dav1d_picture_unref) &&
           bind(g_dav1d, "dav1d_flush", g_dav1d_api.dav1d_flush) &&
           bind(g_dav1d, "dav1d_close", g_dav1d_api.dav1d_close);
}

static BOOL CALLBACK init_once(PINIT_ONCE, PVOID, PVOID *) {
    InitializeCriticalSection(&g_state_lock);
    g_lock_ready = true;

    std::memset(&g_av1_codec, 0, sizeof(g_av1_codec));
    g_av1_codec.name = "libdav1d";
    g_av1_codec.long_name = "dav1d AV1 decoder shim for Billfish";
    g_av1_codec.type = AVMEDIA_TYPE_VIDEO;
    g_av1_codec.id = AV_CODEC_ID_AV1;
    g_av1_codec.capabilities = AV_CODEC_CAP_DELAY;
    g_av1_codec.pix_fmts = g_av1_pix_fmts;
    g_av1_codec.wrapper_name = "libdav1d";

    g_ready = load_orig_api() && load_dav1d_api();
    return TRUE;
}

static bool ensure_ready() {
    InitOnceExecuteOnce(&g_init_once, init_once, nullptr, nullptr);
    return g_ready;
}

static bool is_fake_codec(const AVCodec *codec) {
    return codec == &g_av1_codec;
}

static bool is_av1_context(const AVCodecContext *avctx, const AVCodec *codec = nullptr) {
    return avctx && (avctx->codec_id == AV_CODEC_ID_AV1 || is_fake_codec(codec) || is_fake_codec(avctx->codec));
}

static ShimState *find_state_unlocked(AVCodecContext *avctx) {
    for (ShimState *s = g_states; s; s = s->next) {
        if (s->avctx == avctx) {
            return s;
        }
    }
    return nullptr;
}

static ShimState *find_state(AVCodecContext *avctx) {
    if (!g_lock_ready) {
        return nullptr;
    }
    EnterCriticalSection(&g_state_lock);
    ShimState *s = find_state_unlocked(avctx);
    LeaveCriticalSection(&g_state_lock);
    return s;
}

static void add_state(ShimState *state) {
    EnterCriticalSection(&g_state_lock);
    state->next = g_states;
    g_states = state;
    LeaveCriticalSection(&g_state_lock);
}

static ShimState *remove_state(AVCodecContext *avctx) {
    EnterCriticalSection(&g_state_lock);
    ShimState **cur = &g_states;
    while (*cur) {
        if ((*cur)->avctx == avctx) {
            ShimState *found = *cur;
            *cur = found->next;
            found->next = nullptr;
            LeaveCriticalSection(&g_state_lock);
            return found;
        }
        cur = &(*cur)->next;
    }
    LeaveCriticalSection(&g_state_lock);
    return nullptr;
}

static void destroy_state(ShimState *state) {
    if (!state) {
        return;
    }
    g_dav1d_api.dav1d_data_unref(&state->data);
    if (state->dav1d) {
        g_dav1d_api.dav1d_close(&state->dav1d);
    }
    delete state;
}

static bool pix_fmt_for_dav1d(const Dav1dPicture &pic, enum AVPixelFormat *fmt, int *cw_shift, int *ch_shift, int *bytes_per_sample) {
    const int bpc = pic.p.bpc;
    *bytes_per_sample = bpc > 8 ? 2 : 1;
    switch (pic.p.layout) {
    case DAV1D_PIXEL_LAYOUT_I400:
        *cw_shift = 0;
        *ch_shift = 0;
        if (bpc == 8) *fmt = AV_PIX_FMT_GRAY8;
        else if (bpc == 10) *fmt = AV_PIX_FMT_GRAY10LE;
        else if (bpc == 12) *fmt = AV_PIX_FMT_GRAY12LE;
        else return false;
        return true;
    case DAV1D_PIXEL_LAYOUT_I420:
        *cw_shift = 1;
        *ch_shift = 1;
        if (bpc == 8) *fmt = AV_PIX_FMT_YUV420P;
        else if (bpc == 10) *fmt = AV_PIX_FMT_YUV420P10LE;
        else if (bpc == 12) *fmt = AV_PIX_FMT_YUV420P12LE;
        else return false;
        return true;
    case DAV1D_PIXEL_LAYOUT_I422:
        *cw_shift = 1;
        *ch_shift = 0;
        if (bpc == 8) *fmt = AV_PIX_FMT_YUV422P;
        else if (bpc == 10) *fmt = AV_PIX_FMT_YUV422P10LE;
        else if (bpc == 12) *fmt = AV_PIX_FMT_YUV422P12LE;
        else return false;
        return true;
    case DAV1D_PIXEL_LAYOUT_I444:
        *cw_shift = 0;
        *ch_shift = 0;
        if (bpc == 8) *fmt = AV_PIX_FMT_YUV444P;
        else if (bpc == 10) *fmt = AV_PIX_FMT_YUV444P10LE;
        else if (bpc == 12) *fmt = AV_PIX_FMT_YUV444P12LE;
        else return false;
        return true;
    default:
        return false;
    }
}

static void copy_plane(uint8_t *dst, int dst_stride, const uint8_t *src, ptrdiff_t src_stride, int byte_width, int height) {
    for (int y = 0; y < height; ++y) {
        std::memcpy(dst + static_cast<size_t>(y) * dst_stride, src + static_cast<size_t>(y) * src_stride, byte_width);
    }
}

static enum AVChromaLocation map_chroma(enum Dav1dChromaSamplePosition chr) {
    switch (chr) {
    case DAV1D_CHR_VERTICAL:
        return AVCHROMA_LOC_LEFT;
    case DAV1D_CHR_COLOCATED:
        return AVCHROMA_LOC_TOPLEFT;
    default:
        return AVCHROMA_LOC_UNSPECIFIED;
    }
}

static bool picture_to_frame(AVCodecContext *avctx, const Dav1dPicture &pic, AVFrame *frame) {
    enum AVPixelFormat fmt = AV_PIX_FMT_NONE;
    int cw_shift = 0;
    int ch_shift = 0;
    int bytes_per_sample = 1;
    if (!pix_fmt_for_dav1d(pic, &fmt, &cw_shift, &ch_shift, &bytes_per_sample)) {
        return false;
    }

    g_orig_api.av_frame_unref(frame);
    frame->format = fmt;
    frame->width = pic.p.w;
    frame->height = pic.p.h;
    frame->sample_aspect_ratio = avctx ? avctx->sample_aspect_ratio : AVRational{0, 1};
    frame->pts = pic.m.timestamp;
#if FF_API_PKT_PTS
    frame->pkt_pts = pic.m.timestamp;
#endif
    frame->pkt_dts = pic.m.timestamp;
    frame->best_effort_timestamp = pic.m.timestamp;
    frame->pkt_pos = pic.m.offset;
    frame->pkt_duration = pic.m.duration;
    frame->pkt_size = static_cast<int>(pic.m.size);
    if (pic.frame_hdr) {
        frame->key_frame = pic.frame_hdr->frame_type == DAV1D_FRAME_TYPE_KEY ? 1 : 0;
        frame->pict_type = (pic.frame_hdr->frame_type == DAV1D_FRAME_TYPE_KEY ||
                            pic.frame_hdr->frame_type == DAV1D_FRAME_TYPE_INTRA)
                               ? AV_PICTURE_TYPE_I
                               : AV_PICTURE_TYPE_P;
    }
    if (pic.seq_hdr) {
        frame->color_range = pic.seq_hdr->color_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        frame->color_primaries = static_cast<AVColorPrimaries>(pic.seq_hdr->pri);
        frame->color_trc = static_cast<AVColorTransferCharacteristic>(pic.seq_hdr->trc);
        frame->colorspace = static_cast<AVColorSpace>(pic.seq_hdr->mtrx);
        frame->chroma_location = map_chroma(pic.seq_hdr->chr);
    }

    if (g_orig_api.av_frame_get_buffer(frame, 32) < 0) {
        return false;
    }
    if (g_orig_api.av_frame_make_writable(frame) < 0) {
        return false;
    }

    copy_plane(frame->data[0], frame->linesize[0], static_cast<const uint8_t *>(pic.data[0]), pic.stride[0],
               pic.p.w * bytes_per_sample, pic.p.h);

    if (pic.p.layout != DAV1D_PIXEL_LAYOUT_I400) {
        const int cw = (pic.p.w + (1 << cw_shift) - 1) >> cw_shift;
        const int ch = (pic.p.h + (1 << ch_shift) - 1) >> ch_shift;
        copy_plane(frame->data[1], frame->linesize[1], static_cast<const uint8_t *>(pic.data[1]), pic.stride[1],
                   cw * bytes_per_sample, ch);
        copy_plane(frame->data[2], frame->linesize[2], static_cast<const uint8_t *>(pic.data[2]), pic.stride[1],
                   cw * bytes_per_sample, ch);
    }
    return true;
}

extern "C"
AVCodec *avcodec_find_decoder(enum AVCodecID id) {
    if (!ensure_ready()) {
        return nullptr;
    }
    if (id == AV_CODEC_ID_AV1) {
        return &g_av1_codec;
    }
    return g_orig_api.avcodec_find_decoder(id);
}

extern "C"
AVCodec *avcodec_find_decoder_by_name(const char *name) {
    if (!ensure_ready()) {
        return nullptr;
    }
    if (name && (_stricmp(name, "av1") == 0 || _stricmp(name, "libdav1d") == 0 || _stricmp(name, "dav1d") == 0)) {
        return &g_av1_codec;
    }
    return g_orig_api.avcodec_find_decoder_by_name(name);
}

extern "C"
AVCodecContext *avcodec_alloc_context3(const AVCodec *codec) {
    if (!ensure_ready()) {
        return nullptr;
    }
    AVCodecContext *ctx = g_orig_api.avcodec_alloc_context3(is_fake_codec(codec) ? nullptr : codec);
    if (ctx && is_fake_codec(codec)) {
        ctx->codec_type = AVMEDIA_TYPE_VIDEO;
        ctx->codec = &g_av1_codec;
        ctx->codec_id = AV_CODEC_ID_AV1;
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    }
    return ctx;
}

extern "C"
int avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options) {
    if (!ensure_ready()) {
        return AVERROR_EXTERNAL;
    }
    if (!is_av1_context(avctx, codec)) {
        return g_orig_api.avcodec_open2(avctx, codec, options);
    }
    if (find_state(avctx)) {
        return 0;
    }

    Dav1dSettings settings;
    g_dav1d_api.dav1d_default_settings(&settings);
    settings.n_threads = 0;
    settings.apply_grain = 1;

    ShimState *state = new (std::nothrow) ShimState();
    if (!state) {
        return AVERROR(ENOMEM);
    }
    state->avctx = avctx;
    int ret = g_dav1d_api.dav1d_open(&state->dav1d, &settings);
    if (ret < 0) {
        delete state;
        return ret;
    }

    avctx->codec_type = AVMEDIA_TYPE_VIDEO;
    avctx->codec = &g_av1_codec;
    avctx->codec_id = AV_CODEC_ID_AV1;
    avctx->pix_fmt = AV_PIX_FMT_YUV420P;
    add_state(state);
    if (options) {
        g_orig_api.av_dict_free(options);
    }
    return 0;
}

extern "C"
int avcodec_send_packet(AVCodecContext *avctx, const AVPacket *pkt) {
    if (!ensure_ready()) {
        return AVERROR_EXTERNAL;
    }
    ShimState *state = find_state(avctx);
    if (!state) {
        return g_orig_api.avcodec_send_packet(avctx, pkt);
    }

    if (!pkt || !pkt->data || pkt->size <= 0) {
        state->draining = true;
        return 0;
    }

    if (state->data.sz > 0) {
        const int pending_ret = g_dav1d_api.dav1d_send_data(state->dav1d, &state->data);
        if (pending_ret < 0) {
            if (pending_ret != -EAGAIN) {
                g_dav1d_api.dav1d_data_unref(&state->data);
            }
            return pending_ret == -EAGAIN ? AVERROR(EAGAIN) : pending_ret;
        }
        state->draining = false;
        state->drained = false;
        return 0;
    }

    uint8_t *buf = g_dav1d_api.dav1d_data_create(&state->data, static_cast<size_t>(pkt->size));
    if (!buf) {
        return AVERROR(ENOMEM);
    }
    std::memcpy(buf, pkt->data, static_cast<size_t>(pkt->size));
    state->data.m.timestamp = pkt->pts;
    state->data.m.duration = pkt->duration;
    state->data.m.offset = pkt->pos;
    state->data.m.size = static_cast<size_t>(pkt->size);

    const int ret = g_dav1d_api.dav1d_send_data(state->dav1d, &state->data);
    if (ret < 0) {
        if (ret != -EAGAIN) {
            g_dav1d_api.dav1d_data_unref(&state->data);
        }
        return ret == -EAGAIN ? AVERROR(EAGAIN) : ret;
    }
    state->draining = false;
    state->drained = false;
    return 0;
}

extern "C"
int avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame) {
    if (!ensure_ready()) {
        return AVERROR_EXTERNAL;
    }
    ShimState *state = find_state(avctx);
    if (!state) {
        return g_orig_api.avcodec_receive_frame(avctx, frame);
    }

    if (state->drained) {
        return AVERROR_EOF;
    }

    Dav1dPicture pic;
    std::memset(&pic, 0, sizeof(pic));
    int ret = g_dav1d_api.dav1d_get_picture(state->dav1d, &pic);
    if (ret == -EAGAIN) {
        if (state->draining) {
            state->drained = true;
            return AVERROR_EOF;
        }
        return AVERROR(EAGAIN);
    }
    if (ret < 0) {
        return ret;
    }

    const bool ok = picture_to_frame(avctx, pic, frame);
    g_dav1d_api.dav1d_picture_unref(&pic);
    return ok ? 0 : AVERROR(EINVAL);
}

extern "C"
void avcodec_flush_buffers(AVCodecContext *avctx) {
    if (!ensure_ready()) {
        return;
    }
    ShimState *state = find_state(avctx);
    if (!state) {
        g_orig_api.avcodec_flush_buffers(avctx);
        return;
    }
    g_dav1d_api.dav1d_data_unref(&state->data);
    g_dav1d_api.dav1d_flush(state->dav1d);
    state->draining = false;
    state->drained = false;
}

extern "C"
int avcodec_close(AVCodecContext *avctx) {
    if (!ensure_ready()) {
        return AVERROR_EXTERNAL;
    }
    ShimState *state = remove_state(avctx);
    if (state) {
        destroy_state(state);
        if (avctx && avctx->codec == &g_av1_codec) {
            avctx->codec = nullptr;
        }
        return 0;
    }
    return g_orig_api.avcodec_close(avctx);
}

extern "C"
void avcodec_free_context(AVCodecContext **pavctx) {
    if (!ensure_ready()) {
        return;
    }
    if (pavctx && *pavctx) {
        ShimState *state = remove_state(*pavctx);
        if (state) {
            destroy_state(state);
            if ((*pavctx)->codec == &g_av1_codec) {
                (*pavctx)->codec = nullptr;
            }
        }
    }
    g_orig_api.avcodec_free_context(pavctx);
}

extern "C"
const AVCodecHWConfig *avcodec_get_hw_config(const AVCodec *codec, int index) {
    if (!ensure_ready()) {
        return nullptr;
    }
    if (is_fake_codec(codec)) {
        return nullptr;
    }
    return g_orig_api.avcodec_get_hw_config(codec, index);
}

extern "C"
int av_codec_is_decoder(const AVCodec *codec) {
    if (!ensure_ready()) {
        return 0;
    }
    if (is_fake_codec(codec)) {
        return 1;
    }
    return g_orig_api.av_codec_is_decoder(codec);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self_module = module;
        DisableThreadLibraryCalls(module);
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_lock_ready) {
            EnterCriticalSection(&g_state_lock);
            ShimState *s = g_states;
            g_states = nullptr;
            LeaveCriticalSection(&g_state_lock);
            while (s) {
                ShimState *next = s->next;
                destroy_state(s);
                s = next;
            }
            DeleteCriticalSection(&g_state_lock);
            g_lock_ready = false;
        }
    }
    return TRUE;
}
