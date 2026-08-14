#include "Rfm2gWorker.h"
#include <QThread>
#include <QCoreApplication>
#include <QElapsedTimer>
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
        RFM2gEnableEvent(m_handle, RFM2GEVENT_INTR2);
        // ==========================================
        RFM2gClearEvent(m_handle, RFM2GEVENT_INTR2);
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

void Rfm2gWorker::sendAndWaitDynamics(const QByteArray& data, quint32 txOffset, RFM2G_NODE targetNode, quint32 rxOffset, quint32 timeoutMs)
{
    if (!m_isInitialized) return;
    // ==========================================
    // [新增] 启动高精度硬件计时器
    // ==========================================
    QElapsedTimer perfTimer;
    perfTimer.start();
    // ==========================================
    // 【新增】：发送新指令前，先销毁旧的回传包头
    // ==========================================
    quint32 dummy = 0;
    RFM2gWrite(m_handle, rxOffset, (void*)&dummy, sizeof(quint32));
    // 1. 写入动力学数据
    RFM2G_STATUS res = RFM2gWrite(m_handle, txOffset, (void*)data.constData(), data.size());
    if (res != RFM2G_SUCCESS) {
        emit errorOccurred(QString::fromLocal8Bit("动力学写入失败: %1").arg(RFM2gErrorMsg(res)));
        emit simulationStepFinished(); // 出错也要释放锁
        return;
    }

    // 2. 触发下位机中断开始解算
    RFM2gSendEvent(m_handle, targetNode, RFM2GEVENT_INTR1, 0);

    // 3. 立刻进入阻塞监听，等待下位机交回图像 (此时在子线程，不会卡界面)
    RFM2GEVENTINFO eventInfo;
    eventInfo.Event = RFM2GEVENT_INTR2; // 假设下位机回传也是INTR1
    eventInfo.Timeout = timeoutMs;

    RFM2G_STATUS waitRes = RFM2gWaitForEvent(m_handle, &eventInfo);

    if (waitRes == RFM2G_SUCCESS) {

        // 记录当前正在试探的读取偏移量 (初始为起点)
        quint32 currentReadOffset = rxOffset;
        int parsedCount = 0; // 统计本次中断成功解析了几张图

        // 【新增】：开启链式盲读循环
        while (true) {
            RfmImageHeader header;
            res = RFM2gRead(m_handle, currentReadOffset, (void*)&header, sizeof(RfmImageHeader));

            // 如果读到的内存是合法的，说明这里藏着一张图
            if (res == RFM2G_SUCCESS && header.magicNum == 0xABCD1234) {

                RFM2G_UINT32 imageOffset = currentReadOffset + sizeof(RfmImageHeader) + header.addedSize;
                QByteArray imageData;
                imageData.resize(header.imgDataSize);

                if (RFM2gRead(m_handle, imageOffset, imageData.data(), header.imgDataSize) == RFM2G_SUCCESS) {
                    // 提取成功！带上 camId 跨线程抛出
                    emit parsedImageReceived(header.camId, imageData, header.imgWidth, header.imgHeight, header.imgBitDepth);
                    parsedCount++;
                }

                // ==========================================
                // 核心防御：阅后即焚！
                // 读完立刻把这块内存的魔法数抹除，防止在未来的循环中读到残影
                // ==========================================
                quint32 dummy = 0;
                RFM2gWrite(m_handle, currentReadOffset, (void*)&dummy, sizeof(quint32));

                // 推进指针，计算下一张图可能存在的起始地址，继续循环试探
                currentReadOffset = imageOffset + header.imgDataSize;

            }
            else {
                // 读不到合法的魔法数，说明这轮总线上的所有相机图像都已提取完毕
                break;
            }
        }

        if (parsedCount > 0) {
            // 可选：在日志中打印本次中断总共收到了几个传感器的数据
            // emit logMessage(QString::fromLocal8Bit("成功解析并分发了 %1 个相机的图像流。").arg(parsedCount));
        }
        else {
            emit errorOccurred(QString::fromLocal8Bit("收到中断，但在内存中未找到任何合法的图像包！"));
        }
    }
    else if (waitRes == RFM2G_TIMED_OUT) {
        emit errorOccurred(QString::fromLocal8Bit("单帧仿真超时，下位机未能在 %1 ms 内回传图像！").arg(timeoutMs));
    }

    // 4. 通知主线程，这一回合彻底结束，可以进行下一步
    emit simulationStepFinished();
}
void Rfm2gWorker::startContinuousListen(quint32 rxOffset)
{
    if (!m_isInitialized) return;
    // ==========================================
    // 【新增】：销毁上一局的包头，防止读到残影
    // ==========================================
    quint32 dummy = 0;
    RFM2gWrite(m_handle, rxOffset, (void*)&dummy, sizeof(quint32));
    m_isListening = true;
    emit logMessage(QString::fromLocal8Bit("独立的 RX 接收线程已启动，正在后台持续监听图像回传..."));

    while (m_isListening) {
        RFM2GEVENTINFO eventInfo;
        eventInfo.Event = RFM2GEVENT_INTR2; // 监听下位机发来的 INTR2
        eventInfo.Timeout = 50; // 50ms 超时轮询，既不卡死，响应也快

        RFM2G_STATUS waitRes = RFM2gWaitForEvent(m_handle, &eventInfo);

        // 如果成功等到了下位机发来的中断
        if (waitRes == RFM2G_SUCCESS) {

            quint32 currentReadOffset = rxOffset; // 每次中断都从基地址开始试探
            int parsedCount = 0;

            // ==========================================
            // 开启内部循环：进行多相机连续内存的链式盲读
            // ==========================================
            while (true) {
                RfmImageHeader header;
                RFM2G_STATUS res = RFM2gRead(m_handle, currentReadOffset, (void*)&header, sizeof(RfmImageHeader));

                // 试探当前地址：如果魔法数对得上，说明是一张新图
                if (res == RFM2G_SUCCESS && header.magicNum == 0xABCD1234) {

                    RFM2G_UINT32 imageOffset = currentReadOffset + sizeof(RfmImageHeader) + header.addedSize;
                    QByteArray imageData;
                    imageData.resize(header.imgDataSize);

                    if (RFM2gRead(m_handle, imageOffset, imageData.data(), header.imgDataSize) == RFM2G_SUCCESS) {
                        // 【关键】：提取成功，带上 camId 抛给路由中心进行精准分发！
                        emit parsedImageReceived(header.camId, imageData, header.imgWidth, header.imgHeight, header.imgBitDepth);
                        parsedCount++;
                    }

                    //阅后即焚：立刻抹除当前这个包头的魔法数，防止之后重复读取
                    quint32 zeroMagic = 0;
                    RFM2gWrite(m_handle, currentReadOffset, (void*)&zeroMagic, sizeof(quint32));

                    // 推进指针：计算出紧挨着的下一张图的起始地址，继续 while 试探
                    currentReadOffset = imageOffset + header.imgDataSize;

                }
                else {
                    // 试探失败（读不出 0xABCD1234），说明这轮总线上拼装的所有图像都已提取完毕
                    break;
                }
            }
            // 可选调试日志：您可以在日志区监控每一帧到底收到了几个相机的图
            if (parsedCount > 0) {
                emit logMessage(QString::fromLocal8Bit("后台连续监听: 成功解析并转发 %1 个相机的并发图像。").arg(parsedCount));
            }
            
        }

        // 极为关键的一句：允许该子线程处理外部发来的 stopContinuousListen 信号，防止死循环卡死
        QCoreApplication::processEvents();
    }

    emit logMessage(QString::fromLocal8Bit("独立的 RX 接收线程已停止监听。"));
}

