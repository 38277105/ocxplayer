#include <QResizeEvent>
#include "display.h"
#include"commfun.h"
extern CWriteLog LOG;
#define CHECKISV()                            \
VideoState *is = m_pProcess->GetVideoState(); \
if(!is)                                       \
{                                             \
   return;                                    \
}

#define CHECKISD()                            \
VideoState *is = m_pProcess->GetVideoState(); \
if(!is)                                       \
{                                             \
   return 0;                                \
}

#define CHECKISB()                            \
is = m_pProcess->GetVideoState(); \
if(!is)                                       \
{                                             \
        break;                                \
                                              \
}
#define CHECKISBS(ins)                            \
is = ins->m_pProcess->GetVideoState(); \
if(!is)                                       \
{                                             \
        break;                                \
                                              \
}
#define GETIS(ins) is = ins->m_pProcess->GetVideoState();
#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)
bool CVideoDisplay::m_bInit = false;
CVideoDisplay::CVideoDisplay(CVideoProcess *pProcess, QWidget *pParent):QWidget(pParent)
  ,m_pProcess(pProcess)
  ,window(NULL)
  ,renderer(NULL)
  ,cursor_hidden(0)
  ,cursor_last_shown(0)
  ,is_full_screen(0)
  ,screen_width(352)
  ,screen_height(244)
  ,m_threadEvent(NULL)

