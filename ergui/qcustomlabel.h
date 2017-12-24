#ifndef QCUSTOMLABEL_H
#define QCUSTOMLABEL_H

#include <QLabel>
#include <QRect>
#include <QPoint>
#include <QSharedMemory>
struct eiToneOp;

enum RenderLayer
{
    RenderLayer_Color,
    RenderLayer_Alpha
};

class QCustomLabel : public QLabel
{
    Q_OBJECT
public:
    QCustomLabel(QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags());
    QCustomLabel(const QString &text, QWidget *parent = Q_NULLPTR, Qt::WindowFlags f = Qt::WindowFlags());
    virtual ~QCustomLabel();

    void SetToneEnabled(bool enabled);
    bool IsToneEnabled() {return mpToneOp != nullptr;}
    void SetToneParameters(float expValue, float highLights, float midTone, float shadows, float colorSat, float whitePt);
    void SetRawData(int width, int height, float* rawData);
    void Refresh();
    void FitImage();
    void FitControl();
    void Reset();
    void SaveToCache(QString fileName);
    void LoadFromCache(QString fileName);
    void SetRenderLayer(RenderLayer value);
    const QImage* image() {return mpColorMap;}
    bool SaveImage(const QString& filename);
    RenderLayer GetRenderLayer() { return mLayer; }

Q_SIGNALS:
    void ScaleChanged(float);

protected:
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void paintEvent(QPaintEvent *) override;

private:
    int mWidth;
    int mHeight;
    bool mbDirty;
    RenderLayer mLayer;
    std::vector<float> mRawData;
    eiToneOp* mpToneOp;
    float mScaleFactor;
    QRect mImgRect;
    QPoint mAbsTopLeft;
    QPoint mLastPos;
    QImage* mpColorMap;
    QImage* mpAlphaMap;
};

#endif // QCUSTOMLABEL_H
