#include "qcustomlabel.h"
#include <QDebug>
#include <QMouseEvent>
#include <QPainter>
#include <ei.h>
#include <qapplication.h>
#include <QFileInfo>
#include <QProcess>

QCustomLabel::QCustomLabel(QWidget *parent, Qt::WindowFlags f)
    : QLabel(parent, f)
    , mWidth(0)
    , mHeight(0)
    , mbDirty(false)
    , mLayer(RenderLayer_Color)
    , mpToneOp(nullptr)
    , mScaleFactor(1.0f)
    , mImgRect(0,0,0,0)
    , mAbsTopLeft(0,0)
    , mLastPos(0,0)
    , mpColorMap(nullptr)
    , mpAlphaMap(nullptr)
{
}

QCustomLabel::QCustomLabel(const QString &text, QWidget *parent, Qt::WindowFlags f)
    : QLabel(text, parent, f)
    , mpColorMap(nullptr)
    , mpAlphaMap(nullptr)
{
}

QCustomLabel::~QCustomLabel()
{
    if (mpToneOp)
    {
        ei_delete_toneop(mpToneOp);
    }
    if (mpColorMap)
    {
        delete mpColorMap;
    }
    if (mpAlphaMap)
    {
        delete mpAlphaMap;
    }
}

void QCustomLabel::SetToneEnabled(bool enabled)
{
    if (enabled)
    {
        if (nullptr == mpToneOp)
        {
            mpToneOp = ei_create_toneop();
        }
    }
    else
    {
        if (mpToneOp)
        {
            ei_delete_toneop(mpToneOp);
            mpToneOp = nullptr;
        }
    }
    mbDirty = true;
}

void QCustomLabel::SetToneParameters(float expValue, float highLights, float midTone, float shadows, float colorSat, float whitePt)
{
    if (mpToneOp == nullptr) return;
    ei_toneop_update(mpToneOp,
        true,
        expValue,
        highLights,
        midTone,
        shadows,
        colorSat,
        whitePt);
    mbDirty = true;
}

void QCustomLabel::SetRawData(int width, int height, float *rawData)
{
    mWidth = width;
    mHeight = height;
    mRawData.resize(width * height * 4);
    memcpy(mRawData.data(), rawData, sizeof(float) * width * height * 4);
    mbDirty = true;
}

void QCustomLabel::SetRenderLayer(RenderLayer value)
{
    mLayer = value;
    repaint();
}

bool QCustomLabel::SaveImage(const QString &filename)
{
    if (mpColorMap == nullptr
        || mpAlphaMap == nullptr)
    {
        return false;
    }

    mpColorMap->save(filename);

    if (QFileInfo::exists(filename))
    {
        QString app_path = QApplication::applicationDirPath();
        QString cmd = app_path + "/../pictureupload.exe image auto " + filename;
        QProcess::startDetached(cmd);
    }
    return true;
    /*
    int width = mpColorMap->width();
    int height = mpColorMap->height();
    QImage combined(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y)
    {
        BYTE* refColor = mpColorMap->scanLine(y);
        BYTE* refAlpha = mpAlphaMap->scanLine(y);
        BYTE* bpRGB = combined.scanLine(y);
        for (int x = 0; x < width; ++x)
        {
            bpRGB[0] = refColor[0];
            bpRGB[1] = refColor[1];
            bpRGB[2] = refColor[2];
            bpRGB[3] = *refAlpha;
            refColor += 3;
            refAlpha++;
            bpRGB += 4;
        }
    }
    return combined.save(filename);
    */
}

