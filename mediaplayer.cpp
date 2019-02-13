/*
 * Version:  1.0
 * Author:   Juan Zhou
 * Date-Time:2017-08-22 14:33:30
 * Description:the OCX widget for video and audio playing.
 */
#include <QPainter>

#include <QMoveEvent>
#include <QCheckBox>
#include <QFile>
#include <QUiLoader>
#include <QWindow>
#include "mediaplayer.h"
#include "safetyimpl.h"
#include "process.h"
#include "display.h"
#include "commfun.h"
 CWriteLog LOG("Log","mediaplayer");
MediaPlayer::MediaPlayer(QWidget *pParent):QWidget(pParent)
  ,m_pLayOut(NULL)
  ,m_pProcess(NULL)
  ,m_pDisplay(NULL)
  ,m_pCtrBar(NULL)
{
    LOG.Log(DebugLog,"MediaPlayer parent:%08x",pParent);
    if (SDL_VideoInit(NULL) < 0)
    {
        LOG.Log(ErrLog,"SDL_VideoInit failed");
        //return NULL;
    }

}
MediaPlayer::~MediaPlayer()
{
    Stop();
}
int MediaPlayer::Open(QString szName)
{

    m_strName = szName;

    Stop();

    if(NULL == m_pProcess)
    {
        m_pProcess = new CVideoProcess(this);
    }
    if(NULL == m_pProcess)
    {
        LOG.Log(ErrLog,"create CVideoProcess failed");
        return false;
    }
    if(NULL == m_pDisplay)
    {
        m_pDisplay = new CVideoDisplay(m_pProcess,this);
    }
    if(NULL == m_pDisplay)
    {
        LOG.Log(WarnLog,"MediaPlayer Display Instance failed");
        return false;
    }

    if(NULL == m_pLayOut)
    {
        m_pLayOut = new QVBoxLayout();
        if(m_pLayOut)
        {
            m_pLayOut->setContentsMargins(0,0,0,0);
            m_pLayOut->addWidget(m_pDisplay);
            setLayout(m_pLayOut);
        }
    }
    setLayout(NULL);
    delete m_pLayOut;
    m_pLayOut = NULL;

    static bool bInit = false;
    if(false == bInit)
    {
        bInit = true;
        if(false == m_pProcess->init(m_strName.toStdString().c_str()))
        {
            LOG.Log(WarnLog,"MediaPlayer init failed");
            return false;
        }
    }

    m_pProcess->SetMediaName((char *)m_strName.toStdString().c_str());

    LOG.Log(InfoLog,"MediaPlayer init success");
    if(NULL == m_pProcess->stream_open(NULL))
    {
        LOG.Log(WarnLog,"MediaPlayer stream_open failed");
        return false;
    }
    LOG.Log(InfoLog,"MediaPlayer stream_open success");

    m_pProcess->SetState(EPlay);
    m_pDisplay->SetState(EPlay);
    //connect((QObject *)this,SIGNAL(ShowVideo()),(QObject *)this,SLOT(RefreshVideo()),Qt::QueuedConnection);
    connect((QObject *)this,SIGNAL(ShowVideo()),(QObject *)this,SLOT(RefreshVideo()),Qt::BlockingQueuedConnection);
    connect((QObject *)this,SIGNAL(StopVideo()),(QObject *)this,SLOT(Stop()),Qt::QueuedConnection);
    connect((QObject *)m_pDisplay,SIGNAL(FullScreen()),(QObject *)m_pDisplay,SLOT(FullScreenVideo()),Qt::BlockingQueuedConnection);
    connect((QObject *)this,SIGNAL(RangeChange(int ,int )),(QObject *)m_pDisplay,SLOT(TimeRange(int ,int )),Qt::QueuedConnection);
    connect((QObject *)this,SIGNAL(ValueChange(int )),(QObject *)m_pDisplay,SLOT(TimeValue(int )),Qt::QueuedConnection);
    LOG.Log(InfoLog,"MediaPlayer Display Instance success");

    LOG.Log(InfoLog,"display rect x:%d y:%d width:%d height:%d",m_pDisplay->rect().x(),m_pDisplay->rect().y(),m_pDisplay->rect().width(),m_pDisplay->rect().height());

    m_pDisplay->event_loop();
    m_pDisplay->setWindowFlags(Qt::WindowStaysOnBottomHint);
    CreateCtrBar();
    return true;
}
int MediaPlayer::OpenNet(QString szUrl, QString szUsrName, QString szPwd)
{
    m_strName = szUrl;
    m_strUserName = szUsrName;
    m_strPwd = szPwd;
    return 0;


}
int MediaPlayer::Stop()
{

    if(m_pDisplay)
    {

        LOG.Log(InfoLog,"MediaPlayer Stop");
        m_pDisplay->video_close();

    }

    if(m_pProcess)
    {
        m_pProcess->stream_close();

    }
    if(m_pDisplay)
    {
        m_pDisplay->window_close();
        m_pDisplay->ClearLayout();
    }


    Sleep(100);
    LOG.Log(InfoLog,"delete process and displayer");
    if(m_pDisplay)
    {
        delete m_pDisplay;
        m_pDisplay = NULL;
    }
    if(m_pProcess)
    {
        delete m_pProcess;
        m_pProcess = NULL;
    }


    if(m_pLayOut)
    {
        delete m_pLayOut;
        m_pLayOut = NULL;
    }
    return 0;
}


