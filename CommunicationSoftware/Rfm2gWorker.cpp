#include "Rfm2gWorker.h"
#include <QThread>

Rfm2gWorker::Rfm2gWorker(QObject* parent)
    : QObject(parent), m_handle(NULL), m_isInitialized(false)
{
}

Rfm2gWorker::~Rfm2gWorker()
{
    closeBoard();
}

void Rfm2gWorker::initBoard(const QString& devicePath)
{
    RFM2G_STATUS result;
    QByteArray pathBytes = devicePath.toLocal8Bit();

    // 打开 RFM2g 设备
    result = RFM2gOpen(pathBytes.data(), &m_handle);

    if (result == RFM2G_SUCCESS) {
        m_isInitialized = true;

        // 建议：使能本地中断接收，以便后续使用 WaitForEvent[cite: 3]
        RFM2gEnableEvent(m_handle, RFM2GEVENT_INTR1);

        emit logMessage(QString::fromLocal8Bit("RFM2g 光纤网卡初始化成功: %1").arg(devicePath));
    }
    else {
        // 利用 API 自带的错误解析函数[cite: 3]
        emit errorOccurred(QString::fromLocal8Bit("RFM2g 初始化失败: %1").arg(RFM2gErrorMsg(result)));
    }
}

void Rfm2gWorker::sendDynamicsData(const QByteArray& data, quint32 offset, RFM2G_NODE targetNode)
{
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("RFM2g 未初始化，无法发送数据。"));
        return;
    }

    RFM2G_STATUS result;

    // 1. 将数据写入反射内存 (如果长度超过阈值，底层会自动调用 DMA)[cite: 3]
    result = RFM2gWrite(m_handle, offset, (void*)data.constData(), data.size());

    if (result != RFM2G_SUCCESS) {
        emit errorOccurred(QString::fromLocal8Bit("RFM2g 写入动力学数据失败: %1").arg(RFM2gErrorMsg(result)));
        return;
    }

    emit logMessage(QString::fromLocal8Bit("成功写入 %1 字节动力学数据至 Offset 0x%2").arg(data.size()).arg(offset, 0, 16));

    // 2. 触发下位机中断，通知其数据已更新 (以 INTR1 为例)[cite: 3]
    // 最后一个参数 0x0 为扩展数据，可自行定义
    result = RFM2gSendEvent(m_handle, targetNode, RFM2GEVENT_INTR1, 0x0);

    if (result == RFM2G_SUCCESS) {
        emit logMessage(QString::fromLocal8Bit("已向节点 %1 发送中断事件 INTR1").arg(targetNode));
    }
    else {
        emit errorOccurred(QString::fromLocal8Bit("发送中断事件失败: %1").arg(RFM2gErrorMsg(result)));
    }
}

void Rfm2gWorker::waitForReturnData(quint32 offset, quint32 length, quint32 timeoutMs)
{
    if (!m_isInitialized) return;

    RFM2GEVENTINFO eventInfo;
    eventInfo.Event = RFM2GEVENT_INTR1; // 假设下位机处理完后也发 INTR1
    eventInfo.Timeout = timeoutMs;

    emit logMessage(QString::fromLocal8Bit("正在等待仿真计算机处理完毕的中断信号..."));

    // 1. 阻塞等待下位机中断[cite: 3]
    RFM2G_STATUS result = RFM2gWaitForEvent(m_handle, &eventInfo);

    if (result == RFM2G_SUCCESS) {
        emit logMessage(QString::fromLocal8Bit("收到来自节点 %1 的回传中断！准备读取数据...").arg(eventInfo.NodeId));

        QByteArray readBuffer;
        readBuffer.resize(length);

        // 2. 读取回传的图像或解算数据[cite: 3]
        result = RFM2gRead(m_handle, offset, readBuffer.data(), length);

        if (result == RFM2G_SUCCESS) {
            emit operationCompleted(QString::fromLocal8Bit("成功读取 %1 字节回传数据。").arg(length));
            emit returnDataReceived(readBuffer); // 触发信号，主UI可接管数据
        }
        else {
            emit errorOccurred(QString::fromLocal8Bit("读取回传数据失败: %1").arg(RFM2gErrorMsg(result)));
        }
    }
    else if (result == RFM2G_TIMED_OUT) {
        emit errorOccurred(QString::fromLocal8Bit("等待下位机回传超时！"));
    }
    else {
        emit errorOccurred(QString::fromLocal8Bit("等待中断发生错误: %1").arg(RFM2gErrorMsg(result)));
    }
}

void Rfm2gWorker::closeBoard()
{
    if (m_isInitialized && m_handle != NULL) {
        RFM2gDisableEvent(m_handle, RFM2GEVENT_INTR1); 
        RFM2gClose(&m_handle); 
        m_isInitialized = false;
        m_handle = NULL;
        emit logMessage(QString::fromLocal8Bit("RFM2g 设备已关闭。"));
    }
}