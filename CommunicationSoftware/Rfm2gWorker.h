#ifndef RFM2GWORKER_H
#define RFM2GWORKER_H

#include <QObject>
#include <QByteArray>
#include <QString>
// 引入 GE Fanuc RFM2g 的官方 API 头文件
#include <windows.h>
#include "rfm2g_api.h" 
#include "ProtocolData.h"

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
    // 【修改】：在参数列表最前面增加 quint32 camId
    void parsedImageReceived(quint32 camId, const QByteArray& imageData, quint32 width, quint32 height, quint8 bitDepth);
    // 通知主 UI，当前帧已彻底闭环完成
    void simulationStepFinished();

public slots:
    void initBoard(const QString& devicePath);
    void closeBoard();

    // 发送动力学数据，并触发特定节点的中断
    void sendDynamicsData(const QByteArray& data, quint32 offset, RFM2G_NODE targetNode);

    // 新增：组合槽函数，发送动力学后立即阻塞等待回传
    void sendAndWaitDynamics(const QByteArray& data, quint32 txOffset, RFM2G_NODE targetNode, quint32 rxOffset, quint32 timeoutMs);
    // ==========================================
    // [新增] 独立接收线程使用的连续监听函数
    // ==========================================
    void startContinuousListen(quint32 rxOffset);
    void stopContinuousListen();
    // ==========================================
    // [新增] 安全清空指定的物理内存区块
    // ==========================================
    void clearMemoryRegion(quint32 offset, quint32 size);

private:
    RFM2GHANDLE m_handle;
    bool m_isInitialized;
    // [新增] 控制连续监听循环的标志位
    bool m_isListening = false;
};

#endif // RFM2GWORKER_H