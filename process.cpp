//#include <libavfilter/avfilter.h>
#include "process.h"
#include "display.h"
#include "commfun.h"
#include "mediaplayer.h"
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)
static const char* wanted_stream_spec[AVMEDIA_TYPE_NB] = {0};
extern CWriteLog LOG;
CVideoProcess::CVideoProcess(MediaPlayer *pMedia):
is(NULL)
,format_opts(NULL)
,codec_opts(NULL)
,resample_opts(NULL)
,m_pMedia(pMedia)
,m_eState(EUnknow)
,m_fPlayRate(1.0)
,m_nDiv(10)
,m_nDev(10)
,m_nCurTime(0)
,m_nTotalTime(0)

{
    loop = 1;
    default_width  = 640;
    default_height = 480;
    startup_volume = 100;
   // m_strMediaName("");
    memset(&flush_pkt,0,sizeof(AVPacket));
    decoder_reorder_pts = -1;
    seek_by_bytes = -1;
    show_status = 1;
    infinite_buffer = -1;
    duration = AV_NOPTS_VALUE;
    start_time = AV_NOPTS_VALUE;
    audio_codec_name = NULL;
    subtitle_codec_name = NULL;
    video_codec_name = NULL;
    LOG.Log(InfoLog,"VideoProcess Instance");

}
CVideoProcess::~CVideoProcess()
{
    LOG.Log(DebugLog,"VideoProcess deconstructor");
}

