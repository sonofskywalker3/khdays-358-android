/* The player's HD video through Android's own decoders (movie_hd.h), as movie_hd_win.c does it through
 * Media Foundation: a thread of its own, two extractors and decoders reading ahead at their own pace (the
 * picture into NV12, the sound as 16-bit or float samples into its SDL stream); the clock is the sound
 * played so far, else the monotonic clock less the pauses; each refresh takes the newest picture due. */
#include "movie_hd.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <SDL3/SDL.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define PICTURES_AHEAD 0.15   /* seconds of pictures decoded ahead of the clock */
#define SOUND_AHEAD 0.30      /* seconds of sound queued ahead of the device */
#define WAIT_US 2000          /* how long one look at a decoder's output waits */
#define IDLE_LOOKS 500        /* looks with nothing out, after the input's end, that mean the end */
#define NANOSECONDS 1000000000LL
/* MediaCodecInfo.CodecCapabilities' colour formats */
#define COLOR_I420 19
#define COLOR_NV12 21
#define COLOR_FLEXIBLE 0x7F420888
/* AudioFormat's encodings */
#define PCM_FLOAT 4

typedef struct Picture {
    double time; /* its presentation time in the file, in seconds */
    long number; /* counted from 0, in decoding order */
    int width, height;
    uint8_t *data;
    size_t size;
} Picture;

/* One of the file's streams: its own file handle, extractor and decoder. */
typedef struct Track {
    int fd;
    AMediaExtractor *extractor;
    AMediaCodec *codec;
    int input_end; /* the extractor has given its last sample */
    int idle;      /* looks in a row with nothing out since the input's end */
} Track;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; /* guards the pictures, the status and the clock's bases */
static float gain = 1.0f;                                /* the sound's volume (khdays_hd_set_gain) */

/* The thread's two signals: "opened" stays set; "wake" is taken by the wait it ends. */
static pthread_mutex_t signal_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t signal_cond = PTHREAD_COND_INITIALIZER;

static struct {
    pthread_t thread;
    int have_thread;
    int opened, woken;
    volatile int stop, started, paused;
    int open_ok;
    int status; /* what khdays_hd_status says (movie_hd.h): 0 playing, 1 ended, -1 failed, 2 none */
    char why[256];
    char *path;
    KhdaysHdInfo info;
    /* the picture */
    Track video;
    int color, stride, slice_height, crop_left, crop_top;
    int video_end;
    Picture *pictures; /* decoded, oldest first */
    int count, capacity;
    Picture *spare; /* picture buffers kept to be used again */
    int spare_count, spare_capacity;
    long decoded, taken; /* pictures decoded; the number of the last one taken (-1: none) */
    double first_time;   /* the first picture's time */
    double duration;     /* how long the file says it lasts, or 0 when it does not say */
    /* the sound */
    Track audio;
    int have_audio;
    SDL_AudioStream *sound;
    int sound_end;
    int frame_bytes;            /* bytes per sound frame, all channels */
    volatile int64_t put_frames; /* sound frames put into the stream so far */
    double sound_start;          /* the first sound sample's time */
    int last_queued;             /* after the sound's end: what was still queued, and since when */
    int64_t still_since;
    /* the monotonic clock: used without sound, or once the sound has all played */
    volatile int counter_clock;
    double counter_base;              /* the video time it starts from */
    int64_t counter_start;            /* the clock's value at that time */
    int64_t paused_at, paused_ticks;  /* when the pause began; the nanoseconds spent paused */
    /* what happened, for the report when it closes */
    long shown, late;
    double worst_offset;
} hd = {.status = 2, .taken = -1, .video = {.fd = -1}, .audio = {.fd = -1}};

/* The monotonic clock now, in nanoseconds. */
static int64_t counter(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * NANOSECONDS + now.tv_nsec;
}

static void set_signal(int *which)
{
    pthread_mutex_lock(&signal_lock);
    *which = 1;
    pthread_cond_broadcast(&signal_cond);
    pthread_mutex_unlock(&signal_lock);
}

