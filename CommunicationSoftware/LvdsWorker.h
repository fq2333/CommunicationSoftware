#ifndef LVDSWORKER_H
#define LVDSWORKER_H

#include <QObject>
#include <QString>
#include <QImage>
// 引入板卡开发商提供的头文件
#include "rpc_windows_client.h" 

class LvdsWorker : public QObject
{
    Q_OBJECT
public:
    explicit LvdsWorker(QObject* parent = nullptr);
    ~LvdsWorker();

signals:
    // 与UI交互的信号
    void logMessage(const QString& msg);
    void errorOccurred(const QString& errorMsg);
    void operationCompleted(const QString& result);

public slots:
    // 供外部或UI调用的槽函数
    void initializeBoard(const QString& resourceName);
    void closeBoard();
    void sendLocalImage(const QString& imagePath);
    void resetBoard();                                   // 步骤3：独立复位
    void readSelfTestData(const QString& saveFilePath);  // 步骤5：读取自检数据

private:
    ViSession m_vi;
    bool m_isInitialized;

    // 内部工具函数：将QImage转为板卡需要的 ViUInt32 数组格式
    bool convertImageToDdrData(const QImage& img, ViUInt32* buffer, ViInt32& length);
    bool buildProtocolData(const QByteArray& raw16BitImage, quint32 frameCount, ViUInt32* outBuffer, ViInt32& outWordLen);
    void sendProtocolImage(const QByteArray& raw16BitData, quint32 currentFrameCount);
};

#endif // LVDSWORKER_H