{

}
CVideoDisplay::~CVideoDisplay()
{
    if(m_threadEvent)
    {
        SDL_WaitThread(m_threadEvent,NULL);
        m_threadEvent = NULL;
    }
}
int CVideoDisplay::ThreadEvent(void *p)
{
    CVideoDisplay *pInstance = (CVideoDisplay *)p;
    if(NULL == pInstance)
    {
        LOG.Log(ErrLog,"pInstance is NULL in ThreadEvent");
        return 0;
    }
    SDL_Event event;
    double incr, pos, frac;
    VideoState *is = NULL;
    for (;;) {
        GETIS(pInstance);
        if(is && is->abort_request)
        {
            LOG.Log(InfoLog,"abort_request is true in threadEvent");
            break;
        }
        double x;
        pInstance->refresh_loop_wait_event(&event);
        LOG.Log(DebugLog,"event.type:%d in event_loop",event.type);
        switch (event.type) {
        case SDL_KEYDOWN:
            LOG.Log(InfoLog,"keydown evet");
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
               pInstance->m_pProcess->do_exit(pInstance->renderer,pInstance->window);
                break;
            case SDLK_f:
                pInstance->toggle_full_screen();
                CHECKISBS(pInstance);
                {
                    is->force_refresh = 1;
                }
                break;
            case SDLK_p:
            case SDLK_SPACE:
                pInstance->toggle_pause();
                break;
            case SDLK_m:
                pInstance->toggle_mute();
                break;
            case SDLK_KP_MULTIPLY:
            case SDLK_0:
                pInstance->update_volume(1, SDL_VOLUME_STEP);
                break;
            case SDLK_KP_DIVIDE:
            case SDLK_9:
                pInstance->update_volume(-1, SDL_VOLUME_STEP);
                break;
            case SDLK_s: // S: Step to next frame
                pInstance->m_pProcess->step_to_next_frame();
                break;
            case SDLK_a:
                pInstance->stream_cycle_channel(AVMEDIA_TYPE_AUDIO);
                break;
            case SDLK_v:
                pInstance->stream_cycle_channel(AVMEDIA_TYPE_VIDEO);
                break;
            case SDLK_c:
                pInstance->stream_cycle_channel(AVMEDIA_TYPE_VIDEO);
                pInstance->stream_cycle_channel(AVMEDIA_TYPE_AUDIO);
                pInstance->stream_cycle_channel(AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_t:
                pInstance->stream_cycle_channel(AVMEDIA_TYPE_SUBTITLE);
                break;
            case SDLK_w:
#if CONFIG_AVFILTER
                CHECKISB();
                {
                    if (is->vfilter_idx < nb_vfilters - 1) {
                        if (++is->vfilter_idx >= nb_vfilters)
                            is->vfilter_idx = 0;
                    } else {
                        is->vfilter_idx = 0;
                        pInstance->toggle_audio_display();
                    }
                }

#else
                pInstance->toggle_audio_display();
#endif
                break;
            case SDLK_PAGEUP:
                CHECKISBS(pInstance);
                {
                    if (is->ic->nb_chapters <= 1) {
                        incr = 600.0;
                        goto do_seek;
                    }
                }
                pInstance->seek_chapter(1);
                break;
            case SDLK_PAGEDOWN:
                CHECKISBS(pInstance);
                {
                    if(is->ic->nb_chapters <= 1) {
                        incr = -600.0;
                        goto do_seek;
                    }
                }
                pInstance->seek_chapter(-1);
                break;
            case SDLK_LEFT:
                incr = -10.0;
                goto do_seek;
            case SDLK_RIGHT:
                incr = 10.0;
                goto do_seek;
            case SDLK_UP:
                incr = 60.0;
                goto do_seek;
            case SDLK_DOWN:
                incr = -60.0;
            do_seek:
                CHECKISBS(pInstance);
                    if (CommonFun::seek_by_bytes) {
                        pos = -1;
                        if (pos < 0 && is->video_stream >= 0)
                            pos = pInstance->frame_queue_last_pos(&is->pictq);
                        if (pos < 0 && is->audio_stream >= 0)
                            pos = pInstance->frame_queue_last_pos(&is->sampq);
                        if (pos < 0)
                            pos = avio_tell(is->ic->pb);
                        if (is->ic->bit_rate)
                            incr *= is->ic->bit_rate / 8.0;
                        else
                            incr *= 180000.0;
                        pos += incr;
                        pInstance->stream_seek(pos, incr, 1);
                    } else {
                        pos = CommonFun::get_master_clock(is);
                        if (isnan(pos))
                            pos = (double)is->seek_pos / AV_TIME_BASE;
                        pos += incr;
                        if (is->ic->start_time != AV_NOPTS_VALUE && pos < is->ic->start_time / (double)AV_TIME_BASE)
                            pos = is->ic->start_time / (double)AV_TIME_BASE;
                        pInstance->stream_seek((int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
                    }
                break;
            default:
                break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            CHECKISBS(pInstance);
            LOG.Log(DebugLog,"mousedown event");
            if (event.button.button == SDL_BUTTON_LEFT) {
                static int64_t last_mouse_left_click = 0;
                if (av_gettime_relative() - last_mouse_left_click <= 500000) {
                    pInstance->toggle_full_screen();
                    is->force_refresh = 1;
                    last_mouse_left_click = 0;
                } else {
                    last_mouse_left_click = av_gettime_relative();
                }
            }
        case SDL_MOUSEMOTION:
            LOG.Log(DebugLog,"mousemotion event");
            /*if (cursor_hidden) {
                SDL_ShowCursor(1);
                cursor_hidden = 0;
            }*/
            CHECKISBS(pInstance);
            pInstance->cursor_last_shown = av_gettime_relative();
            if (event.type == SDL_MOUSEBUTTONDOWN) {
                if (event.button.button != SDL_BUTTON_RIGHT)
                    break;
                x = event.button.x;
            } else {
                if (!(event.motion.state & SDL_BUTTON_RMASK))
                    break;
                x = event.motion.x;
            }
                if (CommonFun::seek_by_bytes || is->ic->duration <= 0) {
                    uint64_t size =  avio_size(is->ic->pb);
                    pInstance->stream_seek(size*x/is->width, 0, 1);
                } else {
                    int64_t ts;
                    int ns, hh, mm, ss;
                    int tns, thh, tmm, tss;
                    tns  = is->ic->duration / 1000000LL;
                    thh  = tns / 3600;
                    tmm  = (tns % 3600) / 60;
                    tss  = (tns % 60);
                    frac = x / is->width;
                    ns   = frac * tns;
                    hh   = ns / 3600;
                    mm   = (ns % 3600) / 60;
                    ss   = (ns % 60);
                    LOG.Log(InfoLog,
                           "Seek to %2.0f%% (%2d:%02d:%02d) of total duration (%2d:%02d:%02d)       \n", frac*100,
                            hh, mm, ss, thh, tmm, tss);
                    ts = frac * is->ic->duration;
                    if (is->ic->start_time != AV_NOPTS_VALUE)
                        ts += is->ic->start_time;
                    pInstance->stream_seek(ts, 0, 0);
                }
            break;
        case SDL_WINDOWEVENT:
            LOG.Log(DebugLog,"windowEvent");
            CHECKISBS(pInstance);
            switch (event.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                    pInstance->screen_width  = is->width  = event.window.data1;
                    pInstance->screen_height = is->height = event.window.data2;
                    if (is->vis_texture) {
                        SDL_DestroyTexture(is->vis_texture);
                        is->vis_texture = NULL;
                        LOG.Log(DebugLog,"destroy texture");
                    }
                case SDL_WINDOWEVENT_EXPOSED:
                    is->force_refresh = 1;
                    LOG.Log(DebugLog,"forcerefresh");
            }
            break;
        case SDL_QUIT:
        case FF_QUIT_EVENT:
            LOG.Log(WarnLog,"quit\n\n");
            return 0;//CVideoProcess::Instance()->do_exit(renderer,window);
            break;
        default:
            break;
        }
    }
    return 0;
}

double CVideoDisplay::compute_target_delay(double delay)
{
    VideoState *is = m_pProcess->GetVideoState();
    if(!is)
    {
        return 0;
    }
    double sync_threshold, diff = 0;

    /* update delay to follow master synchronisation source */
    if (CommonFun::get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER) {
        /* if video is slave, we try to correct big delays by
           duplicating or deleting a frame */
        diff = CommonFun::get_clock(&is->vidclk) - CommonFun::get_master_clock(is);

        /* skip or repeat frame. We take into account the
           delay to compute the threshold. I still don't know
           if it is the best guess */
        sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
        if (!isnan(diff) && fabs(diff) < is->max_frame_duration) {
            if (diff <= -sync_threshold)
                delay = FFMAX(0, delay + diff);
            else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
                delay = delay + diff;
            else if (diff >= sync_threshold)
                delay = 2 * delay;
        }
    }

   // LOG.Log(DebugLog,  "video: delay=%0.3f A-V=%f\n",
           // delay, -diff);

    return delay;
}
double CVideoDisplay::vp_duration(Frame *vp, Frame *nextvp)
{
    VideoState *is = m_pProcess->GetVideoState();
    if(!is)
    {
        return 0.0;
    }
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
void CVideoDisplay::update_video_pts(double pts, int64_t pos, int serial)
{
    CHECKISV();
    /* update current video pts */
       CommonFun::set_clock(&is->vidclk, pts, serial);
       CommonFun::sync_clock_to_slave(&is->extclk, &is->vidclk);
}
int CVideoDisplay::video_open()
{
    if(window)
    {
        return 0;
    }
    int w,h;

    w = 352;
    h = 240;
    if(false == m_bInit)
    {
        m_bInit = true;
        if (SDL_VideoInit(NULL) < 0)
        {
            LOG.Log(ErrLog,"SDL_VideoInit failed");
            return NULL;
        }
        LOG.Log(InfoLog,"SDL_VideoInit success");
    }
    if (!window) {
        int flags = SDL_WINDOW_SHOWN;
        if (is_full_screen)
            //flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        //if (borderless)
            flags |= SDL_WINDOW_BORDERLESS;
        //flags |= SDL_WINDOW_UTILITY;
       // else
        //    flags |= SDL_WINDOW_RESIZABLE;
        //HWND h = winId();
        window = SDL_CreateWindowFrom((void *)winId());
        //window = SDL_CreateWindowFrom((void *)winId());
        //window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags);
        if(NULL == window)
        {
            LOG.Log(ErrLog,"SDL_CreateWindow failed");
            return 0;
        }
        //SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        if (window) {
            SDL_RendererInfo info;
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!renderer) {
                LOG.Log(ErrLog,  "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
                renderer = SDL_CreateRenderer(window, -1, 0);
            }
            if (renderer) {
                if (!SDL_GetRendererInfo(renderer, &info))
                    LOG.Log(InfoLog, "Initialized %s renderer", info.name);
                if (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED))
                {
                     //LOG.Log(InfoLog,"hideRenderer");
                    //renderer->hidden = SDL_TRUE;
                }
                else
                {
                    //LOG.Log(InfoLog,"showRender");
                            //renderer->hidden = SDL_FALSE;
                }
            }
            SDL_SetWindowSize(window, w, h);
            LOG.Log(InfoLog,"create Window success");
        }

    } else {
        SDL_SetWindowSize(window, w, h);
    }

    if (!window || !renderer) {
        LOG.Log(ErrLog, "SDL: could not set video mode - exiting\n");
       // do_exit(is);
    }

    CHECKISD();
    is->width  = w;
    is->height = h;

    return 0;
}
int CVideoDisplay::video_close()
{
    if(m_threadEvent)
    {
        if(m_pProcess)
        {
            VideoState *is = m_pProcess->GetVideoState();
            if(is)
            {
                is->abort_request = 1;
            }
        }
        SDL_WaitThread(m_threadEvent,NULL);
        m_threadEvent = NULL;
        LOG.Log(InfoLog,"terminate event_thread");
    }
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    if (window)
    {
        SDL_DestroyWindow(window);
        window = NULL;
    }

    return 0;
}
void CVideoDisplay::refresh_loop_wait_event(SDL_Event *event) {

    double remaining_time = 0.0;
    SDL_PumpEvents();
    while (!SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT)) {
        if (!cursor_hidden && av_gettime_relative() - cursor_last_shown > CURSOR_HIDE_DELAY) {
            SDL_ShowCursor(0);
            cursor_hidden = 1;
        }
        if (remaining_time > 0.0)
            av_usleep((int64_t)(remaining_time * 1000000.0));
        remaining_time = REFRESH_RATE;
        CHECKISV();
        if ((!is->paused || is->force_refresh))
        {
            video_refresh(&remaining_time);
        }
        SDL_PumpEvents();
    }
}
void CVideoDisplay::stream_cycle_channel(int codec_type)
{
    CHECKISV();
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    int old_index;
    AVStream *st;
    AVProgram *p = NULL;
    int nb_streams = is->ic->nb_streams;

    if (codec_type == AVMEDIA_TYPE_VIDEO) {
        start_index = is->last_video_stream;
        old_index = is->video_stream;
    } else if (codec_type == AVMEDIA_TYPE_AUDIO) {
        start_index = is->last_audio_stream;
        old_index = is->audio_stream;
    } else {
        start_index = is->last_subtitle_stream;
        old_index = is->subtitle_stream;
    }
    stream_index = start_index;

    if (codec_type != AVMEDIA_TYPE_VIDEO && is->video_stream != -1) {
        p = av_find_program_from_stream(ic, NULL, is->video_stream);
        if (p) {
            nb_streams = p->nb_stream_indexes;
            for (start_index = 0; start_index < nb_streams; start_index++)
                if (p->stream_index[start_index] == stream_index)
                    break;
            if (start_index == nb_streams)
                start_index = -1;
            stream_index = start_index;
        }
    }

    for (;;) {
        if (++stream_index >= nb_streams)
        {
            if (codec_type == AVMEDIA_TYPE_SUBTITLE)
            {
                stream_index = -1;
                is->last_subtitle_stream = -1;
                goto the_end;
            }
            if (start_index == -1)
                return;
            stream_index = 0;
        }
        if (stream_index == start_index)
            return;
        st = is->ic->streams[p ? p->stream_index[stream_index] : stream_index];
        if (st->codecpar->codec_type == codec_type) {
            /* check that parameters are OK */
            switch (codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (st->codecpar->sample_rate != 0 &&
                    st->codecpar->channels != 0)
                    goto the_end;
                break;
            case AVMEDIA_TYPE_VIDEO:
            case AVMEDIA_TYPE_SUBTITLE:
                goto the_end;
            default:
                break;
            }
        }
    }
 the_end:
    if (p && stream_index != -1)
        stream_index = p->stream_index[stream_index];
    LOG.Log(InfoLog, "Switch %s stream from #%d to #%d\n",
           av_get_media_type_string((AVMediaType)codec_type),
           old_index,
           stream_index);

    m_pProcess->stream_component_close(old_index);
    m_pProcess->stream_component_open(stream_index);
}
void CVideoDisplay::toggle_full_screen()
{
    is_full_screen = !is_full_screen;
    SDL_SetWindowFullscreen(window, is_full_screen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}
void CVideoDisplay::toggle_audio_display()
{
    CHECKISV();
    int next = is->show_mode;
    do {
        next = (next + 1) % VideoState::SHOW_MODE_NB;
    } while (next != is->show_mode && (next == VideoState::SHOW_MODE_VIDEO && !is->video_st || next != VideoState::SHOW_MODE_VIDEO && !is->audio_st));
    if (is->show_mode != next) {
        is->force_refresh = 1;
        is->show_mode = (VideoState::ShowMode)next;
    }
}
void CVideoDisplay::seek_chapter(int incr)
{
    CHECKISV();
   int64_t pos = CommonFun::get_master_clock(is) * AV_TIME_BASE;
   int i;

   if (!is->ic->nb_chapters)
       return;

   /* find the current chapter */
   AVRational avr;
   avr.num = 1;
   avr.den = AV_TIME_BASE;
   for (i = 0; i < is->ic->nb_chapters; i++) {
       AVChapter *ch = is->ic->chapters[i];


       if(av_compare_ts(pos,avr, ch->start, ch->time_base) < 0)
       {
           i--;
           break;
       }
   }

   i += incr;
   i = FFMAX(i, 0);
   if (i >= is->ic->nb_chapters)
       return;

   LOG.Log(ErrLog, "Seeking to chapter %d.\n", i);
   stream_seek(av_rescale_q(is->ic->chapters[i]->start, is->ic->chapters[i]->time_base,avr), 0, 0);
}
int64_t CVideoDisplay::frame_queue_last_pos(FrameQueue *f)
{
    Frame *fp = &f->queue[f->rindex];
    if (f->rindex_shown && fp->serial == f->pktq->serial)
        return fp->pos;
    else
        return -1;
}
void CVideoDisplay::event_loop()
{
    video_open();
    if(NULL == m_threadEvent)
    {
        m_threadEvent = SDL_CreateThread(ThreadEvent,"event_thread",this);
    }
    if(NULL == m_threadEvent)
    {
        LOG.Log(ErrLog,"create event thread failed");
    }

}
void CVideoDisplay::update_volume(int sign, double step)
{
    CHECKISV();
    double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
}
void CVideoDisplay::video_refresh(double *remaining_time)
{
    CHECKISV();
    double time;

    Frame *sp, *sp2;

    if (!is->paused && CommonFun::get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        CommonFun::check_external_clock_speed(is);

    if (is->audio_st) {
        time = av_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + CommonFun::rdftspeed < time) {
            video_display();
            is->last_vis_time = time;
        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + CommonFun::rdftspeed - time);
    }

    if (is->video_st) {
retry:
        if (CommonFun::frame_queue_nb_remaining(&is->pictq) == 0) {
            // nothing to do, no picture to display in the queue
        } else {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = CommonFun::frame_queue_peek_last(&is->pictq);
            vp = CommonFun::frame_queue_peek(&is->pictq);

            if (vp->serial != is->videoq.serial) {
                CommonFun::frame_queue_next(&is->pictq);
                goto retry;
            }

            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (is->paused)
                goto display;

            /* compute nominal last_duration */
            last_duration = vp_duration(lastvp, vp);
            delay = compute_target_delay(last_duration);

            time= av_gettime_relative()/1000000.0;
            if (time < is->frame_timer + delay) {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                goto display;
            }

            is->frame_timer += delay;
            if (delay > 0 && time - is->frame_timer > AV_SYNC_THRESHOLD_MAX)
                is->frame_timer = time;

            SDL_LockMutex(is->pictq.mutex);
            if (!isnan(vp->pts))
                update_video_pts(vp->pts, vp->pos, vp->serial);
            SDL_UnlockMutex(is->pictq.mutex);

            if (CommonFun::frame_queue_nb_remaining(&is->pictq) > 1) {
                Frame *nextvp = CommonFun::frame_queue_peek_next(&is->pictq);
                duration = vp_duration(vp, nextvp);
                if(!is->step && (CommonFun::framedrop>0 || (CommonFun::framedrop && CommonFun::get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)) && time > is->frame_timer + duration){
                    is->frame_drops_late++;
                    CommonFun::frame_queue_next(&is->pictq);
                    goto retry;
                }
            }

            if (is->subtitle_st) {
                    while (CommonFun::frame_queue_nb_remaining(&is->subpq) > 0) {
                        sp = CommonFun::frame_queue_peek(&is->subpq);

                        if (CommonFun::frame_queue_nb_remaining(&is->subpq) > 1)
                            sp2 = CommonFun::frame_queue_peek_next(&is->subpq);
                        else
                            sp2 = NULL;

                        if (sp->serial != is->subtitleq.serial
                                || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                                || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                        {
                            if (sp->uploaded) {
                                int i;
                                for (i = 0; i < sp->sub.num_rects; i++) {
                                    AVSubtitleRect *sub_rect = sp->sub.rects[i];
                                    uint8_t *pixels;
                                    int pitch, j;

                                    if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)&pixels, &pitch)) {
                                        for (j = 0; j < sub_rect->h; j++, pixels += pitch)
                                            memset(pixels, 0, sub_rect->w << 2);
                                        SDL_UnlockTexture(is->sub_texture);
                                    }
                                }
                            }
                            CommonFun::frame_queue_next(&is->subpq);
                        } else {
                            break;
                        }
                    }
            }

            CommonFun::frame_queue_next(&is->pictq);
            is->force_refresh = 1;

            if (is->step && !is->paused)
                stream_toggle_pause();
        }
display:
        /* display picture */
        if (is->force_refresh && is->pictq.rindex_shown)
        {
            video_display();
            //LOG.Log(DebugLog,"video_display");
        }
    }
    is->force_refresh = 0;
    //if (show_status) {
        static int64_t last_time;
        int64_t cur_time;
        int aqsize, vqsize, sqsize;
        double av_diff;

        cur_time = av_gettime_relative();
        if (!last_time || (cur_time - last_time) >= 30000) {
            aqsize = 0;
            vqsize = 0;
            sqsize = 0;
            if (is->audio_st)
                aqsize = is->audioq.size;
            if (is->video_st)
                vqsize = is->videoq.size;
            if (is->subtitle_st)
                sqsize = is->subtitleq.size;
            av_diff = 0;
            if (is->audio_st && is->video_st)
                av_diff = CommonFun::get_clock(&is->audclk) - CommonFun::get_clock(&is->vidclk);
            else if (is->video_st)
                av_diff = CommonFun::get_master_clock(is) - CommonFun::get_clock(&is->vidclk);
            else if (is->audio_st)
                av_diff = CommonFun::get_master_clock(is) - CommonFun::get_clock(&is->audclk);
           /* LOG.Log(DebugLog,
                   "%7.2f %s:%7.3f fd=%4d aq=%5dKB vq=%5dKB sq=%5dB f=%"PRId64"/%"PRId64"   \r",
                   CommonFun::get_master_clock(is),
                   (is->audio_st && is->video_st) ? "A-V" : (is->video_st ? "M-V" : (is->audio_st ? "M-A" : "   ")),
                   av_diff,
                   is->frame_drops_early + is->frame_drops_late,
                   aqsize / 1024,
                   vqsize / 1024,
                   sqsize,
                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_dts : 0,
                   is->video_st ? is->viddec.avctx->pts_correction_num_faulty_pts : 0);*/
            fflush(stdout);
            last_time = cur_time;
        }
    }