static void wait_opened(void)
{
    pthread_mutex_lock(&signal_lock);
    while (!hd.opened) {
        pthread_cond_wait(&signal_cond, &signal_lock);
    }
    pthread_mutex_unlock(&signal_lock);
}

/* Waits `ms` milliseconds, or less when woken. */
static void wait_woken(int ms)
{
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += (long)ms * 1000000L;
    until.tv_sec += until.tv_nsec / NANOSECONDS;
    until.tv_nsec %= NANOSECONDS;
    pthread_mutex_lock(&signal_lock);
    if (!hd.woken) {
        pthread_cond_timedwait(&signal_cond, &signal_lock, &until);
    }
    hd.woken = 0;
    pthread_mutex_unlock(&signal_lock);
}

/* The video's clock, in seconds of the file's time (see the top of this file). */
static double clock_now(void)
{
    if (!hd.started) {
        return hd.first_time;
    }
    if (!hd.counter_clock && hd.sound != NULL) {
        int queued = SDL_GetAudioStreamQueued(hd.sound);
        if (queued < 0) {
            queued = 0;
        }
        return hd.sound_start + (double)(hd.put_frames - queued / hd.frame_bytes) / hd.info.rate;
    }
    {
        const int64_t now = hd.paused ? hd.paused_at : counter();
        return hd.counter_base + (double)(now - hd.counter_start - hd.paused_ticks) / (double)NANOSECONDS;
    }
}

/* Opens the file's first stream whose type starts with `kind` ("video/" or
 * "audio/") and a decoder for it. Returns its format (to delete), or NULL. */
static AMediaFormat *open_track(Track *t, const char *kind)
{
    struct stat st;
    t->fd = open(hd.path, O_RDONLY | O_CLOEXEC);
    if (t->fd < 0 || fstat(t->fd, &st) != 0) {
        snprintf(hd.why, sizeof(hd.why), "the file cannot be opened");
        return NULL;
    }
    t->extractor = AMediaExtractor_new();
    if (t->extractor == NULL || AMediaExtractor_setDataSourceFd(t->extractor, t->fd, 0, st.st_size) != AMEDIA_OK) {
        snprintf(hd.why, sizeof(hd.why), "the file is not one Android reads");
        return NULL;
    }
    for (size_t i = 0; i < AMediaExtractor_getTrackCount(t->extractor); ++i) {
        AMediaFormat *format = AMediaExtractor_getTrackFormat(t->extractor, i);
        const char *mime = NULL;
        if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime) && strncmp(mime, kind, strlen(kind)) == 0) {
            AMediaExtractor_selectTrack(t->extractor, i);
            t->codec = AMediaCodec_createDecoderByType(mime);
            if (t->codec == NULL) {
                snprintf(hd.why, sizeof(hd.why), "no decoder for its %s", mime);
                AMediaFormat_delete(format);
                return NULL;
            }
            return format;
        }
        AMediaFormat_delete(format);
    }
    snprintf(hd.why, sizeof(hd.why), "the file has no %.5s", kind);
    return NULL;
}

static void close_track(Track *t)
{
    if (t->codec != NULL) {
        AMediaCodec_stop(t->codec);
        AMediaCodec_delete(t->codec);
    }
    if (t->extractor != NULL) {
        AMediaExtractor_delete(t->extractor);
    }
    if (t->fd >= 0) {
        close(t->fd);
    }
    memset(t, 0, sizeof(*t));
    t->fd = -1;
}

