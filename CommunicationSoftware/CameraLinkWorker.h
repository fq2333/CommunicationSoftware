#ifndef CAMERALINKWORKER_H
#define CAMERALINKWORKER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QVector>

// ==========================================
// 引入官方 DLL 头文件
// ==========================================
#include "visa.h"
#include "libStandard_pxie.h"

class CameraLinkWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraLinkWorker(QObject* parent = nullptr);
    ~CameraLinkWorker();

signals:
    void logMessage(const QString& msg);
    void errorOccurred(const QString& errorMsg);
    void operationCompleted(const QString& result);

public slots:
    void initializeBoard(const QString& resourceName);
    void closeBoard();
    void resetBoard(); // [新增] 复位槽函数
    void sendLocalImage(const QString& imagePath);
    void sendImageFromMemory(const QByteArray& imageData, quint32 width, quint32 height, quint8 bitDepth);
    void packAndSaveOffline(const QString& imagePath, const QString& savePath);

private:
    bool m_isInitialized;

    // [修改] 使用真实的 VISA 句柄类型
    ViSession m_cmlkHandle;

    bool buildCameraLinkPacket(const QImage& img16, QByteArray& outBuffer);
    
};

#endif // CAMERALINKWORKER_H