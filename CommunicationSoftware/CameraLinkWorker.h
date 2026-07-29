#ifndef CAMERALINKWORKER_H
#define CAMERALINKWORKER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QVector>

class CameraLinkWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraLinkWorker(QObject* parent = nullptr);
    ~CameraLinkWorker();

signals:
    // 与主 UI 交互的标准信号
    void logMessage(const QString& msg);
    void errorOccurred(const QString& errorMsg);
    void operationCompleted(const QString& result);

public slots:
    // 供主线程调用的槽函数
    void initializeBoard(const QString& resourceName);
    void closeBoard();
    void sendLocalImage(const QString& imagePath);
    // ... 其他接口
    void packAndSaveOffline(const QString& imagePath, const QString& savePath);

private:
    bool m_isInitialized;
    // 预留的底层设备句柄（待 DLL 给出后替换为真实类型，如 HANDLE 或 int）
    void* m_cmlkHandle;

    // 核心算法：将 16-bit 图像提取为符合 2tap 12bit 的二进制数据包
    bool buildCameraLinkPacket(const QImage& img16, QVector<quint32>& outBuffer);
};

#endif // CAMERALINKWORKER_H