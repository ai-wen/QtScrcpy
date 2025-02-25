#include <QDesktopWidget>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QShortcut>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QWindow>
#include <QtWidgets/QHBoxLayout>

#include "config.h"
#include "controller.h"
#include "device.h"
#include "iconhelper.h"
#include "qyuvopenglwidget.h"
#include "toolform.h"
#include "ui_videoform.h"
#include "videoform.h"
extern "C"
{
#include "libavutil/frame.h"
}

VideoForm::VideoForm(bool framelessWindow, bool skin, QWidget *parent) : QWidget(parent), ui(new Ui::videoForm), m_skin(skin)
{
    ui->setupUi(this);
    initUI();
    installShortcut();
    updateShowSize(size());
    bool vertical = size().height() > size().width();
    if (m_skin) {
        updateStyleSheet(vertical);
    }
    if (framelessWindow) {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    }
}

VideoForm::~VideoForm()
{
    delete ui;
}

void VideoForm::initUI()
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (m_skin) {
        QPixmap phone;
        if (phone.load(":/res/phone.png")) {
            m_widthHeightRatio = 1.0f * phone.width() / phone.height();
        }

#ifndef Q_OS_OSX
        // mac下去掉标题栏影响showfullscreen
        // 去掉标题栏
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        // 根据图片构造异形窗口
        setAttribute(Qt::WA_TranslucentBackground);
#endif
    }

    m_videoWidget = new QYUVOpenGLWidget();
    m_videoWidget->hide();
    ui->keepRatioWidget->setWidget(m_videoWidget);
    ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

    m_fpsLabel = new QLabel(m_videoWidget);
    QFont ft;
    ft.setPointSize(15);
    ft.setWeight(QFont::Light);
    ft.setBold(true);
    m_fpsLabel->setFont(ft);
    m_fpsLabel->move(5, 15);
    m_fpsLabel->setMinimumWidth(100);
    m_fpsLabel->setStyleSheet(R"(QLabel {color: #00FF00;})");

    setMouseTracking(true);
    m_videoWidget->setMouseTracking(true);
    ui->keepRatioWidget->setMouseTracking(true);
}

QRect VideoForm::getGrabCursorRect()
{
    QRect rc;
#if defined(Q_OS_WIN32)
    rc = QRect(ui->keepRatioWidget->mapToGlobal(m_videoWidget->pos()), m_videoWidget->size());
    // high dpi support
    rc.setTopLeft(rc.topLeft() * m_videoWidget->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * m_videoWidget->devicePixelRatioF());

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#elif defined(Q_OS_OSX)
    rc = m_videoWidget->geometry();
    rc.setTopLeft(ui->keepRatioWidget->mapToGlobal(rc.topLeft()));
    rc.setBottomRight(ui->keepRatioWidget->mapToGlobal(rc.bottomRight()));

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#elif defined(Q_OS_LINUX)
    rc = QRect(ui->keepRatioWidget->mapToGlobal(m_videoWidget->pos()), m_videoWidget->size());
    // high dpi support -- taken from the WIN32 section and untested
    rc.setTopLeft(rc.topLeft() * m_videoWidget->devicePixelRatioF());
    rc.setBottomRight(rc.bottomRight() * m_videoWidget->devicePixelRatioF());

    rc.setX(rc.x() + 10);
    rc.setY(rc.y() + 10);
    rc.setWidth(rc.width() - 20);
    rc.setHeight(rc.height() - 20);
#endif
    return rc;
}

const QSize &VideoForm::frameSize()
{
    return m_frameSize;
}

void VideoForm::resizeSquare()
{
    QRect screenRect = getScreenRect();
    if (screenRect.isEmpty()) {
        qWarning() << "getScreenRect is empty";
        return;
    }
    resize(screenRect.height(), screenRect.height());
}

void VideoForm::removeBlackRect()
{
    resize(ui->keepRatioWidget->goodSize());
}

void VideoForm::showFPS(bool show)
{
    if (!m_fpsLabel) {
        return;
    }
    m_fpsLabel->setVisible(show);
}

