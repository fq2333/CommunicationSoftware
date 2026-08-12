#include "CameraLinkWorker.h"
#include <QThread>
#include <QDebug>
#include <QFile>

CameraLinkWorker::CameraLinkWorker(QObject* parent)
    : QObject(parent), m_isInitialized(false), m_cmlkHandle(nullptr)
{
}

CameraLinkWorker::~CameraLinkWorker()
{
    closeBoard();
}

void CameraLinkWorker::initializeBoard(const QString& resourceName)
{
    emit logMessage(QString::fromLocal8Bit("正在模拟初始化 CameraLink 板卡，资源名: %1").arg(resourceName));

    // 【TODO: 等待 DLL 就绪后替换为真实的 API】
    // 例如: status = CMLK_Init(resourceName.toLocal8Bit().data(), &m_cmlkHandle);

    // 模拟耗时与成功
    QThread::msleep(500);
    m_isInitialized = true;
    emit logMessage(QString::fromLocal8Bit("CameraLink 板卡初始化成功 (模拟)"));
}

void CameraLinkWorker::sendLocalImage(const QString& imagePath)
{
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("CameraLink 未初始化！"));
        return;
    }

    emit logMessage(QString::fromLocal8Bit("加载 CameraLink 图像: %1").arg(imagePath));
    QImage img;
    if (!img.load(imagePath)) {
        emit errorOccurred(QString::fromLocal8Bit("图片加载失败！"));
        return;
    }

    if (img.width() != 4096 || img.height() != 4096) {
        emit errorOccurred(QString::fromLocal8Bit("图像尺寸不匹配! 当前尺寸为 %1x%2").arg(img.width()).arg(img.height()));
        return;
    }

    // 转换为 uint16 灰度格式
    QImage img16 = img.convertToFormat(QImage::Format_Grayscale16);

    QByteArray cmlkDataPacket;
    emit logMessage(QString::fromLocal8Bit("正在进行 2tap 12bit 数据组包..."));

    if (buildCameraLinkPacket(img16, cmlkDataPacket)) {
        emit logMessage(QString::fromLocal8Bit("组包完成，总数据量: %1 Words (32-bit)。准备下发...").arg(cmlkDataPacket.size()));

        // 【TODO: 替换为实际的发送 API】
        // 例如: status = CMLK_Send_RAM(m_cmlkHandle, cmlkDataPacket.data(), cmlkDataPacket.size());

        // 模拟发送过程
        QThread::msleep(1000);

        // 离线测试支持：将组包好的二进制数据存盘，供 FPGA 工程师用作仿真激励源
        QFile file("C:/cmlk_sim_packet.bin");
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reinterpret_cast<const char*>(cmlkDataPacket.constData()), cmlkDataPacket.size() * sizeof(quint32));
            file.close();
            emit logMessage(QString::fromLocal8Bit("已在 C 盘生成 CameraLink 仿真数据文件: cmlk_sim_packet.bin"));
        }

        emit operationCompleted(QString::fromLocal8Bit("CameraLink 图像发送完成！"));
    }
}

