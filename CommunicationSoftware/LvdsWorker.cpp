#include "LvdsWorker.h"
#include <QDebug>
#include <QFileInfo>
#include <QThread>
#include <QFile>
#include <QDataStream>
#include "HardwareRegisters.h"
#include "rpc_windows_client.h"

#include <QtEndian>
#include <QDateTime>
#include <cstring>
// 假设图片最大不超过 32M * 4 字节
#define MAX_DATA_LEN (32 * 1024 * 1024)



// 辅助函数：计算单字节累加校验和[cite: 2]
quint16 calculateChecksum(const quint8* data, size_t length) {
    quint16 sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum += data[i]; // 进位舍弃，保留16位[cite: 2]
    }
    return sum;
}


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
        emit logMessage(QString::fromLocal8Bit("底层句柄打开成功，正在执行寄存器自检..."));

        ViUInt32 get_Data = 0;
        bool selfTestPassed = true;

        // 进行 10 次循环读写测试
        for (ViUInt32 temp_i = 0; temp_i < 10; temp_i++) {
            ViUInt32 writeVal = 0x146f + temp_i;

            // 寄存器写操作
            ViStatus writeStatus = HITMC_SET_MODULE_para(m_vi, 0x00, writeVal);
            // 寄存器读操作
            ViStatus readStatus = HITMC_GET_MODULE_para(m_vi, 0x00, &get_Data);

            // 检查 API 调用状态以及数据一致性
            if (writeStatus != 0 || readStatus != 0 || get_Data != writeVal) {
                selfTestPassed = false;

                // 使用 arg(..., 0, 16) 将数值格式化为十六进制显示，方便调试
                emit errorOccurred(QString::fromLocal8Bit("自检失败！轮次: %1, 写入: 0x%2, 读出: 0x%3")
                    .arg(temp_i)
                    .arg(writeVal, 0, 16)   
                    .arg(get_Data, 0, 16));
                break; // 只要有一次失败，立即退出测试
            }
        }

        if (selfTestPassed) {
            m_isInitialized = true;
            emit logMessage(QString::fromLocal8Bit("LVDS板卡初始化且自检通过！Session: %1").arg(m_vi));
        }
        else {
            // 自检失败说明硬件链路存在异常，为安全起见关闭句柄
            HITMC_MODULE_close(m_vi);
            m_vi = VI_NULL;
            m_isInitialized = false;
            emit errorOccurred(QString::fromLocal8Bit("LVDS板卡自检未通过，已断开连接。"));
        }
    }
    else {
        emit errorOccurred(QString::fromLocal8Bit("LVDS板卡初始化失败，错误码: %1").arg(status));
    }
}

void LvdsWorker::sendLocalImage(const QString& imagePath)
{
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("板卡未初始化，无法发送图片"));
        // 为了便于离线组包测试，如果你还没连硬件，可以把上面的 return 暂时注释掉
        //return;
    }

    emit logMessage(QString::fromLocal8Bit("正在加载本地 PNG 图像: %1").arg(imagePath));
    QImage img;
    if (!img.load(imagePath)) {
        emit errorOccurred(QString::fromLocal8Bit("加载本地图片失败，请检查路径或格式！"));
        return;
    }

    // 1. 尺寸校验 (协议要求 4096 x 4096)
    if (img.width() != 4096 || img.height() != 4096) {
        emit errorOccurred(QString::fromLocal8Bit("图像尺寸不匹配! 协议要求 4096x4096, 当前图像为 %1x%2")
            .arg(img.width()).arg(img.height()));
        // 如果想强行用小图测试，可启用下面这句缩放（注意缩放可能导致图像失真）
        // img = img.scaled(4096, 4096, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        return;
    }

    // 2. 转换为 16位 格式
    // Qt 5.13 以后支持 Format_Grayscale16，每个像素占 2 字节（即 uint16）
    QImage img16 = img.convertToFormat(QImage::Format_Grayscale16);
    if (img16.isNull()) {
        emit errorOccurred(QString::fromLocal8Bit("图像转换为 16-bit 格式失败，请检查 Qt 版本。"));
        return;
    }

    // 3. 提取原始 Raw 像素数据
    // 虽然 4096 * 2 = 8192 字节正好是 4 的倍数，不存在行尾填充(padding)，
    // 但为了代码绝对的安全健壮，推荐使用 constScanLine 逐行拷贝。
    QByteArray raw16BitData;
    int width = img16.width();
    int height = img16.height();
    int bytesPerPixel = 2; // uint16 占两字节

    raw16BitData.reserve(width * height * bytesPerPixel); // 预分配 32MB 内存，提升效率

    for (int y = 0; y < height; ++y) {
        const char* linePtr = reinterpret_cast<const char*>(img16.constScanLine(y));
        raw16BitData.append(linePtr, width * bytesPerPixel);
    }

    emit logMessage(QString::fromLocal8Bit("成功提取 16bit 原始像素数据，准备组包..."));

    // 4. 生成帧计数 (静态变量，每次发送自动+1)
    static quint32 frameCount = 1;

    // 5. 调用上一节编写的组包发送函数
    sendProtocolImage(raw16BitData, frameCount);

    frameCount++;
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