void VideoForm::updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV)
{
    if (m_videoWidget->isHidden()) {
        if (m_loadingWidget) {
            m_loadingWidget->close();
        }
        m_videoWidget->show();
    }

    updateShowSize(QSize(width, height));
    m_videoWidget->setFrameSize(QSize(width, height));
    m_videoWidget->updateTextures(dataY, dataU, dataV, linesizeY, linesizeU, linesizeV);
}

void VideoForm::showToolForm(bool show)
{
    if (!m_toolForm) {
        m_toolForm = new ToolForm(this, ToolForm::AP_OUTSIDE_RIGHT);
        m_toolForm->setDevice(m_device);
    }
    m_toolForm->move(pos().x() + geometry().width(), pos().y() + 30);
    m_toolForm->setVisible(show);
}

void VideoForm::moveCenter()
{
    QRect screenRect = getScreenRect();
    if (screenRect.isEmpty()) {
        qWarning() << "getScreenRect is empty";
        return;
    }
    // 窗口居中
    move(screenRect.center() - QRect(0, 0, size().width(), size().height()).center());
}

void VideoForm::installShortcut()
{
    QShortcut *shortcut = nullptr;

    // switchFullScreen
    shortcut = new QShortcut(QKeySequence("Ctrl+f"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->switchFullScreen();
    });

    // resizeSquare
    shortcut = new QShortcut(QKeySequence("Ctrl+g"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { resizeSquare(); });

    // removeBlackRect
    shortcut = new QShortcut(QKeySequence("Ctrl+w"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() { removeBlackRect(); });

    // postGoHome
    shortcut = new QShortcut(QKeySequence("Ctrl+h"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postGoHome();
    });

    // postGoBack
    shortcut = new QShortcut(QKeySequence("Ctrl+b"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postGoBack();
    });

    // postAppSwitch
    shortcut = new QShortcut(QKeySequence("Ctrl+s"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postAppSwitch();
    });

    // postGoMenu
    shortcut = new QShortcut(QKeySequence("Ctrl+m"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postGoMenu();
    });

    // postVolumeUp
    shortcut = new QShortcut(QKeySequence("Ctrl+up"), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postVolumeUp();
    });

    // postVolumeDown
    shortcut = new QShortcut(QKeySequence("Ctrl+down"), this);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postVolumeDown();
    });

    // postPower
    shortcut = new QShortcut(QKeySequence("Ctrl+p"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postPower();
    });

    // setScreenPowerMode(ControlMsg::SPM_OFF)
    shortcut = new QShortcut(QKeySequence("Ctrl+o"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->setScreenPowerMode(ControlMsg::SPM_OFF);
    });

    // expandNotificationPanel
    shortcut = new QShortcut(QKeySequence("Ctrl+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->expandNotificationPanel();
    });

    // collapsePanel
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+n"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->collapsePanel();
    });

    // copy
    shortcut = new QShortcut(QKeySequence("Ctrl+c"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postCopy();
    });

    // cut
    shortcut = new QShortcut(QKeySequence("Ctrl+x"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->postCut();
    });

    // clipboardPaste
    shortcut = new QShortcut(QKeySequence("Ctrl+v"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->setDeviceClipboard();
    });

    // setDeviceClipboard
    shortcut = new QShortcut(QKeySequence("Ctrl+Shift+v"), this);
    shortcut->setAutoRepeat(false);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (!m_device) {
            return;
        }
        emit m_device->clipboardPaste();
    });
}

QRect VideoForm::getScreenRect()
{
    QRect screenRect;
    QWidget *win = window();
    if (!win) {
        return screenRect;
    }

    QWindow *winHandle = win->windowHandle();
    QScreen *screen = QGuiApplication::primaryScreen();
    if (winHandle) {
        screen = winHandle->screen();
    }
    if (!screen) {
        return screenRect;
    }

    screenRect = screen->availableGeometry();
    return screenRect;
}