int CVideoProcess::is_realtime(AVFormatContext *s)
{
    if(   !strcmp(s->iformat->name, "rtp")
       || !strcmp(s->iformat->name, "rtsp")
       || !strcmp(s->iformat->name, "sdp")
    )
    {
        LOG.Log(InfoLog,"is realtime format:%s",s->iformat->name);
        return 1;
    }

    if(s->pb && (   !strncmp(s->filename, "rtp:", 4)
                 || !strncmp(s->filename, "udp:", 4)
                )
    )
    {
        LOG.Log(InfoLog,"is realtime format:%s",s->iformat->name);
        return 1;
    }
    LOG.Log(InfoLog,"isnot realtime format:%s",s->iformat->name);
    return 0;
}
int CVideoProcess::decoder_start(Decoder *d, int (*fn)(void *), void *arg)
{
    packet_queue_start(d->queue);

    d->decoder_tid = SDL_CreateThread(fn, "decoder", arg);
    if (!d->decoder_tid) {
        LOG.Log(ErrLog, "SDL_CreateThread(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    return 0;
}
int CVideoProcess::stream_has_enough_packets(AVStream *st, int stream_id, PacketQueue *queue) {
    return stream_id < 0 ||
           queue->abort_request ||
           (st->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
           queue->nb_packets > MIN_FRAMES && (!queue->duration || av_q2d(st->time_base) * queue->duration > 1.0);
}
int CVideoProcess::frame_queue_init(FrameQueue *f, PacketQueue *pktq, int max_size, int keep_last)
{
    int i;
    memset(f, 0, sizeof(FrameQueue));
    if (!(f->mutex = SDL_CreateMutex())) {
        LOG.Log(ErrLog, "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    if (!(f->cond = SDL_CreateCond())) {
        LOG.Log(ErrLog,  "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    f->pktq = pktq;
    f->max_size = FFMIN(max_size, FRAME_QUEUE_SIZE);
    f->keep_last = !!keep_last;
    for (i = 0; i < f->max_size; i++)
        if (!(f->queue[i].frame = av_frame_alloc()))
            return AVERROR(ENOMEM);
    return 0;
}
VideoState *CVideoProcess::stream_open(AVInputFormat *iformat)
{

    is = (VideoState *)av_mallocz(sizeof(VideoState));

    if (!is)
    {
        LOG.Log(ErrLog,"malloc VideoState failed in stream_open name:%s",m_strMediaName.toStdString().c_str());
        return NULL;
    }
    memset(is,0,sizeof(VideoState));
    is->filename = av_strdup(m_strMediaName.toStdString().c_str());
    if (!is->filename)
    {
        LOG.Log(ErrLog,"filename is NULL in stream_open");
        goto fail;
    }
    is->iformat = iformat;
    is->ytop    = 0;
    is->xleft   = 0;
    LOG.Log(DebugLog,"stream open");
    /* start video display */
    if (frame_queue_init(&is->pictq, &is->videoq, VIDEO_PICTURE_QUEUE_SIZE, 1) < 0)
    {
        LOG.Log(ErrLog,"frame_queue_init video failed");
        goto fail;
    }
    if (frame_queue_init(&is->subpq, &is->subtitleq, SUBPICTURE_QUEUE_SIZE, 0) < 0)
    {
        LOG.Log(ErrLog,"frame_queue_init subtitle failed");
        goto fail;
    }
    if (frame_queue_init(&is->sampq, &is->audioq, SAMPLE_QUEUE_SIZE, 1) < 0)
    {
        LOG.Log(ErrLog,"frame_queue_init audio failed");
        goto fail;
    }

    if (packet_queue_init(&is->videoq) < 0 ||
        packet_queue_init(&is->audioq) < 0 ||
        packet_queue_init(&is->subtitleq) < 0)
    {
        LOG.Log(ErrLog,"packet_queue_init  failed");
        goto fail;
    }

    if (!(is->continue_read_thread = SDL_CreateCond())) {
        LOG.Log(ErrLog,  "SDL_CreateCond(): %s\n", SDL_GetError());
        goto fail;
    }

    CommonFun::init_clock(&is->vidclk, &is->videoq.serial);
    CommonFun::init_clock(&is->audclk, &is->audioq.serial);
    CommonFun::init_clock(&is->extclk, &is->extclk.serial);
    is->audio_clock_serial = -1;
    if (startup_volume < 0)
        LOG.Log(WarnLog, "-volume=%d < 0, setting to 0", startup_volume);
    if (startup_volume > 100)
        LOG.Log(WarnLog, "-volume=%d > 100, setting to 100", startup_volume);
    startup_volume = av_clip(startup_volume, 0, 100);
    startup_volume = av_clip(SDL_MIX_MAXVOLUME * startup_volume / 100, 0, SDL_MIX_MAXVOLUME);
    is->audio_volume = startup_volume;
    is->muted = 0;
    is->av_sync_type = AV_SYNC_AUDIO_MASTER;

    is->read_tid     = SDL_CreateThread(ThreadReadInfo, "read_thread", this);
    if (!is->read_tid) {
        LOG.Log(ErrLog, "SDL_CreateThread(): %s\n", SDL_GetError());
fail:
        stream_close();
        return NULL;
    }
    LOG.Log(InfoLog," stream_open success name:%s",m_strMediaName.toStdString().c_str());
    return is;
}
void CVideoProcess::stream_seek( int64_t pos, int64_t rel, int seek_by_bytes)
{
   if (!is->seek_req) {
       is->seek_pos = pos;
       is->seek_rel = rel;
       is->seek_flags &= ~AVSEEK_FLAG_BYTE;
       if (seek_by_bytes)
           is->seek_flags |= AVSEEK_FLAG_BYTE;
       is->seek_req = 1;
       SDL_CondSignal(is->continue_read_thread);
   }
}
void CVideoProcess::stream_seek(double incr,int seek_by_bytes)
{
    double pos = 0.0;
    if (seek_by_bytes)
    {
        pos = -1;
        if (pos < 0 && is->video_stream >= 0)
         pos = CommonFun::frame_queue_last_pos(&is->pictq);
        if (pos < 0 && is->audio_stream >= 0)
         pos = CommonFun::frame_queue_last_pos(&is->sampq);
        if (pos < 0)
         pos = avio_tell(is->ic->pb);
        if (is->ic->bit_rate)
         incr *= is->ic->bit_rate / 8.0;
        else
         incr *= 180000.0;
        pos += incr;
        stream_seek(pos, incr, 1);
    }
    else
    {
        pos = CommonFun::get_master_clock(is);
        if (isnan(pos))
         pos = (double)is->seek_pos / AV_TIME_BASE;
        pos += incr;
        if (is->ic->start_time != AV_NOPTS_VALUE && pos < is->ic->start_time / (double)AV_TIME_BASE)
         pos = is->ic->start_time / (double)AV_TIME_BASE;
        stream_seek((int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
    }
}
void CVideoProcess::step_to_next_frame()
{
    /* if the stream is paused unpause it, then step */
    if (is->paused)
        CommonFun::stream_toggle_pause(is);
    is->step = 1;
}
int CVideoProcess::decode_interrupt_cb(void *ctx)
{
    VideoState *is = (VideoState *)ctx;
    return is->abort_request;
}
int CVideoProcess::ThreadReadInfo(void *p)
{

    CVideoProcess *pInstance = (CVideoProcess *)p;
    if(NULL == pInstance)
    {
        LOG.Log(ErrLog,"VideoProcess is NULL in ThreadRead");
        return 0;
    }
    LOG.Log(InfoLog,"enter ThreadRead");
    VideoState *is = pInstance->GetVideoState();
      AVFormatContext *ic = NULL;
      int err, ret;
      int st_index[AVMEDIA_TYPE_NB];
      AVPacket pkt1, *pkt = &pkt1;
      int64_t stream_start_time;
      int pkt_in_play_range = 0;
      AVDictionaryEntry *t;
      SDL_mutex *wait_mutex = SDL_CreateMutex();
      int scan_all_pmts_set = 0;
      int64_t pkt_ts;

      if (!wait_mutex) {
          LOG.Log(ErrLog,  "SDL_CreateMutex(): %s\n", SDL_GetError());
          ret = AVERROR(ENOMEM);
          goto fail;
      }

      memset(st_index, -1, sizeof(st_index));
      is->last_video_stream = is->video_stream = -1;
      is->last_audio_stream = is->audio_stream = -1;
      is->last_subtitle_stream = is->subtitle_stream = -1;
      is->eof = 0;

      ic = avformat_alloc_context();
      if (!ic) {
          LOG.Log(ErrLog,  "Could not allocate context.\n");
          ret = AVERROR(ENOMEM);
          goto fail;
      }
      ic->interrupt_callback.callback = decode_interrupt_cb;
      ic->interrupt_callback.opaque = is;
      if (!av_dict_get(pInstance->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
          av_dict_set(&pInstance->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
          scan_all_pmts_set = 1;
      }
      err = avformat_open_input(&ic, is->filename, is->iformat, &pInstance->format_opts);
      if (err < 0) {
          //print_error(is->filename, err);
          LOG.Log(ErrLog,"avformat_open_input failed in ThreadRead name:%s",is->filename);
          ret = -1;
          goto fail;
      }
      LOG.Log(InfoLog,"avformat_open_input:%s success",is->filename);
      if (scan_all_pmts_set)
          av_dict_set(&pInstance->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

      if ((t = av_dict_get(pInstance->format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
          LOG.Log(ErrLog,  "Option %s not found.\n", t->key);
          ret = AVERROR_OPTION_NOT_FOUND;
          goto fail;
      }
      is->ic = ic;

    //if (genpts)
        ic->flags |= AVFMT_FLAG_GENPTS;

      av_format_inject_global_side_data(ic);

    //if (find_stream_info)
    {
        AVDictionary **opts = CommonFun::setup_find_stream_info_opts(ic, pInstance->codec_opts);
        int orig_nb_streams = ic->nb_streams;

          err = avformat_find_stream_info(ic, opts);

          for (int i = 0; i < orig_nb_streams; i++)
              av_dict_free(&opts[i]);
          av_freep(&opts);

          if (err < 0) {
              LOG.Log(WarnLog,
                     "%s: could not find codec parameters\n", is->filename);
              ret = -1;
              goto fail;
          }
    }

      if (ic->pb)
          ic->pb->eof_reached = 0; // FIXME hack, ffplay maybe should not use avio_feof() to test for the end

    if (pInstance->seek_by_bytes < 0)
        pInstance->seek_by_bytes = !!(ic->iformat->flags & AVFMT_TS_DISCONT) && strcmp("ogg", ic->iformat->name);

      is->max_frame_duration = (ic->iformat->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;

    //if (!window_title && (t = av_dict_get(ic->metadata, "title", NULL, 0)))
     //   window_title = av_asprintf("%s - %s", t->value, input_filename);

    if (pInstance->start_time != AV_NOPTS_VALUE) {
        int64_t timestamp;

        timestamp = pInstance->start_time;
        if (ic->start_time != AV_NOPTS_VALUE)
            timestamp += ic->start_time;
        ret = avformat_seek_file(ic, -1, INT64_MIN, timestamp, INT64_MAX, 0);
        if (ret < 0) {
            LOG.Log(WarnLog, "%s: could not seek to position %0.3f\n",
                    is->filename, (double)timestamp / AV_TIME_BASE);
        }
    }

      is->realtime = pInstance->is_realtime(ic);

    if (pInstance->show_status)
    {
        LOG.Log(DebugLog,"file name:%s  before av_dump_format",is->filename);
        av_dump_format(ic, 0, is->filename, 0);
        LOG.Log(DebugLog,"file name:%s  after av_dump_format",is->filename);
    }

    for (int i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        enum AVMediaType type = st->codecpar->codec_type;
        st->discard = AVDISCARD_ALL;
        if (type >= 0 && wanted_stream_spec[type] && st_index[type] == -1)
            if (avformat_match_stream_specifier(ic,st, wanted_stream_spec[type]) > 0)
                st_index[type] = i;
    }
    for (int i = 0; i < AVMEDIA_TYPE_NB; i++) {
        if (wanted_stream_spec[i] && st_index[i] == -1) {
            LOG.Log(ErrLog, "Stream specifier %s does not match any %s stream\n", wanted_stream_spec[i], av_get_media_type_string((AVMediaType)i));
            st_index[i] = INT_MAX;
        }
    }
    av_estimate_time(ic,avio_tell(ic->pb));

   // if (!video_disable)
    pInstance->SetTotalTime(ic->duration/AV_TIME_BASE);
          st_index[AVMEDIA_TYPE_VIDEO] =
              av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO,
                                  st_index[AVMEDIA_TYPE_VIDEO], -1, NULL, 0);
   // if (!audio_disable)
          st_index[AVMEDIA_TYPE_AUDIO] =
              av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO,
                                  st_index[AVMEDIA_TYPE_AUDIO],
                                  st_index[AVMEDIA_TYPE_VIDEO],
                                  NULL, 0);
  //  if (!video_disable && !subtitle_disable)
          st_index[AVMEDIA_TYPE_SUBTITLE] =
              av_find_best_stream(ic, AVMEDIA_TYPE_SUBTITLE,
                                  st_index[AVMEDIA_TYPE_SUBTITLE],
                                  (st_index[AVMEDIA_TYPE_AUDIO] >= 0 ?
                                   st_index[AVMEDIA_TYPE_AUDIO] :
                                   st_index[AVMEDIA_TYPE_VIDEO]),
                                  NULL, 0);


      if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
          AVStream *st = ic->streams[st_index[AVMEDIA_TYPE_VIDEO]];
          AVCodecParameters *codecpar = st->codecpar;
          AVRational sar = av_guess_sample_aspect_ratio(ic, st, NULL);
          if (codecpar->width)
              pInstance->set_default_window_size(codecpar->width, codecpar->height, sar);
      }

      /* open the streams */
      if (st_index[AVMEDIA_TYPE_AUDIO] >= 0) {
          pInstance->stream_component_open(st_index[AVMEDIA_TYPE_AUDIO]);
      }

      ret = -1;
      if (st_index[AVMEDIA_TYPE_VIDEO] >= 0) {
          ret = pInstance->stream_component_open(st_index[AVMEDIA_TYPE_VIDEO]);
      }


      if (st_index[AVMEDIA_TYPE_SUBTITLE] >= 0) {
          pInstance->stream_component_open( st_index[AVMEDIA_TYPE_SUBTITLE]);
      }

      if (is->video_stream < 0 && is->audio_stream < 0) {
          LOG.Log(ErrLog,  "Failed to open file '%s' or configure filtergraph\n",
                 is->filename);
          ret = -1;
          goto fail;
      }

      if (pInstance->infinite_buffer < 0 && is->realtime)
          pInstance->infinite_buffer = 1;
      PlayState eState = EUnknow;
      for (;;) {
          pInstance->GetState(eState);
          //if (is->abort_request)
          if(EStop == eState)
              break;
          if (is->paused != is->last_paused) {
              is->last_paused = is->paused;
              if (is->paused)
                  is->read_pause_return = av_read_pause(ic);
              else
                  av_read_play(ic);
          }
  #if CONFIG_RTSP_DEMUXER || CONFIG_MMSH_PROTOCOL
          if (is->paused &&
                  (!strcmp(ic->iformat->name, "rtsp") ||
                   (ic->pb && !strncmp(input_filename, "mmsh:", 5)))) {
              /* wait 10 ms to avoid trying to get another packet */
              /* XXX: horrible */
              SDL_Delay(10);
              continue;
          }
  #endif
          if (is->seek_req) {
              int64_t seek_target = is->seek_pos;
              int64_t seek_min    = is->seek_rel > 0 ? seek_target - is->seek_rel + 2: INT64_MIN;
              int64_t seek_max    = is->seek_rel < 0 ? seek_target - is->seek_rel - 2: INT64_MAX;
  // FIXME the +-2 is due to rounding being not done in the correct direction in generation
  //      of the seek_pos/seek_rel variables

              ret = avformat_seek_file(is->ic, -1, seek_min, seek_target, seek_max, is->seek_flags);
              if (ret < 0) {
                  LOG.Log(ErrLog,
                         "%s: error while seeking\n", is->ic->filename);
              } else {
                  if (is->audio_stream >= 0) {
                      pInstance->packet_queue_flush(&is->audioq);
                      pInstance->packet_queue_put(&is->audioq, &(pInstance->flush_pkt));
                  }
                  if (is->subtitle_stream >= 0) {
                      pInstance->packet_queue_flush(&is->subtitleq);
                      pInstance->packet_queue_put(&is->subtitleq, &(pInstance->flush_pkt));
                  }
                  if (is->video_stream >= 0) {
                      pInstance->packet_queue_flush(&is->videoq);
                      pInstance->packet_queue_put(&is->videoq, &(pInstance->flush_pkt));
                  }
                  if (is->seek_flags & AVSEEK_FLAG_BYTE) {
                     CommonFun::set_clock(&is->extclk, NAN, 0);
                  } else {
                     CommonFun::set_clock(&is->extclk, seek_target / (double)AV_TIME_BASE, 0);
                  }
              }
              is->seek_req = 0;
              is->queue_attachments_req = 1;
              is->eof = 0;
              if (is->paused)
                  pInstance->step_to_next_frame();
          }
          if (is->queue_attachments_req) {
              if (is->video_st && is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                  AVPacket copy;
                  if ((ret = av_copy_packet(&copy, &is->video_st->attached_pic)) < 0)
                      goto fail;
                  pInstance->packet_queue_put(&is->videoq, &copy);
                  pInstance->packet_queue_put_nullpacket(&is->videoq, is->video_stream);
              }
              is->queue_attachments_req = 0;
          }

          /* if the queue are full, no need to read more */
          if (pInstance->infinite_buffer<1 &&
                (is->audioq.size + is->videoq.size + is->subtitleq.size > MAX_QUEUE_SIZE
              || (pInstance->stream_has_enough_packets(is->audio_st, is->audio_stream, &is->audioq) &&
                  pInstance->stream_has_enough_packets(is->video_st, is->video_stream, &is->videoq) &&
                  pInstance->stream_has_enough_packets(is->subtitle_st, is->subtitle_stream, &is->subtitleq)))) {
              /* wait 10 ms */
              SDL_LockMutex(wait_mutex);
              SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
              SDL_UnlockMutex(wait_mutex);
              continue;
          }
        if (!is->paused &&
            (!is->audio_st || (is->auddec.finished == is->audioq.serial && CommonFun::frame_queue_nb_remaining(&is->sampq) == 0)) &&
            (!is->video_st || (is->viddec.finished == is->videoq.serial && CommonFun::frame_queue_nb_remaining(&is->pictq) == 0)))
        {
            if (pInstance->loop != 1 && (!pInstance->loop || --pInstance->loop)) {
                pInstance->stream_seek(pInstance->start_time != AV_NOPTS_VALUE ? pInstance->start_time : 0, 0, 0);
            }
            else// if (autoexit)
            {
                ret = AVERROR_EOF;
                goto fail;
            }
        }
          ret = av_read_frame(ic, pkt);
          if (ret < 0) {
              if ((ret == AVERROR_EOF || avio_feof(ic->pb)) && !is->eof) {
                  if (is->video_stream >= 0)
                      pInstance->packet_queue_put_nullpacket(&is->videoq, is->video_stream);
                  if (is->audio_stream >= 0)
                      pInstance->packet_queue_put_nullpacket(&is->audioq, is->audio_stream);
                  if (is->subtitle_stream >= 0)
                      pInstance->packet_queue_put_nullpacket(&is->subtitleq, is->subtitle_stream);
                  is->eof = 1;
              }
              if (ic->pb && ic->pb->error)
                  break;
              SDL_LockMutex(wait_mutex);
              SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
              SDL_UnlockMutex(wait_mutex);
              continue;
          } else {
              is->eof = 0;
          }
          /* check if packet is in play range specified by user, then queue, otherwise discard */
          stream_start_time = ic->streams[pkt->stream_index]->start_time;
          pkt_ts = pkt->pts == AV_NOPTS_VALUE ? pkt->dts : pkt->pts;
          pkt_in_play_range = pInstance->duration == AV_NOPTS_VALUE ||
                  (pkt_ts - (stream_start_time != AV_NOPTS_VALUE ? stream_start_time : 0)) *
                  av_q2d(ic->streams[pkt->stream_index]->time_base) -
                  (double)(pInstance->start_time != AV_NOPTS_VALUE ? pInstance->start_time : 0) / 1000000
                  <= ((double)pInstance->duration / 1000000);
          if (pkt->stream_index == is->audio_stream && pkt_in_play_range) {
              pInstance->packet_queue_put(&is->audioq, pkt);
          } else if (pkt->stream_index == is->video_stream && pkt_in_play_range
                     && !(is->video_st->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
              pInstance->packet_queue_put(&is->videoq, pkt);
          } else if (pkt->stream_index == is->subtitle_stream && pkt_in_play_range) {
              pInstance->packet_queue_put(&is->subtitleq, pkt);
          } else {
              av_packet_unref(pkt);
          }
      }

      ret = 0;
   fail:
      if (ic && !is->ic)
          avformat_close_input(&ic);

      if (ret != 0) {
          SDL_Event event;
          LOG.Log(InfoLog,"Quit Event in ThreadRead");
          event.type = FF_QUIT_EVENT;
          event.user.data1 = is;
          SDL_PushEvent(&event);
      }
      LOG.Log(InfoLog,"end of ThreadRead");
      SDL_DestroyMutex(wait_mutex);
      return 0;
}
int CVideoProcess::ThreadAuto(void *p)
{
    if(NULL == p)
    {
        LOG.Log(ErrLog,"Param is NULL in ThreadAuto");
        return 0;
    }

    CVideoProcess *pInstance = (CVideoProcess *)p;
    if(NULL == pInstance)
    {
        LOG.Log(ErrLog,"VideoProcess is NULL in ThreadAuto");
        return 0;
    }
    VideoState *is = pInstance->GetVideoState();
    LOG.Log(InfoLog,"enter audioThread");
      AVFrame *frame = av_frame_alloc();
      Frame *af;
  #if CONFIG_AVFILTER
      int last_serial = -1;
      int64_t dec_channel_layout;
      int reconfigure;
  #endif
      int got_frame = 0;
      AVRational tb;
      int ret = 0;

      if (!frame)
      {
          LOG.Log(ErrLog,"frame is NULL in ThreadAuto");
          return AVERROR(ENOMEM);
      }
      PlayState eState = EUnknow;
      do {
          //if (is->abort_request)
          if(EStop == eState)
              break;
          if ((got_frame = pInstance->decoder_decode_frame(&is->auddec, frame, NULL)) < 0)
              goto the_end;

          if (got_frame) {
                  tb.num = 1;
                  tb.den = frame->sample_rate;//= (AVRational){1, frame->sample_rate};

  #if CONFIG_AVFILTER
                  dec_channel_layout = CommonFun::get_valid_channel_layout(frame->channel_layout, frame->channels);

                  reconfigure =
                      pInstance->cmp_audio_fmts(is->audio_filter_src.fmt, is->audio_filter_src.channels,
                                     (AVSampleFormat)frame->format, frame->channels)    ||
                      is->audio_filter_src.channel_layout != dec_channel_layout ||
                      is->audio_filter_src.freq           != frame->sample_rate ||
                      is->auddec.pkt_serial               != last_serial;

                  if (reconfigure) {
                      char buf1[1024], buf2[1024];
                      av_get_channel_layout_string(buf1, sizeof(buf1), -1, is->audio_filter_src.channel_layout);
                      av_get_channel_layout_string(buf2, sizeof(buf2), -1, dec_channel_layout);
                      LOG.Log(DebugLog,
                             "Audio frame changed from rate:%d ch:%d fmt:%s layout:%s serial:%d to rate:%d ch:%d fmt:%s layout:%s serial:%d\n",
                             is->audio_filter_src.freq, is->audio_filter_src.channels, av_get_sample_fmt_name(is->audio_filter_src.fmt), buf1, last_serial,
                             frame->sample_rate, frame->channels, av_get_sample_fmt_name((AVSampleFormat)frame->format), buf2, is->auddec.pkt_serial);

                      is->audio_filter_src.fmt            = (AVSampleFormat)frame->format;
                      is->audio_filter_src.channels       = frame->channels;
                      is->audio_filter_src.channel_layout = dec_channel_layout;
                      is->audio_filter_src.freq           = frame->sample_rate;
                      last_serial                         = is->auddec.pkt_serial;

                      if ((ret = CommonFun::configure_audio_filters(is, CommonFun::afilters, 1)) < 0)
                          goto the_end;
                  }

              if ((ret = av_buffersrc_add_frame(is->in_audio_filter, frame)) < 0)
                  goto the_end;

              while ((ret = av_buffersink_get_frame_flags(is->out_audio_filter, frame, 0)) >= 0) {
                  tb = av_buffersink_get_time_base(is->out_audio_filter);
  #endif
                  if (!(af = CommonFun::frame_queue_peek_writable(&is->sampq)))
                      goto the_end;

                  af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
                  af->pos = frame->pkt_pos;
                  af->serial = is->auddec.pkt_serial;
                  AVRational tempR;
                  tempR.num = frame->nb_samples;
                  tempR.den = frame->sample_rate;
                  af->duration = av_q2d(tempR);

                  av_frame_move_ref(af->frame, frame);
                  CommonFun::frame_queue_push(&is->sampq);

  #if CONFIG_AVFILTER
                  if (is->audioq.serial != is->auddec.pkt_serial)
                      break;
              }
              if (ret == AVERROR_EOF)
                  is->auddec.finished = is->auddec.pkt_serial;
  #endif
          }
      } while (ret >= 0 || ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
   the_end:
  #if CONFIG_AVFILTER
      avfilter_graph_free(&is->agraph);
      LOG.Log(InfoLog,"go to the end in audioThread");
  #endif
      av_frame_free(&frame);
      LOG.Log(InfoLog,"go to the end in audioThread");
      return ret;
}
int CVideoProcess::ThreadVideo(void *p)
{
    if(NULL == p)
    {
        LOG.Log(ErrLog,"Param is NULL in ThreadVideo");
        return 0;
    }
    CVideoProcess *pInstance = (CVideoProcess *)p;
    if(NULL == pInstance)
    {
        LOG.Log(ErrLog,"VideoProcess is NULL in ThreadVideo");
        return 0;
    }
    VideoState *is = pInstance->GetVideoState();

    LOG.Log(InfoLog,"enter videoThread");
     AVFrame *frame = av_frame_alloc();
     double pts;
     double duration;
     int ret;
     AVRational tb = is->video_st->time_base;
     AVRational frame_rate = av_guess_frame_rate(is->ic, is->video_st, NULL);
     LOG.Log(DebugLog,"frame_rate:num:%d den:%d",frame_rate.num,frame_rate.den);
 #if CONFIG_AVFILTER
     AVFilterGraph *graph = avfilter_graph_alloc();
     AVFilterContext *filt_out = NULL, *filt_in = NULL;
     int last_w = 0;
     int last_h = 0;
     enum AVPixelFormat last_format = (AVPixelFormat)-2;
     int last_serial = -1;
     int last_vfilter_idx = 0;
     if (!graph) {
         av_frame_free(&frame);
         return AVERROR(ENOMEM);
     }

 #endif

     if (!frame) {
 #if CONFIG_AVFILTER
         avfilter_graph_free(&graph);
 #endif
         LOG.Log(ErrLog,"frame is NULL in ThreadVideo");
         return AVERROR(ENOMEM);
     }
     bool bCreateDisplayThread = false;
     PlayState eState = EUnknow;
     for (;;)
     {
         pInstance->GetState(eState);
         //if (is->abort_request)
         if(EStop == eState)
             break;
         LOG.Log(DebugLog,"get_video_frame");
         ret = pInstance->get_video_frame(frame);
         LOG.Log(DebugLog,"get_video_frame ret:%d",ret);
         if (ret < 0)
         {
             LOG.Log(DebugLog,"ret <0 go to the_end");
             goto the_end;
         }
         if (!ret)
         {

             continue;
         }

 #if CONFIG_AVFILTER
         if (   last_w != frame->width
             || last_h != frame->height
             || last_format != frame->format
             || last_serial != is->viddec.pkt_serial
             || last_vfilter_idx != is->vfilter_idx) {
             LOG.Log(DebugLog,
                    "Video frame changed from size:%dx%d format:%s serial:%d to size:%dx%d format:%s serial:%d\n",
                    last_w, last_h,
                    (const char *)av_x_if_null(av_get_pix_fmt_name(last_format), "none"), last_serial,
                    frame->width, frame->height,
                    (const char *)av_x_if_null(av_get_pix_fmt_name((AVPixelFormat)frame->format), "none"), is->viddec.pkt_serial);
             avfilter_graph_free(&graph);
             graph = avfilter_graph_alloc();
             if ((ret = CommonFun::configure_video_filters(graph, is, CommonFun::vfilters_list ? CommonFun::vfilters_list[is->vfilter_idx] : NULL, frame)) < 0) {
                 SDL_Event event;
                 LOG.Log(ErrLog,"VideoThread Quit Event");
                 event.type = FF_QUIT_EVENT;
                 event.user.data1 = is;
                 SDL_PushEvent(&event);
                 goto the_end;
             }
             filt_in  = is->in_video_filter;
             filt_out = is->out_video_filter;
             last_w = frame->width;
             last_h = frame->height;
             last_format = (AVPixelFormat)frame->format;
             last_serial = is->viddec.pkt_serial;
             last_vfilter_idx = is->vfilter_idx;
             frame_rate = av_buffersink_get_frame_rate(filt_out);
         }

         ret = av_buffersrc_add_frame(filt_in, frame);
         if (ret < 0)
             goto the_end;

         while (ret >= 0) {
             is->frame_last_returned_time = av_gettime_relative() / 1000000.0;

             ret = av_buffersink_get_frame_flags(filt_out, frame, 0);
             if (ret < 0) {
                 if (ret == AVERROR_EOF)
                     is->viddec.finished = is->viddec.pkt_serial;
                 ret = 0;
                 break;
             }

             is->frame_last_filter_delay = av_gettime_relative() / 1000000.0 - is->frame_last_returned_time;
             if (fabs(is->frame_last_filter_delay) > AV_NOSYNC_THRESHOLD / 10.0)
                 is->frame_last_filter_delay = 0;
             tb = av_buffersink_get_time_base(filt_out);
 #endif
             AVRational tempR;
             tempR.num = frame_rate.den;
             tempR.den = frame_rate.num;


             duration = (frame_rate.num && frame_rate.den ? av_q2d(tempR) : 0);
             pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(tb);
             LOG.Log(DebugLog,"queue_picture duration:%f pts:%f ",duration,pts);
             ret = pInstance->queue_picture(frame, pts, duration, frame->pkt_pos, is->viddec.pkt_serial);
             LOG.Log(DebugLog,"nb_extend_buf:%d nb_elements:%d",frame->nb_extended_buf,sizeof(frame->buf)/sizeof((frame->buf)[0]));
             av_frame_unref(frame);
             /*if(false == bCreateDisplayThread)
             {
                  SDL_CreateThread(ThreadDisplay,"event_display",p);//SDL_CreateThread(ThreadEvent,"event_thread",this);
                  bCreateDisplayThread = true;
             }*/


 #if CONFIG_AVFILTER
         }
 #endif

         if (ret < 0)
         {
             LOG.Log(DebugLog,"go to the_end");
             goto the_end;
         }
     }
  the_end:
 #if CONFIG_AVFILTER
     avfilter_graph_free(&graph);
     LOG.Log(InfoLog,"go to the end in threadvideo");
 #endif
     av_frame_free(&frame);
     LOG.Log(InfoLog,"go to the end in threadvideo");
     return 0;
}

int CVideoProcess::ThreadSubtitle(void *p)
{
    if(NULL == p)
    {
        LOG.Log(ErrLog,"param is NULL in ThreadSubtitle");
        return 0;
    }
    CVideoProcess *pInstance = (CVideoProcess *)p;
    if(NULL == pInstance)
    {
        LOG.Log(ErrLog,"VideoProcess is NULL in ThreadSubtitle");
        return 0;
    }
    VideoState *is = pInstance->GetVideoState();
     Frame *sp;
     int got_subtitle;
     double pts;

     for (;;) {
         if (is->abort_request)
             break;
         if (!(sp = CommonFun::frame_queue_peek_writable(&is->subpq)))
         {
             LOG.Log(ErrLog,"Frame is NULL revoke frame_queue_peek_writable in ThreadSubtitle");
             return 0;
         }

         if ((got_subtitle = pInstance->decoder_decode_frame(&is->subdec, NULL, &sp->sub)) < 0)
             break;

         pts = 0;

         if (got_subtitle && sp->sub.format == 0) {
             if (sp->sub.pts != AV_NOPTS_VALUE)
                 pts = sp->sub.pts / (double)AV_TIME_BASE;
             sp->pts = pts;
             sp->serial = is->subdec.pkt_serial;
             sp->width = is->subdec.avctx->width;
             sp->height = is->subdec.avctx->height;
             sp->uploaded = 0;

             /* now we can update the picture count */
             CommonFun::frame_queue_push(&is->subpq);
         } else if (got_subtitle) {
             avsubtitle_free(&sp->sub);
         }
     }
     LOG.Log(InfoLog,"end of thread subtitle");
     return 0;
}
int CVideoProcess::synchronize_audio(int nb_samples)
{
    int wanted_nb_samples = nb_samples;

    /* if not master, then we try to remove or add samples to correct the clock */
    if (CommonFun::get_master_sync_type(is) != AV_SYNC_AUDIO_MASTER)
    {
        double diff, avg_diff;
        int min_nb_samples, max_nb_samples;

        diff = CommonFun::get_clock(&is->audclk) - CommonFun::get_master_clock(is);

        if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD)
        {
            is->audio_diff_cum = diff + is->audio_diff_avg_coef * is->audio_diff_cum;
            if (is->audio_diff_avg_count < AUDIO_DIFF_AVG_NB)
            {
                /* not enough measures to have a correct estimate */
                is->audio_diff_avg_count++;
            }
            else
            {
                /* estimate the A-V difference */
                avg_diff = is->audio_diff_cum * (1.0 - is->audio_diff_avg_coef);

                if (fabs(avg_diff) >= is->audio_diff_threshold)
                {
                    wanted_nb_samples = nb_samples + (int)(diff * is->audio_src.freq);
                    min_nb_samples = ((nb_samples * (100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    max_nb_samples = ((nb_samples * (100 + SAMPLE_CORRECTION_PERCENT_MAX) / 100));
                    wanted_nb_samples = av_clip(wanted_nb_samples, min_nb_samples, max_nb_samples);
                }

            }
        }
        else
        {
            /* too big difference : may be initial PTS errors, so
               reset A-V filter */
            is->audio_diff_avg_count = 0;
            is->audio_diff_cum       = 0;
        }
    }

    return wanted_nb_samples;
}

int CVideoProcess::audio_decode_frame()
{
    int data_size, resampled_data_size;
    int64_t dec_channel_layout;
    av_unused double audio_clock0;
    int wanted_nb_samples;
    Frame *af;
    if(NULL == is)
    {
        return -1;
    }
    if (is->paused)
        return -1;

    do {
#if defined(_WIN32)
        while (CommonFun::frame_queue_nb_remaining(&is->sampq) == 0)
        {
            if(is->audio_tgt.bytes_per_sec < 1)
                return -1;
            if ((av_gettime_relative() - CommonFun::audio_callback_time) > 1000000LL * is->audio_hw_buf_size / is->audio_tgt.bytes_per_sec / 2)
                return -1;
            av_usleep (1000);
        }
#endif
        if (!(af = CommonFun::frame_queue_peek_readable(&is->sampq)))
            return -1;
        //if(is->frame_timer + is->frame_delay > av_gettime_relative()/1000000.0)
        {
            CommonFun::frame_queue_next(&is->sampq);
        }
    } while (af->serial != is->audioq.serial);

    data_size = av_samples_get_buffer_size(NULL, af->frame->channels,
                                           af->frame->nb_samples,
                                           (AVSampleFormat)af->frame->format, 1);

    dec_channel_layout =
        (af->frame->channel_layout && af->frame->channels == av_get_channel_layout_nb_channels(af->frame->channel_layout)) ?
        af->frame->channel_layout : av_get_default_channel_layout(af->frame->channels);
    wanted_nb_samples = synchronize_audio(af->frame->nb_samples);

    if (af->frame->format        != is->audio_src.fmt            ||
        dec_channel_layout       != is->audio_src.channel_layout ||
        af->frame->sample_rate   != is->audio_src.freq           ||
        (wanted_nb_samples       != af->frame->nb_samples && !is->swr_ctx))
    {
        swr_free(&is->swr_ctx);
        is->swr_ctx = swr_alloc_set_opts(NULL,
                                         is->audio_tgt.channel_layout, is->audio_tgt.fmt, is->audio_tgt.freq,
                                         dec_channel_layout,           (AVSampleFormat)af->frame->format, af->frame->sample_rate,
                                         0, NULL);
        if (!is->swr_ctx || swr_init(is->swr_ctx) < 0)
        {
            LOG.Log(ErrLog,
                   "Cannot create sample rate converter for conversion of %d Hz %s %d channels to %d Hz %s %d channels!\n",
                    af->frame->sample_rate, av_get_sample_fmt_name((AVSampleFormat)af->frame->format), af->frame->channels,
                    is->audio_tgt.freq, av_get_sample_fmt_name(is->audio_tgt.fmt), is->audio_tgt.channels);
            swr_free(&is->swr_ctx);
            return -1;
        }
        is->audio_src.channel_layout = dec_channel_layout;
        is->audio_src.channels       = af->frame->channels;
        is->audio_src.freq = af->frame->sample_rate;
        is->audio_src.fmt = (AVSampleFormat)af->frame->format;
    }

    if (is->swr_ctx)
    {
        const uint8_t **in = (const uint8_t **)af->frame->extended_data;
        uint8_t **out = &is->audio_buf1;
        int out_count = (int64_t)wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate + 256;
        int out_size  = av_samples_get_buffer_size(NULL, is->audio_tgt.channels, out_count, is->audio_tgt.fmt, 0);
        int len2;
        if (out_size < 0)
        {
            LOG.Log(ErrLog,  "av_samples_get_buffer_size() failed\n");
            return -1;
        }
        if (wanted_nb_samples != af->frame->nb_samples)
        {
            if (swr_set_compensation(is->swr_ctx, (wanted_nb_samples - af->frame->nb_samples) * is->audio_tgt.freq / af->frame->sample_rate,
                                        wanted_nb_samples * is->audio_tgt.freq / af->frame->sample_rate) < 0) {
                LOG.Log(ErrLog,  "swr_set_compensation() failed\n");
                return -1;
            }
        }
        av_fast_malloc(&is->audio_buf1, &is->audio_buf1_size, out_size);
        if (!is->audio_buf1)
            return AVERROR(ENOMEM);
        len2 = swr_convert(is->swr_ctx, out, out_count, in, af->frame->nb_samples);
        if (len2 < 0)
        {
            LOG.Log(ErrLog,  "swr_convert() failed\n");
            return -1;
        }
        if (len2 == out_count)
        {
            LOG.Log(WarnLog,  "audio buffer is probably too small\n");
            if (swr_init(is->swr_ctx) < 0)
                swr_free(&is->swr_ctx);
        }
        is->audio_buf = is->audio_buf1;
        resampled_data_size = len2 * is->audio_tgt.channels * av_get_bytes_per_sample(is->audio_tgt.fmt);
    }
    else
    {
        is->audio_buf = af->frame->data[0];
        resampled_data_size = data_size;
    }
   // SDL_QueueAudio(m_devAudioId,is->audio_buf,resampled_data_size);
   /*int nSize = resampled_data_size;

   while(nSize > 0)
   {
       Uint8 dst[2048]={0};
       int nSrc = nSize;
       int nDst = sizeof(dst);
       GetAudioStream(is->audio_buf,nSrc,dst,nDst);
       SDL_QueueAudio(m_devAudioId,dst,nDst);
       nSize -= nSrc;


   }*/
    audio_clock0 = is->audio_clock;
    /* update the audio clock with the pts */
    if (!isnan(af->pts))
        is->audio_clock = af->pts + (double) af->frame->nb_samples / af->frame->sample_rate;
    else
        is->audio_clock = NAN;

    is->audio_clock_serial = af->serial;
#ifdef DEBUG
    {
        static double last_clock;
        printf("audio: delay=%0.3f clock=%0.3f clock0=%0.3f\n",
               is->audio_clock - last_clock,
               is->audio_clock, audio_clock0);
        last_clock = is->audio_clock;
    }
#endif
    return resampled_data_size;
}
void CVideoProcess::update_sample_display(short *samples, int samples_size)
{
    int size, len;

    size = samples_size / sizeof(short);
    while (size > 0)
    {
        len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
        if (len > size)
            len = size;
        memcpy(is->sample_array + is->sample_array_index, samples, len * sizeof(short));
        samples += len;
        is->sample_array_index += len;
        if (is->sample_array_index >= SAMPLE_ARRAY_SIZE)
            is->sample_array_index = 0;
        size -= len;
    }
}
void CVideoProcess::sdl_audio_callback(void *opaque, Uint8 *stream, int len)
{

   CVideoProcess * pProcess = (CVideoProcess *)opaque;
   if(NULL == pProcess)
   {
       return;
   }
   PlayState eState = EUnknow;

   VideoState *is = NULL;

   int audio_size, len1,len2;
   int64_t t =  av_gettime_relative();
   LOG.Log(DebugLog,"audiocallback time minus:%d",t-CommonFun::audio_callback_time);
   CommonFun::audio_callback_time = t;


   while (len > 0)
   {
       pProcess->GetState(eState);
       if(EStop == eState)
       {
           LOG.Log(InfoLog,"the state is stop in audio_callback_time");
           return;
       }
       is = pProcess->GetVideoState();
       if(NULL == is)
       {
           return;
       }
       if (is->audio_buf_index >= is->audio_buf_size)
       {
          audio_size = pProcess->audio_decode_frame();
          if (audio_size < 0)
          {
               /* if error, just output silence */
              is->audio_buf = NULL;
              if(is->audio_tgt.frame_size * is->audio_tgt.frame_size < 1)
              {
                  return;
              }
              is->audio_buf_size = SDL_AUDIO_MIN_BUFFER_SIZE / is->audio_tgt.frame_size * is->audio_tgt.frame_size;
           }
          else
          {
               if (is->show_mode != VideoState::SHOW_MODE_VIDEO)
               {
                   pProcess->update_sample_display((int16_t *)is->audio_buf, audio_size);
               }
               is->audio_buf_size = audio_size;
          }
          is->audio_buf_index = 0;
       }
       len1 = is->audio_buf_size - is->audio_buf_index;
       if (len1 > len)
           len1 = len;
       len2 = len;
       if (!is->muted && is->audio_buf && is->audio_volume == SDL_MIX_MAXVOLUME)
       {

          // memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
           pProcess->GetAudioStream((uint8_t *)is->audio_buf + is->audio_buf_index,len1,stream,len2);
       }
       else
       {

           if (!is->muted && is->audio_buf)
           {
               uint8_t dst[4096] = {0};

               pProcess->GetAudioStream((uint8_t *)is->audio_buf + is->audio_buf_index,len1,dst,len2);
               memset(stream,0,len2);
               SDL_MixAudioDevice(pProcess->GetAudioDeviceId(),stream, dst, len2, is->audio_volume);
               //SDL_MixAudio(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1, is->audio_volume);
           }
           else
           {
               len2 = len1;
               memset(stream, 0, len1);
           }
       }
       len -= len2;
       stream += len2;
       is->audio_buf_index += len1;
       //is->audio_buf_size -= len1;
   }
   is->audio_write_buf_size = is->audio_buf_size - is->audio_buf_index;

   if (!isnan(is->audio_clock)) {
       CommonFun::set_clock_at(&is->audclk, is->audio_clock - (double)(2 * is->audio_hw_buf_size + is->audio_write_buf_size) / is->audio_tgt.bytes_per_sec, is->audio_clock_serial, CommonFun::audio_callback_time / 1000000.0);
       CommonFun::sync_clock_to_slave(&is->extclk, &is->audclk);
   }
}
int CVideoProcess::audio_open(void *opaque, int64_t wanted_channel_layout, int wanted_nb_channels, int wanted_sample_rate, struct AudioParams *audio_hw_params)
{
    SDL_AudioSpec wanted_spec, spec;
    const char *env;
    static const int next_nb_channels[] = {0, 0, 1, 6, 2, 6, 4, 6};
    static const int next_sample_rates[] = {0, 44100, 48000, 96000, 192000};
    int next_sample_rate_idx = FF_ARRAY_ELEMS(next_sample_rates) - 1;

    env = SDL_getenv("SDL_AUDIO_CHANNELS");
    if (env) {
        wanted_nb_channels = atoi(env);
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
    }
    if (!wanted_channel_layout || wanted_nb_channels != av_get_channel_layout_nb_channels(wanted_channel_layout)) {
        wanted_channel_layout = av_get_default_channel_layout(wanted_nb_channels);
        wanted_channel_layout &= ~AV_CH_LAYOUT_STEREO_DOWNMIX;
    }
    wanted_nb_channels = av_get_channel_layout_nb_channels(wanted_channel_layout);
    wanted_spec.channels = wanted_nb_channels;
    wanted_spec.freq = wanted_sample_rate;
    if (wanted_spec.freq <= 0 || wanted_spec.channels <= 0) {
        LOG.Log(ErrLog, "Invalid sample rate or channel count!\n");
        return -1;
    }


    while (next_sample_rate_idx && next_sample_rates[next_sample_rate_idx] >= wanted_spec.freq)
        next_sample_rate_idx--;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.silence = 0;
    wanted_spec.samples = FFMAX(SDL_AUDIO_MIN_BUFFER_SIZE, 2 << av_log2(wanted_spec.freq / SDL_AUDIO_MAX_CALLBACKS_PER_SEC));
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = opaque;
     LOG.Log(InfoLog,"audio channelS:%d freq:%d samples:%d",wanted_spec.channels,wanted_spec.freq,wanted_spec.samples);
    //while (SDL_OpenAudio(&wanted_spec, &spec) < 0)
    //SDL_AudioDeviceID dev;

    m_devAudioId = SDL_OpenAudioDevice(NULL,0,&wanted_spec, &spec,SDL_AUDIO_ALLOW_FORMAT_CHANGE);
    while (m_devAudioId < 0)
    {
        LOG.Log(WarnLog, "SDL_OpenAudio (%d channels, %d Hz): %s\n",
               wanted_spec.channels, wanted_spec.freq, SDL_GetError());
        wanted_spec.channels = next_nb_channels[FFMIN(7, wanted_spec.channels)];
        if (!wanted_spec.channels) {
            wanted_spec.freq = next_sample_rates[next_sample_rate_idx--];
            wanted_spec.channels = wanted_nb_channels;
            if (!wanted_spec.freq) {
                LOG.Log(ErrLog,
                       "No more combinations to try, audio open failed\n");
                return -1;
            }
        }
        wanted_channel_layout = av_get_default_channel_layout(wanted_spec.channels);
    }
    LOG.Log(InfoLog,"SDL_OpenAudioDevice: %d",m_devAudioId);
    SDL_PauseAudioDevice(m_devAudioId, 0);
    if (spec.format != AUDIO_S16SYS) {
        LOG.Log(ErrLog,
               "SDL advised audio format %d is not supported!\n", spec.format);
        return -1;
    }
    if (spec.channels != wanted_spec.channels) {
        wanted_channel_layout = av_get_default_channel_layout(spec.channels);
        if (!wanted_channel_layout) {
            LOG.Log(ErrLog,
                   "SDL advised channel count %d is not supported!\n", spec.channels);
            return -1;
        }
    }

    audio_hw_params->fmt = AV_SAMPLE_FMT_S16;
    audio_hw_params->freq = spec.freq;
    audio_hw_params->channel_layout = wanted_channel_layout;
    audio_hw_params->channels =  spec.channels;
    audio_hw_params->frame_size = av_samples_get_buffer_size(NULL, audio_hw_params->channels, 1, audio_hw_params->fmt, 1);
    audio_hw_params->bytes_per_sec = av_samples_get_buffer_size(NULL, audio_hw_params->channels, audio_hw_params->freq, audio_hw_params->fmt, 1);
    if (audio_hw_params->bytes_per_sec <= 0 || audio_hw_params->frame_size <= 0) {
        LOG.Log(ErrLog,  "av_samples_get_buffer_size failed\n");
        return -1;
    }
    LOG.Log(DebugLog,"OpenDevice retain freq:%d channels:%d",spec.freq,spec.channels);
    return spec.size;
}

bool CVideoProcess::init(const char *szName)
{
    m_strMediaName = szName;

    int flags;

    CommonFun::init_dynload();

      av_log_set_flags(AV_LOG_SKIP_REPEATED);

      /* register all codecs, demux and protocols */
#if CONFIG_AVDEVICE
      avdevice_register_all();
#endif
#if CONFIG_AVFILTER
    avfilter_register_all();
#endif
      av_register_all();
      avformat_network_init();

    CommonFun::init_opts();

      signal(SIGINT , sigterm_handler); /* Interrupt (ANSI).    */
      signal(SIGTERM, sigterm_handler); /* Termination (ANSI).  */

   // show_banner(szName, 1, options);

   // parse_options(NULL, szName, 1, options, opt_input_file);



    /*if (display_disable)
    {
        video_disable = 1;
    }*/
      flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER;
    /*if (audio_disable)
        flags &= ~SDL_INIT_AUDIO;
    else */
    {
          /* Try to work around an occasional ALSA buffer underflow issue when the
           * period size is NPOT due to ALSA resampling by forcing the buffer size. */
          if (!SDL_getenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"))
              SDL_setenv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE","1", 1);
    }
   // if (display_disable)
    //    flags &= ~SDL_INIT_VIDEO;
      if (SDL_Init (flags)) {
          LOG.Log(ErrLog,  "Could not initialize SDL - %s\n", SDL_GetError());
          LOG.Log(ErrLog,  "(Did you set the DISPLAY variable?)\n");
          return false;
      }

      SDL_EventState(SDL_SYSWMEVENT, SDL_IGNORE);
      SDL_EventState(SDL_USEREVENT, SDL_IGNORE);

      if (av_lockmgr_register(lockmgr)) {
          LOG.Log(ErrLog,  "Could not initialize lock manager!\n");
          do_exit();
      }

      av_init_packet(&flush_pkt);
      flush_pkt.data = (uint8_t *)&flush_pkt;
      SDL_LogSetOutputFunction(LogOutput,NULL);

    return true;
}
/* open a given stream. Return 0 if OK */
int CVideoProcess::stream_component_open(int stream_index)
{

    AVFormatContext *ic = is->ic;
    AVCodecContext *avctx;
    AVCodec *codec;
    const char *forced_codec_name = NULL;
    AVDictionary *opts = NULL;
    AVDictionaryEntry *t = NULL;
    int sample_rate, nb_channels;
    int64_t channel_layout;
    int ret = 0;
    int stream_lowres = 0;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
    {
        LOG.Log(ErrLog,"stream index:%d is wrong in stream_component_open",stream_index);
        return -1;
    }

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return AVERROR(ENOMEM);

    ret = avcodec_parameters_to_context(avctx, ic->streams[stream_index]->codecpar);
    if (ret < 0)
    {
        LOG.Log(ErrLog,"avcodec_parameters_to_contex ret:%d in stream_component_open",ret);
        goto fail;
    }
    av_codec_set_pkt_timebase(avctx, ic->streams[stream_index]->time_base);

    codec = avcodec_find_decoder(avctx->codec_id);

    switch(avctx->codec_type){
        case AVMEDIA_TYPE_AUDIO   : is->last_audio_stream    = stream_index; forced_codec_name =    audio_codec_name; break;
        case AVMEDIA_TYPE_SUBTITLE: is->last_subtitle_stream = stream_index; forced_codec_name = subtitle_codec_name; break;
        case AVMEDIA_TYPE_VIDEO   : is->last_video_stream    = stream_index; forced_codec_name =    video_codec_name; break;
    }
    if (forced_codec_name)
        codec = avcodec_find_decoder_by_name(forced_codec_name);
    if (!codec) {
        if (forced_codec_name) LOG.Log(WarnLog,
                                      "No codec could be found with name '%s'\n", forced_codec_name);
        else                   LOG.Log(WarnLog,
                                      "No codec could be found with id %d\n", avctx->codec_id);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    avctx->codec_id = codec->id;
    if(stream_lowres > av_codec_get_max_lowres(codec)){
        LOG.Log(WarnLog, "The maximum value for lowres supported by the decoder is %d\n",
                av_codec_get_max_lowres(codec));
        stream_lowres = av_codec_get_max_lowres(codec);
    }
    av_codec_set_lowres(avctx, stream_lowres);

#if FF_API_EMU_EDGE
    if(stream_lowres) avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif
    //if (fast)
    //   avctx->flags2 |= AV_CODEC_FLAG2_FAST;
#if FF_API_EMU_EDGE
    if(codec->capabilities & AV_CODEC_CAP_DR1)
        avctx->flags |= CODEC_FLAG_EMU_EDGE;
#endif

    opts = CommonFun::filter_codec_opts(codec_opts, avctx->codec_id, ic, ic->streams[stream_index], codec);
    if (!av_dict_get(opts, "threads", NULL, 0))
        av_dict_set(&opts, "threads", "auto", 0);
    if (stream_lowres)
        av_dict_set_int(&opts, "lowres", stream_lowres, 0);
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
        av_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2(avctx, codec, &opts)) < 0) {
        goto fail;
    }
    if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        LOG.Log(ErrLog, "Option %s not found.\n", t->key);
        ret =  AVERROR_OPTION_NOT_FOUND;
        goto fail;
    }

    is->eof = 0;
    ic->streams[stream_index]->discard = AVDISCARD_DEFAULT;
    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
#if CONFIG_AVFILTER
        {
            AVFilterContext *sink;

            is->audio_filter_src.freq           = avctx->sample_rate;
            is->audio_filter_src.channels       = avctx->channels;
            is->audio_filter_src.channel_layout = get_valid_channel_layout(avctx->channel_layout, avctx->channels);
            is->audio_filter_src.fmt            = avctx->sample_fmt;
            if ((ret = CommonFun::configure_audio_filters(is, CommonFun::afilters, 0)) < 0)
                goto fail;
            sink = is->out_audio_filter;
            sample_rate    = av_buffersink_get_sample_rate(sink);
            nb_channels    = av_buffersink_get_channels(sink);
            channel_layout = av_buffersink_get_channel_layout(sink);
        }
#else

        sample_rate    = avctx->sample_rate;
        nb_channels    = avctx->channels;
        channel_layout = avctx->channel_layout;

#endif

        /* prepare audio output */
        if ((ret = audio_open(this, channel_layout, nb_channels, sample_rate, &is->audio_tgt)) < 0)
        {
            LOG.Log(ErrLog,"audio_open failed");
            goto fail;
        }
        is->audio_hw_buf_size = ret;
        is->audio_src = is->audio_tgt;
        is->audio_buf_size  = 0;
        is->audio_buf_index = 0;

        /* init averaging filter */
        is->audio_diff_avg_coef  = exp(log(0.01) / AUDIO_DIFF_AVG_NB);
        is->audio_diff_avg_count = 0;
        /* since we do not have a precise anough audio FIFO fullness,
           we correct audio sync only if larger than this threshold */
        is->audio_diff_threshold = (double)(is->audio_hw_buf_size) / is->audio_tgt.bytes_per_sec;

        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];

        decoder_init(&is->auddec, avctx, &is->audioq, is->continue_read_thread);
        if ((is->ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK)) && !is->ic->iformat->read_seek) {
            is->auddec.start_pts = is->audio_st->start_time;
            is->auddec.start_pts_tb = is->audio_st->time_base;
        }
        if ((ret = decoder_start(&is->auddec, ThreadAuto, this)) < 0)
        {
            LOG.Log(ErrLog,"decoder_start audio failed");
            goto out;
        }
        SDL_PauseAudio(0);
        break;
    case AVMEDIA_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];

        decoder_init(&is->viddec, avctx, &is->videoq, is->continue_read_thread);
        if ((ret = decoder_start(&is->viddec, ThreadVideo, this)) < 0)
        {
            LOG.Log(ErrLog,"decoder_start video failed");
            goto out;
        }
        is->queue_attachments_req = 1;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        is->subtitle_stream = stream_index;
        is->subtitle_st = ic->streams[stream_index];

        decoder_init(&is->subdec, avctx, &is->subtitleq, is->continue_read_thread);
        if ((ret = decoder_start(&is->subdec, ThreadSubtitle, this)) < 0)
        {
            LOG.Log(ErrLog,"decoder_start subtitle failed");
            goto out;
        }
        break;
    default:
        break;
    }
    goto out;

fail:
    avcodec_free_context(&avctx);
out:
    av_dict_free(&opts);

    return ret;
}
void CVideoProcess::stream_close()
{
    if(NULL == is)
    {
        return;
    }
    SetState(EStop);
    Sleep(100);
    LOG.Log(InfoLog,"stream_close");

    /* XXX: use a special url_shutdown call to abort parse cleanly */

       is->abort_request = 1;
       SDL_WaitThread(is->read_tid, NULL);

       /* close each stream */
       if (is->audio_stream >= 0)
       {
           stream_component_close(is->audio_stream);
           LOG.Log(InfoLog,"stream component close audio");
       }
       if (is->video_stream >= 0)
       {
           stream_component_close(is->video_stream);
           LOG.Log(InfoLog,"stream component close video");
       }
       if (is->subtitle_stream >= 0)
       {
           stream_component_close(is->subtitle_stream);
           LOG.Log(InfoLog,"stream component close subtitle");
       }

       avformat_close_input(&is->ic);
       LOG.Log(DebugLog,"avformat_close_input");
       packet_queue_destroy(&is->videoq);
       LOG.Log(DebugLog,"destroy videoq");
       packet_queue_destroy(&is->audioq);
       LOG.Log(DebugLog,"destroy audioq");
       packet_queue_destroy(&is->subtitleq);
       LOG.Log(DebugLog,"destroy subtitleq");

       /* free all pictures */
       CommonFun::frame_queue_destory(&is->pictq);
       LOG.Log(DebugLog,"free picq");
       CommonFun::frame_queue_destory(&is->sampq);
       LOG.Log(DebugLog,"free sampq");
       CommonFun::frame_queue_destory(&is->subpq);
       LOG.Log(DebugLog,"free subtitleq");
       if(is->continue_read_thread)
       {
           SDL_DestroyCond(is->continue_read_thread);
           is->continue_read_thread = NULL;
           LOG.Log(DebugLog,"destroy read continue thread");
       }
       LOG.Log(DebugLog,"before free img context");
       if(is->img_convert_ctx)
       {
           LOG.Log(DebugLog,"before2 free img context");
           sws_freeContext(is->img_convert_ctx);
           is->img_convert_ctx = NULL;
           LOG.Log(DebugLog,"free img context");
       }
       LOG.Log(DebugLog,"before free sub context");
       if(is->sub_convert_ctx)
       {
           LOG.Log(DebugLog,"before2 free sub context");
           sws_freeContext(is->sub_convert_ctx);
           is->sub_convert_ctx = NULL;
           LOG.Log(DebugLog,"free sub context");
       }
       /*LOG.Log(DebugLog,"before free filename");
       if(is->filename)
       {
           av_free(is->filename);
           LOG.Log(DebugLog,"free file name:%s",is->filename);
       }*/
       LOG.Log(DebugLog,"before destroy vis_texture");
       if (is->vis_texture)
       {
           LOG.Log(DebugLog,"before2 destroy vis_texture");
           SDL_DestroyTexture(is->vis_texture);
           is->vis_texture = NULL;
           LOG.Log(DebugLog,"destroy vis_texture");
       }
       LOG.Log(DebugLog,"before destroy vid_texture");
       if (is->vid_texture)
       {
           LOG.Log(DebugLog,"before2 destroy vid_texture");
           SDL_DestroyTexture(is->vid_texture);
           is->vid_texture = NULL;
           LOG.Log(DebugLog,"destroy vid_texture");
       }
       LOG.Log(DebugLog,"before destroy sub_texture");
       if (is->sub_texture)
       {
           LOG.Log(DebugLog,"before2 destroy sub_texture");
           SDL_DestroyTexture(is->sub_texture);
           is->sub_texture = NULL;
           LOG.Log(DebugLog,"destroy sub_texture");
       }
       av_free(is);
       LOG.Log(InfoLog,"free is");
       is = NULL;
       //SetFirstShow(true);

}
void CVideoProcess::stream_component_close(int stream_index)
{
    AVFormatContext *ic = is->ic;
       AVCodecParameters *codecpar;

       if (stream_index < 0 || stream_index >= ic->nb_streams)
           return;
       codecpar = ic->streams[stream_index]->codecpar;

       switch (codecpar->codec_type) {
       case AVMEDIA_TYPE_AUDIO:
           decoder_abort(&is->auddec, &is->sampq);
           //SDL_CloseAudio();
           SDL_CloseAudioDevice(m_devAudioId);
           LOG.Log(InfoLog,"SDL_CloseAudioDevice: %d",m_devAudioId);
           decoder_destroy(&is->auddec);
           swr_free(&is->swr_ctx);
           av_freep(&is->audio_buf1);
           is->audio_buf1_size = 0;
           is->audio_buf = NULL;

           if (is->rdft) {
               av_rdft_end(is->rdft);
               av_freep(&is->rdft_data);
               is->rdft = NULL;
               is->rdft_bits = 0;
           }
           break;
       case AVMEDIA_TYPE_VIDEO:
           decoder_abort(&is->viddec, &is->pictq);
           decoder_destroy(&is->viddec);
           break;
       case AVMEDIA_TYPE_SUBTITLE:
           decoder_abort(&is->subdec, &is->subpq);
           decoder_destroy(&is->subdec);
           break;
       default:
           break;
       }

       ic->streams[stream_index]->discard = AVDISCARD_ALL;
       switch (codecpar->codec_type) {
       case AVMEDIA_TYPE_AUDIO:
           is->audio_st = NULL;
           is->audio_stream = -1;
           break;
       case AVMEDIA_TYPE_VIDEO:
           is->video_st = NULL;
           is->video_stream = -1;
           break;
       case AVMEDIA_TYPE_SUBTITLE:
           is->subtitle_st = NULL;
           is->subtitle_stream = -1;
           break;
       default:
           break;
       }
}
void CVideoProcess::set_default_window_size(int width, int height, AVRational sar)
{
   SDL_Rect rect;
   CommonFun::calculate_display_rect(&rect, 0, 0, INT_MAX, height, width, height, sar);
   default_width  = rect.w;
   default_height = rect.h;
}
void CVideoProcess::do_exit(SDL_Renderer *renderer,SDL_Window *window)
{
    if (is) {
          stream_close();
      }
      if (renderer)
          SDL_DestroyRenderer(renderer);
      if (window)
          SDL_DestroyWindow(window);
      av_lockmgr_register(NULL);
    CommonFun::uninit_opts();
#if CONFIG_AVFILTER
    av_freep(&CommonFun::vfilters_list);
#endif
      avformat_network_deinit();
      if (show_status)
          printf("\n");
      SDL_Quit();
      //LOG.Log(InfoLog, "exit the program");
      //exit(0);
}
int CVideoProcess::get_video_frame(AVFrame *frame)
{
     int got_picture;

     if ((got_picture = decoder_decode_frame(&is->viddec, frame, NULL)) < 0)
         return -1;

     if (got_picture) {
         double dpts = NAN;

         if (frame->pts != AV_NOPTS_VALUE)
             dpts = av_q2d(is->video_st->time_base) * frame->pts;

         frame->sample_aspect_ratio = av_guess_sample_aspect_ratio(is->ic, is->video_st, frame);

         if (CommonFun::framedrop>0 || (CommonFun::framedrop && CommonFun::get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER))
         {
             if (frame->pts != AV_NOPTS_VALUE)
             {
                 double diff = dpts - CommonFun::get_master_clock(is);
                 if (!isnan(diff) && fabs(diff) < AV_NOSYNC_THRESHOLD &&
                     diff - is->frame_last_filter_delay < 0 &&
                     is->viddec.pkt_serial == is->vidclk.serial &&
                     is->videoq.nb_packets)
                 {
                     is->frame_drops_early++;
                     av_frame_unref(frame);
                     got_picture = 0;
                 }
             }
         }
     }

     return got_picture;
}
int CVideoProcess::queue_picture(AVFrame *src_frame, double pts, double duration, int64_t pos, int serial)
{
    Frame *vp;

    #if defined(DEBUG_SYNC)
        printf("frame_type=%c pts=%0.3f\n",
        av_get_picture_type_char(src_frame->pict_type), pts);
    #endif
    LOG.Log(DebugLog,"pictq.size:%d pictq.maxsize:%d",is->pictq.size,is->pictq.max_size);
    if (!(vp = CommonFun::frame_queue_peek_writable(&is->pictq)))
        return -1;

    vp->sar = src_frame->sample_aspect_ratio;
    vp->uploaded = 0;

    vp->width = src_frame->width;
    vp->height = src_frame->height;
    vp->format = src_frame->format;

    vp->pts = pts;
    vp->duration = duration;
    vp->pos = pos;
    vp->serial = serial;

    set_default_window_size(vp->width, vp->height, vp->sar);

    av_frame_move_ref(vp->frame, src_frame);
    CommonFun::frame_queue_push(&is->pictq);
    return 0;
}
double CVideoProcess::vp_duration(Frame *vp, Frame *nextvp)
{
    if (vp->serial == nextvp->serial) {
         double duration = nextvp->pts - vp->pts;
         if (isnan(duration) || duration <= 0 || duration > is->max_frame_duration)
             return vp->duration;
         else
             return duration;
     } else {
         return 0.0;
     }
}
void CVideoProcess::decoder_abort(Decoder *d, FrameQueue *fq)
{
   packet_queue_abort(d->queue);
   CommonFun::frame_queue_signal(fq);
   SDL_WaitThread(d->decoder_tid, NULL);
   d->decoder_tid = NULL;
   packet_queue_flush(d->queue);
}
/* packet queue handling */
int CVideoProcess::packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    if (!q->mutex) {
        LOG.Log(ErrLog,  "SDL_CreateMutex(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->cond = SDL_CreateCond();
    if (!q->cond) {
        LOG.Log(ErrLog,  "SDL_CreateCond(): %s\n", SDL_GetError());
        return AVERROR(ENOMEM);
    }
    q->abort_request = 1;
    return 0;
}

void CVideoProcess::packet_queue_flush(PacketQueue *q)
{
    MyAVPacketList *pkt, *pkt1;

    SDL_LockMutex(q->mutex);
    for (pkt = q->first_pkt; pkt; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt = NULL;
    q->first_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;
    SDL_UnlockMutex(q->mutex);
}

void CVideoProcess::packet_queue_destroy(PacketQueue *q)
{
    packet_queue_flush(q);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

void CVideoProcess::packet_queue_abort(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);

    q->abort_request = 1;

    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
}

void CVideoProcess::packet_queue_start(PacketQueue *q)
{
    SDL_LockMutex(q->mutex);
    q->abort_request = 0;
    packet_queue_put_private(q, &flush_pkt);
    SDL_UnlockMutex(q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
int CVideoProcess::packet_queue_get(PacketQueue *q, AVPacket *pkt, int block, int *serial)
{
    MyAVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for (;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            q->duration -= pkt1->pkt.duration;
            *pkt = pkt1->pkt;
            if (serial)
                *serial = pkt1->serial;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);
    return ret;
}

void CVideoProcess::decoder_init(Decoder *d, AVCodecContext *avctx, PacketQueue *queue, SDL_cond *empty_queue_cond) {
    memset(d, 0, sizeof(Decoder));
    d->avctx = avctx;
    d->queue = queue;
    d->empty_queue_cond = empty_queue_cond;
    d->start_pts = AV_NOPTS_VALUE;
    d->pkt_serial = -1;
}

int CVideoProcess::decoder_decode_frame(Decoder *d, AVFrame *frame, AVSubtitle *sub) {
    int ret = AVERROR(EAGAIN);

    for (;;)
    {
        AVPacket pkt;
        LOG.Log(DebugLog,"type:%d first queue->serial:%d pkt_serial:%d",d->avctx->codec_type,d->queue->serial,d->pkt_serial);
        if (d->queue->serial == d->pkt_serial)
        {
            do
            {
                if (d->queue->abort_request)
                {
                    LOG.Log(DebugLog,"queue abort_request");
                    return -1;
                }
                switch (d->avctx->codec_type)
                {
                    case AVMEDIA_TYPE_VIDEO:
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0)
                        {
                            if (decoder_reorder_pts == -1) {
                                frame->pts = frame->best_effort_timestamp;
                            } else if (!decoder_reorder_pts) {
                                frame->pts = frame->pkt_dts;
                            }

                        }
                        LOG.Log(DebugLog,"receive frame ret:%d",ret);
                        break;
                    case AVMEDIA_TYPE_AUDIO:
                        ret = avcodec_receive_frame(d->avctx, frame);
                        if (ret >= 0)
                        {
                            AVRational tb;
                            tb.num = 1;
                            tb.den= frame->sample_rate;//(AVRational){1, };
                            if (frame->pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(frame->pts, av_codec_get_pkt_timebase(d->avctx), tb);
                            else if (d->next_pts != AV_NOPTS_VALUE)
                                frame->pts = av_rescale_q(d->next_pts, d->next_pts_tb, tb);
                            if (frame->pts != AV_NOPTS_VALUE) {
                                d->next_pts = frame->pts + frame->nb_samples;
                                d->next_pts_tb = tb;
                            }
                        }
                        break;

                }
                LOG.Log(DebugLog,"avcodec_receive_frame ret:%d",ret);
                if (ret == AVERROR_EOF) {
                    d->finished = d->pkt_serial;
                    avcodec_flush_buffers(d->avctx);
                    LOG.Log(DebugLog,"AVERROR_EOF");
                    return 0;
                }
                if (ret >= 0)
                {
                    return 1;
                }

            } while (ret != AVERROR(EAGAIN));
        }

        do
        {
            if (d->queue->nb_packets == 0)
            {

                SDL_CondSignal(d->empty_queue_cond);

            }
            if (d->packet_pending)
            {
                av_packet_move_ref(&pkt, &d->pkt);
                d->packet_pending = 0;
                LOG.Log(DebugLog,"packet_pending");
            } else
            {
                if (packet_queue_get(d->queue, &pkt, 1, &d->pkt_serial) < 0)
                {
                    LOG.Log(DebugLog,"packet_queue_get failed");
                    return -1;
                }
            }
            LOG.Log(DebugLog,"before queue->serial:%d pkt_serial:%d",d->queue->serial,d->pkt_serial);
        } while (d->queue->serial != d->pkt_serial);

        if (pkt.data == flush_pkt.data)
        {
            avcodec_flush_buffers(d->avctx);
            d->finished = 0;
            d->next_pts = d->start_pts;
            d->next_pts_tb = d->start_pts_tb;
            LOG.Log(DebugLog,"pkt.data == flush_pkt.data");
        }
        else
        {
            if (d->avctx->codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                int got_frame = 0;
                ret = avcodec_decode_subtitle2(d->avctx, sub, &got_frame, &pkt);
                if (ret < 0) {
                    ret = AVERROR(EAGAIN);
                } else {
                    if (got_frame && !pkt.data) {
                       d->packet_pending = 1;
                       av_packet_move_ref(&d->pkt, &pkt);
                    }
                    ret = got_frame ? 0 : (pkt.data ? AVERROR(EAGAIN) : AVERROR_EOF);
                }
            }
            else
            {
                if (avcodec_send_packet(d->avctx, &pkt) == AVERROR(EAGAIN)) {
                    LOG.Log(ErrLog,  "Receive_frame and send_packet both returned EAGAIN, which is an API violation.\n");
                    d->packet_pending = 1;
                    av_packet_move_ref(&d->pkt, &pkt);
                }
                 LOG.Log(DebugLog,"after queue->serial:%d pkt_serial:%d",d->queue->serial,d->pkt_serial);
            }
            av_packet_unref(&pkt);
        }
    }
}

void CVideoProcess::decoder_destroy(Decoder *d) {
    av_packet_unref(&d->pkt);
    avcodec_free_context(&d->avctx);
}
int CVideoProcess::packet_queue_put_nullpacket(PacketQueue *q, int stream_index)
{
    AVPacket pkt1, *pkt = &pkt1;
      av_init_packet(pkt);
      pkt->data = NULL;
      pkt->size = 0;
      pkt->stream_index = stream_index;
      return packet_queue_put(q, pkt);
}
int CVideoProcess::packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    int ret;

    SDL_LockMutex(q->mutex);
    ret = packet_queue_put_private(q, pkt);
    SDL_UnlockMutex(q->mutex);

    if (pkt != &flush_pkt && ret < 0)
        av_packet_unref(pkt);

    return ret;
}
int CVideoProcess::packet_queue_put_private(PacketQueue *q, AVPacket *pkt)
{
    MyAVPacketList *pkt1;

    if (q->abort_request)
       return -1;

    pkt1 = (MyAVPacketList *)av_malloc(sizeof(MyAVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;
    if (pkt == &flush_pkt)
        q->serial++;
    pkt1->serial = q->serial;

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);
    q->duration += pkt1->pkt.duration;
    /* XXX: should duplicate packet data in DV case */
    SDL_CondSignal(q->cond);
    return 0;
}
int64_t CVideoProcess::get_valid_channel_layout(int64_t channel_layout, int channels)
{
  if (channel_layout && av_get_channel_layout_nb_channels(channel_layout) == channels)
      return channel_layout;
  else
      return 0;
}

int CVideoProcess::lockmgr(void **mtx, enum AVLockOp op)
{
   switch(op) {
      case AV_LOCK_CREATE:
          *mtx = SDL_CreateMutex();
          if(!*mtx) {
              LOG.Log(ErrLog,  "SDL_CreateMutex(): %s\n", SDL_GetError());
              return 1;
          }
          return 0;
      case AV_LOCK_OBTAIN:
          return !!SDL_LockMutex((SDL_mutex *)*mtx);
      case AV_LOCK_RELEASE:
          return !!SDL_UnlockMutex((SDL_mutex *)*mtx);
      case AV_LOCK_DESTROY:
          SDL_DestroyMutex((SDL_mutex *)*mtx);
          return 0;
   }
   return 1;
}
void CVideoProcess::sigterm_handler(int sig)
{
    LOG.Log(InfoLog,"exit 123");
    exit(123);
}
void CVideoProcess::SetState(PlayState eState)
{
   // boost::lock_guard<CVideoProcess> guard(*this);
    m_eState = eState;
}
PlayState CVideoProcess::GetState(PlayState & eState)
{
   // boost::lock_guard<CVideoProcess> guard(*this);
    eState = m_eState;
    return m_eState;
}
void CVideoProcess::SetPlayRate(float fRate)
{
    m_fPlayRate=fRate;
    //int nMaxDev = CommonFun::get_max_dev(10,10*fRate);
    if(fRate > 1.0)
    {
        m_nDev = 2*max(10,10*fRate);//nMaxDev;
        m_nDiv = 2*min(10,10*fRate);//nMaxDev;
        if(0 != m_nDiv%2)
        {
            m_nDiv+=1;
        }
    }
    else if (fRate < 1.0)
    {
        m_nDev = 4*max(10,10*fRate);//nMaxDev;
        m_nDiv = 4*min(10,10*fRate);//nMaxDev;
        if(0 != m_nDiv%2)
        {
            m_nDiv+=1;
        }
    }
    int64_t tsize = avio_size(is->ic->pb);
    int i = 0;
    double nmax = 0.0;
    //float f = 1.0/fRate;
   // SDL_SetAudioDevicePlayRate(m_devAudioId,f);
}
void CVideoProcess::EmitVideo()
{
    if(m_pMedia)
    {
        //m_pMedia->EmitVideo();
        emit (m_pMedia->ShowVideo());
    }
}
void CVideoProcess::EmitStop()
{
    if(m_pMedia)
    {
        //m_pMedia->EmitStop();
        emit (m_pMedia->StopVideo());
    }
}
int CVideoProcess::GetAudioStream(Uint8 *src,int & nSrc,Uint8 *dst,int & nDst)
{
    int nLen = nDst;
    int nLenSrc = nSrc;
    if(m_fPlayRate > 1.0)
    {
        int nP = m_nDev;//10 * m_fPlayRate;
        int nT = nSrc / nP;
        int nM = nSrc % nP;
        for(int i=0;i<nT;i++)
        {
            memcpy(dst,src,m_nDiv);
            dst+=m_nDiv;
            src+=nP;
        }
        memcpy(dst,src,nM);
        nLen = nT*m_nDiv+nM;
        nLenSrc = nSrc;
    }
    else if(m_fPlayRate < 1.0)
    {
        int nP = m_nDiv;//10 * m_fPlayRate;
        int nR = m_nDev - nP;
        int nT = min(nSrc/nP,nDst / m_nDev);

        int i = 0;
        nLen = nT * m_nDev;
        nLenSrc = nP * nT;
        int nM2 = min(m_nDev,nDst - nT * m_nDev);
        int nM1 = min(nP,nSrc - nT * nP);
        while(i < nT)
        {
            memcpy(dst,src,nP);
            src+=nP;
            memset(dst+nP,dst[nP-1],nR);
            dst+=m_nDev;
            i++;
        }
        if(nM2 > 0)
        {
            if(nM1 >= nM2)
            {
                memcpy(dst,src,nM2);
                nLen += nM2;
                nLenSrc += nM2;
                src+=nM2;
                dst+=nM2;
            }
            else if(nM1 > 0)
            {

                memcpy(dst,src,nM1);
                memset(dst,dst[nM1-1],nM2-nM1);
                dst+=nM2;
                nLen+=nM2;
                nLenSrc+=nM1;
            }

        }

    }
    else
    {
        nLen = min(nSrc,nDst);
        nLenSrc = nLen;
        memcpy(dst,src,nLen);
    }
    nSrc = nLenSrc;
    nDst = nLen;
    return nLen;
}
void CVideoProcess::SetTotalTime(int64_t nT)
{
    if(nT != m_nTotalTime)
    {
        m_nTotalTime=nT;
        if(m_pMedia)
        {
            emit (m_pMedia->RangeChange(0,nT));
        }
    }
}
void CVideoProcess::SetCurTime(int64_t nT)
{
    if(is && ! is->realtime)
    {
        return;
    }
    if(nT != m_nCurTime)
    {
        if(nT >= m_nTotalTime)
        {
            EmitStop();
            return;
        }
        m_nCurTime=nT;
        if(m_pMedia)
        {
            emit (m_pMedia->ValueChange(nT));
        }
    }
}
/*int CVideoProcess::GetAudioStream(Uint8 *src,int & nSrc,Uint8 *dst,int & nDst)
{
    int nLen = nDst;
    int nLenSrc = nSrc;
    if(m_fPlayRate > 1.0)
    {
        int nP = 10 * m_fPlayRate;
        int nT = nSrc / nP;
        int nM = nSrc % nP;
        for(int i=0;i<nT;i++)
        {
            memcpy(dst,src,10);
            dst+=10;
            src+=nP;
        }
        memcpy(dst,src,nM);
        nLen = nT*10+nM;
        nLenSrc = nSrc;
    }
    else if(m_fPlayRate < 1.0)
    {
        int nP = 10 * m_fPlayRate;
        int nR = 10 - nP;
        int nT = min(nSrc/nP,nDst / 10);

        int i = 0;
        nLen = nT * 10;
        nLenSrc = nP * nT;
        int nM2 = min(10,nDst - nT * 10);
        int nM1 = min(nP,nSrc - nT * nP);
        while(i < nT)
        {
            memcpy(dst,src,nP);
            src+=nP;
            memset(dst+nP,dst[nP-1],nR);
            dst+=10;
            i++;
        }
        if(nM1 > 0)
        {
            if(nM1 >= nM2)
            {
                memcpy(dst,src,nM2);
            }
            else
            {

                memcpy(dst,src,nM1);
                dst+=nM1;
                memset(dst,src[nM1-1],nM2-nM1);
            }
            nLen += nM2;
            nLenSrc += nM1;
        }

    }
    else
    {
        nLen = min(nSrc,nDst);
        nLenSrc = nLen;
        memcpy(dst,src,nLen);
    }
    nSrc = nLenSrc;
    nDst = nLen;
    return nLen;
}
*/
void CVideoProcess::LogOutput(void *userdata,
                          int category, SDL_LogPriority priority,
                          const char *message)
{
    switch(priority)
    {
    case SDL_LOG_PRIORITY_VERBOSE:
        LOG.Log(DebugLog,"from SDL2 %s",message);
        break;
    case SDL_LOG_PRIORITY_DEBUG:
        LOG.Log(DebugLog,"from SDL2 %s",message);
        break;
    case SDL_LOG_PRIORITY_INFO:
        LOG.Log(InfoLog,"from SDL2 %s",message);
        break;
    case SDL_LOG_PRIORITY_WARN:
        LOG.Log(WarnLog,"from SDL2 %s",message);
        break;
    case SDL_LOG_PRIORITY_ERROR:
        LOG.Log(ErrLog,"from SDL2 %s",message);
        break;
    case SDL_LOG_PRIORITY_CRITICAL:
        LOG.Log(InfoLog,"from SDL2 %s",message);
        break;
    case SDL_NUM_LOG_PRIORITIES:
        LOG.Log(InfoLog,"from SDL2 %s",message);
        break;
    default:
        break;
    }
}