/* Gives the decoder the stream's next sample, if it has room for one. */
static void feed(Track *t)
{
    size_t room = 0;
    uint8_t *buffer;
    ssize_t n;
    const ssize_t i = t->input_end ? -1 : AMediaCodec_dequeueInputBuffer(t->codec, 0);
    if (i < 0) {
        return;
    }
    buffer = AMediaCodec_getInputBuffer(t->codec, (size_t)i, &room);
    n = buffer != NULL ? AMediaExtractor_readSampleData(t->extractor, buffer, room) : -1;
    if (n < 0) {
        AMediaCodec_queueInputBuffer(t->codec, (size_t)i, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        t->input_end = 1;
        return;
    }
    AMediaCodec_queueInputBuffer(t->codec, (size_t)i, 0, (size_t)n,
                                 (uint64_t)AMediaExtractor_getSampleTime(t->extractor), 0);
    AMediaExtractor_advance(t->extractor);
}

/* Takes the decoder's next output buffer, feeding it as it needs: its index,
 * or -1 with nothing out yet, -2 when its format changed, -3 on an error. */
static ssize_t next_output(Track *t, AMediaCodecBufferInfo *info)
{
    ssize_t o;
    feed(t);
    o = AMediaCodec_dequeueOutputBuffer(t->codec, info, WAIT_US);
    if (o >= 0) {
        t->idle = 0;
        return o;
    }
    if (o == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        return -2;
    }
    if (o == AMEDIACODEC_INFO_TRY_AGAIN_LATER || o == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        t->idle = t->input_end ? t->idle + 1 : 0;
        return -1;
    }
    return -3;
}

/* Reads the picture's format as the decoder gives it now: its size, the area
 * shown and the layout of its planes. */
static int read_video_type(void)
{
    AMediaFormat *format = AMediaCodec_getOutputFormat(hd.video.codec);
    int32_t width = 0, height = 0, left = 0, top = 0, right = 0, bottom = 0;
    int32_t stride = 0, slice = 0, color = 0;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_STRIDE, &stride);
    AMediaFormat_getInt32(format, "slice-height", &slice);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color);
    if (width <= 0 || height <= 0) {
        AMediaFormat_delete(format);
        snprintf(hd.why, sizeof(hd.why), "the picture has no size");
        return 0;
    }
    hd.crop_left = hd.crop_top = 0;
    /* the decoder's rows are whole macroblocks (1088 for 1080); the crop says which part is shown */
    if (AMediaFormat_getRect(format, "crop", &left, &top, &right, &bottom) && right >= left && bottom >= top &&
        right < width && bottom < height) {
        hd.crop_left = left;
        hd.crop_top = top;
        hd.info.width = right - left + 1;
        hd.info.height = bottom - top + 1;
    } else {
        hd.info.width = width;
        hd.info.height = height;
    }
    AMediaFormat_delete(format);
    hd.stride = stride >= width ? stride : width;
    hd.slice_height = slice >= height ? slice : height;
    hd.color = color;
    if (color != COLOR_NV12 && color != COLOR_I420) {
        snprintf(hd.why, sizeof(hd.why), "the decoder gives its pictures in colour format 0x%x", (unsigned)color);
        return 0;
    }
    hd.info.width &= ~1; /* NV12's chroma has one sample per two columns and two rows */
    hd.info.height &= ~1;
    hd.crop_left &= ~1;
    hd.crop_top &= ~1;
    return 1;
}

/* Opens the picture's decoder and reads the file's length and frame rate.
 * Returns 0 (with hd.why) if the picture cannot be played. */
static int open_video(void)
{
    AMediaFormat *format = open_track(&hd.video, "video/");
    int64_t duration = 0;
    int32_t rate = 0;
    float rate_f = 0.0f;
    media_status_t status;
    if (format == NULL) {
        return 0;
    }
    if (AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &duration)) {
        hd.duration = (double)duration / 1e6;
    }
    if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, &rate) && rate > 0) {
        hd.info.fps = rate;
    } else if (AMediaFormat_getFloat(format, AMEDIAFORMAT_KEY_FRAME_RATE, &rate_f) && rate_f > 0.0f) {
        hd.info.fps = rate_f;
    }
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_NV12);
    status = AMediaCodec_configure(hd.video.codec, format, NULL, NULL, 0);
    AMediaFormat_delete(format);
    if (status != AMEDIA_OK || AMediaCodec_start(hd.video.codec) != AMEDIA_OK) {
        snprintf(hd.why, sizeof(hd.why), "the picture's decoder does not start (%d)", (int)status);
        return 0;
    }
    return read_video_type();
}

