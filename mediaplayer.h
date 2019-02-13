
#ifndef MEDIAPLAYER_H
#define MEDIAPLAYER_H
#include <QWidget>
#include <QPushButton>
#include <QAxBindable>
#include <QString>
#include <QLayout>

class CVideoProcess;
class CVideoDisplay;
struct SDL_Thread;
class  MediaPlayer : public QWidget,QAxBindable
{
Q_OBJECT
    Q_CLASSINFO("ClassID",     "{e082f857-19b0-45f7-b940-41de3c0cf772}")
    Q_CLASSINFO("InterfaceID", "{a623f1e7-6888-4c9c-b91e-160f39411ea2}")
    Q_CLASSINFO("EventsID",    "{fc7ab2b8-fd88-4197-b95c-b2b0b70fa70c}")
Q_PROPERTY(QString mediaName READ mediaName)
Q_PROPERTY(QString userName READ userName)
Q_PROPERTY(QString  passWord READ passWord)
public:
    MediaPlayer(QWidget * pParent=0);
    ~MediaPlayer();
    //property mediaName to save the local name or remote url for playing
    QString mediaName(){return m_strName;};
    //userName for login
    QString userName(){return m_strUserName;};
    //passWord for login
    QString passWord(){return m_strPwd;};
    //to avoid the popup warning box in js
    virtual QAxAggregated *createAggregate();
private:
    void EmitVideo();
    void EmitStop();

signals:
    void ShowVideo();
    void StopVideo();
    void RangeChange(int nMin,int nMax);
    void ValueChange(int nValue);

public slots:
    //play the local media file
    int Open(QString szName);
    //play the media file or the real-time media inf from internet
    int OpenNet(QString szUrl,QString szUsrName,QString szPwd);
    //control the playing pause,mute,fast,slow...
    int SetControlInfo(QString szInf);
private slots:
    void RefreshVideo();
    //stop play
    int Stop();
private:
    virtual void paintEvent(QPaintEvent *event);
    void CreateCtrBar();



private:
    QString m_strName;    //media file name or url
    QString m_strUserName;//userName
    QString m_strPwd;     //user passWord
    QLayout *m_pLayOut;//
    CVideoProcess * m_pProcess;
    CVideoDisplay * m_pDisplay;
    QWidget *m_pCtrBar;





};

#endif // MEDIAPLAYER_H
