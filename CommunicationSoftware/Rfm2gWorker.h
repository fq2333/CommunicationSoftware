#ifndef RFM2GWORKER_H
#define RFM2GWORKER_H

#include <QObject>
#include <QByteArray>
#include <QString>
// 引入 GE Fanuc RFM2g 的官方 API 头文件
#include <windows.h>
#include "rfm2g_api.h" 

class Rfm2gWorker : public QObject
{
    Q_OBJECT
public:
    explicit Rfm2gWorker(QObject* parent = nullptr);
    ~Rfm2gWorker();

signals:
    void logMessage(const QString& msg);
    void errorOccurred(const QString& errorMsg);
    void operationCompleted(const QString& result);
    // 将接收到的数据发给主UI或其他Worker（如后续要转给LVDS发送）
    void returnDataReceived(const QByteArray& data);

public slots:
    void initBoard(const QString& devicePath);
    void closeBoard();

    // 发送动力学数据，并触发特定节点的中断
    void sendDynamicsData(const QByteArray& data, quint32 offset, RFM2G_NODE targetNode);

    // 监听下位机返回的中断，并读取指定长度的数据
    void waitForReturnData(quint32 offset, quint32 length, quint32 timeoutMs);

private:
    RFM2GHANDLE m_handle;
    bool m_isInitialized;
};

#endif // RFM2GWORKER_H