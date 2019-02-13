#ifndef READANDDISPLAY_H
#define READANDDISPLAY_H
#define HAVE_LIBC
#include <QString>
#include <inttypes.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>

#include <libavutil/avstring.h>
#include <libavutil/eval.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libavutil/dict.h>
#include <libavutil/parseutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/avassert.h>
#include <libavutil/time.h>
#include <libavutil/display.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavcodec/avfft.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_error.h>

#include <boost/thread/thread.hpp>
#include <boost/thread/lockable_adapter.hpp>
#include <boost/thread/lock_guard.hpp>


#include <assert.h>
//#include "commfun.h"


#define MAX_QUEUE_SIZE (15 * 1024 * 1024)
#define MIN_FRAMES 25
#define EXTERNAL_CLOCK_MIN_FRAMES 2
#define EXTERNAL_CLOCK_MAX_FRAMES 10

/* Minimum SDL audio buffer size, in samples. */
#define SDL_AUDIO_MIN_BUFFER_SIZE 512
/* Calculate actual buffer size keeping in mind not cause too frequent audio callbacks */
#define SDL_AUDIO_MAX_CALLBACKS_PER_SEC 30

/* Step size for volume control in dB */
#define SDL_VOLUME_STEP (0.75)

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0

/* maximum audio speed change to get correct sync */
#define SAMPLE_CORRECTION_PERCENT_MAX 10

/* external clock speed adjustment constants for realtime sources based on buffer fullness */
#define EXTERNAL_CLOCK_SPEED_MIN  0.900
#define EXTERNAL_CLOCK_SPEED_MAX  1.010
#define EXTERNAL_CLOCK_SPEED_STEP 0.001

/* we use about AUDIO_DIFF_AVG_NB A-V differences to make the average */
#define AUDIO_DIFF_AVG_NB   20

/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

/* NOTE: the size must be big enough to compensate the hardware audio buffersize size */
/* TODO: We assume that a decoded and resampled frame fits into this buffer */
#define SAMPLE_ARRAY_SIZE (8 * 65536)

#define CURSOR_HIDE_DELAY 1000000

#define USE_ONEPASS_SUBTITLE_RENDER 1
#define CONFIG_AVFILTER 0

