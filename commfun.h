#ifndef CLOCKTOOL_H
#define CLOCKTOOL_H
#include <stdio.h>
#include <stdlib.h>
#include "process.h"
#include "logWrite.h"

class CommonFun
{
public:
//for all the clockFun
static void check_external_clock_speed(VideoState *is);
static double get_master_clock(VideoState *is);
static int get_master_sync_type(VideoState *is);
static void init_clock(Clock *c, int *queue_serial);
static void set_clock_speed(Clock *c, double speed);
static void set_clock(Clock *c, double pts, int serial);
static void set_clock_at(Clock *c, double pts, int serial, double time);
static double get_clock(Clock *c);
static void sync_clock_to_slave(Clock *c, Clock *slave);
//for all the rectFun
static void calculate_display_rect(SDL_Rect *rect,
                                   int scr_xleft, int scr_ytop, int scr_width, int scr_height,
                                   int pic_width, int pic_height, AVRational pic_sar);
//for all the frameFun
static int64_t frame_queue_last_pos(FrameQueue *f);
static int frame_queue_nb_remaining(FrameQueue *f);
static void frame_queue_next(FrameQueue *f);
static void frame_queue_push(FrameQueue *f);
static Frame *frame_queue_peek_readable(FrameQueue *f);
static Frame *frame_queue_peek_writable(FrameQueue *f);
static Frame *frame_queue_peek_last(FrameQueue *f);
static Frame *frame_queue_peek(FrameQueue *f);
static int get_max_dev(int n1,int n2);
static Frame *frame_queue_peek_next(FrameQueue *f);
static void frame_queue_signal(FrameQueue *f);
static void frame_queue_destory(FrameQueue *f);
static int frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last);
static void frame_queue_unref_item(Frame *vp);
static void stream_toggle_pause(VideoState *is);

static void stream_close(VideoState *is);
static void init_dynload(void);
static void init_opts(void);
static void uninit_opts(void);
static AVDictionary **setup_find_stream_info_opts(AVFormatContext *s,AVDictionary *codec_opts);
static AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                       AVFormatContext *s, AVStream *st, AVCodec *codec);
static int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);
static int64_t get_valid_channel_layout(int64_t channel_layout, int channels);
static int opt_add_vfilter(void *optctx, const char *opt, const char *arg);
static void exit_program(int ret);
static void *grow_array(void *array, int elem_size, int *size, int new_size);

#if CONFIG_AVFILTER
static double get_rotation(AVStream *st);
static int configure_audio_filters(VideoState *is, const char *afilters, int force_output_format);
static int configure_video_filters(AVFilterGraph *graph, VideoState *is, const char *vfilters, AVFrame *frame);
static int configure_filtergraph(AVFilterGraph *graph, const char *filtergraph,                                      AVFilterContext *source_ctx, AVFilterContext *sink_ctx);

#endif

static int framedrop;
static double rdftspeed;
static int64_t audio_callback_time;
static int seek_by_bytes;
static unsigned sws_flags;
static AVDictionary *sws_dict;
static AVDictionary *swr_opts;
static AVDictionary *format_opts, *codec_opts, *resample_opts;
static int autorotate;
#if CONFIG_AVFILTER
static  const char **vfilters_list;
static int nb_vfilters;
static char *afilters;
#endif
}
;

#endif // CLOCKTOOL_H