void VideoForm::updateStyleSheet(bool vertical)
{
    if (vertical) {
        setStyleSheet(R"(
                 #videoForm {
                     border-image: url(:/image/videoform/phone-v.png) 150px 65px 85px 65px;
                     border-width: 150px 65px 85px 65px;
                 }
                 )");
    } else {
        setStyleSheet(R"(
                 #videoForm {
                     border-image: url(:/image/videoform/phone-h.png) 65px 85px 65px 150px;
                     border-width: 65px 85px 65px 150px;
                 }
                 )");
    }
    layout()->setContentsMargins(getMargins(vertical));
}

QMargins VideoForm::getMargins(bool vertical)
{
    QMargins margins;
    if (vertical) {
        margins = QMargins(10, 68, 12, 62);
    } else {
        margins = QMargins(68, 12, 62, 10);
    }
    return margins;
}

void VideoForm::updateShowSize(const QSize &newSize)
{
    if (m_frameSize != newSize) {
        m_frameSize = newSize;

        m_widthHeightRatio = 1.0f * newSize.width() / newSize.height();
        ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);

        bool vertical = m_widthHeightRatio < 1.0f ? true : false;
        QSize showSize = newSize;
        QRect screenRect = getScreenRect();
        if (screenRect.isEmpty()) {
            qWarning() << "getScreenRect is empty";
            return;
        }
        if (vertical) {
            showSize.setHeight(qMin(newSize.height(), screenRect.height() - 200));
            showSize.setWidth(showSize.height() * m_widthHeightRatio);
        } else {
            showSize.setWidth(qMin(newSize.width(), screenRect.width() / 2));
            showSize.setHeight(showSize.width() / m_widthHeightRatio);
        }

        if (isFullScreen() && m_device) {
            emit m_device->switchFullScreen();
        }

        if (isMaximized()) {
            showNormal();
        }

        if (m_skin) {
            QMargins m = getMargins(vertical);
            showSize.setWidth(showSize.width() + m.left() + m.right());
            showSize.setHeight(showSize.height() + m.top() + m.bottom());
        }

        if (showSize != size()) {
            resize(showSize);
            if (m_skin) {
                updateStyleSheet(vertical);
            }
            moveCenter();
        }
    }
}

void VideoForm::onSwitchFullScreen()
{
    if (isFullScreen()) {
        // 横屏全屏铺满全屏，恢复时，恢复保持宽高比
        if (m_widthHeightRatio > 1.0f) {
            ui->keepRatioWidget->setWidthHeightRatio(m_widthHeightRatio);
        }

        showNormal();
        // fullscreen window will move (0,0). qt bug?
        move(m_fullScreenBeforePos);

#ifdef Q_OS_OSX
        //setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        //show();
#endif
        if (m_skin) {
            updateStyleSheet(m_frameSize.height() > m_frameSize.width());
        }
        showToolForm(true);
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS);
#endif
    } else {
        // 横屏全屏铺满全屏，不保持宽高比
        if (m_widthHeightRatio > 1.0f) {
            ui->keepRatioWidget->setWidthHeightRatio(-1.0f);
        }

        m_fullScreenBeforePos = pos();
        // 这种临时增加标题栏再全屏的方案会导致收不到mousemove事件，导致setmousetrack失效
        // mac fullscreen must show title bar
#ifdef Q_OS_OSX
        //setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
#endif
        showToolForm(false);
        if (m_skin) {
            layout()->setContentsMargins(0, 0, 0, 0);
        }
        showFullScreen();

        // 全屏状态禁止电脑休眠、息屏
#ifdef Q_OS_WIN32
        ::SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
    }
}

void VideoForm::updateFPS(quint32 fps)
{
    //qDebug() << "FPS:" << fps;
    if (!m_fpsLabel) {
        return;
    }
    m_fpsLabel->setText(QString("FPS:%1").arg(fps));
}

void VideoForm::staysOnTop(bool top)
{
    bool needShow = false;
    if (isVisible()) {
        needShow = true;
    }
    setWindowFlag(Qt::WindowStaysOnTopHint, top);
    if (m_toolForm) {
        m_toolForm->setWindowFlag(Qt::WindowStaysOnTopHint, top);
    }
    if (needShow) {
        show();
    }
}