/* A buffer for a picture of `size` bytes: a spare one of that size, or a new one. */
static uint8_t *picture_buffer(size_t size)
{
    while (hd.spare_count > 0) {
        Picture p = hd.spare[--hd.spare_count];
        if (p.size == size) {
            return p.data;
        }
        free(p.data);
    }
    return (uint8_t *)malloc(size);
}

/* Keeps a picture's buffer to be used again (freed if there is no room). */
static void keep_spare(Picture p)
{
    if (hd.spare_count == hd.spare_capacity) {
        const int capacity = hd.spare_capacity ? hd.spare_capacity * 2 : 8;
        Picture *grown = (Picture *)realloc(hd.spare, (size_t)capacity * sizeof(Picture));
        if (grown == NULL) {
            free(p.data);
            return;
        }
        hd.spare = grown;
        hd.spare_capacity = capacity;
    }
    hd.spare[hd.spare_count++] = p;
}

/* Adds a decoded picture (`size` bytes at `data`) to the list. */
static int add_picture(uint8_t *data, size_t size, double time)
{
    pthread_mutex_lock(&lock);
    if (hd.count == hd.capacity) {
        const int capacity = hd.capacity ? hd.capacity * 2 : 16;
        Picture *grown = (Picture *)realloc(hd.pictures, (size_t)capacity * sizeof(Picture));
        if (grown == NULL) {
            pthread_mutex_unlock(&lock);
            free(data);
            snprintf(hd.why, sizeof(hd.why), "out of memory for the pictures");
            return 0;
        }
        hd.pictures = grown;
        hd.capacity = capacity;
    }
    hd.pictures[hd.count].time = time;
    hd.pictures[hd.count].number = hd.decoded++;
    hd.pictures[hd.count].width = hd.info.width;
    hd.pictures[hd.count].height = hd.info.height;
    hd.pictures[hd.count].data = data;
    hd.pictures[hd.count].size = size;
    hd.count++;
    pthread_mutex_unlock(&lock);
    return 1;
}

/* Copies a decoded buffer's planes (NV12, or I420 with its chroma
 * interleaved here) into a new picture of the shown size and adds it. */
static int keep_picture(const uint8_t *frame, size_t length, double time)
{
    const int w = hd.info.width, h = hd.info.height;
    const size_t size = (size_t)w * (size_t)h * 3 / 2;
    const size_t luma_size = (size_t)hd.stride * (size_t)hd.slice_height;
    const size_t need = hd.color == COLOR_NV12 ? luma_size + luma_size / 2 : luma_size + 2 * (luma_size / 4);
    uint8_t *data;
    if (length < need) {
        snprintf(hd.why, sizeof(hd.why), "a picture is %zu bytes, not the %zu of %dx%d (stride %d)", length, need,
                 hd.stride, hd.slice_height, hd.stride);
        return 0;
    }
    data = picture_buffer(size);
    if (data == NULL) {
        snprintf(hd.why, sizeof(hd.why), "out of memory for the pictures");
        return 0;
    }
    for (int y = 0; y < h; ++y) {
        memcpy(data + (size_t)y * w, frame + (size_t)(hd.crop_top + y) * hd.stride + hd.crop_left, (size_t)w);
    }
    if (hd.color == COLOR_NV12) {
        const uint8_t *chroma = frame + luma_size;
        for (int y = 0; y < h / 2; ++y) {
            memcpy(data + (size_t)w * h + (size_t)y * w,
                   chroma + (size_t)(hd.crop_top / 2 + y) * hd.stride + hd.crop_left, (size_t)w);
        }
    } else {
        const int half_stride = hd.stride / 2;
        const uint8_t *u = frame + luma_size, *v = u + luma_size / 4;
        for (int y = 0; y < h / 2; ++y) {
            const size_t row = (size_t)(hd.crop_top / 2 + y) * half_stride + hd.crop_left / 2;
            uint8_t *out = data + (size_t)w * h + (size_t)y * w;
            for (int x = 0; x < w / 2; ++x) {
                out[2 * x] = u[row + x];
                out[2 * x + 1] = v[row + x];
            }
        }
    }
    return add_picture(data, size, time);
}