void QCustomLabel::Refresh()
{
    bool needFit = (mpColorMap == nullptr);
    if (mWidth == 0 || mHeight == 0 || !mbDirty) return;
    if (nullptr == mpColorMap
            || mpColorMap->width() != mWidth
            || mpColorMap->height() != mHeight)
    {
        delete mpColorMap;
        mpColorMap = new QImage(mWidth, mHeight, QImage::Format_RGB888);
    }

    std::vector<BYTE> resultBuf;
    resultBuf.resize(mWidth * 3);
    if (mpToneOp)
    {
        for (int y = mHeight - 1; y >= 0; --y)
        {
            int counter = 0;
            for (int x = 0; x < mWidth; ++x)
            {
                int pix = (y * mWidth + x) * 4;
                eiColor refColor = {mRawData[pix], mRawData[pix + 1], mRawData[pix + 2]};
                BYTE* bpRGB = &resultBuf[counter];
                ei_toneop_apply(mpToneOp, refColor);
                bpRGB[0] =(BYTE)((refColor.r > 1.0f ? 1.0f : refColor.r) * 255);
                bpRGB[1] =(BYTE)((refColor.g > 1.0f ? 1.0f : refColor.g) * 255);
                bpRGB[2] =(BYTE)((refColor.b > 1.0f ? 1.0f : refColor.b) * 255);
                counter += 3;
            }
            memcpy(mpColorMap->scanLine(mHeight - y - 1), resultBuf.data(), mpColorMap->bytesPerLine());
        }
    }
    else
    {
        for (int y = mHeight - 1; y >= 0; --y)
        {
            int counter = 0;
            for (int x = 0; x < mWidth; ++x)
            {
                int pix = (y * mWidth + x) * 4;
                eiColor refColor = {mRawData[pix], mRawData[pix + 1], mRawData[pix + 2]};
                BYTE* bpRGB = &resultBuf[counter];
                bpRGB[0] =(BYTE)((refColor.r > 1.0f ? 1.0f : refColor.r) * 255);
                bpRGB[1] =(BYTE)((refColor.g > 1.0f ? 1.0f : refColor.g) * 255);
                bpRGB[2] =(BYTE)((refColor.b > 1.0f ? 1.0f : refColor.b) * 255);
                counter += 3;
            }
            memcpy(mpColorMap->scanLine(mHeight - y - 1), resultBuf.data(), mpColorMap->bytesPerLine());
        }
    }

    // Process alpha
    if (nullptr == mpAlphaMap
            || mpAlphaMap->width() != mWidth
            || mpAlphaMap->height() != mHeight)
    {
        delete mpAlphaMap;
        mpAlphaMap = new QImage(mWidth, mHeight, QImage::Format_Alpha8);
    }
    for (int y = mHeight - 1; y >= 0; --y)
    {
        int counter = 0;
        for (int x = 0; x < mWidth; ++x)
        {
            int pix = (y * mWidth + x) * 4;
            eiScalar refAlpha = mRawData[pix + 3];
            resultBuf[counter] = (BYTE)((refAlpha > 1.0f ? 1.0f : refAlpha) * 255);
            ++counter;
        }
        memcpy(mpAlphaMap->scanLine(mHeight - y - 1), resultBuf.data(), mpAlphaMap->bytesPerLine());
    }

    if (needFit)
    {
        FitImage();
    }
    repaint();
}

void QCustomLabel::mouseMoveEvent(QMouseEvent *ev)
{
    if (ev->buttons() & Qt::MidButton)
    {
        mAbsTopLeft += ev->pos() - mLastPos;
        mLastPos = ev->pos();
        repaint();
    }
}

void QCustomLabel::mousePressEvent(QMouseEvent *ev)
{
    mImgRect.moveTopLeft(mAbsTopLeft - ev->pos());
    mLastPos = ev->pos();
}

void QCustomLabel::wheelEvent(QWheelEvent *ev)
{
    if (mpColorMap == nullptr) return;
    int delta = ev->delta();

    if (delta > 0 && mScaleFactor < 3.0f)
    {
        mScaleFactor *= 1.2f;
    }
    else if (delta < 0 && mScaleFactor > 0.2f)
    {
        mScaleFactor /= 1.2f;
    }
    else
    {
        return;
    }

    const float scaleDelta = 1.2f;
    mLastPos = ev->pos();
    mImgRect.moveTopLeft((mAbsTopLeft - ev->pos()));
    QRect oldRect = mImgRect;
    if (delta > 0)
    {
        mImgRect.setTopLeft(mImgRect.topLeft() * scaleDelta);
        mImgRect.setBottomRight(mImgRect.bottomRight() * scaleDelta);
    }
    else
    {
        mImgRect.setTopLeft(mImgRect.topLeft() / scaleDelta);
        mImgRect.setBottomRight(mImgRect.bottomRight() / scaleDelta);
    }
    QPoint ptDelta = QPoint(mImgRect.topLeft() - oldRect.topLeft());
    mAbsTopLeft += ptDelta;
    repaint();
    emit ScaleChanged(mScaleFactor);
}