void Rfm2gWorker::stopContinuousListen()
{
    m_isListening = false;
}
// 在文件末尾添加：
void Rfm2gWorker::clearMemoryRegion(quint32 offset, quint32 size)
{
    if (!m_isInitialized || m_handle == NULL) return;

    // 构造一段全为 0 的空数据
    QByteArray zeros(size, '\0');

    // 直接覆盖底层物理内存
    RFM2G_STATUS res = RFM2gWrite(m_handle, offset, (void*)zeros.constData(), size);

    if (res == RFM2G_SUCCESS) {
        emit logMessage(QString::fromLocal8Bit("RFM 内存区 0x%1 已安全清零 (%2 字节)。").arg(offset, 0, 16).arg(size));
    }
    else {
        emit errorOccurred(QString::fromLocal8Bit("RFM 内存清零失败: %1").arg(RFM2gErrorMsg(res)));
    }
}
void Rfm2gWorker::closeBoard()
{
    if (m_isInitialized && m_handle != NULL) {
        RFM2gDisableEvent(m_handle, RFM2GEVENT_INTR2);
        RFM2gClose(&m_handle); 
        m_isInitialized = false;
        m_handle = NULL;
        emit logMessage(QString::fromLocal8Bit("RFM2g 设备已关闭。"));
    }
}