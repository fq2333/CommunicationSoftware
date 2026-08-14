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
#include <QThread>
#include "LvdsWorker.h"
#include "CameraLinkWorker.h" 
#include "Rfm2gWorker.h"
#include <sstream>

CommunicationSoftware::CommunicationSoftware(QWidget* parent)
    : QMainWindow(parent),
    mLvdsThread(nullptr), mLvdsWorker(nullptr),
    m_leResource(nullptr), m_leImagePath(nullptr),
    m_imageView(nullptr), m_imageScene(nullptr),
    m_dataTable(nullptr), m_logBrowser(nullptr),
    m_cmlkThread(nullptr), m_cmlkWorker(nullptr),
    m_rfmTxThread(nullptr), m_rfmTxWorker(nullptr),
    m_rfmRxThread(nullptr), m_rfmRxWorker(nullptr)
{
    ui.setupUi(this);
    // 【新增】：初始化 UDP 套接字，挂载到当前主窗口树下，退出时自动销毁
    m_udpSocket = new QUdpSocket(this);
    initUI();
    initThreads();
}

CommunicationSoftware::~CommunicationSoftware()
{
    cleanupThreads();
}

void CommunicationSoftware::initThreads()
{
    qRegisterMetaType<RFM2G_NODE>("RFM2G_NODE");

    // --- LVDS 线程 ---
    mLvdsThread = new QThread(this);
    mLvdsWorker = new LvdsWorker();
    mLvdsWorker->moveToThread(mLvdsThread);
    connect(this, &CommunicationSoftware::sigInitLvds, mLvdsWorker, &LvdsWorker::initializeBoard);
    connect(this, &CommunicationSoftware::sigSendImage, mLvdsWorker, &LvdsWorker::sendLocalImage);
    connect(this, &CommunicationSoftware::sigCloseBoard, mLvdsWorker, &LvdsWorker::closeBoard);
    connect(this, &CommunicationSoftware::sigResetBoard, mLvdsWorker, &LvdsWorker::resetBoard);
    connect(this, &CommunicationSoftware::sigReadSelfTestData, mLvdsWorker, &LvdsWorker::readSelfTestData);
    connect(mLvdsWorker, &LvdsWorker::logMessage, this, &CommunicationSoftware::onWorkerLogMessage);
    connect(mLvdsWorker, &LvdsWorker::errorOccurred, this, &CommunicationSoftware::onWorkerError);
    connect(mLvdsWorker, &LvdsWorker::operationCompleted, this, &CommunicationSoftware::onWorkerFinished);
    connect(mLvdsThread, &QThread::finished, mLvdsWorker, &QObject::deleteLater);
    mLvdsThread->start();

    // --- CameraLink 线程 ---
    m_cmlkThread = new QThread(this);
    m_cmlkWorker = new CameraLinkWorker();
    m_cmlkWorker->moveToThread(m_cmlkThread);

    // [新增] 硬件控制信号绑定
    connect(this, &CommunicationSoftware::sigInitCmlk, m_cmlkWorker, &CameraLinkWorker::initializeBoard);
    connect(this, &CommunicationSoftware::sigResetCmlk, m_cmlkWorker, &CameraLinkWorker::resetBoard);
    connect(this, &CommunicationSoftware::sigCloseCmlk, m_cmlkWorker, &CameraLinkWorker::closeBoard);
    connect(this, &CommunicationSoftware::sigSendCmlkImage, m_cmlkWorker, &CameraLinkWorker::sendLocalImage);
    connect(this, &CommunicationSoftware::sigPackAndSaveCmlk, m_cmlkWorker, &CameraLinkWorker::packAndSaveOffline);
    connect(m_cmlkWorker, &CameraLinkWorker::logMessage, this, &CommunicationSoftware::onWorkerLogMessage);
    connect(m_cmlkWorker, &CameraLinkWorker::errorOccurred, this, &CommunicationSoftware::onWorkerError);
    connect(m_cmlkWorker, &CameraLinkWorker::operationCompleted, this, &CommunicationSoftware::onWorkerFinished);
    connect(m_cmlkThread, &QThread::finished, m_cmlkWorker, &QObject::deleteLater);
    m_cmlkThread->start();

    // --- RFM2g 线程 ---
    // ==========================================
    // 1. 初始化 TX (发送) 核心线程
    // ==========================================
    m_rfmTxThread = new QThread(this);
    m_rfmTxWorker = new Rfm2gWorker();
    m_rfmTxWorker->moveToThread(m_rfmTxThread);

    connect(this, &CommunicationSoftware::sigInitRfm, m_rfmTxWorker, &Rfm2gWorker::initBoard);
    connect(this, &CommunicationSoftware::sigSendRfmData, m_rfmTxWorker, &Rfm2gWorker::sendDynamicsData);
    connect(this, &CommunicationSoftware::sigSendAndWaitRfm, m_rfmTxWorker, &Rfm2gWorker::sendAndWaitDynamics);

    connect(m_rfmTxWorker, &Rfm2gWorker::logMessage, this, &CommunicationSoftware::onWorkerLogMessage);
    connect(m_rfmTxWorker, &Rfm2gWorker::errorOccurred, this, &CommunicationSoftware::onWorkerError);
    connect(m_rfmTxWorker, &Rfm2gWorker::simulationStepFinished, this, &CommunicationSoftware::onSimulationStepFinished);

    // 闭环模式下，TX 核心收到的图像依然需要路由
    //connect(m_rfmTxWorker, &Rfm2gWorker::parsedImageReceived, mLvdsWorker, &LvdsWorker::sendImageFromMemory);
    connect(m_rfmTxWorker, &Rfm2gWorker::parsedImageReceived, this, &CommunicationSoftware::routeImageToVideo);

    // ==========================================
    // 2. 初始化 RX (接收) 核心线程
    // ==========================================
    m_rfmRxThread = new QThread(this);
    m_rfmRxWorker = new Rfm2gWorker();
    m_rfmRxWorker->moveToThread(m_rfmRxThread);

    // RX 同样需要初始化板卡
    connect(this, &CommunicationSoftware::sigInitRfm, m_rfmRxWorker, &Rfm2gWorker::initBoard);
    // 绑定开环监听启停信号
    connect(this, &CommunicationSoftware::sigStartRxListen, m_rfmRxWorker, &Rfm2gWorker::startContinuousListen);
    connect(this, &CommunicationSoftware::sigStopRxListen, m_rfmRxWorker, &Rfm2gWorker::stopContinuousListen);

    connect(m_rfmRxWorker, &Rfm2gWorker::logMessage, this, &CommunicationSoftware::onWorkerLogMessage);
    connect(m_rfmRxWorker, &Rfm2gWorker::errorOccurred, this, &CommunicationSoftware::onWorkerError);

    // 开环模式下，RX 核心截获的图像进行路由分发
    //connect(m_rfmRxWorker, &Rfm2gWorker::parsedImageReceived, mLvdsWorker, &LvdsWorker::sendImageFromMemory);
    connect(m_rfmRxWorker, &Rfm2gWorker::parsedImageReceived, this, &CommunicationSoftware::routeImageToVideo);
    // ==========================================
   // ==========================================
    // 3. 将中枢路由信号接到具体的板卡 Worker 上
    // ==========================================
    connect(this, &CommunicationSoftware::sigSendToLvdsMem, mLvdsWorker, &LvdsWorker::sendImageFromMemory);

    // 【新增】：将路由中心的 CameraLink 分发信号，准确接到刚才声明的槽函数上
    connect(this, &CommunicationSoftware::sigSendToCmlkMem, m_cmlkWorker, &CameraLinkWorker::sendImageFromMemory);
    // 内存清理与启动
    connect(m_rfmTxThread, &QThread::finished, m_rfmTxWorker, &QObject::deleteLater);
    connect(m_rfmRxThread, &QThread::finished, m_rfmRxWorker, &QObject::deleteLater);
    m_rfmTxThread->start();
    m_rfmRxThread->start();
}

