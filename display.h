#ifndef DISPLAY_H
#define DISPLAY_H
#include <QWidget>
#include <QFrame>
#include <QWindow>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include "process.h"

class CVideoDisplay : public QWidget
{  
    Q_OBJECT
public:
    CVideoDisplay(CVideoProcess *pProcess,QWidget *pParent = NULL);
    ~CVideoDisplay();
    static int ThreadEvent(void *p);

    void SetVideoState(VideoState *videoS);
    double compute_target_delay(double delay);
    double vp_duration(Frame *vp, Frame *nextvp);
    void update_video_pts(double pts, int64_t pos, int serial);
    int window_open(int w = 352,int h=240);
    int video_close();
    void window_close();
    void video_refresh(double *remaining_time);
    void video_display();
    void video_image_display();
    int upload_texture(SDL_Texture *tex, AVFrame *frame, struct SwsContext **img_convert_ctx);
    int realloc_texture(SDL_Texture **texture, Uint32 new_format, int new_width, int new_height, SDL_BlendMode blendmode, int init_texture);

    void stream_toggle_pause();

    void toggle_audio_display();
    void stream_seek(int64_t pos, int64_t rel, int seek_by_bytes);
    void event_loop();
    void update_volume(int sign, double step);
    void stream_cycle_channel(int codec_type);
    SDL_Renderer *GetRender(){return m_renderer;}
    SDL_Window *GetWindow(){return m_window;}
    void SetState(PlayState eState);
    PlayState GetState(PlayState &eState);
    void ClearLayout();


    bool IsFirstShow(){return m_bFirstShow;}
    void SetFirstShow(bool bFirst) {m_bFirstShow = bFirst;}
    void ShowCtrBar();
    void SetCtrBar(QWidget *pCtrBar);
    void SetVolume(int nValue);
private:

    virtual void mouseDoubleClickEvent(QMouseEvent *event);
    virtual void mousePressEvent(QMouseEvent *event);
    virtual void keyPressEvent(QKeyEvent *event);
    virtual void enterEvent(QEvent *event);
    virtual void leaveEvent(QEvent *event);
    virtual bool eventFilter(QObject *watched, QEvent *event);

signals:
    void FullScreen();

public slots:

    void ShowVideo();
    //
    void FullScreenVideo();
    void toggle_mute();
    void toggle_pause();
    void Stop();
    void FastPlay();
    void SlowPlay();
    void TimeRange(int nMin,int nMax);
    void TimeValue(int nValue);
    void TimeReleased();

private:

    SDL_Window *m_window;
    SDL_Renderer *m_renderer;
    QLabel *m_pCurLabel;
    QLabel *m_pTotalLabel;
    QSlider *m_pPlaySlider;
    QSlider *m_pVolumeSlider;
    int m_screenWidth ;
    int m_screenHeight ;
    int m_w;
    int m_h;
    bool m_bTimePress;
    bool m_bFirstShow;
    bool m_bFull;
    float m_fPlayRate;
    QWidget *m_pCtrBar;
    SDL_mutex *m_pMuRender;
    QVBoxLayout *m_pLayOut;
    //static bool m_bInit;
    CVideoProcess *m_pProcess;
    SDL_Thread *m_threadEvent;
    QWindow *m_pPosWindow;
    PlayState m_eState;

};

#endif // DISPLAY_H