void VideoForm::setDevice(Device *device)
{
    m_device = device;
}

void VideoForm::mousePressEvent(QMouseEvent *event)
{
     if( Qt::FramelessWindowHint == (windowFlags() & Qt::FramelessWindowHint)
            && m_toolForm->isVisible())
    {   //无窗口模式
        if (event->button() == Qt::LeftButton)
        {
            isLeftPressDown = true;

            if(dir != NONE)
            {
                this->mouseGrabber(); //返回当前抓取鼠标输入的窗口
            }
            else
            {
                m_movePoint = event->globalPos() - this->frameGeometry().topLeft();
                //globalPos()鼠标位置，topLeft()窗口左上角的位置
            }
        }
    }
    if (event->button() == Qt::MiddleButton) {
        if (m_device && !m_device->isCurrentCustomKeymap()) {
            emit m_device->postGoHome();
            return;
        }
    }

    if (event->button() == Qt::RightButton) {
        if (m_device && !m_device->isCurrentCustomKeymap()) {
            emit m_device->postGoBack();
            return;
        }
    }

    if (m_videoWidget->geometry().contains(event->pos())) {
        if (!m_device) {
            return;
        }
        event->setLocalPos(m_videoWidget->mapFrom(this, event->localPos().toPoint()));
        emit m_device->mouseEvent(event, m_videoWidget->frameSize(), m_videoWidget->size());

        // debug keymap pos
        if (event->button() == Qt::LeftButton) {
            qreal x = event->localPos().x() / m_videoWidget->size().width();
            qreal y = event->localPos().y() / m_videoWidget->size().height();
            QString posTip = QString(R"("pos": {"x": %1, "y": %2})").arg(x).arg(y);
            qInfo() << posTip.toStdString().c_str();
        }
    } else {
        if (event->button() == Qt::LeftButton) {
            m_dragPosition = event->globalPos() - frameGeometry().topLeft();
            event->accept();
        }
    }
}

void VideoForm::mouseReleaseEvent(QMouseEvent *event)
{
    if( Qt::FramelessWindowHint == (windowFlags() & Qt::FramelessWindowHint)
            && m_toolForm->isVisible())
    {   //无窗口模式
        if (event->button() == Qt::LeftButton)
        {
            isLeftPressDown = false;
            if (dir != NONE)
            {
                this->releaseMouse(); //释放鼠标抓取
                this->setCursor(QCursor(Qt::ArrowCursor));
                dir = NONE; //热心网友指正
            }
        }
    }
    if (m_dragPosition.isNull()) {
        if (!m_device) {
            return;
        }
        event->setLocalPos(m_videoWidget->mapFrom(this, event->localPos().toPoint()));
        // local check
        QPointF local = event->localPos();
        if (local.x() < 0) {
            local.setX(0);
        }
        if (local.x() > m_videoWidget->width()) {
            local.setX(m_videoWidget->width());
        }
        if (local.y() < 0) {
            local.setY(0);
        }
        if (local.y() > m_videoWidget->height()) {
            local.setY(m_videoWidget->height());
        }
        event->setLocalPos(local);
        emit m_device->mouseEvent(event, m_videoWidget->frameSize(), m_videoWidget->size());
    } else {
        m_dragPosition = QPoint(0, 0);
    }
}