/* The newest decoded picture's time (very negative when there is none). */
static double newest_time(void)
{
    double t;
    pthread_mutex_lock(&lock);
    t = hd.count > 0 ? hd.pictures[hd.count - 1].time : -1.0e9;
    pthread_mutex_unlock(&lock);
    return t;
}

/* The picture's end: a file whose pictures end well before its length is cut
 * short, and fails as a broken file does (the game's movie is shown). */
static int picture_end(void)
{
    const double last = newest_time();
    hd.video_end = 1;
    if (hd.duration > 0.0 && last + 0.5 < hd.duration) {
        snprintf(hd.why, sizeof(hd.why), "its pictures end at %.2f s of its %.2f s (the file cut short?)", last,
                 hd.duration);
        return 0;
    }
    return 1;
}

/* Decodes one picture (or finds the end). Returns 0 on an error. */
static int read_picture(void)
{
    while (!hd.stop) {
        AMediaCodecBufferInfo info;
        const ssize_t o = next_output(&hd.video, &info);
        int ok = 1;
        if (o == -1) {
            if (hd.video.idle > IDLE_LOOKS) {
                return picture_end(); /* a decoder that never says its last picture came */
            }
            continue;
        }
        if (o == -2) {
            if (!read_video_type()) {
                return 0;
            }
            continue;
        }
        if (o == -3) {
            snprintf(hd.why, sizeof(hd.why), "the picture cannot be decoded");
            return 0;
        }
        if (info.size > 0) {
            size_t length = 0;
            const uint8_t *frame = AMediaCodec_getOutputBuffer(hd.video.codec, (size_t)o, &length);
            ok = frame != NULL && keep_picture(frame + info.offset, (size_t)info.size, (double)info.presentationTimeUs / 1e6);
            if (frame == NULL) {
                snprintf(hd.why, sizeof(hd.why), "a picture cannot be read");
            }
        }
        AMediaCodec_releaseOutputBuffer(hd.video.codec, (size_t)o, false);
        if (ok && (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM)) {
            ok = picture_end();
        }
        return ok;
    }
    return 1;
}

/* Takes the sound's format from its decoder: the stream is made for it (paused
 * until the video starts), or told of the change. Returns 0 if it cannot play. */
static int sound_format(void)
{
    AMediaFormat *format = AMediaCodec_getOutputFormat(hd.audio.codec);
    int32_t channels = 0, rate = 0, encoding = 0;
    SDL_AudioSpec spec;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &rate);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_PCM_ENCODING, &encoding);
    AMediaFormat_delete(format);
    if (channels <= 0 || rate <= 0) {
        return 0;
    }
    spec.format = encoding == PCM_FLOAT ? SDL_AUDIO_F32 : SDL_AUDIO_S16;
    spec.channels = channels;
    spec.freq = rate;
    hd.frame_bytes = channels * (encoding == PCM_FLOAT ? 4 : 2);
    hd.info.channels = channels;
    hd.info.rate = rate;
    if (hd.sound != NULL) {
        return SDL_SetAudioStreamFormat(hd.sound, &spec, NULL);
    }
    hd.sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (hd.sound == NULL) {
        fprintf(stderr, "movies: no sound for the HD video: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetAudioStreamGain(hd.sound, gain);
    return 1;
}

/* Decodes some sound into the stream (or finds its end). An error ends the
 * sound there. */
static void read_sound(void)
{
    for (int looks = 0; looks < 16 && !hd.sound_end; ++looks) {
        AMediaCodecBufferInfo info;
        const ssize_t o = next_output(&hd.audio, &info);
        if (o == -1) {
            if (hd.audio.idle > IDLE_LOOKS) {
                hd.sound_end = 1;
            }
            continue;
        }
        if (o == -2) {
            if (!sound_format()) {
                hd.sound_end = 1;
            }
            continue;
        }
        if (o == -3) {
            fprintf(stderr, "movies: the HD video's sound cannot be decoded; it ends there\n");
            hd.sound_end = 1;
            break;
        }
        if (info.size > 0 && hd.sound != NULL) {
            size_t length = 0;
            const uint8_t *data = AMediaCodec_getOutputBuffer(hd.audio.codec, (size_t)o, &length);
            if (data != NULL) {
                if (hd.put_frames == 0) {
                    hd.sound_start = (double)info.presentationTimeUs / 1e6;
                }
                SDL_PutAudioStreamData(hd.sound, data + info.offset, info.size);
                __atomic_add_fetch(&hd.put_frames, info.size / hd.frame_bytes, __ATOMIC_SEQ_CST);
            }
        }
        AMediaCodec_releaseOutputBuffer(hd.audio.codec, (size_t)o, false);
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
            hd.sound_end = 1;
        }
        break;
    }
    if (hd.sound_end && hd.sound != NULL) {
        /* nothing more comes: flush, so what the stream holds back for its resampler plays too */
        SDL_FlushAudioStream(hd.sound);
    }
}

