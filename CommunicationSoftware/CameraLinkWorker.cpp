#include "CameraLinkWorker.h"
#include <QThread>
#include <QDebug>
#include <QFile>
#include <QFileInfo> // 【新增】用于解析文件路径前缀
CameraLinkWorker::CameraLinkWorker(QObject* parent)
    : QObject(parent), m_isInitialized(false), m_cmlkHandle(0)
{
}

CameraLinkWorker::~CameraLinkWorker()
{
    closeBoard();
}

void CameraLinkWorker::initializeBoard(const QString& resourceName)
{
    if (m_isInitialized) return;

    emit logMessage(QString::fromLocal8Bit("正在解析资源名并初始化 CameraLink，输入: %1").arg(resourceName));

    // ==========================================
    // 1. 智能解析标准 VISA 资源名 (例如: PXI1::0::0::INSTR)
    // ==========================================
    ViUInt32 bus = 1, device = 0, function = 0; // 默认给 Demo 中的 1,0,0

    // 按照 "::" 切割字符串
    QStringList parts = resourceName.split("::");
    if (parts.size() >= 3) {
        QString busStr = parts[0];
        // 剥离开头的 "PXI" 字母，提取纯数字
        if (busStr.startsWith("PXI", Qt::CaseInsensitive)) {
            busStr = busStr.mid(3);
        }
        bus = busStr.toUInt();
        device = parts[1].toUInt();
        function = parts[2].toUInt();
    }
    else {
        emit errorOccurred(QString::fromLocal8Bit("资源名格式不正确，请使用如 PXI1::0::0::INSTR 格式"));
        return;
    }

    emit logMessage(QString::fromLocal8Bit("PCIe 拓扑解析结果 - 总线(Bus): %1, 设备(Device): %2, 功能(Function): %3")
        .arg(bus).arg(device).arg(function));

    // ==========================================
    // 2. 调用官方推荐的 BDF 物理直连初始化
    // ==========================================
    try {
        // 调用 Demo 中的 PXIe_viOpenByBdf
        ViStatus status = PXIe_viOpenByBdf(bus, device, function, &m_cmlkHandle); 

            if (status >= 0) {
                m_isInitialized = true;
                emit logMessage(QString::fromLocal8Bit("CameraLink 板卡 BDF 物理直连初始化成功!"));
            }
            else {
                emit errorOccurred(QString::fromLocal8Bit("CameraLink BDF 初始化失败，错误码: %1").arg(status));
            }
    }
    catch (...) {
        // 捕获可能因为没装 xdma 驱动而导致的系统崩溃[cite: 20]
        emit errorOccurred(QString::fromLocal8Bit("致命异常：调用 PXIe_viOpenByBdf 发生崩溃！请确认已安装 XDMA 驱动。"));
    }
}

void CameraLinkWorker::closeBoard()
{
    if (m_isInitialized && m_cmlkHandle != 0) {
        emit logMessage(QString::fromLocal8Bit("正在关闭 CameraLink 接口..."));

        // 调用 DLL: HITATCI_PXIe_Close
        HITATCI_PXIe_Close(m_cmlkHandle);

        m_isInitialized = false;
        m_cmlkHandle = 0;
        emit logMessage(QString::fromLocal8Bit("CameraLink 设备已安全关闭。"));
    }
}

void CameraLinkWorker::resetBoard()
{
    if (!m_isInitialized) return;
    
    // 按照 Demo 逻辑，进行 FPGA 复位[cite: 20]
    ViUInt32 write_data = 0;
    HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x4, write_data);
    QThread::msleep(1);
    
    write_data = 1;
    HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x4, write_data);
    QThread::msleep(1);
    
    emit logMessage(QString::fromLocal8Bit("CameraLink 板卡已完成硬件复位。"));
}