void VideoForm::mouseMoveEvent(QMouseEvent *event)
{
     if( Qt::FramelessWindowHint == (windowFlags() & Qt::FramelessWindowHint)
            && m_toolForm->isVisible())
    {   //无窗口模式
        QPoint globalPoint = event->globalPos();   //鼠标全局坐标

        QRect rect = this->rect();  //rect == QRect(0,0 1280x720)
        QPoint topLeft = mapToGlobal(rect.topLeft());
        QPoint bottomRight = mapToGlobal(rect.bottomRight());

        if (this->windowState() != Qt::WindowMaximized)
        {
            if(!isLeftPressDown)  //没有按下左键时
            {
                this->region(globalPoint); //窗口大小的改变——判断鼠标位置，改变光标形状
            }
            else
            {
                if(dir != NONE)
                {
                    QRect newRect(topLeft, bottomRight); //定义一个矩形  拖动后最大1000*1618

                    switch(dir)
                    {
                        case LEFT:

                            if(bottomRight.x() - globalPoint.x() <= this->minimumWidth())
                            {
                                newRect.setLeft(topLeft.x());  //小于界面的最小宽度时，设置为左上角横坐标为窗口x
                                //只改变左边界
                            }
                            else
                            {
                                newRect.setLeft(globalPoint.x());
                            }
                            break;
                        case RIGHT:
                            newRect.setWidth(globalPoint.x() - topLeft.x());  //只能改变右边界
                            break;
                        case UP:
                            if(bottomRight.y() - globalPoint.y() <= this->minimumHeight())
                            {
                                newRect.setY(topLeft.y());
                            }
                            else
                            {
                                newRect.setY(globalPoint.y());
                            }
                            break;
                        case DOWN:
                            newRect.setHeight(globalPoint.y() - topLeft.y());
                            break;
                        case LEFTTOP:
                            if(bottomRight.x() - globalPoint.x() <= this->minimumWidth())
                            {
                                newRect.setX(topLeft.x());
                            }
                            else
                            {
                                newRect.setX(globalPoint.x());
                            }

                            if(bottomRight.y() - globalPoint.y() <= this->minimumHeight())
                            {
                                newRect.setY(topLeft.y());
                            }
                            else
                            {
                                newRect.setY(globalPoint.y());
                            }
                            break;
                         case RIGHTTOP:
                              if (globalPoint.x() - topLeft.x() >= this->minimumWidth())
                              {
                                  newRect.setWidth(globalPoint.x() - topLeft.x());
                              }
                              else
                              {
                                  newRect.setWidth(bottomRight.x() - topLeft.x());
                              }
                              if (bottomRight.y() - globalPoint.y() >= this->minimumHeight())
                              {
                                  newRect.setY(globalPoint.y());
                              }
                              else
                              {
                                  newRect.setY(topLeft.y());
                              }
                              break;
                         case LEFTBOTTOM:
                              if (bottomRight.x() - globalPoint.x() >= this->minimumWidth())
                              {
                                  newRect.setX(globalPoint.x());
                              }
                              else
                              {
                                  newRect.setX(topLeft.x());
                              }
                              if (globalPoint.y() - topLeft.y() >= this->minimumHeight())
                              {
                                  newRect.setHeight(globalPoint.y() - topLeft.y());
                              }
                              else
                              {
                                  newRect.setHeight(bottomRight.y() - topLeft.y());
                              }
                              break;
                          case RIGHTBOTTOM:
                              newRect.setWidth(globalPoint.x() - topLeft.x());
                              newRect.setHeight(globalPoint.y() - topLeft.y());
                              break;
                          default:
                              break;
                    }
                    this->setGeometry(newRect);
                }
                else
                {
                    move(event->globalPos() - m_movePoint); //移动窗口
                    event->accept();
                }
            }
        }
    }

    if (m_videoWidget->geometry().contains(event->pos())) {
        if (!m_device) {
            return;
        }
        event->setLocalPos(m_videoWidget->mapFrom(this, event->localPos().toPoint()));
        emit m_device->mouseEvent(event, m_videoWidget->frameSize(), m_videoWidget->size());
    } else if (!m_dragPosition.isNull()) {
        if (event->buttons() & Qt::LeftButton) {
            move(event->globalPos() - m_dragPosition);
            event->accept();
        }
    }
}

void VideoForm::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton )
    {
        //双击隐藏或显示工具栏
        static bool showW = m_toolForm->isVisible();
        showW  = (showW == true) ? false : true ;

        if(false == showW)
        {
            m_toolForm->hide();
        }
        else
        {
            m_toolForm->show();
        }
    }

    if (event->button() == Qt::LeftButton && !m_videoWidget->geometry().contains(event->pos())) {
        if (!isMaximized()) {
            removeBlackRect();
        }
    }

    if (event->button() == Qt::RightButton && m_device && !m_device->isCurrentCustomKeymap()) {
        emit m_device->postBackOrScreenOn(event->type() == QEvent::MouseButtonPress);
    }

    if (m_videoWidget->geometry().contains(event->pos())) {
        if (!m_device) {
            return;
        }
        event->setLocalPos(m_videoWidget->mapFrom(this, event->localPos().toPoint()));
        emit m_device->mouseEvent(event, m_videoWidget->frameSize(), m_videoWidget->size());
    }
}