bool CameraLinkWorker::buildCameraLinkPacket(const QImage& img16, QByteArray& outBuffer)
{
    int width = img16.width();
    int height = img16.height();

    // 协议规定 2tap，即每个时钟(clk)出 2 个像素（共 24bit）。
    // 在紧凑打包下，每 2 个像素占用 3 个字节 (3 Bytes)。
    int bytesPerLine = (width / 2) * 3;

    // 预分配内存：4096行 * 6144字节/行 = 25,165,824 字节 (精准 24MB)
    outBuffer.reserve(height * bytesPerLine);

    for (int y = 0; y < height; ++y) {
        // 获取第 y 行的 16-bit 像素数据指针
        const quint16* scanLine = reinterpret_cast<const quint16*>(img16.constScanLine(y));

        // 遍历一行中的每对像素 (A 和 B)
        for (int x = 0; x < width; x += 2) {
            // 1. 取出像素，强制屏蔽高4位，仅保留有效 12bit (0x0FFF)
            quint16 pixelA = scanLine[x] & 0x0FFF;     // 像素 A (左侧，分配在 Port A/B)
            quint16 pixelB = scanLine[x + 1] & 0x0FFF; // 像素 B (右侧，分配在 Port C)

            // 2. 将两个 12bit 组合成一个 24bit 的逻辑块
            // cmlkData24 结构: [23:12] 为 PixelB, [11:0] 为 PixelA
            quint32 cmlkData24 = (pixelB << 12) | pixelA;

            // 3. 将 24bit 拆分为 3 个连续的 8bit 字节并压入缓冲区 (小端模式存储)
            outBuffer.append(static_cast<char>(cmlkData24 & 0xFF));         // Byte 0: A 的低 8 位
            outBuffer.append(static_cast<char>((cmlkData24 >> 8) & 0xFF));    // Byte 1: A 的高 4 位 + B 的低 4 位
            outBuffer.append(static_cast<char>((cmlkData24 >> 16) & 0xFF));   // Byte 2: B 的高 8 位
        }
    }

    return true;
}
void CameraLinkWorker::packAndSaveOffline(const QString& imagePath, const QString& savePath)
{
    emit logMessage(QString::fromLocal8Bit("加载 CameraLink 图像: %1").arg(imagePath));
    QImage img;
    if (!img.load(imagePath)) {
        emit errorOccurred(QString::fromLocal8Bit("图片加载失败，请检查路径。"));
        return;
    }

    if (img.width() != 4096 || img.height() != 4096) {
        emit errorOccurred(QString::fromLocal8Bit("图像尺寸不匹配! 协议要求 4096x4096，当前为 %1x%2").arg(img.width()).arg(img.height()));
        return;
    }

    // 转为 16-bit 格式
    QImage img16 = img.convertToFormat(QImage::Format_Grayscale16);
    if (img16.isNull()) {
        emit errorOccurred(QString::fromLocal8Bit("图像转换为 16-bit 格式失败。"));
        return;
    }

    QByteArray cmlkDataPacket;
    emit logMessage(QString::fromLocal8Bit("正在执行 2tap 12bit 协议组包..."));

    // 注意：请确保底层的这个组包函数已经是生成紧凑格式（QByteArray）的最新版本
    if (buildCameraLinkPacket(img16, cmlkDataPacket)) {

        // 【修改1】单位改为“字节”
        emit logMessage(QString::fromLocal8Bit("组包完成，总数据量: %1 字节。正在写入文件...").arg(cmlkDataPacket.size()));

        QFile file(savePath);
        if (file.open(QIODevice::WriteOnly)) {

            // 【修改2：致命错误修正】QByteArray 天然支持整体写入，不需要强转指针，绝不能再乘 sizeof(quint32)
            qint64 written = file.write(cmlkDataPacket);

            file.close();

            if (written == cmlkDataPacket.size()) {
                emit operationCompleted(QString::fromLocal8Bit("CameraLink 数据包已成功保存至: ") + savePath);
            }
            else {
                emit errorOccurred(QString::fromLocal8Bit("文件写入不完整，预期 %1 字节，实际写入 %2 字节！")
                    .arg(cmlkDataPacket.size()).arg(written));
            }
        }
        else {
            emit errorOccurred(QString::fromLocal8Bit("无法创建保存文件，请检查目录权限: ") + savePath);
        }
    }
}
void CameraLinkWorker::closeBoard()
{
    if (m_isInitialized) {
        emit logMessage(QString::fromLocal8Bit("正在关闭 CameraLink 接口..."));
        // 【TODO: 替换为实际的关闭 API】
        // CMLK_Close(m_cmlkHandle);
        m_isInitialized = false;
        m_cmlkHandle = nullptr;
    }
}