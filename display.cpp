
#include <QMouseEvent>
#include <QDesktopWidget>
#include <QApplication>
#include <QFile>
#include <QUiLoader>
#include <QVBoxLayout>
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
is = pInstance->m_pProcess->GetVideoState(); \
if(!is)                                       \
{                                             \
        break;                                \
                                              \
}

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)

//bool CVideoDisplay::m_bInit = false;
CVideoDisplay::CVideoDisplay(CVideoProcess *pProcess, QWidget *pParent):QWidget(pParent)
  ,m_pProcess(pProcess)
  ,m_window(NULL)
  ,m_renderer(NULL)
  ,m_screenWidth(352)
  ,m_screenHeight(244)
  ,m_w(352)
  ,m_h(244)
  ,m_threadEvent(NULL)
  ,m_eState(EUnknow)
  ,m_bFirstShow(true)
  ,m_pMuRender(NULL)
  ,m_pCtrBar(NULL)
  ,m_pPosWindow(NULL)
  ,m_pCurLabel(NULL)
  ,m_pTotalLabel(NULL)
  ,m_pPlaySlider(NULL)
  ,m_pVolumeSlider(NULL)
  ,m_pLayOut(NULL)
  ,m_bFull(false)
  ,m_fPlayRate(1.0)
  ,m_bTimePress(false)
{

    if(pParent)
    {
        m_w = pParent->size().width();
        m_h = pParent->size().height();
        LOG.Log(DebugLog,"CVideoDisplay w:%d h:%d",m_w,m_h);
    }
    QDesktopWidget* pdesk = QApplication::desktop();
    if (pdesk)
    {
        int nIndex = pdesk->screenNumber();
        QRect rcScreen = pdesk->screenGeometry(nIndex);
        m_screenWidth = rcScreen.width();
        m_screenHeight = rcScreen.height();
    }
    m_pMuRender = SDL_CreateMutex();
    if(NULL == m_pMuRender)
    {
        LOG.Log(ErrLog,"Create Mutex failed");
    }
    setFocusPolicy(Qt::ClickFocus);

    setGeometry(0,0,m_w,m_h);
}
CVideoDisplay::~CVideoDisplay()
{
    LOG.Log(InfoLog,"CVideoDisplay destructor");
    if(m_threadEvent)
    {
        SDL_WaitThread(m_threadEvent,NULL);
        m_threadEvent = NULL;
    }
    if(m_pCtrBar)
    {
        m_pCtrBar->hide();
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
    PlayState eState = EUnknow;

    for (;;)
    {
        pInstance->GetState(eState);

        if(EStop == eState)
        {
            LOG.Log(InfoLog,"m_bStop is true in threadEvent");
            return 0;
        }
        CHECKISB();
        double x;
        if(pInstance->IsFirstShow())
        {
            //Sleep(50);
            LOG.Log(DebugLog,"Emit ShowVideo");
            pInstance->m_pProcess->EmitVideo();
            Sleep(100);
        }
        else
        {
            LOG.Log(DebugLog,"Call ShowVideo");

            pInstance->ShowVideo();
        }
       /* if(pInstance->m_pProcess)
        {
            pInstance->m_pProcess->audio_decode_frame();
        }*/

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
    if (CommonFun::get_master_sync_type(is) != AV_SYNC_VIDEO_MASTER)
    {
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
int CVideoDisplay::window_open(int w/* = 352*/,int h/*=240*/)
{
     /*if(false == m_bInit)
    {
        m_bInit = true;
       if (SDL_VideoInit(NULL) < 0)
        {
            LOG.Log(ErrLog,"SDL_VideoInit failed");
            return NULL;
        }

        LOG.Log(InfoLog,"SDL_VideoInit success");
    }*/

    if (!m_window)
    {
         m_window = SDL_CreateWindowFrom((void *)winId());
        //window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags);
        if(NULL == m_window)
        {
            LOG.Log(ErrLog,"SDL_CreateWindow failed");
            return 0;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        if (m_window)
        {
            SDL_RendererInfo info;
            m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (!m_renderer) {
                LOG.Log(ErrLog,  "Failed to initialize a hardware accelerated renderer: %s\n", SDL_GetError());
                m_renderer = SDL_CreateRenderer(m_window, -1, 0);
            }

            SDL_SetWindowSize(m_window, w, h);
            LOG.Log(InfoLog,"create Window success w:%d h:%d",w,h);
        }
    }
    else
    {
        SDL_SetWindowSize(m_window, w, h);
    }

    if (!m_window || !m_renderer)
    {
        LOG.Log(ErrLog, "SDL: could not set video mode - exiting\n");
    }
    SetState(EUnknow);
   // m_eState = EUnknow;
    CHECKISD();
    is->width  = w;
    is->height = h;

    SetFirstShow(false);

    return 0;
}
int CVideoDisplay::video_close()
{
    SetState(EStop);
    LOG.Log(InfoLog,"video_close");
    if(m_threadEvent)
    {
        SDL_WaitThread(m_threadEvent,NULL);
        m_threadEvent = NULL;
        LOG.Log(InfoLog,"VideoDisplay video_close");
    }

    return 0;
}
void CVideoDisplay::window_close()
{
    SDL_LockMutex(m_pMuRender);
    if (m_renderer)
    {
        SDL_DestroyRenderer(m_renderer);
        LOG.Log(InfoLog,"destroy render in video_close");
        m_renderer = NULL;
    }
    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        LOG.Log(InfoLog,"destroy window in video_close");
        m_window = NULL;
    }
    SDL_UnlockMutex(m_pMuRender);
    SetFirstShow(true);
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

void CVideoDisplay::event_loop()
{
    if(NULL == m_threadEvent)
    {
        m_threadEvent = SDL_CreateThread(ThreadEvent,"event_thread",this);
        LOG.Log(InfoLog,"create event thread");
    }
    if(NULL == m_threadEvent)
    {
        LOG.Log(ErrLog,"create event thread failed");
    }
    LOG.Log(InfoLog,"create event thread success");

    return ;
}
void CVideoDisplay::update_volume(int sign, double step)
{
    CHECKISV();
    double volume_level = is->audio_volume ? (20 * log(is->audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    is->audio_volume = av_clip(is->audio_volume == new_volume ? (is->audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
    new_volume = is->audio_volume;
}
void CVideoDisplay::video_refresh(double *remaining_time)
{
    CHECKISV();
    double time;

    Frame *sp, *sp2;

    if (!is->paused && CommonFun::get_master_sync_type(is) == AV_SYNC_EXTERNAL_CLOCK && is->realtime)
        CommonFun::check_external_clock_speed(is);

 /* if (is->audio_st)
    {
        time = av_gettime_relative() / 1000000.0;
        if (is->force_refresh || is->last_vis_time + CommonFun::rdftspeed < time)
        {
            video_display();
            is->last_vis_time = time;
            //LOG.Log(DebugLog,"force_refresh:%d last_vis_time:%f time:%f",is->force_refresh,is->last_vis_time,time);

        }
        *remaining_time = FFMIN(*remaining_time, is->last_vis_time + CommonFun::rdftspeed - time);
    }*/

    if (is->video_st)
    {
retry:
        if (CommonFun::frame_queue_nb_remaining(&is->pictq) == 0)
        {
            LOG.Log(DebugLog,"nothing to do, no picture to display in the queue pause:%d realtime:%d",
                    is->paused,is->realtime);
        }
        else
        {
            double last_duration, duration, delay;
            Frame *vp, *lastvp;

            /* dequeue the picture */
            lastvp = CommonFun::frame_queue_peek_last(&is->pictq);
            vp = CommonFun::frame_queue_peek(&is->pictq);

            if (vp->serial != is->videoq.serial)
            {
                LOG.Log(DebugLog,"pictq next 1");
                CommonFun::frame_queue_next(&is->pictq);
                goto retry;
            }
            //LOG.Log(DebugLog,"last serial:%d now serial:%d",lastvp->serial,vp->serial);
            if (lastvp->serial != vp->serial)
                is->frame_timer = av_gettime_relative() / 1000000.0;

            if (is->paused)
            {
                LOG.Log(DebugLog,"pause is true");
                goto display;
            }

            /* compute nominal last_duration */
            last_duration = vp_duration(lastvp, vp);
            delay = compute_target_delay(last_duration)/m_fPlayRate;

            time= av_gettime_relative()/1000000.0;

            if (time < is->frame_timer + delay)
            {
                *remaining_time = FFMIN(is->frame_timer + delay - time, *remaining_time);
                LOG.Log(DebugLog,"last_duration:%f delay:%f frame_timer:%f remaining_time:%f time:%f",
                        last_duration,delay,is->frame_timer,*remaining_time,time);
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

            if (is->subtitle_st)
            {
                while (CommonFun::frame_queue_nb_remaining(&is->subpq) > 0)
                {
                    sp = CommonFun::frame_queue_peek(&is->subpq);

                    if (CommonFun::frame_queue_nb_remaining(&is->subpq) > 1)
                        sp2 = CommonFun::frame_queue_peek_next(&is->subpq);
                    else
                        sp2 = NULL;

                    if (sp->serial != is->subtitleq.serial
                            || (is->vidclk.pts > (sp->pts + ((float) sp->sub.end_display_time / 1000)))
                            || (sp2 && is->vidclk.pts > (sp2->pts + ((float) sp2->sub.start_display_time / 1000))))
                    {
                        if (sp->uploaded)
                        {
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
                        LOG.Log(DebugLog,"pictq next 3");
                        CommonFun::frame_queue_next(&is->subpq);
                    }
                    else
                    {
                        break;
                    }
                }
            }
            LOG.Log(DebugLog,"pictq 4");
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
        }
    }
    is->force_refresh = 0;

    /*static int64_t last_time;
    int64_t cur_time;
    int aqsize, vqsize, sqsize;
    double av_diff;

    cur_time = av_gettime_relative();
    if (!last_time || (cur_time - last_time) >= 30000)
    {
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

        fflush(stdout);
        last_time = cur_time;
    }*/

}

void CVideoDisplay::video_display()
{
    if(m_pProcess)
    {
       // m_pProcess->audio_decode_frame();
    }
    CHECKISV();
    SDL_LockMutex(m_pMuRender);
    if (!m_window)
    {
        if(isFullScreen())
        {
            window_open(m_screenWidth,m_screenHeight);
        }
        else
        {
            window_open(m_w,m_h);
        }
        //window_open(1280,1024);
        LOG.Log(InfoLog,"window in NULL open video");
    }

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);
    if (is->video_st)
    {
        video_image_display();
    }

    SDL_RenderPresent(m_renderer);
    SDL_UnlockMutex(m_pMuRender);
}

void CVideoDisplay::video_image_display()
{
    CHECKISV();
    Frame *vp;
    Frame *sp = NULL;
    SDL_Rect rect;

    vp = CommonFun::frame_queue_peek_last(&is->pictq);
    int nT = vp->pts+0.5;
    if(m_pProcess && (nT != m_pProcess->GetCurTime()))
    {
        m_pProcess->SetCurTime(nT);
    }
    LOG.Log(DebugLog,"last pts:%f pos:%64d rindex:%d",vp->pts,vp->pos,is->pictq.rindex);
    if (is->subtitle_st)
    {
        if (CommonFun::frame_queue_nb_remaining(&is->subpq) > 0)
        {
            sp = CommonFun::frame_queue_peek(&is->subpq);
            LOG.Log(DebugLog,"now pts:%f pos:%d start_display_time:%f end_display:%f",sp->pts,sp->pos,
                    sp->sub.start_display_time/1000,sp->sub.end_display_time);
            if (vp->pts >= sp->pts + ((float) sp->sub.start_display_time / 1000))
            {
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

    CommonFun::calculate_display_rect(&rect, is->xleft, is->ytop, is->width, is->height, vp->width, vp->height, vp->sar);
     if (!vp->uploaded)
     {
         LOG.Log(DebugLog,"frame width:%d frame height:%d",vp->frame->width,vp->frame->height);
        int sdl_pix_fmt = vp->frame->format == AV_PIX_FMT_YUV420P ? SDL_PIXELFORMAT_YV12 : SDL_PIXELFORMAT_ARGB8888;
        if (realloc_texture(&is->vid_texture, sdl_pix_fmt, vp->frame->width, vp->frame->height, SDL_BLENDMODE_NONE, 0) < 0)
        {
            LOG.Log(ErrLog,"realloc_texture failed");
            return;
        }
        else
        {
           LOG.Log(DebugLog,"realloc_texture success");
        }
        if (upload_texture(is->vid_texture, vp->frame, &is->img_convert_ctx) < 0) 
        {
            LOG.Log(ErrLog,"upload_texture failed");
            return;
        }
        else
        {
            LOG.Log(DebugLog,"upload_texture success");
        }
        vp->uploaded = 1;
        vp->flip_v = vp->frame->linesize[0] < 0;
    }

    int nRet = SDL_RenderCopy(m_renderer,is->vid_texture,NULL,&rect);//SDL_RenderCopyEx(renderer, is->vid_texture, NULL, &rect, 0, NULL, vp->flip_v ? SDL_FLIP_VERTICAL : (SDL_RendererFlip)0);
    if(nRet <0)
    {
        const char *errInfo = SDL_GetError();
        LOG.Log(ErrLog,"RenderCopyEx error:%s",errInfo);
    }


    if (sp) {
#if USE_ONEPASS_SUBTITLE_RENDER

        nRet = SDL_RenderCopy(m_renderer, is->sub_texture, NULL, &rect);
        LOG.Log(DebugLog,"SDL_RenderCopy nRet:%d",nRet);

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
    if (SDL_QueryTexture(*texture, &format, &access, &w, &h) < 0 || new_width != w || new_height != h || new_format != format)
    {
        void *pixels;
        int pitch;
        SDL_DestroyTexture(*texture);
        if (!(*texture = SDL_CreateTexture(m_renderer, new_format, SDL_TEXTUREACCESS_STREAMING, new_width, new_height)))
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
void CVideoDisplay::Stop()
{
    if(m_pProcess)
    {
        m_pProcess->EmitStop();
    }
}
void CVideoDisplay::FastPlay()
{
    m_fPlayRate+=0.1;
    if(m_fPlayRate > 5.0)
    {
        m_fPlayRate = 5;
    }
   if(m_pProcess)
   {
       m_pProcess->SetPlayRate(m_fPlayRate);

   }
}
void CVideoDisplay::SlowPlay()
{
    m_fPlayRate -= 0.1;
    if(m_fPlayRate < 0.1)
    {
        m_fPlayRate = 0.1;
    }
    if(m_pProcess)
    {
        m_pProcess->SetPlayRate(m_fPlayRate);
    }
}
void CVideoDisplay::TimeRange(int nMin, int nMax)
{
    if(m_pPlaySlider)
    {
        m_pPlaySlider->setRange(nMin,nMax);
    }
    if(m_pCurLabel && m_pTotalLabel)
    {

        if(nMax < 60)
        {
            QString str = QString::asprintf("/%d",nMax);
            m_pCurLabel->setNum(nMin);
            m_pTotalLabel->setText(str);
        }
        else if(nMax/60 < 100)
        {
            QString str = QString::asprintf("/%02d:%02d",nMax/60,nMax%60);
            m_pTotalLabel->setText(str);
            str = QString::asprintf("%02d:%02d",nMin/60,nMin%60);
            m_pCurLabel->setText(str);
        }
        else
        {
            QString str = QString::asprintf("/%03d:%02d",nMax/60,nMax%60);
            m_pTotalLabel->setText(str);
            str = QString::asprintf("%02d:%02d",nMin/60,nMin%60);
            m_pCurLabel->setText(str);
        }
    }
}
void CVideoDisplay::TimeValue(int nValue)
{
    if(m_pCurLabel)
    {
        if(nValue < 60)
        {
            m_pCurLabel->setNum(nValue);
        }
        else if(nValue/60 < 100)
        {
            QString str = QString::asprintf("%02d:%02d",nValue/60,nValue%60);
            m_pCurLabel->setText(str);
        }
        else
        {
            QString str = QString::asprintf("%03d:%02d",nValue/60,nValue%60);
            m_pCurLabel->setText(str);
        }
    }
    if(m_bTimePress)
    {
        return;
    }
    if(m_pPlaySlider)
    {
        m_pPlaySlider->setValue(nValue);
    }

}
void CVideoDisplay::TimeReleased()
{
    if(m_pPlaySlider)
    {

        if(m_pProcess)
        {
            double nOld = m_pProcess->GetCurTime();
            double nValue = m_pPlaySlider->value();
            double nSeek = nValue - nOld;
            m_pProcess->stream_seek(nSeek,0);
        }
        //TimeValue(nValue);
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
void CVideoDisplay::SetState(PlayState eState)
{
    m_eState = eState;
}
PlayState CVideoDisplay::GetState(PlayState & eState)
{
    eState = m_eState;
    return m_eState;
}
void CVideoDisplay::ClearLayout()
{
    if(m_pLayOut)
    {
        parentWidget()->setLayout(NULL);
        delete m_pLayOut;
        m_pLayOut = NULL;
    }
}
void CVideoDisplay::ShowCtrBar()
{
    if(m_pCtrBar)
    {

        if(m_pPosWindow)
        {
            //m_pCtrBar->setWindowFlags(Qt::WindowStaysOnTopHint |Qt::FramelessWindowHint|Qt::SubWindow);
            QPoint pt;
            pt = m_pPosWindow->position();
            pt = m_pPosWindow->mapToGlobal(pt);
            int y = pt.y()+height()-m_pCtrBar->height();
            m_pCtrBar->setGeometry(pt.x(),y,width(),m_pCtrBar->height());
        }
       // m_pCtrBar->setParent(this);
        m_pCtrBar->show();
    }
}
void CVideoDisplay::SetCtrBar(QWidget *pCtrBar)
{
    m_pCtrBar = pCtrBar;
    if(m_pCtrBar)
    {
        m_pPosWindow = QWindow::fromWinId(winId());
        m_pCtrBar->installEventFilter(this);
        m_pCurLabel = m_pCtrBar->findChild<QLabel *>("curLabel");
        m_pTotalLabel = m_pCtrBar->findChild<QLabel *>("totalLabel");
        m_pPlaySlider = m_pCtrBar->findChild<QSlider *>("timeSlider");
        if(m_pCurLabel)
        {
            m_pCurLabel->setText("");
        }
        if(m_pTotalLabel)
        {
            m_pTotalLabel->setText("");
        }
        if(m_pPlaySlider)
        {
            m_pPlaySlider->installEventFilter(this);
            m_pPlaySlider->setValue(0);
        }
        m_pVolumeSlider= m_pCtrBar->findChild<QSlider *>("volumeSlider");
        if(m_pVolumeSlider)
        {
            m_pVolumeSlider->installEventFilter(this);
            m_pVolumeSlider->setRange(0,SDL_MIX_MAXVOLUME);
            m_pVolumeSlider->setValue(SDL_MIX_MAXVOLUME);
        }
        //connect((QObject *)m_pPlaySlider,SIGNAL(sliderReleased()),(QObject *)this,SLOT(TimeReleased()));
    }
}
void CVideoDisplay::SetVolume(int nValue)
{
    CHECKISV();
    is->audio_volume = av_clip(nValue,0, SDL_MIX_MAXVOLUME);
}
void CVideoDisplay::ShowVideo()
{

    LOG.Log(DebugLog,"ShowVideo");
    double remaining_time = REFRESH_RATE;
    CHECKISV();

    if (remaining_time > 0.0)
                av_usleep((int64_t)(remaining_time * 1000000.0));
    if ((!is->paused || is->force_refresh))
    {
        video_refresh(&remaining_time);
    }


}
void CVideoDisplay::mouseDoubleClickEvent(QMouseEvent *event)
{
    QWidget::mouseDoubleClickEvent(event);
    LOG.Log(DebugLog,"mouseDoubleClickEvent");
    FullScreenVideo();
}
void CVideoDisplay::mousePressEvent(QMouseEvent *event)
{
    LOG.Log(DebugLog,"mousePressEvent button:%d",event->button());
    if(Qt::LeftButton == event->button())
    {
        toggle_pause();
    }
    QWidget::mousePressEvent(event);

}
void CVideoDisplay::keyPressEvent(QKeyEvent *event)
{
    LOG.Log(DebugLog,"code:%x key:%s",event->key(),event->text().toStdString().c_str());
    double incr=0.0;
    CHECKISV();
    switch(event->key())
    {
    case 65://key 'a'
        stream_cycle_channel(AVMEDIA_TYPE_AUDIO);
        break;
    case 67://key 'c'
        stream_cycle_channel(AVMEDIA_TYPE_AUDIO);
        stream_cycle_channel(AVMEDIA_TYPE_VIDEO);
        stream_cycle_channel(AVMEDIA_TYPE_SUBTITLE);
        break;
    case 70://key 'f' or 'F'
        FullScreenVideo();
        break;
    case 77://key 'm' or 'M'
        toggle_mute();
        break;
    case 80://key 'p' or 'P'
        toggle_pause();
        break;
    case 81://keq 'q' or 'Q'
        if(m_pProcess)
        {
            m_pProcess->EmitStop();
        }
        break;
    case 83://key 's' or 'S' Step to next frame
        if(m_pProcess)
        {
            m_pProcess->step_to_next_frame();
        }
        break;
    case 0x2b://key '+'
        update_volume(1, SDL_VOLUME_STEP);
        break;
    case 0x2d://key '-'
        update_volume(-1, SDL_VOLUME_STEP);
        break;
    case 84://key 't'
        stream_cycle_channel(AVMEDIA_TYPE_SUBTITLE);
        break;
    case 86://key 'v'
        stream_cycle_channel(AVMEDIA_TYPE_VIDEO);
        break;
    case 0x1000016://page up

        if(is->ic->nb_chapters <= 1)
        {
            incr = 600.0;
            goto do_seek;
        }
        break;
    case 0x1000017://page down

        if(is->ic->nb_chapters <= 1)
        {
            incr = -600.0;
            goto do_seek;
        }
        break;
    case 0x1000013://up
        incr = 60.0;
        goto do_seek;
        break;
    case 0x1000015://down
        incr = -60.0;
        goto do_seek;
        break;
    case 0x1000012://left
        incr = -10.0;
        goto do_seek;
        break;
    case 0x1000014://right
        incr = 10.0;
        goto do_seek;
        break;

    do_seek:
       if(m_pProcess)
       {
          m_pProcess->stream_seek(incr,0);
       }
       break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void CVideoDisplay::enterEvent(QEvent *event)
{

   ShowCtrBar();
}
void CVideoDisplay::leaveEvent(QEvent *event)
{
    if(!m_bFull && m_pCtrBar)
    {
        QPoint pt = QCursor::pos();
        pt = mapFromGlobal(pt);
        if(!geometry().contains(pt))
        {
              m_pCtrBar->hide();
        }
    }
}
bool CVideoDisplay::eventFilter(QObject *watched, QEvent *event)
{
    if(watched == m_pCtrBar && QEvent::Leave == event->type())
    {
        leaveEvent(event);

    }
    else if(watched == m_pPlaySlider)
    {
        if(QEvent::MouseButtonRelease == event->type())
        {
            m_bTimePress = false;
            TimeReleased();
        }
        else if(QEvent::MouseButtonPress == event->type())
        {
            m_bTimePress = true;
        }

    }
    else if(watched == m_pVolumeSlider && QEvent::MouseButtonRelease == event->type())
    {
        int nValue = m_pVolumeSlider->value();
        SetVolume(nValue);
    }
    return false;
}


void CVideoDisplay::FullScreenVideo()
{


    window_close();
    SDL_LockMutex(m_pMuRender);
    if(NULL == m_pLayOut)
    {
        m_pLayOut = new QVBoxLayout();
        if(m_pLayOut)
        {
            m_pLayOut->setContentsMargins(0,0,0,0);
            m_pLayOut->addWidget(this);
            parentWidget()->setLayout(m_pLayOut);
        }
    }
    if(isFullScreen())
    {

        m_bFull = false;
        setWindowFlags(Qt::Widget);
        showNormal();
    }
    else
    {
        m_bFull = true;
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        showFullScreen();
    }
    if(m_pLayOut)
    {
        parentWidget()->setLayout(NULL);
        delete m_pLayOut;
        m_pLayOut = NULL;
    }
    SDL_UnlockMutex(m_pMuRender);
    ShowCtrBar();
}