/* The seconds of sound queued in the stream, not yet played. */
static double sound_queued(void)
{
    const int queued = SDL_GetAudioStreamQueued(hd.sound);
    return queued > 0 ? (double)queued / (double)hd.frame_bytes / hd.info.rate : 0.0;
}

/* Opens the sound, if the file has one Android can decode, and decodes until
 * its format is known. Returns 0 if there is no sound to play; the video then
 * plays silent. */
static int open_audio(void)
{
    AMediaFormat *format = open_track(&hd.audio, "audio/");
    media_status_t status = AMEDIA_ERROR_UNKNOWN;
    if (format != NULL) {
        status = AMediaCodec_configure(hd.audio.codec, format, NULL, NULL, 0);
        AMediaFormat_delete(format);
    }
    if (status == AMEDIA_OK && AMediaCodec_start(hd.audio.codec) == AMEDIA_OK) {
        hd.have_audio = 1;
        for (int tries = 0; tries < 200 && hd.sound == NULL && !hd.sound_end; ++tries) {
            read_sound();
        }
    }
    if (hd.sound == NULL) {
        close_track(&hd.audio);
        hd.have_audio = 0;
        hd.sound_end = 0;
        hd.why[0] = '\0'; /* a silent video plays */
        return 0;
    }
    return 1;
}

/* Switches to the monotonic clock from now on, starting at `time`. */
static void use_counter_clock(double time)
{
    pthread_mutex_lock(&lock);
    hd.counter_base = time;
    hd.counter_start = counter();
    hd.paused_ticks = 0;
    if (hd.paused) {
        hd.paused_at = hd.counter_start;
    }
    __atomic_store_n(&hd.counter_clock, 1, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&lock);
}

/* Opens the file and decodes its first picture and a little of its sound.
 * Returns whether the video can play (if not, hd.why says why). */
static int open_and_begin(void)
{
    if (!open_video()) {
        return 0;
    }
    while (hd.count == 0 && !hd.video_end && !hd.stop) { /* the first picture */
        if (!read_picture()) {
            break;
        }
    }
    if (hd.count == 0) {
        if (hd.why[0] == '\0') {
            snprintf(hd.why, sizeof(hd.why), "the file has no picture");
        }
        return 0;
    }
    hd.first_time = hd.pictures[0].time;
    if (open_audio()) {
        while (!hd.sound_end && sound_queued() < SOUND_AHEAD) {
            read_sound();
        }
    } else {
        use_counter_clock(hd.first_time);
    }
    return 1;
}

/* Once the sound has all played -- or what is left of it has not moved for a
 * quarter of a second -- the clock goes on from the sound's end on the
 * monotonic clock. */
static void follow_sound_end(void)
{
    const int queued = SDL_GetAudioStreamQueued(hd.sound);
    if (queued != hd.last_queued || hd.still_since == 0) {
        hd.last_queued = queued;
        hd.still_since = counter();
    }
    if (queued <= 0 || counter() - hd.still_since > NANOSECONDS / 4) {
        use_counter_clock(clock_now());
    }
}

/* One turn of the thread while the video plays: decodes pictures and sound to
 * keep them a little ahead of the clock. Returns 1 if it decoded something, 0
 * if nothing was needed, or -1 if a picture cannot be decoded. */
