#include "LvdsWorker.h"
#include <QDebug>
#include <QFileInfo>
#include <QThread>
#include "HardwareRegisters.h"
#include "rpc_windows_client.h"
// 假设图片最大不超过 32M * 4 字节
#define MAX_DATA_LEN (32 * 1024 * 1024)

LvdsWorker::LvdsWorker(QObject* parent)
    : QObject(parent), m_vi(VI_NULL), m_isInitialized(false)
{
}

LvdsWorker::~LvdsWorker()
{
    closeBoard();
}

void LvdsWorker::initializeBoard(const QString& resourceName)
{
    ViStatus status = 0;
    ViBoolean idQuery = VI_TRUE;
    ViBoolean reset = VI_TRUE;

    QByteArray resNameBytes = resourceName.toLocal8Bit();
    char* resNameStr = resNameBytes.data();

    status = HITMC_MODULE_init(resNameStr, idQuery, reset, &m_vi);

    // 限制重试次数，防止死循环阻塞线程
    int retryCount = 0;
    while (status != OPT_OK && retryCount < 3) {
        status = HITMC_MODULE_init(resNameStr, idQuery, reset, &m_vi);
        retryCount++;
        QThread::msleep(100);
    }

    if (status == OPT_OK) {
        m_isInitialized = true;
        emit logMessage(QString("LVDS板卡初始化成功，Session: %1").arg(m_vi));
    }
    else {
        emit errorOccurred(QString("LVDS板卡初始化失败，错误码: %1").arg(status));
    }
}

void LvdsWorker::sendLocalImage(const QString& imagePath)
{
    if (!m_isInitialized) {
        emit errorOccurred("板卡未初始化，无法发送图片");
        return;
    }

    QImage img(imagePath);
    if (img.isNull()) {
        emit errorOccurred("加载本地图片失败: " + imagePath);
        return;
    }

    // 分配堆内存，避免栈溢出
    ViUInt32* wr_data = new ViUInt32[MAX_DATA_LEN];
    ViInt32 wr_len = 0;

    // 1. 转换图像数据为DDR格式
    if (!convertImageToDdrData(img, wr_data, wr_len)) {
        emit errorOccurred("图像数据格式转换失败");
        delete[] wr_data;
        return;
    }

    // 2. 写入DDR
    emit logMessage(QString("正在将 %1 字节的图像数据写入DDR...").arg(wr_len * 4));
    //ViStatus status = HITMC_RAM_SEND(m_vi, 0x20000000, wr_data, wr_len);
    ViStatus status = HITMC_RAM_SEND(m_vi, HardwareReg::DDR_BASE_ADDR, wr_data, wr_len);
    if (status == OPT_OK) {
        emit operationCompleted("图像数据成功写入DDR并发送");

        // 可选：在此处调用 HITMC_SET_MODULE_para 配置寄存器触发硬件发送逻辑
        // HITMC_SET_MODULE_para(m_vi, TRIGGER_REG_ADDR, 0x01);
        // 寄存器写操作，语义清晰
        //status = HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_SELF_TEST, HardwareReg::VAL_SELF_TEST_BASE + temp_i);
    }
    else {
        emit errorOccurred(QString("写入DDR失败，错误码: %1").arg(status));
    }

    delete[] wr_data;
}

bool LvdsWorker::convertImageToDdrData(const QImage& img, ViUInt32* buffer, ViInt32& length)
{
    // 这里需要根据硬件要求的具体格式（如RGB888, YUV等）进行像素解析
    // 简单示例：将RGB值打包进 32位 数组
    const uchar* bits = img.bits();
    int byteCount = img.sizeInBytes();

    length = (byteCount + 3) / 4; // 计算需要多少个32位字

    if (length > MAX_DATA_LEN) {
        return false;
    }

    memset(buffer, 0, length * sizeof(ViUInt32));
    memcpy(buffer, bits, byteCount);

    return true;
}

void LvdsWorker::closeBoard()
{
    if (m_isInitialized && m_vi != VI_NULL) {
        ViStatus status = HITMC_MODULE_close(m_vi);
        m_isInitialized = false;
        m_vi = VI_NULL;
        emit logMessage(QString("LVDS板卡已关闭，状态码: %1").arg(status));
    }
}