static unsigned sws_flags = SWS_BICUBIC;
typedef struct MyAVPacketList {
    AVPacket pkt;
    struct MyAVPacketList *next;
    int serial;
} MyAVPacketList;
typedef struct OptionDef {
    const char *name;
    int flags;
#define HAS_ARG    0x0001
#define OPT_BOOL   0x0002
#define OPT_EXPERT 0x0004
#define OPT_STRING 0x0008
#define OPT_VIDEO  0x0010
#define OPT_AUDIO  0x0020
#define OPT_INT    0x0080
#define OPT_FLOAT  0x0100
#define OPT_SUBTITLE 0x0200
#define OPT_INT64  0x0400
#define OPT_EXIT   0x0800
#define OPT_DATA   0x1000
#define OPT_PERFILE  0x2000     /* the option is per-file (currently ffmpeg-only).
                                   implied by OPT_OFFSET or OPT_SPEC */
#define OPT_OFFSET 0x4000       /* option is specified as an offset in a passed optctx */
#define OPT_SPEC   0x8000       /* option is to be stored in an array of SpecifierOpt.
                                   Implies OPT_OFFSET. Next element after the offset is
                                   an int containing element count in the array. */
#define OPT_TIME  0x10000
#define OPT_DOUBLE 0x20000
#define OPT_INPUT  0x40000
#define OPT_OUTPUT 0x80000
     union {
        void *dst_ptr;
        int (*func_arg)(void *, const char *, const char *);
        size_t off;
    } u;
    const char *help;
    const char *argname;
} OptionDef;
typedef struct PacketQueue {
    MyAVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int64_t duration;
    int abort_request;
    int serial;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 3
#define SUBPICTURE_QUEUE_SIZE 16
#define SAMPLE_QUEUE_SIZE 9
#define FRAME_QUEUE_SIZE FFMAX(SAMPLE_QUEUE_SIZE, FFMAX(VIDEO_PICTURE_QUEUE_SIZE, SUBPICTURE_QUEUE_SIZE))

typedef struct AudioParams {
    int freq;
    int channels;
    int64_t channel_layout;
    enum AVSampleFormat fmt;
    int frame_size;
    int bytes_per_sec;
} AudioParams;

typedef struct Clock {
    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    int paused;
    int *queue_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

/* Common struct for handling all types of decoded data and allocated render buffers. */
typedef struct Frame {
    AVFrame *frame;
    AVSubtitle sub;
    int serial;
    double pts;           /* presentation timestamp for the frame */
    double duration;      /* estimated duration of the frame */
    int64_t pos;          /* byte position of the frame in the input file */
    int width;
    int height;
    int format;
    AVRational sar;
    int uploaded;
    int flip_v;
} Frame;

typedef struct FrameQueue {
    Frame queue[FRAME_QUEUE_SIZE];
    int rindex;
    int windex;
    int size;
    int max_size;
    int keep_last;
    int rindex_shown;
    SDL_mutex *mutex;
    SDL_cond *cond;
    PacketQueue *pktq;
} FrameQueue;

enum {
    AV_SYNC_AUDIO_MASTER, /* default choice */
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_CLOCK, /* synchronize to an external clock */
};

typedef struct Decoder {
    AVPacket pkt;
    PacketQueue *queue;
    AVCodecContext *avctx;
    int pkt_serial;
    int finished;
    int packet_pending;
    SDL_cond *empty_queue_cond;
    int64_t start_pts;
    AVRational start_pts_tb;
    int64_t next_pts;
    AVRational next_pts_tb;
    SDL_Thread *decoder_tid;
} Decoder;

typedef struct VideoState {
    SDL_Thread *read_tid;
    AVInputFormat *iformat;
    int abort_request;
    int force_refresh;
    int paused;
    int last_paused;
    int queue_attachments_req;
    int seek_req;
    int seek_flags;
    int64_t seek_pos;
    int64_t seek_rel;
    int read_pause_return;
    AVFormatContext *ic;
    int realtime;

    Clock audclk;
    Clock vidclk;
    Clock extclk;

    FrameQueue pictq;
    FrameQueue subpq;
    FrameQueue sampq;

    Decoder auddec;
    Decoder viddec;
    Decoder subdec;

    int audio_stream;

    int av_sync_type;

    double audio_clock;
    int audio_clock_serial;
    double audio_diff_cum; /* used for AV difference average computation */
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;
    AVStream *audio_st;
    PacketQueue audioq;
    int audio_hw_buf_size;
    uint8_t *audio_buf;
    uint8_t *audio_buf1;
    unsigned int audio_buf_size; /* in bytes */
    unsigned int audio_buf1_size;
    int audio_buf_index; /* in bytes */
    int audio_write_buf_size;
    int audio_volume;
    int muted;
    struct AudioParams audio_src;
    struct AudioParams audio_tgt;
    struct SwrContext *swr_ctx;
    int frame_drops_early;
    int frame_drops_late;

    enum ShowMode {
        SHOW_MODE_NONE = -1, SHOW_MODE_VIDEO = 0, SHOW_MODE_WAVES, SHOW_MODE_RDFT, SHOW_MODE_NB
    } show_mode;
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    int last_i_start;
    RDFTContext *rdft;
    int rdft_bits;
    FFTSample *rdft_data;
    int xpos;
    double last_vis_time;
    SDL_Texture *vis_texture;
    SDL_Texture *sub_texture;
    SDL_Texture *vid_texture;

    int subtitle_stream;
    AVStream *subtitle_st;
    PacketQueue subtitleq;

    double frame_timer;

    double frame_last_filter_delay;
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;
    double max_frame_duration;      // maximum duration of a frame - above this, we consider the jump a timestamp discontinuity
    struct SwsContext *img_convert_ctx;
    struct SwsContext *sub_convert_ctx;
    int eof;

    char *filename;
    int width, height, xleft, ytop;
    int step;

    int last_video_stream, last_audio_stream, last_subtitle_stream;

    SDL_cond *continue_read_thread;
#if CONFIG_AVFILTER
    struct AudioParams audio_filter_src;
#endif
#if CONFIG_AVFILTER
    int vfilter_idx;
    AVFilterContext *in_video_filter;   // the first filter in the video chain
    AVFilterContext *out_video_filter;  // the last filter in the video chain
    AVFilterContext *in_audio_filter;   // the first filter in the audio chain
    AVFilterContext *out_audio_filter;  // the last filter in the audio chain
    AVFilterGraph *agraph;              // audio filter graph
#endif
} VideoState;
enum PlayState
{
    EPlay=0,
    EPause,
    EStop,
    EUnknow=-1
};
class MediaPlayer;
class CVideoProcess :public boost::basic_lockable_adapter<boost::mutex>
{  
public:
    CVideoProcess(MediaPlayer*pMedia = 0);
    ~CVideoProcess();
    static int ThreadReadInfo(void *p);
    static int ThreadAuto(void *p);
    static int ThreadVideo(void *p);
    static int ThreadSubtitle(void *p);
public:
    bool init(const char *szName);
    void do_exit(SDL_Renderer *renderer=NULL,SDL_Window *window=NULL);

    VideoState *stream_open(AVInputFormat *iformat);
    int stream_component_open(int stream_index);
    void stream_close();
    void stream_component_close(int stream_index);
    int stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue);
    void stream_seek(int64_t pos, int64_t rel, int seek_by_bytes);
    void stream_seek(double incr,int seek_by_bytes);
    void set_default_window_size(int width, int height, AVRational sar);


    int get_video_frame(AVFrame *frame);
    int queue_picture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial);
     int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last);
    double vp_duration(Frame *vp, Frame *nextvp);

    void decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond);
    int decoder_start(Decoder *d, int (*fn)(void *), void *arg);
    void decoder_abort(Decoder *d, FrameQueue *fq);
    void decoder_destroy(Decoder *d);
    int decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub);

    int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial);
    void packet_queue_start(PacketQueue *q);
    void packet_queue_abort(PacketQueue *q);
    void packet_queue_destroy(PacketQueue *q);
    void packet_queue_flush(PacketQueue *q);
    int packet_queue_init(PacketQueue *q);
    int packet_queue_put_nullpacket(PacketQueue *q, int stream_index);
    int packet_queue_put(PacketQueue *q, AVPacket *pkt);
    int packet_queue_put_private(PacketQueue *q, AVPacket *pkt);
    int64_t get_valid_channel_layout(int64_t channel_layout, int channels);

    int is_realtime(AVFormatContext *s);


    void step_to_next_frame();

    int audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params);
    int audio_decode_frame();
    int synchronize_audio(int nb_samples);
    void update_sample_display(short *samples, int samples_size);


    VideoState *GetVideoState(){return is;}
    void SetMediaName(char *szName){if(szName){
        m_strMediaName = szName;}
    }
    MediaPlayer *GetMedia(){return m_pMedia;}
    void SetState(PlayState eState);
    PlayState GetState(PlayState & eState);
    float GetPlayRate(){return m_fPlayRate;}
    void SetPlayRate(float fRate);
    void EmitVideo();
    void EmitStop();
    int GetAudioStream(Uint8 *src,int & nSrc,Uint8 *dst,int & nDst);
    void SetTotalTime(int64_t nT);
    int64_t GetTotalTime(){return m_nTotalTime;}
    void SetCurTime(int64_t nT);
    int64_t GetCurTime(){return m_nCurTime;}
    SDL_AudioDeviceID GetAudioDeviceId(){return m_devAudioId;}

    static int decode_interrupt_cb(void *ctx);
    static void sdl_audio_callback(void *opaque, Uint8 *stream, int len);
    static int lockmgr(void **mtx, enum AVLockOp op);
    static void sigterm_handler(int sig);
    static void LogOutput(void *userdata,
                              int category, SDL_LogPriority priority,
                              const char *message);

private:
    QString m_strMediaName;

    AVPacket flush_pkt;
    int64_t duration;
    int64_t start_time;
    int64_t m_nCurTime;
    int64_t m_nTotalTime;
    SDL_AudioDeviceID m_devAudioId;
    int decoder_reorder_pts;
    int default_width;
    int default_height;
    int seek_by_bytes;
    int show_status;
    int infinite_buffer;
    int startup_volume;
    int loop = 1;
    int m_nDiv;
    int m_nDev;
    float m_fPlayRate;
    VideoState * is;
    char *audio_codec_name;
    char *subtitle_codec_name;
    char *video_codec_name;
     AVDictionary *format_opts, *codec_opts, *resample_opts;
    MediaPlayer *m_pMedia;
    PlayState m_eState;
    boost::mutex m_isMutex;


};

#endif // READANDDISPLAY_H