void CVideoDisplay::video_display()
{
    CHECKISV();
   /* if (!window)
    {
        video_open();
        LOG.Log(InfoLog,"window in NULL open video");
    }*/

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
   /* if (is->audio_st && is->show_mode != SHOW_MODE_VIDEO)
        video_audio_display(is);
    else */if (is->video_st)
    {
        video_image_display();
        //LOG.Log(DebugLog,"Image display");
    }
    //renderer->hidden = SDL_FALSE;
    SDL_RenderPresent(renderer);
}

void CVideoDisplay::video_image_display()
{
    CHECKISV();
    Frame *vp;
    Frame *sp = NULL;
    SDL_Rect rect;

    vp = CommonFun::frame_queue_peek_last(&is->pictq);
    if (is->subtitle_st) {
        if (CommonFun::frame_queue_nb_remaining(&is->subpq) > 0) {
            sp = CommonFun::frame_queue_peek(&is->subpq);

            if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000)) {
                if (!sp->uploaded) {
                    uint8_t* pixels[4];
                    int pitch[4];
                    int i;
                    if (!sp->width || !sp->height) {
                        sp->width = vp->width;
                        sp->height = vp->height;
                    }
                    if (realloc_texture(&is->sub_texture, SDL_PIXELFORMAT_ARGB8888, sp->width, sp->height, SDL_BLENDMODE_BLEND, 1) < 0)
                    {
                        LOG.Log(ErrLog,"realloc_texture failed");
                        return;
                    }

                    for (i = 0; i < sp->sub.num_rects; i++) {
                        AVSubtitleRect *sub_rect = sp->sub.rects[i];

                        sub_rect->x = av_clip(sub_rect->x, 0, sp->width );
                        sub_rect->y = av_clip(sub_rect->y, 0, sp->height);
                        sub_rect->w = av_clip(sub_rect->w, 0, sp->width  - sub_rect->x);
                        sub_rect->h = av_clip(sub_rect->h, 0, sp->height - sub_rect->y);

                        is->sub_convert_ctx = sws_getCachedContext(is->sub_convert_ctx,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_PAL8,
                            sub_rect->w, sub_rect->h, AV_PIX_FMT_BGRA,
                            0, NULL, NULL, NULL);
                        if (!is->sub_convert_ctx) {
                            LOG.Log(ErrLog, "Cannot initialize the conversion context\n");
                            return;
                        }
                        if (!SDL_LockTexture(is->sub_texture, (SDL_Rect *)sub_rect, (void **)pixels, pitch)) {
                            sws_scale(is->sub_convert_ctx, (const uint8_t * const *)sub_rect->data, sub_rect->linesize,
                                      0, sub_rect->h, pixels, pitch);
                            SDL_UnlockTexture(is->sub_texture);
                        }
                    }
                    sp->uploaded = 1;
                }
            } else
            {
                sp = NULL;
                LOG.Log(ErrLog,"sp is NULL in Image display");
            }
        }
    }
    return;
    CommonFun::calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);
     if (!vp->uploaded) {
        int sdl_pix_fmt = vp->frame->format == AV_PIX_FMT_YUV420P ? SDL_PIXELFORMAT_YV12 : SDL_PIXELFORMAT_ARGB8888;
        if (realloc_texture(&is->vid_texture, sdl_pix_fmt, vp->frame->width, vp->frame->height, SDL_BLENDMODE_NONE, 0) < 0)
        {
            LOG.Log(ErrLog,"realloc_texture failed");
            return;
        }
        if (upload_texture(is->vid_texture, vp->frame, &is->img_convert_ctx) < 0) 
        {
            LOG.Log(ErrLog,"upload_texture failed");
            return;
        }
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
       // LOG.Log(InfoLog,"!uploaded");
    }
    // LOG.Log(DebugLog,"rect x:%d y:%d width:%d height:%d is.width:%d is.height:%d vp.width:%d vp.height:%d ",rect.x,rect.y,rect.w,rect.h,is->width,is->height,vp->width,vp->height);

    SDL_RenderCopyEx(renderer, is->vid_texture, NULL, &rect, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : (SDL_RendererFlip)0);
    if (SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED))
    {
        // LOG.Log(InfoLog,"hideRenderer");
        //renderer->hidden = SDL_TRUE;
    }
    else
    {
        //LOG.Log(InfoLog,"showRender");
                //renderer->hidden = SDL_FALSE;
    }

    if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER

        SDL_RenderCopy(renderer, is->sub_texture, NULL, &rect);

