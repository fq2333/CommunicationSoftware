#include "CommunicationSoftware.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QToolBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTableWidget>
#include <QHeaderView>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QTime>
#include <QFileDialog>
#include <QButtonGroup>
#include <QComboBox>
#include <QRadioButton>
#include <QSpinBox>
#include <QComboBox>


#include <QThread>
#include "LvdsWorker.h"
#include "CameraLinkWorker.h" // [新增]
#include "Rfm2gWorker.h"
#include <sstream>

CommunicationSoftware::CommunicationSoftware(QWidget* parent)
    : QMainWindow(parent),
    mLvdsThread(nullptr), mLvdsWorker(nullptr),
    m_leResource(nullptr), m_leImagePath(nullptr),
    m_imageView(nullptr), m_imageScene(nullptr),
    m_dataTable(nullptr), m_logBrowser(nullptr),
    m_cmlkThread(nullptr), m_cmlkWorker(nullptr),
    m_rfmThread(nullptr), m_rfmWorker(nullptr)// 初始化指针为空，防止野指针
{
    ui.setupUi(this);
    initUI();

    // 放开这里的注释！让线程启动，绑定连接
    initThreads();
}

CommunicationSoftware::~CommunicationSoftware()
{
    cleanupThreads();
    // 如果 ui 是指针则需要 delete ui;
}
void CommunicationSoftware::initThreads()
{
    // 实例化 Thread 和 Worker
    mLvdsThread = new QThread(this);
    // 注意：Worker 不要传 parent，否则无法 moveToThread
    mLvdsWorker = new LvdsWorker();

    // 将工作对象移入子线程
    mLvdsWorker->moveToThread(mLvdsThread);

    // ========================================================
    // 关键点1：将主界面(UI)的指令信号，绑定到 Worker 的执行槽函数
    // ========================================================
    connect(this, &CommunicationSoftware::sigInitLvds, mLvdsWorker, &LvdsWorker::initializeBoard);
    connect(this, &CommunicationSoftware::sigSendImage, mLvdsWorker, &LvdsWorker::sendLocalImage);
    connect(this, &CommunicationSoftware::sigCloseBoard, mLvdsWorker, &LvdsWorker::closeBoard);
    connect(this, &CommunicationSoftware::sigResetBoard, mLvdsWorker, &LvdsWorker::resetBoard);
    connect(this, &CommunicationSoftware::sigReadSelfTestData, mLvdsWorker, &LvdsWorker::readSelfTestData);
    // ========================================================
    // 关键点2：将 Worker 的状态反馈信号，绑定到 UI 界面的槽函数！
    // 只有写了这三行，底层 emits 的信息才能跑到 m_logBrowser 里显示
    // ========================================================
    connect(mLvdsWorker, &LvdsWorker::logMessage, this, &CommunicationSoftware::onWorkerLogMessage);
    connect(mLvdsWorker, &LvdsWorker::errorOccurred, this, &CommunicationSoftware::onWorkerError);
    connect(mLvdsWorker, &LvdsWorker::operationCompleted, this, &CommunicationSoftware::onWorkerFinished);

    // 确保线程结束时安全清理内存
    connect(mLvdsThread, &QThread::finished, mLvdsWorker, &QObject::deleteLater);

    // 启动多线程
    mLvdsThread->start();

    // ==========================================
    // [新增] CameraLink 多线程初始化
    // ==========================================
    m_cmlkThread = new QThread(this);
    m_cmlkWorker = new CameraLinkWorker();
    m_cmlkWorker->moveToThread(m_cmlkThread);

    // 绑定跨线程工作信号
    connect(this, &CommunicationSoftware::sigPackAndSaveCmlk, m_cmlkWorker, &CameraLinkWorker::packAndSaveOffline);

    // 绑定状态回传信号到统一的 UI 槽函数（复用 LVDS 的槽即可，在同一个日志框打印）
    connect(m_cmlkWorker, &CameraLinkWorker::logMessage, this, &CommunicationSoftware::onWorkerLogMessage);
    connect(m_cmlkWorker, &CameraLinkWorker::errorOccurred, this, &CommunicationSoftware::onWorkerError);
    connect(m_cmlkWorker, &CameraLinkWorker::operationCompleted, this, &CommunicationSoftware::onWorkerFinished);

    connect(m_cmlkThread, &QThread::finished, m_cmlkWorker, &QObject::deleteLater);
    m_cmlkThread->start();

    // ==========================================
    // 3. RFM2g 光纤反射内存 多线程初始化
    // ==========================================
    m_rfmThread = new QThread(this);
    m_rfmWorker = new Rfm2gWorker();

    // 移入子线程
    m_rfmWorker->moveToThread(m_rfmThread);

    // 绑定主UI到Worker的控制信号
    connect(this, &CommunicationSoftware::sigInitRfm, m_rfmWorker, &Rfm2gWorker::initBoard);
    connect(this, &CommunicationSoftware::sigSendRfmData, m_rfmWorker, &Rfm2gWorker::sendDynamicsData);
    connect(this, &CommunicationSoftware::sigWaitRfmReturn, m_rfmWorker, &Rfm2gWorker::waitForReturnData);

    // 绑定Worker状态回传到UI日志
    connect(m_rfmWorker, &Rfm2gWorker::logMessage, this, &CommunicationSoftware::onWorkerLogMessage);
    connect(m_rfmWorker, &Rfm2gWorker::errorOccurred, this, &CommunicationSoftware::onWorkerError);
    connect(m_rfmWorker, &Rfm2gWorker::operationCompleted, this, &CommunicationSoftware::onWorkerFinished);

    // 确保安全退出
    connect(m_rfmThread, &QThread::finished, m_rfmWorker, &QObject::deleteLater);

    // 启动线程
    m_rfmThread->start();


}
void CommunicationSoftware::initUI()
{
    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);

    // ==========================================
    // 左侧：接口参数配置区
    // ==========================================
    QToolBox* configToolBox = new QToolBox(mainSplitter);
    configToolBox->setMinimumWidth(300);
    configToolBox->setMaximumWidth(400);

    QWidget* lvdsPage = new QWidget();
    QVBoxLayout* lvdsLayout = new QVBoxLayout(lvdsPage);

    // 使用成员变量实例化
    m_leResource = new QLineEdit("10.109.3.100::PXI29::12::INSTR", lvdsPage);
    QPushButton* btnInitLvds = new QPushButton(QString::fromLocal8Bit("初始化LVDS板卡"), lvdsPage);
    // [新增] 实例化断开连接按钮
    QPushButton* btnDisconnect = new QPushButton(QString::fromLocal8Bit("断开连接"), lvdsPage);


    m_leImagePath = new QLineEdit("C:/test_image.bmp", lvdsPage);
    QPushButton* btnSelectImage = new QPushButton(QString::fromLocal8Bit("选择本地图像..."), lvdsPage);
    QPushButton* btnSendImage = new QPushButton(QString::fromLocal8Bit("写入板卡并发送"), lvdsPage);

    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("设备资源名:")));
    lvdsLayout->addWidget(m_leResource);
    lvdsLayout->addWidget(btnInitLvds);
    lvdsLayout->addWidget(btnDisconnect); // [新增] 将按钮加入布局

    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("图像文件路径:")));
    lvdsLayout->addWidget(m_leImagePath);
    lvdsLayout->addWidget(btnSelectImage);
    lvdsLayout->addWidget(btnSendImage);
    


    // 新增独立复位按钮
    QPushButton* btnResetBoard = new QPushButton(QString::fromLocal8Bit("复位板卡"), lvdsPage);
    lvdsLayout->insertWidget(3, btnResetBoard); // 插入到初始化按钮下方

    // 新增自检数据保存路径及按钮
    QLineEdit* leSavePath = new QLineEdit(QString::fromLocal8Bit("rx_selftest_data.bin"), lvdsPage);
    QPushButton* btnReadData = new QPushButton(QString::fromLocal8Bit("读取自检接收数据"), lvdsPage);
    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("接收数据保存路径:")));
    lvdsLayout->addWidget(leSavePath);
    lvdsLayout->addWidget(btnReadData);

    lvdsLayout->addStretch();
    configToolBox->addItem(lvdsPage, QString::fromLocal8Bit("1. LVDS接口配置"));


    // ==========================================
    // [新增] 2. CameraLink 离线组包配置页
    // ==========================================
    QWidget* cmlkPage = new QWidget();
    QVBoxLayout* cmlkLayout = new QVBoxLayout(cmlkPage);

    m_leCmlkImagePath = new QLineEdit(QString::fromLocal8Bit("C:/test_image_12bit.png"), cmlkPage);
    QPushButton* btnSelectCmlkImage = new QPushButton(QString::fromLocal8Bit("选择本地图像..."), cmlkPage);

    // 默认输出路径设为软件运行目录，避免 C 盘权限问题
    QString defaultBinPath = QCoreApplication::applicationDirPath() + "/cmlk_sim_packet.bin";
    m_leCmlkSavePath = new QLineEdit(defaultBinPath, cmlkPage);
    QPushButton* btnSelectCmlkSave = new QPushButton(QString::fromLocal8Bit("选择保存位置..."), cmlkPage);

    QPushButton* btnPackAndSave = new QPushButton(QString::fromLocal8Bit("生成 CameraLink 数据包 (.bin)"), cmlkPage);

    // 组装 UI
    cmlkLayout->addWidget(new QLabel(QString::fromLocal8Bit("输入图像文件路径 (4096x4096):")));
    cmlkLayout->addWidget(m_leCmlkImagePath);
    cmlkLayout->addWidget(btnSelectCmlkImage);

    cmlkLayout->addWidget(new QLabel(QString::fromLocal8Bit("生成的 .bin 文件保存路径:")));
    cmlkLayout->addWidget(m_leCmlkSavePath);
    cmlkLayout->addWidget(btnSelectCmlkSave);

    cmlkLayout->addWidget(btnPackAndSave);
    cmlkLayout->addStretch();

    configToolBox->addItem(cmlkPage, QString::fromLocal8Bit("2. CameraLink 接口配置"));


    // (... 省略 UDP/RS422 页面的添加逻辑，与之前一致 ...)
    // ==========================================
    // 3. 动力学数据流与 RFM2g 配置页
    // ==========================================
    QWidget* rfmPage = new QWidget();
    QVBoxLayout* rfmLayout = new QVBoxLayout(rfmPage);

    QLineEdit* leRfmDevice = new QLineEdit("/dev/rfm2g0", rfmPage); // Windows下可能是 "\\.\rfm2g0"
    QPushButton* btnInitRfm = new QPushButton(QString::fromLocal8Bit("初始化 RFM 光纤网卡"), rfmPage);

    QLineEdit* leRfmTargetNode = new QLineEdit("1", rfmPage); // 下位机节点ID
    QLineEdit* leRfmTxOffset = new QLineEdit("0x1000", rfmPage); // 发送动力学数据的偏移
    QPushButton* btnSendDynamics = new QPushButton(QString::fromLocal8Bit("模拟发送本地动力学数据并触发中断"), rfmPage);

    QLineEdit* leRfmRxOffset = new QLineEdit("0x2000", rfmPage); // 接收回传数据的偏移
    QLineEdit* leRfmRxLength = new QLineEdit("1024", rfmPage);   // 接收长度
    QPushButton* btnWaitReturn = new QPushButton(QString::fromLocal8Bit("开启监听下位机回传"), rfmPage);

    rfmLayout->addWidget(new QLabel(QString::fromLocal8Bit("设备路径 (Device Path):")));
    rfmLayout->addWidget(leRfmDevice);
    rfmLayout->addWidget(btnInitRfm);
    rfmLayout->addWidget(new QLabel(QString::fromLocal8Bit("目标节点ID与发送Offset:")));
    rfmLayout->addWidget(leRfmTargetNode);
    rfmLayout->addWidget(leRfmTxOffset);
    rfmLayout->addWidget(btnSendDynamics);
    rfmLayout->addWidget(new QLabel(QString::fromLocal8Bit("接收Offset与期望字节长度:")));
    rfmLayout->addWidget(leRfmRxOffset);
    rfmLayout->addWidget(leRfmRxLength);
    rfmLayout->addWidget(btnWaitReturn);
    rfmLayout->addStretch();

    //configToolBox->addItem(rfmPage, QString::fromLocal8Bit("3. RFM2g 光纤反射内存配置"));

    // --- 3.2 动力学数据源选择 ---
    QGroupBox* grpSource = new QGroupBox(QString::fromLocal8Bit("动力学数据源配置"), rfmPage);
    QVBoxLayout* srcLayout = new QVBoxLayout(grpSource);

    m_radioLocalFile = new QRadioButton(QString::fromLocal8Bit("本地离线 J2000 数据回放"), grpSource);
    m_radioUdpNetwork = new QRadioButton(QString::fromLocal8Bit("外部 UDP 实时数据接入 (转发)"), grpSource);
    m_radioLocalFile->setChecked(true); // 默认选中本地

    QButtonGroup* srcGroup = new QButtonGroup(this);
    srcGroup->addButton(m_radioLocalFile);
    srcGroup->addButton(m_radioUdpNetwork);

    srcLayout->addWidget(m_radioLocalFile);
    srcLayout->addWidget(m_radioUdpNetwork);

    // --- 3.3 本地文件参数 ---
    QGroupBox* grpLocal = new QGroupBox(QString::fromLocal8Bit("本地回放参数"), grpSource);
    QGridLayout* localLayout = new QGridLayout(grpLocal);

    m_leJ2000Path = new QLineEdit("C:/J2000_Trajectory_Data.txt", grpLocal);
    QPushButton* btnSelectData = new QPushButton(QString::fromLocal8Bit("浏览..."), grpLocal);
    m_spinIntervalMs = new QSpinBox(grpLocal);
    m_spinIntervalMs->setRange(1, 5000); // 1ms 到 5s
    m_spinIntervalMs->setValue(10);      // 默认 10ms 步长
    m_spinIntervalMs->setSuffix(" ms");

    localLayout->addWidget(new QLabel(QString::fromLocal8Bit("数据文件路径:")), 0, 0);
    localLayout->addWidget(m_leJ2000Path, 0, 1);
    localLayout->addWidget(btnSelectData, 0, 2);
    localLayout->addWidget(new QLabel(QString::fromLocal8Bit("逐行发送间隔:")), 1, 0);
    localLayout->addWidget(m_spinIntervalMs, 1, 1);
    srcLayout->addWidget(grpLocal);

    // --- 3.4 UDP 网络参数 (预留界面) ---
    QGroupBox* grpUdp = new QGroupBox(QString::fromLocal8Bit("UDP 网络参数"), grpSource);
    QGridLayout* udpLayout = new QGridLayout(grpUdp);

    m_cmbUdpMode = new QComboBox(grpUdp);
    m_cmbUdpMode->addItems({ QString::fromLocal8Bit("单播 (Unicast)"), QString::fromLocal8Bit("组播 (Multicast)"), QString::fromLocal8Bit("广播 (Broadcast)") });
    m_leUdpLocalPort = new QLineEdit("8080", grpUdp);
    m_leUdpRemoteIp = new QLineEdit("192.168.1.100", grpUdp);
    m_leUdpRemotePort = new QLineEdit("8081", grpUdp);

    udpLayout->addWidget(new QLabel(QString::fromLocal8Bit("网络模式:")), 0, 0);
    udpLayout->addWidget(m_cmbUdpMode, 0, 1);
    udpLayout->addWidget(new QLabel(QString::fromLocal8Bit("本地监听端口:")), 1, 0);
    udpLayout->addWidget(m_leUdpLocalPort, 1, 1);
    udpLayout->addWidget(new QLabel(QString::fromLocal8Bit("远程目标 IP:")), 2, 0);
    udpLayout->addWidget(m_leUdpRemoteIp, 2, 1);
    udpLayout->addWidget(new QLabel(QString::fromLocal8Bit("远程目标端口:")), 3, 0);
    udpLayout->addWidget(m_leUdpRemotePort, 3, 1);
    srcLayout->addWidget(grpUdp);

    // 动态联动：切换单选框时，禁用/启用对应的配置面板
    connect(m_radioLocalFile, &QRadioButton::toggled, grpLocal, &QGroupBox::setEnabled);
    connect(m_radioUdpNetwork, &QRadioButton::toggled, grpUdp, &QGroupBox::setEnabled);
    grpUdp->setEnabled(false); // 初始状态禁用UDP面板

    rfmLayout->addWidget(grpSource);

    // --- 3.5 启停控制 ---
    m_btnStartDynamics = new QPushButton(QString::fromLocal8Bit("启动数据发送"), rfmPage);
    m_btnStartDynamics->setMinimumHeight(40);
    rfmLayout->addWidget(m_btnStartDynamics);
    rfmLayout->addStretch();

    // 假设已将 rfmPage 加入 configToolBox
    configToolBox->addItem(rfmPage, QString::fromLocal8Bit("3. RFM2g 动力学通信配置"));

    // ==========================================
    // 事件绑定与核心定时器初始化
    // ==========================================
    connect(btnSelectData, &QPushButton::clicked, this, [=]() {
        QString path = QFileDialog::getOpenFileName(this, QString::fromLocal8Bit("选择 J2000 数据文件"), "", "Text Files (*.txt *.csv *dat);;All Files (*)");
        if (!path.isEmpty()) m_leJ2000Path->setText(path);
        });

    // 实例化核心定时器，绑定到读取槽函数
    m_dynamicsTimer = new QTimer(this);
    // 设置定时器精度为毫秒级（对于低延迟仿真尤为关键）
    m_dynamicsTimer->setTimerType(Qt::PreciseTimer);
    connect(m_dynamicsTimer, &QTimer::timeout, this, &CommunicationSoftware::onDynamicsTimerTimeout);

    connect(m_btnStartDynamics, &QPushButton::clicked, this, &CommunicationSoftware::on_btnStartDynamics_clicked);


    // 2. 信号绑定 (假设已在 initThreads() 中实例化了 m_rfmWorker 和 m_rfmThread)
    connect(btnInitRfm, &QPushButton::clicked, this, [=]() {
        emit sigInitRfm(leRfmDevice->text());
        });

    connect(btnSendDynamics, &QPushButton::clicked, this, [=]() {
        // 模拟从本地文件或UDP缓冲提取的一帧动力学数据 (例如J2000坐标系下的速度位置等)
        QByteArray dummyData("Simulated Dynamics Data...");

        // 将界面上的十六进制字符串转为数字
        bool ok;
        quint32 txOffset = leRfmTxOffset->text().toUInt(&ok, 16);
        RFM2G_NODE targetNode = leRfmTargetNode->text().toUShort();

        emit sigSendRfmData(dummyData, txOffset, targetNode);
        });

    connect(btnWaitReturn, &QPushButton::clicked, this, [=]() {
        bool ok;
        quint32 rxOffset = leRfmRxOffset->text().toUInt(&ok, 16);
        quint32 rxLength = leRfmRxLength->text().toUInt();

        // 发起阻塞监听任务（在子线程中运行，设定超时 5000ms）
        emit sigWaitRfmReturn(rxOffset, rxLength, 5000);
        });



    // ==========================================
    // 右侧：图像显示与数据监控区
    // ==========================================
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    mainSplitter->setStretchFactor(1, 1);

    // -> 2.1 图像可视化区
    QGroupBox* grpImage = new QGroupBox(QString::fromLocal8Bit("图像可视化展示"), rightSplitter);
    QVBoxLayout* imgLayout = new QVBoxLayout(grpImage);

    // 使用成员变量实例化
    m_imageView = new QGraphicsView(grpImage);
    m_imageScene = new QGraphicsScene(this);
    m_imageView->setScene(m_imageScene);
    imgLayout->addWidget(m_imageView);

    // -> 2.2 数据与日志区
    QTabWidget* dataTabWidget = new QTabWidget(rightSplitter);

    // 使用成员变量实例化 TableWidget
    m_dataTable = new QTableWidget(0, 5, dataTabWidget);
    m_dataTable->setHorizontalHeaderLabels({ QString::fromLocal8Bit("时间"), QString::fromLocal8Bit("方向"),
        QString::fromLocal8Bit("接口"), QString::fromLocal8Bit("数据类型"),QString::fromLocal8Bit("解码内容 (Hex/解析)") });
    m_dataTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_dataTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    m_dataTable->setAlternatingRowColors(true);
    m_dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dataTabWidget->addTab(m_dataTable, QString::fromLocal8Bit("实时数据包解析监控"));

    // 使用成员变量实例化 TextBrowser
    m_logBrowser = new QTextBrowser(dataTabWidget);
    dataTabWidget->addTab(m_logBrowser, QString::fromLocal8Bit("系统运行日志"));

    rightSplitter->setStretchFactor(0, 6);
    rightSplitter->setStretchFactor(1, 4);

    // ==========================================
    // 3. 信号与槽的连接
    // ==========================================
    connect(btnInitLvds, &QPushButton::clicked, this, [=]() {
        emit sigInitLvds(m_leResource->text()); // 直接读取成员变量
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送初始化指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

    connect(btnSendImage, &QPushButton::clicked, this, [=]() {
        emit sigSendImage(m_leImagePath->text()); // 直接读取成员变量
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 准备加载图片并发送...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

    // [新增] 断开连接按钮绑定
    connect(btnDisconnect, &QPushButton::clicked, this, [=]() {
        emit sigCloseBoard();
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送断开连接指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

    // 发送图片按钮
    connect(btnSendImage, &QPushButton::clicked, this, [=]() {
        emit sigSendImage(m_leImagePath->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 准备加载图片并发送...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });
    // [新增] 绑定选择本地图像按钮
    connect(btnSelectImage, &QPushButton::clicked, this, [=]() {
        // 弹出文件选择对话框
        QString filePath = QFileDialog::getOpenFileName(
            this,
            QString::fromLocal8Bit("选择本地图像文件"), // 对话框标题
            "",                                        // 默认打开路径（为空则为当前目录）
            QString::fromLocal8Bit("图像文件 (*.bmp *.png *.jpg *.jpeg);;所有文件 (*.*)") // 文件类型过滤
        );

        // 如果用户选择了文件（没有点击取消）
        if (!filePath.isEmpty()) {
            // 将路径回填到输入框
            m_leImagePath->setText(filePath);

            // 在系统运行日志中记录一下
            if (m_logBrowser) {
                QString timeStr = QTime::currentTime().toString("HH:mm:ss");
                m_logBrowser->append(QString::fromLocal8Bit("[%1] 已选择图像: %2").arg(timeStr).arg(filePath));
            }
        }
        });

    // 绑定复位按钮
    connect(btnResetBoard, &QPushButton::clicked, this, [=]() {
        emit sigResetBoard();
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送独立复位指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

    // 绑定读取自检数据按钮
    connect(btnReadData, &QPushButton::clicked, this, [=]() {
        emit sigReadSelfTestData(leSavePath->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 请求读取自检接收数据...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });


    // ==========================================
    // [新增] CameraLink 信号与槽的连接
    // ==========================================

    // 选择输入图像按钮
    connect(btnSelectCmlkImage, &QPushButton::clicked, this, [=]() {
        QString filePath = QFileDialog::getOpenFileName(this, QString::fromLocal8Bit("选择CameraLink测试图像"), "", "(*.png *.bmp)");
        if (!filePath.isEmpty()) m_leCmlkImagePath->setText(filePath);
        });

    // 选择输出文件按钮
    connect(btnSelectCmlkSave, &QPushButton::clicked, this, [=]() {
        QString filePath = QFileDialog::getSaveFileName(this, QString::fromLocal8Bit("选择数据包保存位置"), m_leCmlkSavePath->text(), "(*.bin)");
        if (!filePath.isEmpty()) m_leCmlkSavePath->setText(filePath);
        });

    // 生成按钮：发射信号给子线程
    connect(btnPackAndSave, &QPushButton::clicked, this, [=]() {
        emit sigPackAndSaveCmlk(m_leCmlkImagePath->text(), m_leCmlkSavePath->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 正在启动 CameraLink 离线组包任务...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

}

// ==========================================
// 槽函数实现示例，可以直接操作界面的成员变量
// ==========================================
void CommunicationSoftware::onWorkerLogMessage(const QString& msg)
{
    if (m_logBrowser) {
        QString timeStr = QTime::currentTime().toString("HH:mm:ss.zzz");
        m_logBrowser->append(QString("[%1] %2").arg(timeStr).arg(msg));
    }
}
void CommunicationSoftware::onWorkerError(const QString& errorMsg)
{
    if (m_logBrowser) {
        QString timeStr = QTime::currentTime().toString("HH:mm:ss.zzz");
        m_logBrowser->append(QString("[%1] %2").arg(timeStr).arg(errorMsg));
    }
}
void CommunicationSoftware::onWorkerFinished(const QString& result)
{
    if (m_logBrowser) {
        QString timeStr = QTime::currentTime().toString("HH:mm:ss.zzz");
        m_logBrowser->append(QString("[%1] %2").arg(timeStr).arg(result));
    }
}
// [新增] 窗口关闭事件拦截
void CommunicationSoftware::closeEvent(QCloseEvent* event)
{
    if (m_logBrowser) {
        m_logBrowser->append(QString("[%1] 正在关闭软件，清理后台线程资源...").arg(QTime::currentTime().toString("HH:mm:ss")));
    }

    // 调用统一的清理函数
    cleanupThreads();

    // 接受关闭事件，允许窗口销毁
    event->accept();
}
// [新增] 统一的线程清理函数
void CommunicationSoftware::cleanupThreads()
{
    if (mLvdsThread) {
        // 1. 如果线程正在运行，先通知底层Worker执行关闭板卡操作
        if (mLvdsThread->isRunning()) {
            emit sigCloseBoard();

            // 2. 告诉线程准备退出事件循环
            mLvdsThread->quit();

            // 3. 阻塞主线程，等待子线程彻底结束（防止资源泄漏或野指针崩溃）
            // 设置 3000ms 超时，防止由于硬件卡死导致软件无法关闭
            if (!mLvdsThread->wait(3000)) {
                mLvdsThread->terminate(); // 极端情况下的强制终止
                mLvdsThread->wait();
            }
        }

        // 4. 清理线程对象内存
        delete mLvdsThread;
        mLvdsThread = nullptr;
        // 注意：mLvdsWorker 会由于 QThread::finished 信号绑定了 deleteLater 而自动释放，无需手动 delete
        mLvdsWorker = nullptr;
    }

    // [新增] 清理 CMLK 线程
    if (m_cmlkThread) {
        if (m_cmlkThread->isRunning()) {
            m_cmlkThread->quit();
            if (!m_cmlkThread->wait(3000)) {
                m_cmlkThread->terminate();
                m_cmlkThread->wait();
            }
        }
        delete m_cmlkThread;
        m_cmlkThread = nullptr;
        m_cmlkWorker = nullptr;
    }
    // [新增] 清理 GNC 线程
    if (m_rfmThread) {
        if (m_rfmThread->isRunning()) {
            m_rfmThread->quit();
            if (!m_rfmThread->wait(3000)) {
                m_rfmThread->terminate();
                m_rfmThread->wait();
            }
        }
        delete m_rfmThread;
        m_rfmThread = nullptr;
        m_rfmWorker = nullptr;
    }

}

void CommunicationSoftware::on_btnStartDynamics_clicked()
{
    // 如果当前正在运行，则执行“停止”逻辑
    if (m_dynamicsTimer->isActive()) {
        m_dynamicsTimer->stop();
        if (m_dynamicsFile.is_open()) {
            m_dynamicsFile.close();
        }
        m_btnStartDynamics->setText(QString::fromLocal8Bit("启动数据发送"));
        m_logBrowser->append(QString::fromLocal8Bit("已手动停止动力学数据发送。"));

        // 如果接入了UDP，这里可以抛出关闭 UDP 监听的信号
        // emit sigStopUdpListen();
        return;
    }

    // 执行“启动”逻辑
    if (m_radioLocalFile->isChecked()) {
        // --- 本地文件回放模式 ---
        std::string filePath = m_leJ2000Path->text().toLocal8Bit().constData();
        m_dynamicsFile.open(filePath, std::ios::in);

        if (!m_dynamicsFile.is_open()) {
            onWorkerError(QString::fromLocal8Bit("无法打开动力学数据文件：") + m_leJ2000Path->text());
            return;
        }

        m_currentLineNumber = 0;
        int interval = m_spinIntervalMs->value();

        m_dynamicsTimer->start(interval); // 启动定时器
        m_btnStartDynamics->setText(QString::fromLocal8Bit("停止数据发送"));
        m_logBrowser->append(QString::fromLocal8Bit("开始读取本地文件，发送步长: %1 ms").arg(interval));

    }
    else {
        // --- UDP 网络模式 ---
        m_btnStartDynamics->setText(QString::fromLocal8Bit("停止 UDP 转发"));
        m_logBrowser->append(QString::fromLocal8Bit("已启动 UDP 监听端口: ") + m_leUdpLocalPort->text() + QString::fromLocal8Bit("，准备向光反直接转发。"));

        // 预留触发 UDP Worker 的信号
        // emit sigStartUdpListen(m_leUdpLocalPort->text().toUShort());
    }
}

// 定时器心跳槽函数：每次执行只读一行，立刻交出 CPU 控制权给 UI 事件循环
void CommunicationSoftware::onDynamicsTimerTimeout()
{
    std::string dataLine;

    // 如果读到文件末尾
    if (!std::getline(m_dynamicsFile, dataLine)) {
        on_btnStartDynamics_clicked(); // 复用按钮槽函数执行停止清理
        m_logBrowser->append(QString::fromLocal8Bit("文件读取完毕，发送结束。"));
        return;
    }

    m_currentLineNumber++;
    const int DATA_SIZE = 133;

    // 兼容空格、制表符和逗号分隔
    std::replace(dataLine.begin(), dataLine.end(), ',', ' ');

    std::array<double, 133> vecDouble{};
    std::istringstream input(dataLine);

    int valueCount = 0;
    double value = 0.0;

    while (valueCount < DATA_SIZE && input >> value) {
        vecDouble[valueCount] = value;
        ++valueCount;
    }

    if (valueCount != DATA_SIZE) {
        onWorkerError(QString::fromLocal8Bit("第 %1 行数据列数错误：实际 %2 列，应为 %3 列，已跳过。")
            .arg(m_currentLineNumber).arg(valueCount).arg(DATA_SIZE));
        return;
    }

    // 序列化并通过信号跨线程丢给底层光反卡发送
    QByteArray rawData(reinterpret_cast<const char*>(vecDouble.data()), DATA_SIZE * sizeof(double));

    // 注意：假设界面的光反 Offset 等参数存储在对应的类成员中，这里硬编码为您示例
    quint32 baseOffset = 0x1000;
    RFM2G_NODE targetNode = 1;

    emit sigSendRfmData(rawData, baseOffset, targetNode);

    // 可选：如果要避免 UI 刷屏太快导致卡顿，可以做降采样显示日志（每发送 100 行打印一次）
    if (m_currentLineNumber % 100 == 0) {
        m_logBrowser->append(QString::fromLocal8Bit("已发送 %1 行动力学数据...").arg(m_currentLineNumber));
    }
}