void CameraLinkWorker::sendLocalImage(const QString& imagePath)
{
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("CameraLink 未初始化，无法发送！"));
        return;
    }

    emit logMessage(QString::fromLocal8Bit("加载 CameraLink 本地图像: %1").arg(imagePath));
    QImage img;
    if (!img.load(imagePath)) {
        emit errorOccurred(QString::fromLocal8Bit("图片加载失败！"));
        return;
    }

    if (img.width() != 4096 || img.height() != 4096) {
        emit errorOccurred(QString::fromLocal8Bit("图像尺寸不匹配! 必须为 4096x4096"));
        return;
    }

    QImage img16 = img.convertToFormat(QImage::Format_Grayscale16);
    QByteArray cmlkDataPacket;

    if (buildCameraLinkPacket(img16, cmlkDataPacket)) {
        
        // ==========================================
        // 步骤 1：上位机使用 axi4-lite 配置图像参数
        // ==========================================
        ViUInt32 write_data;
        
        // 0x8: 一帧图片有多少个24bit (4096x4096/2 = 8388608)
        write_data = 8388608;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x8, write_data);
        
        // 0x10: 一行有多少个24bit (2048)
        write_data = 2048;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x10, write_data);
        
        // 0x14: 一帧有多少行 (4096)
        write_data = 4096;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x14, write_data);
        
        // 0x18: 每行发送完成后等待的周期数 (10)
        write_data = 10;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x18, write_data);

        // ==========================================
        // 步骤 2：使用 PCIE 将图片写入 DDR 内存
        // ==========================================
        ViUInt32 dmaByteLen = cmlkDataPacket.size(); 
        emit logMessage(QString::fromLocal8Bit("启动 DMA 传输，目标地址 0x20000000，数据量: %1 Bytes...").arg(dmaByteLen));

        ViStatus status = HITATCI_PXIe_viOut32_dma(m_cmlkHandle, BAR0_SPACE, 0x20000000, 
                                                   (ViPUInt32)cmlkDataPacket.data(), dmaByteLen);
        if (status < 0) {
            emit errorOccurred(QString::fromLocal8Bit("CameraLink DMA 内存写入失败，错误码: %1").arg(status));
            return;
        }

        // ==========================================
        // 步骤 3：控制 FPGA 开始从 DDR 读取数据
        // ==========================================
        write_data = 1;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x2c, write_data);
        QThread::msleep(1);

        // ==========================================
        // 步骤 4：发送启动命令 (制造一个跳变的上升沿)
        // ==========================================
        emit logMessage(QString::fromLocal8Bit("触发 CameraLink 硬件接口向外打图..."));
        
        write_data = 0;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x1c, write_data);
        QThread::msleep(1);
        
        write_data = 1;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x1c, write_data);
        QThread::msleep(1);

        // ==========================================
        // 步骤 5：轮询状态查询，等待发送结束
        // ==========================================
        ViUInt32 read_data = 0;
        int timeoutCounter = 0;
        
        while (true) {
            status = HITATCI_PXIe_viIn32(m_cmlkHandle, BAR0_SPACE, 0x34, &read_data);
            
            if (status != 0) {
                emit errorOccurred(QString::fromLocal8Bit("读取状态寄存器 0x34 失败!"));
                break;
            }
            
            // 状态为 1 表示发送结束
            if (read_data == 1) {
                emit operationCompleted(QString::fromLocal8Bit("CameraLink 本地图像发送完毕！"));
                break;
            }
            
            if (++timeoutCounter > 50) {
                emit errorOccurred(QString::fromLocal8Bit("CameraLink 发送超时卡死！"));
                break;
            }
            
            QThread::msleep(100); 
        }

        // ==========================================
        // 步骤 6：实验结束/发送完成后的硬件复位清理
        // ==========================================
        write_data = 0;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x2c, write_data);
        QThread::msleep(1);
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

        // ==========================================
        // 【新增】：利用 QFileInfo 剥离用户输入的后缀，自动生成双路径
        // ==========================================
        QFileInfo fileInfo(savePath);
        // 获取不带后缀的纯路径 (例如 "C:/cmlk_sim_packet")
        QString basePath = fileInfo.absolutePath() + "/" + fileInfo.baseName();

        QString binPath = basePath + ".bin";
        QString rawPath = basePath + ".raw";

        bool binOk = false, rawOk = false;

        // 1. 保存为 .bin 格式
        QFile binFile(binPath);
        if (binFile.open(QIODevice::WriteOnly)) {
            qint64 written = binFile.write(cmlkDataPacket);
            binFile.close();
            binOk = (written == cmlkDataPacket.size());
        }

        // 2. 保存为 .raw 格式
        QFile rawFile(rawPath);
        if (rawFile.open(QIODevice::WriteOnly)) {
            qint64 written = rawFile.write(cmlkDataPacket);
            rawFile.close();
            rawOk = (written == cmlkDataPacket.size());
        }

        // 检查双写结果
        if (binOk && rawOk) {
            emit operationCompleted(QString::fromLocal8Bit("CameraLink 数据包已成功双份保存:\n  -> %1\n  -> %2").arg(binPath).arg(rawPath));
        }
        else {
            emit errorOccurred(QString::fromLocal8Bit("文件写入失败或不完整，请检查磁盘空间及目录权限！"));
        }
    }
}
void CameraLinkWorker::sendImageFromMemory(const QByteArray& imageData, quint32 width, quint32 height, quint8 bitDepth)
{
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("CameraLink 未初始化，无法发送内存图像！"));
        return;
    }

    if (width != 4096 || height != 4096) {
        emit errorOccurred(QString::fromLocal8Bit("内存图像尺寸不匹配! CameraLink 必须为 4096x4096"));
        return;
    }

    emit logMessage(QString::fromLocal8Bit("正在处理路由中心分发来的内存图像并执行组包..."));

    // 1. 将光纤传来的内存裸数据还原为 QImage
    int bytesPerPixel = (bitDepth == 16) ? 2 : 1;
    int bytesPerLine = width * bytesPerPixel;
    QImage::Format format = (bitDepth == 16) ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8;
    QImage rawImg(reinterpret_cast<const uchar*>(imageData.constData()), width, height, bytesPerLine, format);

    // 2. 统一转换为 16 位以适应底层 12-bit 组包协议
    QImage img16 = rawImg.convertToFormat(QImage::Format_Grayscale16);
    QByteArray cmlkDataPacket;

    if (buildCameraLinkPacket(img16, cmlkDataPacket)) {

        // ==========================================
        // 步骤 1：上位机使用 axi4-lite 配置图像参数
        // ==========================================
        ViUInt32 write_data;

        // 0x8: 一帧图片有多少个24bit (4096x4096/2 = 8388608)
        write_data = 8388608;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x8, write_data);

        // 0x10: 一行有多少个24bit (2048)
        write_data = 2048;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x10, write_data);

        // 0x14: 一帧有多少行 (4096)
        write_data = 4096;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x14, write_data);

        // 0x18: 每行发送完成后等待的周期数 (10)
        write_data = 10;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x18, write_data);

        // ==========================================
        // 步骤 2：使用 PCIE 将图片写入 DDR 内存
        // ==========================================
        ViUInt32 dmaByteLen = cmlkDataPacket.size();

        ViStatus status = HITATCI_PXIe_viOut32_dma(m_cmlkHandle, BAR0_SPACE, 0x20000000,
            (ViPUInt32)cmlkDataPacket.data(), dmaByteLen);
        if (status < 0) {
            emit errorOccurred(QString::fromLocal8Bit("CameraLink DMA 内存写入失败，错误码: %1").arg(status));
            return;
        }

        // ==========================================
        // 步骤 3：控制 FPGA 开始从 DDR 读取数据
        // ==========================================
        write_data = 1;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x2c, write_data);
        QThread::msleep(1);

        // ==========================================
        // 步骤 4：发送启动命令 (制造一个跳变的上升沿)
        // ==========================================
        write_data = 0;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x1c, write_data);
        QThread::msleep(1);

        write_data = 1;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x1c, write_data);
        QThread::msleep(1);

        // ==========================================
        // 步骤 5：轮询状态查询，等待发送结束
        // ==========================================
        ViUInt32 read_data = 0;
        int timeoutCounter = 0;

        while (true) {
            status = HITATCI_PXIe_viIn32(m_cmlkHandle, BAR0_SPACE, 0x34, &read_data);

            if (status != 0) {
                emit errorOccurred(QString::fromLocal8Bit("读取状态寄存器 0x34 失败!"));
                break;
            }

            if (read_data == 1) {
                emit operationCompleted(QString::fromLocal8Bit("CameraLink 内存高速路由图像发送完毕！"));
                break;
            }

            if (++timeoutCounter > 50) {
                emit errorOccurred(QString::fromLocal8Bit("CameraLink 发送超时卡死！"));
                break;
            }

            QThread::msleep(100);
        }

        // ==========================================
        // 步骤 6：实验结束/发送完成后的硬件复位清理
        // ==========================================
        write_data = 0;
        HITATCI_PXIe_viOut32(m_cmlkHandle, BAR0_SPACE, 0x2c, write_data);
        QThread::msleep(1);
    }
}