#else
        int i;
        double xratio = (double)rect.w / (double)sp->width;
        double yratio = (double)rect.h / (double)sp->height;
        LOG.Log(DebugLog,"sub.num.rects:%d",sp->sub.num_rects);

        for (i = 0; i < sp->sub.num_rects; i++) {
            SDL_Rect *sub_rect = (SDL_Rect*)sp->sub.rects[i];
            SDL_Rect target = {.x = rect.x + sub_rect->x * xratio,
                               .y = rect.y + sub_rect->y * yratio,
                               .w = sub_rect->w * xratio,
                               .h = sub_rect->h * yratio};
            SDL_RenderCopy(renderer, is->sub_texture, sub_rect, &target);
        }
#endif
    }
}
int CVideoDisplay::upload_texture(SDL_Texture *tex, AVFrame *frame, struct SwsContext **img_convert_ctx)
{
  int ret = 0;
  switch (frame->format) {
      case AV_PIX_FMT_YUV420P:
          if (frame->linesize[0] < 0 || frame->linesize[1] < 0 || frame->linesize[2] < 0) {
              LOG.Log(ErrLog,  "Negative linesize is not supported for YUV.\n");
              return -1;
          }
          ret = SDL_UpdateYUVTexture(tex, NULL, frame->data[0], frame->linesize[0],
                                                frame->data[1], frame->linesize[1],
                                                frame->data[2], frame->linesize[2]);
          break;
      case AV_PIX_FMT_BGRA:
          if (frame->linesize[0] < 0) {
              ret = SDL_UpdateTexture(tex, NULL, frame->data[0] + frame->linesize[0] * (frame->height - 1), -frame->linesize[0]);
          } else {
              ret = SDL_UpdateTexture(tex, NULL, frame->data[0], frame->linesize[0]);
          }
          break;
      default:
          /* This should only happen if we are not using avfilter... */
          *img_convert_ctx = sws_getCachedContext(*img_convert_ctx,
              frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height,
              AV_PIX_FMT_BGRA, sws_flags, NULL, NULL, NULL);
          if (*img_convert_ctx != NULL) {
              uint8_t *pixels[4];
              int pitch[4];
              if (!SDL_LockTexture(tex, NULL, (void **)pixels, pitch)) {
                  sws_scale(*img_convert_ctx, (const uint8_t * const *)frame->data, frame->linesize,
                            0, frame->height, pixels, pitch);
                  SDL_UnlockTexture(tex);
              }
          } else {
              LOG.Log(ErrLog, "Cannot initialize the conversion context\n");
              ret = -1;
          }
          break;
  }
  return ret;
}
int CVideoDisplay::realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture)
{
    Uint32 format;
    int access, w, h;
    if (SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format) {
        void *pixels;
        int pitch;
        SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
            return -1;
        if (SDL_SetTextureBlendMode(*texture, blendmode) < 0)
            return -1;
        if (init_texture) {
            if (SDL_LockTexture(*texture, NULL, &pixels, &pitch) < 0)
                return -1;
            memset(pixels, 0, pitch * new_height);
            SDL_UnlockTexture(*texture);
        }
    }
    return 0;
}
void CVideoDisplay::toggle_mute()
{
    VideoState *is = m_pProcess->GetVideoState();
    if(is)
    {
         is->muted = !is->muted;
    }
}
void CVideoDisplay::toggle_pause()
{

    stream_toggle_pause();
    VideoState *is = m_pProcess->GetVideoState();
    if(is)
    {
         is->step = 0;
    }
}
void CVideoDisplay::stream_toggle_pause()
{
    VideoState *is = m_pProcess->GetVideoState();
    if(!is)
    {
         return;
    }
    if (is->paused) {
         is->frame_timer += av_gettime_relative() / 1000000.0 - is->vidclk.last_updated;
         if (is->read_pause_return != AVERROR(ENOSYS)) {
             is->vidclk.paused = 0;
         }
         CommonFun::set_clock(&is->vidclk, CommonFun::get_clock(&is->vidclk), is->vidclk.serial);
     }
     CommonFun::set_clock(&is->extclk, CommonFun::get_clock(&is->extclk), is->extclk.serial);
     is->paused = is->audclk.paused = is->vidclk.paused = is->extclk.paused = !is->paused;
}

void CVideoDisplay::stream_seek(int64_t pos, int64_t rel, int seek_by_bytes)
{
    VideoState *is = m_pProcess->GetVideoState();
    if(!is)
    {
         return;
    }
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
void CVideoDisplay::resizeEvent(QResizeEvent *event)
{
    if(window)
    {
        //event->size();
       // QSize sz = event->size();
        SDL_SetWindowSize(window,event->size().width(),event->size().height());
    }
    QWidget::resizeEvent(event);
}
void CVideoDisplay::moveEvent(QMoveEvent *event)
{
    if(window)
    {

    }
    QWidget::moveEvent(event);
}