void QCustomLabel::paintEvent(QPaintEvent *ev)
{
    if (nullptr == mpColorMap)
    {
        mImgRect = QRect(0,0,0,0);
        QLabel::paintEvent(ev);
        return;
    }
    QPainter painter(this);
    painter.translate(mLastPos);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    if (mLayer == RenderLayer_Color)
    {
        painter.drawImage(mImgRect, *mpColorMap);
    }
    else
    {
        painter.drawImage(mImgRect, *mpAlphaMap);
    }
}

void QCustomLabel::FitImage()
{
    if (nullptr == mpColorMap) return;
    const QRect& ctlRect = rect();
    mImgRect = mpColorMap->rect();
    mImgRect.moveTo((ctlRect.width() - mImgRect.width()) / 2,
                    (ctlRect.height() - mImgRect.height()) / 2);
    mAbsTopLeft = mImgRect.topLeft();
    mLastPos = QPoint(0,0);
    repaint();
    mScaleFactor = 1.0f;
    emit ScaleChanged(mScaleFactor);
}

void QCustomLabel::FitControl()
{
    if (nullptr == mpColorMap) return;
    const QRect& imgRect = mpColorMap->rect();
    const QRect& ctlRect = rect();
    mImgRect = imgRect;
    float scaleFactor = 1.0f;
    if (ctlRect.width() > ctlRect.height())
    {
        scaleFactor = (float)ctlRect.width() / (float)imgRect.width();
        if (imgRect.height() * scaleFactor > ctlRect.height())
        {
            scaleFactor = (float)ctlRect.height() / (float)imgRect.height();
        }
    }
    else
    {
        scaleFactor = (float)ctlRect.height() / (float)imgRect.height();
        if (imgRect.width() * scaleFactor > ctlRect.width())
        {
            scaleFactor = (float)ctlRect.width() / (float)imgRect.width();
        }
    }
    mImgRect.setWidth(imgRect.width() * scaleFactor);
    mImgRect.setHeight(imgRect.height() * scaleFactor);
    mImgRect.moveTop((ctlRect.height() - mImgRect.height()) / 2);
    mImgRect.moveLeft((ctlRect.width() - mImgRect.width()) / 2);
    mAbsTopLeft = mImgRect.topLeft();
    mLastPos = QPoint(0,0);
    repaint();
    mScaleFactor = scaleFactor;
    emit ScaleChanged(mScaleFactor);
}

void QCustomLabel::Reset()
{
    clear();
    setText("Elara");
    delete mpColorMap;
    mpColorMap = nullptr;
    delete mpAlphaMap;
    mpAlphaMap = nullptr;
}

void QCustomLabel::SaveToCache(QString fileName)
{
    if (mWidth == 0
        || mHeight == 0
        || mRawData.size() == 0)
    {
        return;
    }
    QFile cache(fileName);
    cache.open(QIODevice::WriteOnly);
    QDataStream stream(&cache);
    stream << mWidth << mHeight;
    stream.writeRawData((char*)mRawData.data(), sizeof(float) * (int)mRawData.size());
    cache.close();
}

void QCustomLabel::LoadFromCache(QString fileName)
{
    QFile cache(fileName);
    if (!cache.exists()) return;
    cache.open(QIODevice::ReadOnly);
    QDataStream stream(&cache);
    stream >> mWidth;
    stream >> mHeight;
    mRawData.resize(mWidth * mHeight * 4);
    stream.readRawData((char*)mRawData.data(), sizeof(float) * (int)mRawData.size());
    cache.close();
    mbDirty = true;
    Refresh();
}