// 假设输入 raw16BitImage 是按本地端存储的 4096*4096*16bit (33554432 字节) 图像像素数据
bool LvdsWorker::buildProtocolData(const QByteArray& raw16BitImage, quint32 frameCount, ViUInt32* outBuffer, ViInt32& outWordLen)
{
    const int IMAGE_WIDTH = 4096;
    const int IMAGE_HEIGHT = 4096;
    const int ROW_BYTE_SIZE = 8196; // 4098列 * 2字节[cite: 2]

    // 总字节数 = 4097行 * 8196字节 = 33,579,012 字节
    const int TOTAL_BYTES = 4097 * ROW_BYTE_SIZE;

    if (raw16BitImage.size() < IMAGE_WIDTH * IMAGE_HEIGHT * 2) {
        emit errorOccurred(QString::fromLocal8Bit("原始图像数据大小不足!"));
        return false;
    }

    // 将输出缓存强转为字节指针，方便按协议字节逐个拼接
    quint8* pDest = reinterpret_cast<quint8*>(outBuffer);
    memset(pDest, 0, TOTAL_BYTES);

    // ==========================================
    // 第一步：构建第1行 (辅助数据)[cite: 2]
    // ==========================================
    quint8* auxRow = pDest; // 第1行首地址
    int offset = 0;

    // 1. 帧标识 8B: 0x4954926444160001[cite: 2]
    quint64 frameSync = 0x4954926444160001;
    qToBigEndian(frameSync, auxRow + offset);
    offset += 8;

    // 2. 曝光开始时间码 8B (高4字节秒，低4字节微秒)[cite: 2]
    // 此处以当前系统时间为例，实际需根据您的硬件状态读取
    qint64 currentMSecs = QDateTime::currentMSecsSinceEpoch();
    quint32 startSecs = currentMSecs / 1000;
    quint32 startUSecs = (currentMSecs % 1000) * 1000;
    qToBigEndian(startSecs, auxRow + offset);     offset += 4;
    qToBigEndian(startUSecs, auxRow + offset);    offset += 4;

    // 3. 曝光结束时间码 8B[cite: 2]
    // 假设曝光时间10ms
    quint32 endSecs = startSecs;
    quint32 endUSecs = startUSecs + 10000;
    qToBigEndian(endSecs, auxRow + offset);       offset += 4;
    qToBigEndian(endUSecs, auxRow + offset);      offset += 4;

    // 4. 帧计数 4B[cite: 2]
    qToBigEndian(frameCount, auxRow + offset);
    offset += 4;

    // 5. 预留字段 8166B[cite: 2]
    // (初始化时已memset为0，直接跳过)
    offset += 8166;

    // 6. 辅助数据累加校验和 2B[cite: 2]
    quint16 auxChecksum = calculateChecksum(auxRow, 8194);
    qToBigEndian(auxChecksum, auxRow + 8194);

    // ==========================================
    // 第二步：构建第2~4097行 (有效图像数据)[cite: 2]
    // ==========================================
    const quint8* pRawPixels = reinterpret_cast<const quint8*>(raw16BitImage.constData());

    for (int row = 0; row < IMAGE_HEIGHT; ++row) {
        quint8* currentRowPtr = pDest + (row + 1) * ROW_BYTE_SIZE; // 跳过第1行

        // 1. 行标识 2B: 0x4994[cite: 2]
        quint16 rowSync = 0x4994;
        qToBigEndian(rowSync, currentRowPtr);

        // 2. 图像数据 8192B (需要保证是大端格式)[cite: 2]
        // 假设您的 raw16BitImage 来源于 x86 内存（小端），需要转大端
        const quint16* srcPixel = reinterpret_cast<const quint16*>(pRawPixels + row * 8192);
        quint16* destPixel = reinterpret_cast<quint16*>(currentRowPtr + 2);
        for (int p = 0; p < 4096; ++p) {
            qToBigEndian(srcPixel[p], destPixel + p);
        }

        // 3. 行累加校验和 2B[cite: 2]
        quint16 rowChecksum = calculateChecksum(currentRowPtr, 8194);
        qToBigEndian(rowChecksum, currentRowPtr + 8194);
    }

    // ==========================================
    // 第三步：计算写DDR所需的 32bit 字长度
    // ==========================================
    // 总计 33,579,012 字节，正好能被 4 整除 (等于 8,394,753 Words)
    outWordLen = TOTAL_BYTES / 4;

    return true;
}
void LvdsWorker::sendProtocolImage(const QByteArray& raw16BitData, quint32 currentFrameCount)
{
    // ... 检查板卡是否初始化 ...
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("板卡未初始化，无法发送图片"));
        return;
    }

    
    if (raw16BitData.isNull()) {
        emit errorOccurred(QString::fromLocal8Bit("加载图片失败 "));
        return;
    }
    // MAX_DATA_LEN 为之前定义的 32*1024*1024 words
    ViUInt32* wr_data = new ViUInt32[32 * 1024 * 1024];
    ViInt32 wr_len = 0;

    emit logMessage(QString::fromLocal8Bit("正在进行协议数据组包与大端转换..."));

    if (buildProtocolData(raw16BitData, currentFrameCount, wr_data, wr_len)) {
        emit logMessage(QString::fromLocal8Bit("组包完成，准备下发至DDR，长度: %1 Words").arg(wr_len));


        // 离线测试代码 (存入本地文件以便用十六进制软件查验包头和校验和)
        FILE* fp = fopen("test_packet.bin", "wb");
        if (fp) {
            fwrite(wr_data, 4, wr_len, fp);
            fclose(fp);
            emit logMessage(QString::fromLocal8Bit("已生成组包文件test_packet.bin，请使用 Hex 工具查看验证！"));
        }


        // ==========================================
        // 3) 发送前复位操作 (已根据最新说明更新)
        // ==========================================
        emit logMessage(QString::fromLocal8Bit("3) 执行发送前复位操作..."));
        HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_RESET, 0x0);
        HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_2, 0x0); // 新增：清零2C
        HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_1, 0x0); // 新增：清零1C
        HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_RESET, 0x1);


        //// 写入DDR
        //ViStatus status = HITMC_RAM_SEND(m_vi, 0x20000000, wr_data, wr_len);

        // ==========================================
        // 4) 向DDR中加载发送文件
        // ==========================================
        emit logMessage(QString::fromLocal8Bit("正在向DDR写入数据，长度: %1 Words").arg(wr_len));
        ViStatus status = HITMC_RAM_SEND(m_vi, HardwareReg::DDR_BASE_ADDR, wr_data, wr_len);

        if (status == 0) {

            // ==========================================
            // 5) 启动发送 (已根据最新说明更新：构造脉冲触发)
            // ==========================================
            emit logMessage(QString::fromLocal8Bit("5) DDR写入成功，启动FPGA发送..."));
            HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_2, 0x1);
            HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_1, 0x1);
            HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_1, 0x0); // 新增：写入0形成下降沿脉冲

            // ==========================================
            // 6) 监测发送是否完成 (带超时保护机制)
            // ==========================================
            emit logMessage(QString::fromLocal8Bit("正在监测发送状态，等待完成..."));
            ViUInt32 get_Data = 0;
            bool isFinished = false;
            int timeoutCount = 0;
            const int MAX_TIMEOUT_MS = 10000; // 设定10秒超时（根据实际波特率和图幅调整）
            const int SLEEP_INTERVAL_MS = 10; // 每次轮询间隔10毫秒

            while (!isFinished && (timeoutCount * SLEEP_INTERVAL_MS) < MAX_TIMEOUT_MS) {
                HITMC_GET_MODULE_para(m_vi, HardwareReg::REG_TX_STATUS, &get_Data);

                // 读出数据的bit0位为1则表示发送结束
                if ((get_Data & 0x01) == 0x01) {
                    isFinished = true;
                }
                else {
                    QThread::msleep(SLEEP_INTERVAL_MS); // 休眠以释放CPU资源，不会阻塞UI
                    timeoutCount++;
                }
            }

            if (isFinished) {
                emit operationCompleted(QString::fromLocal8Bit("第 %1 帧图像发送结束！耗时约 %2 ms")
                    .arg(currentFrameCount)
                    .arg(timeoutCount * SLEEP_INTERVAL_MS));
            }
            else {
                emit errorOccurred(QString::fromLocal8Bit("发送超时！未检测到FPGA发送完成标志。"));
            }

            //// ==========================================
            //// 7) 发送完成后进行停止DDR操作和复位
            //// ==========================================
            //emit logMessage(QString::fromLocal8Bit("执行停止DDR及复位操作..."));
            //HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_2, 0x0);
            //HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_RESET, 0x0);

        }
        else {
            emit errorOccurred(QString::fromLocal8Bit("DDR写入失败，错误码：%1").arg(status));
        }


    }

    delete[] wr_data;
}
void LvdsWorker::readSelfTestData(const QString& saveFilePath)
{
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("板卡未初始化，无法读取自检数据。"));
        return;
    }

    ViUInt32 get_Data = 0;

    // 1. 读取接收状态
    HITMC_GET_MODULE_para(m_vi, HardwareReg::REG_RX_LINES, &get_Data);
    emit logMessage(QString::fromLocal8Bit("接收数据行数: %1 行").arg(get_Data));

    HITMC_GET_MODULE_para(m_vi, HardwareReg::REG_RX_BITS, &get_Data);
    emit logMessage(QString::fromLocal8Bit("接收到的总bit数量: %1 bits").arg(get_Data));

    // 准备写文件
    QFile file(saveFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QString::fromLocal8Bit("无法创建或打开保存文件: ") + saveFilePath);
        return;
    }

    // 2. 读取DDR中的数据
    HITMC_GET_MODULE_para(m_vi, HardwareReg::REG_RX_DDR_WPTR, &get_Data);
    ViInt32 rd_len = (get_Data - HardwareReg::RX_DDR_BASE_ADDR) / 4;

    if (rd_len > 0) {
        emit logMessage(QString::fromLocal8Bit("正在从DDR中读取 %1 Words 的接收数据...").arg(rd_len));
        ViInt32* rd_data = new ViInt32[rd_len];

        ViStatus status = HITMC_RAM_REV(m_vi, HardwareReg::RX_DDR_BASE_ADDR, rd_data, &rd_len);
        file.write(reinterpret_cast<const char*>(rd_data), rd_len * 4);
        //if (status == 0) { // 假设0为成功 这个可能不对
        //    file.write(reinterpret_cast<const char*>(rd_data), rd_len * 4);
        //}
        //else {
        //    emit errorOccurred(QString::fromLocal8Bit("读取接收DDR数据失败，错误码：%1").arg(status));
        //}
        delete[] rd_data;
    }
    else {
        emit logMessage(QString::fromLocal8Bit("DDR中没有可读取的自检接收数据。"));
    }

    // 3. 读取未写入DDR的剩余数据
    HITMC_GET_MODULE_para(m_vi, HardwareReg::REG_RX_REMAIN_LEN, &get_Data);
    ViInt32 remain_len = get_Data;

    if (remain_len > 0) {
        emit logMessage(QString::fromLocal8Bit("正在从FIFO中读取剩余未入DDR的数据: %1 Words").arg(remain_len));
        for (int i = 0; i < remain_len; i++) {
            ViUInt32 remainData = 0;
            HITMC_GET_MODULE_para(m_vi, HardwareReg::REG_RX_REMAIN_DATA, &remainData);
            file.write(reinterpret_cast<const char*>(&remainData), 4);
        }
    }

    file.close();
    emit operationCompleted(QString::fromLocal8Bit("自检接收数据已全部保存至: ") + saveFilePath);
}
void LvdsWorker::resetBoard()
{
    if (!m_isInitialized) {
        emit errorOccurred(QString::fromLocal8Bit("板卡未初始化，无法执行复位。"));
        return;
    }

    emit logMessage(QString::fromLocal8Bit("执行独立复位操作..."));
    HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_RESET, 0x0);
    HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_2, 0x0);
    HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_1, 0x0);
    HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_RESET, 0x1);

    emit operationCompleted(QString::fromLocal8Bit("板卡复位完成！"));
}

void LvdsWorker::closeBoard()
{
    if (m_isInitialized && m_vi != VI_NULL) {
        emit logMessage(QString::fromLocal8Bit("正在执行关卡前寄存器清理..."));

        // 步骤6 新增：写0清理状态
        HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_START_TX_2, 0x0);
        HITMC_SET_MODULE_para(m_vi, HardwareReg::REG_RESET, 0x0);

        ViStatus status = HITMC_MODULE_close(m_vi);
        m_isInitialized = false;
        m_vi = VI_NULL;
        emit logMessage(QString::fromLocal8Bit("LVDS板卡已安全关闭，状态码: %1").arg(status));
    }
}