void VideoForm::wheelEvent(QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    if (m_videoWidget->geometry().contains(event->position().toPoint())) {
        if (!m_device) {
            return;
        }
        QPointF pos = m_videoWidget->mapFrom(this, event->position().toPoint());
        QWheelEvent wheelEvent(
            pos, event->globalPosition(), event->pixelDelta(), event->angleDelta(), event->buttons(), event->modifiers(), event->phase(), event->inverted());
#else
    if (m_videoWidget->geometry().contains(event->pos())) {
        if (!m_device) {
            return;
        }
        QPointF pos = m_videoWidget->mapFrom(this, event->pos());

        QWheelEvent wheelEvent(
            pos, event->globalPosF(), event->pixelDelta(), event->angleDelta(), event->delta(), event->orientation(),
            event->buttons(), event->modifiers(), event->phase(), event->source(), event->inverted());
#endif
        emit m_device->wheelEvent(&wheelEvent, m_videoWidget->frameSize(), m_videoWidget->size());
    }
}

void VideoForm::keyPressEvent(QKeyEvent *event)
{
    if (!m_device) {
        return;
    }
    if (Qt::Key_Escape == event->key() && !event->isAutoRepeat() && isFullScreen()) {
        emit m_device->switchFullScreen();
    }

    emit m_device->keyEvent(event, m_videoWidget->frameSize(), m_videoWidget->size());
}

void VideoForm::keyReleaseEvent(QKeyEvent *event)
{
    if (!m_device) {
        return;
    }
    emit m_device->keyEvent(event, m_videoWidget->frameSize(), m_videoWidget->size());
}

void VideoForm::paintEvent(QPaintEvent *paint)
{
    Q_UNUSED(paint)
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void VideoForm::showEvent(QShowEvent *event)
{
    Q_UNUSED(event)
    if (!isFullScreen()) {
        showToolForm();
    }
}

void VideoForm::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    QSize goodSize = ui->keepRatioWidget->goodSize();
    if (goodSize.isEmpty()) {
        return;
    }
    QSize curSize = size();
    // 限制VideoForm尺寸不能小于keepRatioWidget good size
    if (m_widthHeightRatio > 1.0f) {
        // hor
        if (curSize.height() <= goodSize.height()) {
            setMinimumHeight(goodSize.height());
        } else {
            setMinimumHeight(0);
        }
    } else {
        // ver
        if (curSize.width() <= goodSize.width()) {
            setMinimumWidth(goodSize.width());
        } else {
            setMinimumWidth(0);
        }
    }
}

void VideoForm::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    if (!m_device) {
        return;
    }
    Config::getInstance().setRect(m_device->getSerial(), geometry());
}

void VideoForm::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void VideoForm::dragMoveEvent(QDragMoveEvent *event)
{
    Q_UNUSED(event)
}

void VideoForm::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event)
}

void VideoForm::dropEvent(QDropEvent *event)
{
    if (!m_device) {
        return;
    }
    const QMimeData *qm = event->mimeData();
    QList<QUrl> urls = qm->urls();

    for (const QUrl &url : urls) {
        QString file = url.toLocalFile();
        QFileInfo fileInfo(file);

        if (!fileInfo.exists()) {
            QMessageBox::warning(this, "QtScrcpy", tr("file does not exist"), QMessageBox::Ok);
            continue;
        }

        if (fileInfo.isFile() && fileInfo.suffix() == "apk") {
            emit m_device->installApkRequest(file);
            continue;
        }
        emit m_device->pushFileRequest(file, Config::getInstance().getPushFilePath() + fileInfo.fileName());
    }
}