int MediaPlayer::SetControlInfo(QString szInf)
{
    if(szInf.contains("Stop"))
    {
       LOG.Log(InfoLog,"SetControlInfo:%s",szInf.toStdString().c_str());
       Stop();
    }
    return 0;
}

void MediaPlayer::paintEvent(QPaintEvent *event)
{

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing,true);
    painter.setBrush(QBrush(QColor(0,0,0,220),Qt::SolidPattern));
   // painter.drawRect(0,0,geometry().width(),geometry().height()-40);

   // painter.setPen(QPen(QBrush(Qt::red),2,Qt::SolidLine));
    painter.drawRect(geometry());
    //painter.drawEllipse(0,0,geometry().width()/2,geometry().height());
    //painter.drawEllipse(geometry().width()/2,0,geometry().width(),geometry().height());
    painter.end();
    //render(this,QPoint(),QRegion(),QWidget::DrawWindowBackground);
    LOG.Log(DebugLog,"draw mediaPlayer");

}

void MediaPlayer::CreateCtrBar()
{
    if(NULL == m_pCtrBar)
    {
        QFile fBar(":/control.ui");
        if(false == fBar.open(QFile::ReadOnly))
        {
            LOG.Log(ErrLog,"open file control.ui failed");
            return;
        }
        QUiLoader loder;
        m_pCtrBar = loder.load(&fBar,NULL);

        if(m_pCtrBar)
        {
            //Qt::SubWindow
            m_pCtrBar->setWindowFlags(Qt::WindowStaysOnTopHint|Qt::FramelessWindowHint|Qt::Tool);
            m_pCtrBar->hide();

            m_pCtrBar->setAttribute(Qt::WA_TranslucentBackground);
            LOG.Log(InfoLog,"create control bar success");
        }


    }


    if(m_pDisplay)
    {
        QCheckBox *pPlay = m_pCtrBar->findChild<QCheckBox *>("playBtn");
        QCheckBox *pStop = m_pCtrBar->findChild<QCheckBox *>("stopBtn");
        QPushButton *pSlow = m_pCtrBar->findChild<QPushButton *>("slowBtn");
        QPushButton *pFast = m_pCtrBar->findChild<QPushButton *>("fastBtn");
        QCheckBox *pSound = m_pCtrBar->findChild<QCheckBox *>("soundBtn");

        connect((QObject *)pPlay,SIGNAL(clicked()),(QObject *)m_pDisplay,SLOT(toggle_pause()));
        connect((QObject *)pStop,SIGNAL(clicked()),(QObject *)m_pDisplay,SLOT(Stop()));
        connect((QObject *)pSlow,SIGNAL(clicked()),(QObject *)m_pDisplay,SLOT(SlowPlay()));
        connect((QObject *)pFast,SIGNAL(clicked()),(QObject *)m_pDisplay,SLOT(FastPlay()));
        connect((QObject *)pSound,SIGNAL(clicked()),(QObject *)m_pDisplay,SLOT(toggle_mute()));
        m_pDisplay->SetCtrBar(m_pCtrBar);
    }
}


void MediaPlayer::EmitVideo()
{
    emit (ShowVideo());
    LOG.Log(DebugLog,"EmitVideo");
}
void MediaPlayer::EmitStop()
{
    emit (StopVideo());
}
void MediaPlayer::RefreshVideo()
{

    if(m_pDisplay)
    {
        LOG.Log(DebugLog,"MediaPlayer RefreshVideo");
        m_pDisplay->ShowVideo();
    }
}
QAxAggregated * MediaPlayer::createAggregate()
{
    //static CSafetyImp objSafety;
    return new CSafetyImp();
}
/*QACFACTORY_DEFAULT(MediaPlayer,
                   "QUuid::createUuid().toString().toStdString().c_str()",
                   QUuid::createUuid().toString().toStdString().c_str(),
                   QUuid::createUuid().toString().toStdString().c_str(),
                   QUuid::createUuid().toString().toStdString().c_str(),
                   QUuid::createUuid().toString().toStdString().c_str()
                  );
                  */

