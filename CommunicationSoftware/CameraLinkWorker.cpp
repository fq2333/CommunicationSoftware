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

    QVector<quint32> cmlkDataPacket;
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

bool CameraLinkWorker::buildCameraLinkPacket(const QImage& img16, QVector<quint32>& outBuffer)
{
    int width = img16.width();
    int height = img16.height();

    // 协议规定 2tap，即每个时钟(clk)出 2 个像素。所以每行有效 clk 数 = width / 2
    int validClkPerLine = width / 2;
    // 协议要求行间隔 10 clk[cite: 1]
    int blankingClkPerLine = 10;

    int totalClkPerLine = validClkPerLine + blankingClkPerLine;

    // 预分配内存，提升效率 (4096行 * 每行总clk数)
    outBuffer.reserve(height * totalClkPerLine);

    for (int y = 0; y < height; ++y) {
        // 获取第 y 行的 16-bit 像素数据指针
        const quint16* scanLine = reinterpret_cast<const quint16*>(img16.constScanLine(y));

        // 遍历一行中的每对像素 (A 和 B)
        for (int x = 0; x < width; x += 2) {
            // 取出像素，并强制屏蔽掉高4位，只保留最低 12bit (0x0FFF)
            quint32 pixelA = scanLine[x] & 0x0FFF;     // 像素 A (左)
            quint32 pixelB = scanLine[x + 1] & 0x0FFF; // 像素 B (右)

            // 组装成一个 24bit 的数据包。硬件上映射为 Cmlk_data[23:0][cite: 1]
            // 通常低 12位放像素A (对应 Port A/B)，高 12位放像素B (对应 Port C)
            quint32 cmlkData24 = (pixelB << 12) | pixelA;

            // 压入缓存区
            outBuffer.append(cmlkData24);
        }

        // 行尾插入 10 clk 的行间隔空白数据 (Blanking)[cite: 1]
        // 实际硬件的行有效信号 (Syn_line) 会在这 10 个周期内拉低[cite: 1]
        for (int i = 0; i < blankingClkPerLine; ++i) {
            outBuffer.append(0x00000000);
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

    QVector<quint32> cmlkDataPacket;
    emit logMessage(QString::fromLocal8Bit("正在执行 2tap 12bit 协议组包..."));

    // buildCameraLinkPacket 函数在上一条回复中已提供
    if (buildCameraLinkPacket(img16, cmlkDataPacket)) {
        emit logMessage(QString::fromLocal8Bit("组包完成，总数据量: %1 Words (32-bit)。正在写入文件...").arg(cmlkDataPacket.size()));

        QFile file(savePath);
        if (file.open(QIODevice::WriteOnly)) {
            // 将 QVector 中的数据原封不动写入 .bin 文件
            qint64 written = file.write(reinterpret_cast<const char*>(cmlkDataPacket.constData()),
                cmlkDataPacket.size() * sizeof(quint32));
            file.close();

            if (written == cmlkDataPacket.size() * sizeof(quint32)) {
                emit operationCompleted(QString::fromLocal8Bit("CameraLink 数据包已成功保存至: ") + savePath);
            }
            else {
                emit errorOccurred(QString::fromLocal8Bit("文件写入不完整，可能磁盘空间不足！"));
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