static int decode_ahead(void)
{
    const double now = clock_now();
    int did = 0;
    if (!hd.video_end && newest_time() < now + PICTURES_AHEAD) {
        if (!read_picture()) {
            return -1;
        }
        did = 1;
    }
    if (hd.sound != NULL && !hd.sound_end && sound_queued() < SOUND_AHEAD) {
        read_sound();
        did = 1;
    }
    if (hd.started && hd.sound != NULL && !hd.counter_clock && hd.sound_end) {
        follow_sound_end();
    }
    return did;
}

/* The video's thread: opens the file, then keeps decoding ahead until it is
 * told to stop or a picture fails. */
static void *run(void *unused)
{
    (void)unused;
    if (open_and_begin()) {
        hd.open_ok = 1;
        hd.status = 0;
    }
    set_signal(&hd.opened);
    while (hd.open_ok && !hd.stop) {
        const int did = hd.paused ? 0 : decode_ahead();
        if (did < 0) {
            pthread_mutex_lock(&lock);
            hd.status = -1;
            pthread_mutex_unlock(&lock);
            break;
        }
        if (did == 0) {
            wait_woken(2); /* nothing to do: wait a little, or until woken */
        }
    }
    close_track(&hd.video);
    close_track(&hd.audio);
    return NULL;
}

void khdays_hd_close(void)
{
    if (!hd.have_thread) {
        return;
    }
    __atomic_store_n(&hd.stop, 1, __ATOMIC_SEQ_CST);
    set_signal(&hd.woken);
    pthread_join(hd.thread, NULL);
    if (hd.sound != NULL) {
        SDL_DestroyAudioStream(hd.sound); /* the sound stops at once */
    }
    if (hd.open_ok) {
        fprintf(stderr,
                "movies: the HD video showed %ld pictures, %ld late (passed over), its picture at most "
                "%.1f ms off its clock\n",
                hd.shown, hd.late, hd.worst_offset * 1000.0);
    }
    pthread_mutex_lock(&lock);
    for (int i = 0; i < hd.count; ++i) {
        free(hd.pictures[i].data);
    }
    for (int i = 0; i < hd.spare_count; ++i) {
        free(hd.spare[i].data);
    }
    free(hd.pictures);
    free(hd.spare);
    free(hd.path);
    memset(&hd, 0, sizeof(hd));
    hd.status = 2;
    hd.taken = -1;
    hd.video.fd = hd.audio.fd = -1;
    pthread_mutex_unlock(&lock);
}

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    khdays_hd_close();
    hd.path = strdup(path);
    if (hd.path == NULL || pthread_create(&hd.thread, NULL, run, NULL) != 0) {
        snprintf(why, size, "no thread for it");
        free(hd.path);
        hd.path = NULL;
        return 0;
    }
    hd.have_thread = 1;
    wait_opened(); /* the thread always signals it, opened or not */
    if (!hd.open_ok) {
        snprintf(why, size, "%s", hd.why[0] ? hd.why : "it cannot be played");
        khdays_hd_close();
        return 0;
    }
    *info = hd.info;
    return 1;
}

void khdays_hd_start(void)
{
    if (!hd.open_ok || hd.started) {
        return;
    }
    if (hd.counter_clock) {
        hd.counter_start = counter();
    }
    __atomic_store_n(&hd.started, 1, __ATOMIC_SEQ_CST);
    if (hd.sound != NULL) {
        SDL_ResumeAudioStreamDevice(hd.sound);
    }
    set_signal(&hd.woken);
}

void khdays_hd_pause(int pause)
{
    if (!hd.open_ok || (hd.paused != 0) == (pause != 0)) {
        return;
    }
    pthread_mutex_lock(&lock);
    if (pause) {
        hd.paused_at = counter();
    } else {
        hd.paused_ticks += counter() - hd.paused_at;
    }
    __atomic_store_n(&hd.paused, pause != 0, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&lock);
    if (hd.sound != NULL) {
        if (pause) {
            SDL_PauseAudioStreamDevice(hd.sound);
        } else if (hd.started) {
            SDL_ResumeAudioStreamDevice(hd.sound);
        }
    }
    set_signal(&hd.woken);
}