void CommunicationSoftware::initUI()
{
    QSettings settings("MyCompany", "GNC_Simulation_Config");

    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);

    QToolBox* configToolBox = new QToolBox(mainSplitter);
    configToolBox->setMinimumWidth(400);
    configToolBox->setMaximumWidth(500);

    // ==========================================
    // 1. LVDS 接口配置页
    // ==========================================
    QWidget* lvdsPage = new QWidget();
    QVBoxLayout* lvdsLayout = new QVBoxLayout(lvdsPage);

    m_leResource = new QLineEdit("10.109.3.100::PXI29::12::INSTR", lvdsPage);
    QPushButton* btnInitLvds = new QPushButton(QString::fromLocal8Bit("初始化LVDS板卡"), lvdsPage);
    QPushButton* btnDisconnect = new QPushButton(QString::fromLocal8Bit("断开连接"), lvdsPage);
    m_leImagePath = new QLineEdit("C:/test_image.bmp", lvdsPage);
    QPushButton* btnSelectImage = new QPushButton(QString::fromLocal8Bit("选择本地图像..."), lvdsPage);
    QPushButton* btnSendImage = new QPushButton(QString::fromLocal8Bit("写入板卡并发送"), lvdsPage);
    QPushButton* btnResetBoard = new QPushButton(QString::fromLocal8Bit("复位板卡"), lvdsPage);
    QLineEdit* leSavePath = new QLineEdit(QString::fromLocal8Bit("rx_selftest_data.bin"), lvdsPage);
    QPushButton* btnReadData = new QPushButton(QString::fromLocal8Bit("读取自检接收数据"), lvdsPage);

    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("设备资源名:")));
    lvdsLayout->addWidget(m_leResource);
    lvdsLayout->addWidget(btnInitLvds);
    lvdsLayout->addWidget(btnResetBoard);
    lvdsLayout->addWidget(btnDisconnect);
    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("图像文件路径:")));
    lvdsLayout->addWidget(m_leImagePath);
    lvdsLayout->addWidget(btnSelectImage);
    lvdsLayout->addWidget(btnSendImage);
    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("接收数据保存路径:")));
    lvdsLayout->addWidget(leSavePath);
    lvdsLayout->addWidget(btnReadData);
    lvdsLayout->addStretch();
    configToolBox->addItem(lvdsPage, QString::fromLocal8Bit("1. LVDS接口配置"));

    // ==========================================
    // 2. CameraLink 接口配置页
    // ==========================================
    QWidget* cmlkPage = new QWidget();
    QVBoxLayout* cmlkLayout = new QVBoxLayout(cmlkPage);

    // 【修改这行代码】：利用 settings 读取上一次的配置，如果没读到，默认给 "PXI1::0::0::INSTR"
    m_leCmlkResource = new QLineEdit(settings.value("CmlkResource", "PXI1::0::0::INSTR").toString(), cmlkPage);

    QPushButton* btnInitCmlk = new QPushButton(QString::fromLocal8Bit("初始化 CameraLink 板卡"), cmlkPage);
    QPushButton* btnResetCmlk = new QPushButton(QString::fromLocal8Bit("复位板卡"), cmlkPage);
    QPushButton* btnDisconnectCmlk = new QPushButton(QString::fromLocal8Bit("断开连接"), cmlkPage);

    m_leCmlkImagePath = new QLineEdit(QString::fromLocal8Bit("C:/test_image_12bit.png"), cmlkPage);
    QPushButton* btnSelectCmlkImage = new QPushButton(QString::fromLocal8Bit("选择本地图像..."), cmlkPage);
    QPushButton* btnSendCmlkImage = new QPushButton(QString::fromLocal8Bit("写入 CameraLink 板卡并发送"), cmlkPage); // [新增]

    QString defaultBinPath = QCoreApplication::applicationDirPath() + "/cmlk_sim_packet.bin";
    m_leCmlkSavePath = new QLineEdit(defaultBinPath, cmlkPage);
    QPushButton* btnSelectCmlkSave = new QPushButton(QString::fromLocal8Bit("选择保存位置..."), cmlkPage);
    // 【修改这行】：更新按钮的文字提示
    QPushButton* btnPackAndSave = new QPushButton(QString::fromLocal8Bit("生成 CameraLink 数据包 (.bin 与 .raw)"), cmlkPage);

    // 拼装 UI
    cmlkLayout->addWidget(new QLabel(QString::fromLocal8Bit("设备资源名:")));
    cmlkLayout->addWidget(m_leCmlkResource);
    cmlkLayout->addWidget(btnInitCmlk);
    cmlkLayout->addWidget(btnResetCmlk);
    cmlkLayout->addWidget(btnDisconnectCmlk);

    cmlkLayout->addWidget(new QLabel(QString::fromLocal8Bit("输入图像文件路径 (4096x4096):")));
    cmlkLayout->addWidget(m_leCmlkImagePath);
    cmlkLayout->addWidget(btnSelectCmlkImage);
    cmlkLayout->addWidget(btnSendCmlkImage);

    cmlkLayout->addWidget(new QLabel(QString::fromLocal8Bit("离线生成 .bin 保存路径:")));
    cmlkLayout->addWidget(m_leCmlkSavePath);
    cmlkLayout->addWidget(btnSelectCmlkSave);
    cmlkLayout->addWidget(btnPackAndSave);

    cmlkLayout->addStretch();
    configToolBox->addItem(cmlkPage, QString::fromLocal8Bit("2. CameraLink 接口配置"));

    // ==========================================
    // [新增] 绑定 CameraLink 的硬件控制事件
    // ==========================================
    connect(btnInitCmlk, &QPushButton::clicked, this, [=]() {
        emit sigInitCmlk(m_leCmlkResource->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送 CameraLink 初始化指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

    connect(btnResetCmlk, &QPushButton::clicked, this, [=]() {
        emit sigResetCmlk();
        });

    connect(btnDisconnectCmlk, &QPushButton::clicked, this, [=]() {
        emit sigCloseCmlk();
        });

    connect(btnSendCmlkImage, &QPushButton::clicked, this, [=]() {
        emit sigSendCmlkImage(m_leCmlkImagePath->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 准备加载 CameraLink 图像并投递至硬件...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });
    connect(btnSelectCmlkImage, &QPushButton::clicked, this, [=]() {
        QString filePath = QFileDialog::getOpenFileName(this, QString::fromLocal8Bit("选择CameraLink测试图像"), "", "(*.png *.bmp)");
        if (!filePath.isEmpty()) m_leCmlkImagePath->setText(filePath);
        });
    // 【修改这行】：让弹出的文件保存对话框同时支持提示这两种格式
    connect(btnSelectCmlkSave, &QPushButton::clicked, this, [=]() {
        QString filePath = QFileDialog::getSaveFileName(this,
            QString::fromLocal8Bit("选择数据包保存位置 (将自动生成同名双文件)"),
            m_leCmlkSavePath->text(),
            "Data Files (*.bin *.raw)");

        if (!filePath.isEmpty()) m_leCmlkSavePath->setText(filePath);
        });
    connect(btnPackAndSave, &QPushButton::clicked, this, [=]() {
        emit sigPackAndSaveCmlk(m_leCmlkImagePath->text(), m_leCmlkSavePath->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 正在启动 CameraLink 离线组包任务...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

    // ==========================================
    // 3. 动力学数据流与 RFM2g 配置页
    // ==========================================
    QWidget* rfmPage = new QWidget();
    QVBoxLayout* rfmLayout = new QVBoxLayout(rfmPage);

    QLineEdit* leRfmDevice = new QLineEdit("\\\\.\\rfm2g1", rfmPage);
    QPushButton* btnInitRfm = new QPushButton(QString::fromLocal8Bit("初始化 RFM 光纤网卡"), rfmPage);

    m_leRfmTargetNode = new QLineEdit(settings.value("TargetNode", "231").toString(), rfmPage);
    m_leRfmTxOffset = new QLineEdit(settings.value("TxOffset", "0x1000").toString(), rfmPage);
    m_leRfmRxOffset = new QLineEdit(settings.value("RxOffset", "0x2000").toString(), rfmPage);

    rfmLayout->addWidget(new QLabel(QString::fromLocal8Bit("设备路径 (Device Path):")));
    rfmLayout->addWidget(leRfmDevice);
    rfmLayout->addWidget(btnInitRfm);
    rfmLayout->addWidget(new QLabel(QString::fromLocal8Bit("目标节点ID与发送Offset:")));
    rfmLayout->addWidget(m_leRfmTargetNode);
    rfmLayout->addWidget(m_leRfmTxOffset);
    rfmLayout->addWidget(new QLabel(QString::fromLocal8Bit("接收数据(图像头)Offset:")));
    rfmLayout->addWidget(m_leRfmRxOffset);

    // ...[在 initUI 中找到配置路由的地方，替换为以下代码] ...
    QGroupBox * grpRoute = new QGroupBox(QString::fromLocal8Bit("数据转发路由与相机绑定"), rfmPage);
    QGridLayout* routeLayout = new QGridLayout(grpRoute);

    // 1. 实例化复选框
    m_chkRouteLvds = new QCheckBox(QString::fromLocal8Bit("LVDS 接口"), grpRoute);
    m_chkRouteCmlk = new QCheckBox(QString::fromLocal8Bit("CameraLink"), grpRoute);
    m_chkRoute2711 = new QCheckBox(QString::fromLocal8Bit("2711 接口"), grpRoute);
    m_chkRouteRS422 = new QCheckBox(QString::fromLocal8Bit("RS422 接口"), grpRoute);

    // 2. 实例化对应的相机 ID 数字调节框 (假设相机 ID 范围为 1~16)
    m_spinLvdsCamId = new QSpinBox(grpRoute);  m_spinLvdsCamId->setRange(1, 16); m_spinLvdsCamId->setPrefix(QString::fromLocal8Bit("绑定相机: "));
    m_spinCmlkCamId = new QSpinBox(grpRoute);  m_spinCmlkCamId->setRange(1, 16); m_spinCmlkCamId->setPrefix(QString::fromLocal8Bit("绑定相机: "));
    m_spin2711CamId = new QSpinBox(grpRoute);  m_spin2711CamId->setRange(1, 16); m_spin2711CamId->setPrefix(QString::fromLocal8Bit("绑定相机: "));
    m_spinRS422CamId = new QSpinBox(grpRoute); m_spinRS422CamId->setRange(1, 16); m_spinRS422CamId->setPrefix(QString::fromLocal8Bit("绑定相机: "));

    // 3. 从配置文件恢复勾选状态和绑定的相机 ID
    m_chkRouteLvds->setChecked(settings.value("RouteLvds", true).toBool());
    m_spinLvdsCamId->setValue(settings.value("LvdsCamId", 1).toInt()); // 默认绑相机1

    m_chkRouteCmlk->setChecked(settings.value("RouteCmlk", false).toBool());
    m_spinCmlkCamId->setValue(settings.value("CmlkCamId", 2).toInt()); // 默认绑相机2

    m_chkRoute2711->setChecked(settings.value("Route2711", false).toBool());
    m_spin2711CamId->setValue(settings.value("2711CamId", 3).toInt()); // 默认绑相机3

    m_chkRouteRS422->setChecked(settings.value("RouteRS422", false).toBool());
    m_spinRS422CamId->setValue(settings.value("RS422CamId", 4).toInt()); // 默认绑相机4

    // 4. 将控件整齐地放入网格中
    routeLayout->addWidget(m_chkRouteLvds, 0, 0);
    routeLayout->addWidget(m_spinLvdsCamId, 0, 1);
    routeLayout->addWidget(m_chkRouteCmlk, 0, 2);
    routeLayout->addWidget(m_spinCmlkCamId, 0, 3);
    routeLayout->addWidget(m_chkRoute2711, 1, 0);
    routeLayout->addWidget(m_spin2711CamId, 1, 1);
    routeLayout->addWidget(m_chkRouteRS422, 1, 2);
    routeLayout->addWidget(m_spinRS422CamId, 1, 3);

    rfmLayout->addWidget(grpRoute);

    QGroupBox* grpSource = new QGroupBox(QString::fromLocal8Bit("动力学数据源配置"), rfmPage);
    QVBoxLayout* srcLayout = new QVBoxLayout(grpSource);
    m_radioLocalFile = new QRadioButton(QString::fromLocal8Bit("本地离线 J2000 数据回放"), grpSource);
    m_radioUdpNetwork = new QRadioButton(QString::fromLocal8Bit("外部 UDP 实时数据接入 (转发)"), grpSource);
    m_radioLocalFile->setChecked(true);

    QButtonGroup* srcGroup = new QButtonGroup(this);
    srcGroup->addButton(m_radioLocalFile);
    srcGroup->addButton(m_radioUdpNetwork);
    srcLayout->addWidget(m_radioLocalFile);
    srcLayout->addWidget(m_radioUdpNetwork);

    QGroupBox* grpLocal = new QGroupBox(QString::fromLocal8Bit("本地回放参数"), grpSource);
    QGridLayout* localLayout = new QGridLayout(grpLocal);
    m_leJ2000Path = new QLineEdit(settings.value("J2000Path", "C:/J2000_Data.txt").toString(), grpLocal);
    QPushButton* btnSelectData = new QPushButton(QString::fromLocal8Bit("浏览..."), grpLocal);
    m_spinIntervalMs = new QSpinBox(grpLocal);
    m_spinIntervalMs->setRange(1, 5000);
    m_spinIntervalMs->setValue(settings.value("TimerInterval", 10).toInt());
    m_spinIntervalMs->setSuffix(" ms");

    localLayout->addWidget(new QLabel(QString::fromLocal8Bit("数据文件路径:")), 0, 0);
    localLayout->addWidget(m_leJ2000Path, 0, 1);
    localLayout->addWidget(btnSelectData, 0, 2);
    localLayout->addWidget(new QLabel(QString::fromLocal8Bit("逐行发送间隔:")), 1, 0);
    localLayout->addWidget(m_spinIntervalMs, 1, 1);
    srcLayout->addWidget(grpLocal);

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

    connect(m_radioLocalFile, &QRadioButton::toggled, grpLocal, &QGroupBox::setEnabled);
    connect(m_radioUdpNetwork, &QRadioButton::toggled, grpUdp, &QGroupBox::setEnabled);
    grpUdp->setEnabled(false);

    rfmLayout->addWidget(grpSource);

    // 将上面那段替换为以下代码：
    m_chkWaitAck = new QCheckBox(QString::fromLocal8Bit("闭环同步 (需等待图像回传)"), rfmPage);
    m_chkWaitAck->setChecked(false); // 默认取消勾选（开环测试模式）

    m_btnStartDynamics = new QPushButton(QString::fromLocal8Bit("启动数据流"), rfmPage);
    m_btnStartDynamics->setMinimumHeight(40);

    QHBoxLayout* startLayout = new QHBoxLayout();
    startLayout->addWidget(m_chkWaitAck);
    startLayout->addWidget(m_btnStartDynamics);

    rfmLayout->addLayout(startLayout);
    rfmLayout->addStretch();
    configToolBox->addItem(rfmPage, QString::fromLocal8Bit("3. RFM2g 动力学通信配置"));

    // ==========================================
    // 事件绑定与核心定时器初始化
    // ==========================================
    connect(btnSelectData, &QPushButton::clicked, this, [=]() {
        QString path = QFileDialog::getOpenFileName(this, QString::fromLocal8Bit("选择 J2000 数据文件"), "", "Text Files (*.txt *.csv *.dat);;All Files (*)");
        if (!path.isEmpty()) m_leJ2000Path->setText(path);
        });

    m_dynamicsTimer = new QTimer(this);
    m_dynamicsTimer->setTimerType(Qt::PreciseTimer);
    connect(m_dynamicsTimer, &QTimer::timeout, this, &CommunicationSoftware::onDynamicsTimerTimeout);
    // ==========================================
    // 【新增】实现动态修改定时器发送间隔
    // ==========================================
    // 注意：因为 QSpinBox 的 valueChanged 有 int 和 QString 两个重载，必须使用 QOverload 明确指定
    connect(m_spinIntervalMs, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int newValue) {
        // 只有在定时器正在运行（仿真进行中）时，才需要热更新
        if (m_dynamicsTimer && m_dynamicsTimer->isActive()) {
            m_dynamicsTimer->setInterval(newValue);

            //// 可选：在日志中打印一条提示，让您确信修改已生效
            //if (m_logBrowser) {
            //    m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送步长已动态调整为: %2 ms")
            //        .arg(QTime::currentTime().toString("HH:mm:ss"))
            //        .arg(newValue));
            //}
        }
        });
    connect(m_btnStartDynamics, &QPushButton::clicked, this, &CommunicationSoftware::handleStartDynamicsClicked);

    // 【替换为以下代码】：
    connect(btnInitRfm, &QPushButton::clicked, this, [=]() {
        // 发送初始化板卡的信号
        emit sigInitRfm(leRfmDevice->text());

        bool ok;
        quint32 txOffset = m_leRfmTxOffset->text().toUInt(&ok, 16);
        quint32 rxOffset = m_leRfmRxOffset->text().toUInt(&ok, 16);

        // 延迟 500 毫秒执行清零（确保板卡确实已经 Open 完毕）
        QTimer::singleShot(500, this, [=]() {
            // 利用阻塞同步调用，安全跨线程清空 Tx (动力学 2KB 空间) 和 Rx (图像头 2KB 空间)
            if (m_rfmTxThread && m_rfmTxThread->isRunning()) {
                QMetaObject::invokeMethod(m_rfmTxWorker, "clearMemoryRegion", Qt::BlockingQueuedConnection,
                    Q_ARG(quint32, txOffset), Q_ARG(quint32, 2048));

                QMetaObject::invokeMethod(m_rfmTxWorker, "clearMemoryRegion", Qt::BlockingQueuedConnection,
                    Q_ARG(quint32, rxOffset), Q_ARG(quint32, 2048));
            }
            });
        });

    // ==========================================
    // 右侧：图像显示与数据监控区 (代码保留不变)
    // ==========================================
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    mainSplitter->setStretchFactor(1, 1);

    // -> 2.1 图像可视化区
    QGroupBox* grpImage = new QGroupBox(QString::fromLocal8Bit("图像可视化展示"), rightSplitter);
    QVBoxLayout* imgLayout = new QVBoxLayout(grpImage);
    // ==========================================
    // [新增] 相机监视切换控制栏
    // ==========================================
    QHBoxLayout* camSelectLayout = new QHBoxLayout();
    camSelectLayout->addWidget(new QLabel(QString::fromLocal8Bit("当前监视相机:")));

    m_cmbCameraSelect = new QComboBox(grpImage);
    // 假设您最多支持 4 个相机并发，您可以根据实际情况增减
    m_cmbCameraSelect->addItems({
        QString::fromLocal8Bit("相机 1"),
        QString::fromLocal8Bit("相机 2"),
        QString::fromLocal8Bit("相机 3"),
        QString::fromLocal8Bit("相机 4")
        });
    // 默认选中第一个 (索引为 0，即相机 1)
    m_cmbCameraSelect->setCurrentIndex(0);

    camSelectLayout->addWidget(m_cmbCameraSelect);
    camSelectLayout->addStretch(); // 把控件推到左边

    imgLayout->addLayout(camSelectLayout);
    // 实例化视图和场景
    m_imageView = new QGraphicsView(grpImage);
    m_imageScene = new QGraphicsScene(this);
    m_imageView->setScene(m_imageScene);

    // ==========================================
    // [新增] 预先创建 Pixmap 图元并添加到场景中
    // ==========================================
    m_pixmapItem = new QGraphicsPixmapItem();
    m_imageScene->addItem(m_pixmapItem);

    // 隐藏滚动条并设置黑色背景（更契合图像显示习惯）
    m_imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setBackgroundBrush(Qt::black);

    imgLayout->addWidget(m_imageView);

    // -> 2.2 数据与日志区
    QWidget* rightWidget = new QWidget(rightSplitter);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // 进制显示切换控制栏
    QHBoxLayout* fmtLayout = new QHBoxLayout();
    fmtLayout->addWidget(new QLabel(QString::fromLocal8Bit("矩阵显示格式:")));

    // 修改按钮文字，使其含义更精确
    m_radioHex = new QRadioButton(QString::fromLocal8Bit("Hex (单字节)"));
    m_radioDec = new QRadioButton(QString::fromLocal8Bit("Dec (单字节)"));
    m_radioDouble = new QRadioButton(QString::fromLocal8Bit("Double (还原浮点)")); // [新增]

    m_radioHex->setChecked(true);

    fmtLayout->addWidget(m_radioHex);
    fmtLayout->addWidget(m_radioDec);
    fmtLayout->addWidget(m_radioDouble); // [新增]
    fmtLayout->addStretch();
    rightLayout->addLayout(fmtLayout);

    QTabWidget* dataTabWidget = new QTabWidget(rightWidget);
    rightLayout->addWidget(dataTabWidget);

    // ==========================================
    // [修改] 动力学 Tx 矩阵监控
    // ==========================================
    m_dataTable = new QTableWidget(dataTabWidget);
    initMatrixTable(m_dataTable, 100); // 预分配 100 行 x 16 列 = 1600 字节的展示空间
    dataTabWidget->addTab(m_dataTable, QString::fromLocal8Bit("动力学 Tx 数据矩阵"));

    // ==========================================
    // [修改] 图像 Rx 矩阵监控
    // ==========================================
    m_rxImageTable = new QTableWidget(dataTabWidget);
    initMatrixTable(m_rxImageTable, 100); // 同样预分配
    dataTabWidget->addTab(m_rxImageTable, QString::fromLocal8Bit("回传图像 Rx 数据矩阵"));

    m_logBrowser = new QTextBrowser(dataTabWidget);
    dataTabWidget->addTab(m_logBrowser, QString::fromLocal8Bit("系统运行日志"));

    rightSplitter->setStretchFactor(0, 6);
    rightSplitter->setStretchFactor(1, 4);

    // [新增] 记得把新按钮也绑定到刷新槽函数上
    connect(m_radioHex, &QRadioButton::toggled, this, &CommunicationSoftware::onFormatToggled);
    connect(m_radioDec, &QRadioButton::toggled, this, &CommunicationSoftware::onFormatToggled);
    connect(m_radioDouble, &QRadioButton::toggled, this, &CommunicationSoftware::onFormatToggled);

    // 其余零散按钮事件绑定
    connect(btnInitLvds, &QPushButton::clicked, this, [=]() {
        emit sigInitLvds(m_leResource->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送初始化指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });
    connect(btnSendImage, &QPushButton::clicked, this, [=]() {
        emit sigSendImage(m_leImagePath->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 准备加载图片并发送...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });
    connect(btnDisconnect, &QPushButton::clicked, this, [=]() {
        emit sigCloseBoard();
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送断开连接指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });
    connect(btnSelectImage, &QPushButton::clicked, this, [=]() {
        QString filePath = QFileDialog::getOpenFileName(this, QString::fromLocal8Bit("选择本地图像文件"), "", QString::fromLocal8Bit("图像文件 (*.bmp *.png *.jpg *.jpeg);;所有文件 (*.*)"));
        if (!filePath.isEmpty()) {
            m_leImagePath->setText(filePath);
            if (m_logBrowser) m_logBrowser->append(QString::fromLocal8Bit("[%1] 已选择图像: %2").arg(QTime::currentTime().toString("HH:mm:ss")).arg(filePath));
        }
        });
    connect(btnResetBoard, &QPushButton::clicked, this, [=]() {
        emit sigResetBoard();
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 发送独立复位指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });
    connect(btnReadData, &QPushButton::clicked, this, [=]() {
        emit sigReadSelfTestData(leSavePath->text());
        m_logBrowser->append(QString::fromLocal8Bit("[%1] 请求读取自检接收数据...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });
    
}

void CommunicationSoftware::onWorkerLogMessage(const QString& msg)
{
    if (m_logBrowser) m_logBrowser->append(QString("[%1] %2").arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(msg));
}
void CommunicationSoftware::onWorkerError(const QString& errorMsg)
{
    if (m_logBrowser) m_logBrowser->append(QString("[%1] %2").arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(errorMsg));
}
void CommunicationSoftware::onWorkerFinished(const QString& result)
{
    if (m_logBrowser) m_logBrowser->append(QString("[%1] %2").arg(QTime::currentTime().toString("HH:mm:ss.zzz")).arg(result));
}

void CommunicationSoftware::closeEvent(QCloseEvent* event)
{
    QSettings settings("MyCompany", "GNC_Simulation_Config");
    settings.setValue("TxOffset", m_leRfmTxOffset->text());
    settings.setValue("TargetNode", m_leRfmTargetNode->text());
    settings.setValue("RxOffset", m_leRfmRxOffset->text());
    settings.setValue("J2000Path", m_leJ2000Path->text());
    settings.setValue("TimerInterval", m_spinIntervalMs->value());
    // 【新增这行】：保存 CameraLink 的资源名
    settings.setValue("CmlkResource", m_leCmlkResource->text());
    // 保存所有路由的复选框状态
    settings.setValue("RouteLvds", m_chkRouteLvds->isChecked());
    settings.setValue("RouteCmlk", m_chkRouteCmlk->isChecked());
    settings.setValue("Route2711", m_chkRoute2711->isChecked());
    settings.setValue("RouteRS422", m_chkRouteRS422->isChecked());
    // 【新增】：保存硬件接口与相机的动态绑定关系
    settings.setValue("LvdsCamId", m_spinLvdsCamId->value());
    settings.setValue("CmlkCamId", m_spinCmlkCamId->value());
    settings.setValue("2711CamId", m_spin2711CamId->value());
    settings.setValue("RS422CamId", m_spinRS422CamId->value());

    cleanupThreads();
    event->accept();
}

void CommunicationSoftware::cleanupThreads()
{
    // ==========================================
    // 【关键新增】：先通知后台连续监听的 RX 线程退出死循环
    // ==========================================
    emit sigStopRxListen();

    // 1. 清理 LVDS 线程
    if (mLvdsThread) {
        if (mLvdsThread->isRunning()) {
            emit sigCloseBoard();
            mLvdsThread->quit();
            if (!mLvdsThread->wait(3000)) {
                mLvdsThread->terminate();
                mLvdsThread->wait();
            }
        }
        delete mLvdsThread;
        mLvdsThread = nullptr;
        mLvdsWorker = nullptr;
    }

    // 2. 清理 CameraLink 线程
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

    // ==========================================
    // [新增] RFM 板卡安全退出流程
    // ==========================================
    // 清理 RFM TX (发送) 核心线程
    if (m_rfmTxThread && m_rfmTxThread->isRunning()) {
        bool ok;
        quint32 txOffset = m_leRfmTxOffset->text().toUInt(&ok, 16);
        quint32 rxOffset = m_leRfmRxOffset->text().toUInt(&ok, 16);

        // [结束时初始清0] 擦除动力学数据区和图像包头区，防止下次启动发生乱码或残影
        QMetaObject::invokeMethod(m_rfmTxWorker, "clearMemoryRegion", Qt::BlockingQueuedConnection,
            Q_ARG(quint32, txOffset), Q_ARG(quint32, 2048));
        QMetaObject::invokeMethod(m_rfmTxWorker, "clearMemoryRegion", Qt::BlockingQueuedConnection,
            Q_ARG(quint32, rxOffset), Q_ARG(quint32, 2048));

        // [安全断开句柄]
        QMetaObject::invokeMethod(m_rfmTxWorker, "closeBoard", Qt::BlockingQueuedConnection);

        m_rfmTxThread->quit();
        if (!m_rfmTxThread->wait(3000)) {
            m_rfmTxThread->terminate();
            m_rfmTxThread->wait();
        }
        delete m_rfmTxThread;
        m_rfmTxThread = nullptr;
        m_rfmTxWorker = nullptr;
    }

    // 清理 RFM RX (接收) 核心线程
    if (m_rfmRxThread && m_rfmRxThread->isRunning()) {
        // [安全断开句柄]
        QMetaObject::invokeMethod(m_rfmRxWorker, "closeBoard", Qt::BlockingQueuedConnection);

        m_rfmRxThread->quit();
        if (!m_rfmRxThread->wait(3000)) {
            m_rfmRxThread->terminate();
            m_rfmRxThread->wait();
        }
        delete m_rfmRxThread;
        m_rfmRxThread = nullptr;
        m_rfmRxWorker = nullptr;
    }
}

void CommunicationSoftware::handleStartDynamicsClicked()
{
    // ==========================================
    // 1. 通用的停止逻辑：如果是运行状态，则全部关闭
    // ==========================================
    // 【修改】：把 UDP 绑定的判断也加进去
    if (m_dynamicsTimer->isActive() || m_udpSocket->state() == QAbstractSocket::BoundState) {
        m_dynamicsTimer->stop();
        if (m_dynamicsFile.is_open()) m_dynamicsFile.close();

        // 停止 UDP 监听并断开槽函数，防止收到残余网络包
        m_udpSocket->abort();
        m_udpSocket->disconnect();

        m_btnStartDynamics->setText(QString::fromLocal8Bit("启动闭环数据流"));
        m_logBrowser->append(QString::fromLocal8Bit("已手动停止动力学数据发送/UDP监听。"));

        emit sigStopRxListen();
        return;
    }

    m_currentLineNumber = 0; // 重置帧号

    // ==========================================
    // 2. 启动本地离线回放
    // ==========================================
    if (m_radioLocalFile->isChecked()) {
        std::string filePath = m_leJ2000Path->text().toLocal8Bit().constData();
        if (m_dynamicsFile.is_open()) {
            m_dynamicsFile.close();
        }
        m_dynamicsFile.clear();
        m_dynamicsFile.open(filePath, std::ios::in);

        if (!m_dynamicsFile.is_open()) {
            onWorkerError(QString::fromLocal8Bit("无法打开动力学数据文件：") + m_leJ2000Path->text());
            return;
        }

        int interval = m_spinIntervalMs->value();
        m_dynamicsTimer->start(interval);
        m_btnStartDynamics->setText(QString::fromLocal8Bit("停止闭环数据流"));
        m_logBrowser->append(QString::fromLocal8Bit("开始读取本地文件，发送步长: %1 ms").arg(interval));

        if (!m_chkWaitAck->isChecked()) {
            bool ok;
            quint32 rxOffset = m_leRfmRxOffset->text().toUInt(&ok, 16);
            emit sigStartRxListen(rxOffset);
        }
    }
    // ==========================================
    // 3. 启动 UDP 实时转发
    // ==========================================
    else {
        quint16 port = m_leUdpLocalPort->text().toUShort();

        // 绑定指定端口 (AnyIPv4 允许接收局域网内任意 IP 发向该端口的数据)
        if (m_udpSocket->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress)) {

            // 绑定数据就绪信号
            connect(m_udpSocket, &QUdpSocket::readyRead, this, &CommunicationSoftware::onUdpDataReceived);

            m_btnStartDynamics->setText(QString::fromLocal8Bit("停止 UDP 转发"));
            m_logBrowser->append(QString::fromLocal8Bit("已启动 UDP 监听，端口: %1，准备向光纤直通转发。").arg(port));

            // 如果是非闭环模式，立即唤醒独立的 RX 线程截获图像
            if (!m_chkWaitAck->isChecked()) {
                bool ok;
                quint32 rxOffset = m_leRfmRxOffset->text().toUInt(&ok, 16);
                emit sigStartRxListen(rxOffset);
            }
        }
        else {
            onWorkerError(QString::fromLocal8Bit("UDP 端口绑定失败，可能被其他程序占用！"));
        }
    }
}

void CommunicationSoftware::onDynamicsTimerTimeout()
{
    // 获取当前是否处于严格闭环模式
    bool strictSync = m_chkWaitAck->isChecked();

    // 如果是闭环模式，且上一帧还在等待，则跳过本次定时器滴答
    if (strictSync && m_isWaitingForAck) return;

    std::string dataLine;
    if (!std::getline(m_dynamicsFile, dataLine)) {
        m_dynamicsTimer->stop();
        // 【关键修复 1】：读完文件后，必须主动关闭文件！
        if (m_dynamicsFile.is_open()) {
            m_dynamicsFile.close();
        }
        m_btnStartDynamics->setText(QString::fromLocal8Bit("启动数据流"));
        m_logBrowser->append(QString::fromLocal8Bit("文件读取完毕，仿真发送结束。"));
        return;
    }

    // 只有在严格同步模式下才上锁
    if (strictSync) {
        m_isWaitingForAck = true;
    }

    m_currentLineNumber++;
    const int DATA_SIZE = 133;
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
        onWorkerError(QString::fromLocal8Bit("第 %1 行数据列数错误，已跳过。").arg(m_currentLineNumber));
        m_isWaitingForAck = false; // 解锁以防死锁
        return;
    }

    // ==========================================
    // [新增] 构造动力学硬核协议头
    // ==========================================
    RfmDynamicsHeader header;
    header.magicNum = 0xD0D0D0D0;
    header.frameId = m_currentLineNumber; // 直接使用行号作为递增的帧序号，完美防重
    header.dataCount = DATA_SIZE;         // 133
    header.dataBytes = DATA_SIZE * sizeof(double); // 1064

    // ==========================================
    // [修改] 内存拼接：将表头和数据打包到一块连续的 QByteArray 中
    // ==========================================
    QByteArray packetData;
    // 1. 先把表头塞入头部 (占用 16 字节)
    packetData.append(reinterpret_cast<const char*>(&header), sizeof(RfmDynamicsHeader));
    // 2. 再把 133 个 double 裸数据紧接着塞进去 (占用 1064 字节)
    packetData.append(reinterpret_cast<const char*>(vecDouble.data()), header.dataBytes);

    // 获取 UI 配置
    bool ok;
    quint32 txOffset = m_leRfmTxOffset->text().toUInt(&ok, 16);
    RFM2G_NODE targetNode = m_leRfmTargetNode->text().toUShort(&ok, 0);
    quint32 rxOffset = m_leRfmRxOffset->text().toUInt(&ok, 16);
    quint32 timeout = 5000;

    // 根据模式选择发送策略 (发送刚才拼接好的 packetData)
    if (strictSync) {
        emit sigSendAndWaitRfm(packetData, txOffset, targetNode, rxOffset, timeout);
    }
    else {
        emit sigSendRfmData(packetData, txOffset, targetNode);
    }

    // 缓存并刷新 Tx 矩阵 
    m_latestTxData = packetData; // 把整包数据(含表头)交给矩阵显示
    updateMatrixTable(m_dataTable, m_latestTxData);
    /*if (m_currentLineNumber % 10 == 0) {
        updateMatrixTable(m_dataTable, m_latestTxData);
    }*/
}

void CommunicationSoftware::onSimulationStepFinished()
{
    m_isWaitingForAck = false;
}

void CommunicationSoftware::routeImageToVideo(quint32 camId, const QByteArray& imageData, quint32 width, quint32 height, quint8 bitDepth)
{

    // 1. 缓存并刷新 Rx 矩阵表
    m_latestRxData = imageData;
    updateMatrixTable(m_rxImageTable, m_latestRxData);

    // ==========================================
    // [新增] 动态获取用户当前选中的相机 ID
    // currentIndex() 从 0 开始，所以 + 1 对应 camId
    // ==========================================
    quint32 selectedCamId = static_cast<quint32>(m_cmbCameraSelect->currentIndex() + 1);

    // 只有当传来的图像属于用户正在看的相机时，才更新界面！
    // 这样彻底避免了多相机同时打图导致的界面闪退和数据表疯狂跳动
    if (camId == selectedCamId) {

        // 1. 缓存并刷新 Rx 矩阵表
        m_latestRxData = imageData;
        updateMatrixTable(m_rxImageTable, m_latestRxData);

        // 2. 图像可视化实时渲染
        m_displayFrameCounter++;

        // 降频刷新保护：每 2 帧更新一次界面（保证流畅度又不卡死 UI）
        /*if (m_displayFrameCounter % 1 == 0) {*/
        if (1) {
            int target_w = m_imageView->viewport()->width();
            int target_h = m_imageView->viewport()->height();

            if (target_w < 100) target_w = 400;
            if (target_h < 100) target_h = 300;

            int bytesPerPixel = (bitDepth == 16) ? 2 : 1;
            int bytesPerLine = width * bytesPerPixel;

            if (imageData.size() >= height * bytesPerLine) {
                QImage::Format format = (bitDepth == 16) ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8;
                QImage rawImg(reinterpret_cast<const uchar*>(imageData.constData()), width, height, bytesPerLine, format);

                QImage displayImg;
                if (width > target_w || height > target_h) {
                    displayImg = rawImg.scaled(target_w, target_h, Qt::KeepAspectRatio, Qt::FastTransformation);
                }
                else {
                    displayImg = rawImg;
                }

                if (bitDepth == 16) {
                    double max_DN = 4095; // 根据您的实际位深修改
                    double alpha = 255.0 / max_DN;

                    QImage img8(displayImg.size(), QImage::Format_Grayscale8);

                    for (int y = 0; y < displayImg.height(); ++y) {
                        const quint16* srcLine = reinterpret_cast<const quint16*>(displayImg.constScanLine(y));
                        uchar* dstLine = img8.scanLine(y);
                        for (int x = 0; x < displayImg.width(); ++x) {
                            int val = srcLine[x] * alpha;
                            dstLine[x] = (val > 255) ? 255 : val;
                        }
                    }
                    displayImg = img8;
                }

                m_pixmapItem->setPixmap(QPixmap::fromImage(displayImg));
                m_imageView->setSceneRect(m_pixmapItem->boundingRect());
                m_imageView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
            }
        }
    }

    // ==========================================
    // 3. 后台物理硬件转发路由 (完全不受界面切换影响)
    // ==========================================

    // 判断该图是否归属 LVDS 接口
    if (m_chkRouteLvds->isChecked() && camId == static_cast<quint32>(m_spinLvdsCamId->value())) {
        emit sigSendToLvdsMem(imageData, width, height, bitDepth);
    }

    // 判断该图是否归属 CameraLink 接口
    if (m_chkRouteCmlk->isChecked() && camId == static_cast<quint32>(m_spinCmlkCamId->value())) {
        emit sigSendToCmlkMem(imageData, width, height, bitDepth);
    }

    // 判断该图是否归属 2711 接口
    if (m_chkRoute2711->isChecked() && camId == static_cast<quint32>(m_spin2711CamId->value())) {
        // emit sigSendTo2711Mem(imageData, width, height, bitDepth);
    }

    // 判断该图是否归属 RS422 接口
    if (m_chkRouteRS422->isChecked() && camId == static_cast<quint32>(m_spinRS422CamId->value())) {
        // emit sigSendToRS422Mem(imageData, width, height, bitDepth);
    }
}

// 1. 初始化空矩阵 (隐藏所有表头，纯净显示数据)
void CommunicationSoftware::initMatrixTable(QTableWidget* table, int maxRows)
{
    table->setColumnCount(16); // 固定 16 列
    table->setRowCount(maxRows);

    // 隐藏水平和垂直表头
    table->horizontalHeader()->hide();
    table->verticalHeader()->hide();

    // 让 16 列均匀撑满整个表格宽度
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // 去除网格线和选中高亮，更像纯净的内存区 (可选)
    // table->setShowGrid(false); 
    // table->setSelectionMode(QAbstractItemView::NoSelection);

    // 预先 `new` 出所有的单元格，后续只需 `setText`，极大地降低 CPU 损耗
    for (int r = 0; r < maxRows; ++r) {
        for (int c = 0; c < 16; ++c) {
            QTableWidgetItem* item = new QTableWidgetItem("-");
            item->setTextAlignment(Qt::AlignCenter);
            table->setItem(r, c, item);
        }
    }
}

void CommunicationSoftware::updateMatrixTable(QTableWidget* table, const QByteArray& data)
{
    if (!table || data.isEmpty()) return;

    // 暂停 UI 刷新，防止闪烁
    table->setUpdatesEnabled(false);

    // ==========================================
    // 模式 A：还原为 Double 浮点数查看
    // ==========================================
    if (m_radioDouble->isChecked()) {
        // 计算包含多少个完整的 double
        int doubleCount = data.size() / sizeof(double);
        // 限制最大显示数量，防止越界 (100行 x 16列 = 1600个格子)
        int maxItems = qMin(doubleCount, table->rowCount() * 16);

        // 将裸内存指针安全地强转为 double 指针
        const double* dPtr = reinterpret_cast<const double*>(data.constData());

        for (int i = 0; i < maxItems; ++i) {
            int r = i / 16;
            int c = i % 16;
            // 使用 'g' 格式，保留 6 位有效数字，自动切换科学计数法
            table->item(r, c)->setText(QString::number(dPtr[i], 'g', 6));
        }

        // 清空多余的单元格
        for (int i = maxItems; i < table->rowCount() * 16; ++i) {
            table->item(i / 16, i % 16)->setText("-");
        }
    }
    // ==========================================
    // 模式 B：单字节内存抓包 (Hex / Dec)
    // ==========================================
    else {
        bool isHex = m_radioHex->isChecked();
        int maxBytes = qMin(data.size(), table->rowCount() * 16);

        for (int i = 0; i < maxBytes; ++i) {
            int r = i / 16;
            int c = i % 16;

            quint8 byte = static_cast<quint8>(data.at(i));
            QString text = isHex ? QString("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper()
                : QString::number(byte);

            table->item(r, c)->setText(text);
        }

        for (int i = maxBytes; i < table->rowCount() * 16; ++i) {
            table->item(i / 16, i % 16)->setText("-");
        }
    }

    // 恢复 UI 刷新
    table->setUpdatesEnabled(true);
}

// 3. 点击单选框时，读取缓存，瞬间重新刷入
void CommunicationSoftware::onFormatToggled()
{
    updateMatrixTable(m_dataTable, m_latestTxData);
    updateMatrixTable(m_rxImageTable, m_latestRxData);
}
void CommunicationSoftware::onUdpDataReceived()
{
    bool strictSync = m_chkWaitAck->isChecked();

    // 循环读取所有积压的网络数据报
    while (m_udpSocket->hasPendingDatagrams()) {

        QByteArray datagram;
        datagram.resize(m_udpSocket->pendingDatagramSize());
        m_udpSocket->readDatagram(datagram.data(), datagram.size());

        // 假设外部仿真机发送的是 133 个 double 的纯二进制数组
        const int DATA_SIZE = 133;
        const int EXPECTED_BYTES = DATA_SIZE * sizeof(double); // 1064 字节

        // 拦截器 1：校验 UDP 数据包长度
        if (datagram.size() != EXPECTED_BYTES) {
            onWorkerError(QString::fromLocal8Bit("UDP 报文长度错误: 收到 %1 字节，预期应为 %2 字节。")
                .arg(datagram.size()).arg(EXPECTED_BYTES));
            continue;
        }

        // 拦截器 2：严格闭环模式下的丢帧策略
        if (strictSync && m_isWaitingForAck) {
            // 如果上位机还在死等上一帧的图像，而网络上又飞来了新的动力学，
            // 工业级做法是：为了保证绝对的实时性，必须丢弃旧包，防止雪崩式延迟积压
            continue;
        }

        if (strictSync) {
            m_isWaitingForAck = true;
        }

        m_currentLineNumber++; // 递增充当 FrameID

        // 1. 构造硬核动力学协议头
        RfmDynamicsHeader header;
        header.magicNum = 0xD0D0D0D0;
        header.frameId = m_currentLineNumber;
        header.dataCount = DATA_SIZE;
        header.dataBytes = EXPECTED_BYTES;

        // 2. 内存拼包：协议头 + 1064 字节裸负荷
        QByteArray packetData;
        packetData.append(reinterpret_cast<const char*>(&header), sizeof(RfmDynamicsHeader));
        packetData.append(datagram);

        // 获取 UI 面板参数
        bool ok;
        quint32 txOffset = m_leRfmTxOffset->text().toUInt(&ok, 16);
        RFM2G_NODE targetNode = m_leRfmTargetNode->text().toUShort(&ok, 0);
        quint32 rxOffset = m_leRfmRxOffset->text().toUInt(&ok, 16);
        quint32 timeout = 5000;

        // 3. 兵分两路：闭环打图 vs 开环盲发
        if (strictSync) {
            emit sigSendAndWaitRfm(packetData, txOffset, targetNode, rxOffset, timeout);
        }
        else {
            emit sigSendRfmData(packetData, txOffset, targetNode);
        }

        // 4. 刷新监控矩阵 (把包含表头的整包送去解码展示)
        m_latestTxData = packetData;
        updateMatrixTable(m_dataTable, m_latestTxData);
    }
}