int khdays_hd_status(char *why, size_t size)
{
    static int trace = -1;
    static Uint64 traced_at;
    int status;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_MOVIE_HD") != NULL;
    }
    pthread_mutex_lock(&lock);
    if (trace && SDL_GetTicks() - traced_at >= 1000) {
        /* with KHDAYS_TRACE_MOVIE_HD: once a second, where the video is */
        traced_at = SDL_GetTicks();
        fprintf(stderr,
                "movie hd: status %d, started %d, clock %.3f, pictures %d (newest %.3f), taken %ld, "
                "decoded %ld, video end %d, sound end %d, queued %d, counter clock %d\n",
                hd.status, hd.started, clock_now(), hd.count, hd.count > 0 ? hd.pictures[hd.count - 1].time : -1.0,
                hd.taken, hd.decoded, hd.video_end, hd.sound_end, hd.sound != NULL ? SDL_GetAudioStreamQueued(hd.sound) : -1,
                hd.counter_clock);
    }
    status = hd.status;
    if (status == 0 && hd.started && hd.video_end && (hd.sound == NULL || hd.counter_clock)) {
        /* every picture is decoded; it has ended once the last one was taken and its time has passed */
        const double last = hd.count > 0 ? hd.pictures[hd.count - 1].time : hd.first_time;
        const double frame = hd.info.fps > 0.0 ? 1.0 / hd.info.fps : 0.0;
        if ((hd.count == 0 || hd.pictures[hd.count - 1].number == hd.taken) && clock_now() >= last + frame) {
            status = 1;
        }
    }
    if (status == -1 && why != NULL && size > 0) {
        snprintf(why, size, "%s", hd.why);
    }
    pthread_mutex_unlock(&lock);
    return status;
}

void khdays_hd_set_gain(float g)
{
    gain = g;
    if (hd.sound != NULL) {
        SDL_SetAudioStreamGain(hd.sound, g);
    }
}

int khdays_hd_picture_begin(KhdaysHdPicture *picture)
{
    double now, half;
    int pick = -1;
    pthread_mutex_lock(&lock);
    if (!hd.open_ok || hd.count == 0) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    now = clock_now();
    half = hd.info.fps > 0.0 ? 0.5 / hd.info.fps : 0.0;
    for (int i = 0; i < hd.count && hd.pictures[i].time <= now + half; ++i) {
        pick = i;
    }
    if (pick < 0 || hd.pictures[pick].number == hd.taken) {
        pthread_mutex_unlock(&lock);
        return 0;
    }
    for (int i = 0; i < pick; ++i) {
        if (hd.pictures[i].number != hd.taken) {
            ++hd.late; /* its time passed before a refresh took it: it was never shown */
        }
        keep_spare(hd.pictures[i]);
    }
    memmove(hd.pictures, hd.pictures + pick, (size_t)(hd.count - pick) * sizeof(Picture));
    hd.count -= pick;
    hd.taken = hd.pictures[0].number;
    ++hd.shown;
    if (hd.started) {
        const double off = now - hd.pictures[0].time;
        if ((off < 0 ? -off : off) > hd.worst_offset) {
            hd.worst_offset = off < 0 ? -off : off;
        }
    }
    picture->width = hd.pictures[0].width;
    picture->height = hd.pictures[0].height;
    picture->pitch = hd.pictures[0].width;
    picture->luma = hd.pictures[0].data;
    picture->chroma = hd.pictures[0].data + (size_t)hd.pictures[0].width * (size_t)hd.pictures[0].height;
    picture->time = hd.pictures[0].time;
    picture->fps = hd.info.fps;
    set_signal(&hd.woken); /* there is room ahead now: the thread decodes on */
    return 1;              /* the lock stays held until khdays_hd_picture_done */
}

void khdays_hd_picture_done(void)
{
    pthread_mutex_unlock(&lock);
}
