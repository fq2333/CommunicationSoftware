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

#include <QThread>
#include "LvdsWorker.h"
CommunicationSoftware::CommunicationSoftware(QWidget* parent)
    : QMainWindow(parent),
    mLvdsThread(nullptr), mLvdsWorker(nullptr),
    m_leResource(nullptr), m_leImagePath(nullptr),
    m_imageView(nullptr), m_imageScene(nullptr),
    m_dataTable(nullptr), m_logBrowser(nullptr) // 初始化指针为空，防止野指针
{
    ui.setupUi(this);
    initUI();

    // 实例化 Worker 和 Thread
    QThread* lvdsThread = new QThread(this);
    LvdsWorker* lvdsWorker = new LvdsWorker();

    // 将Worker移入子线程
    lvdsWorker->moveToThread(lvdsThread);

    // 绑定UI和Worker的信号槽
    connect(lvdsWorker, &LvdsWorker::logMessage, this, [=](const QString& msg) {
        // ui.textBrowser->append(msg); // 更新UI日志
        });
    connect(lvdsWorker, &LvdsWorker::errorOccurred, this, [=](const QString& err) {
        // ui.textBrowser->append("ERROR: " + err);
        });

    // 绑定UI按钮操作到Worker (使用Qt::QueuedConnection以保证线程安全)
    connect(this, &CommunicationSoftware::sigInitLvds, lvdsWorker, &LvdsWorker::initializeBoard);
    connect(this, &CommunicationSoftware::sigSendImage, lvdsWorker, &LvdsWorker::sendLocalImage);

    // 确保线程结束时清理Worker
    connect(lvdsThread, &QThread::finished, lvdsWorker, &QObject::deleteLater);

    // 启动线程
    lvdsThread->start();

    // 触发初始化
    emit sigInitLvds("10.109.3.100::PXI29::12::INSTR");
}

CommunicationSoftware::~CommunicationSoftware()
{
    cleanupThreads();
    // 如果 ui 是指针则需要 delete ui;
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
    QPushButton* btnInitLvds = new QPushButton("初始化 LVDS/光纤 板卡", lvdsPage);

    m_leImagePath = new QLineEdit("C:/test_image.bmp", lvdsPage);
    QPushButton* btnSelectImage = new QPushButton("选择本地图像...", lvdsPage);
    QPushButton* btnSendImage = new QPushButton("写入板卡并发送", lvdsPage);

    lvdsLayout->addWidget(new QLabel("设备资源名 (Resource Name):"));
    lvdsLayout->addWidget(m_leResource);
    lvdsLayout->addWidget(btnInitLvds);
    lvdsLayout->addWidget(new QLabel("图像文件路径:"));
    lvdsLayout->addWidget(m_leImagePath);
    lvdsLayout->addWidget(btnSelectImage);
    lvdsLayout->addWidget(btnSendImage);
    lvdsLayout->addStretch();

    configToolBox->addItem(lvdsPage, "1. LVDS & 光纤 接口配置");

    // (... 省略 UDP/RS422 页面的添加逻辑，与之前一致 ...)

    // ==========================================
    // 右侧：图像显示与数据监控区
    // ==========================================
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, mainSplitter);
    mainSplitter->setStretchFactor(1, 1);

    // -> 2.1 图像可视化区
    QGroupBox* grpImage = new QGroupBox("图像可视化展示", rightSplitter);
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
    m_dataTable->setHorizontalHeaderLabels({ "时间", "方向", "接口", "数据类型", "解码内容 (Hex/解析)" });
    m_dataTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_dataTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    m_dataTable->setAlternatingRowColors(true);
    m_dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dataTabWidget->addTab(m_dataTable, "实时数据包解析监控");

    // 使用成员变量实例化 TextBrowser
    m_logBrowser = new QTextBrowser(dataTabWidget);
    dataTabWidget->addTab(m_logBrowser, "系统运行日志");

    rightSplitter->setStretchFactor(0, 6);
    rightSplitter->setStretchFactor(1, 4);

    // ==========================================
    // 3. 信号与槽的连接
    // ==========================================
    connect(btnInitLvds, &QPushButton::clicked, this, [=]() {
        emit sigInitLvds(m_leResource->text()); // 直接读取成员变量
        m_logBrowser->append(QString("[%1] 发送初始化指令...").arg(QTime::currentTime().toString("HH:mm:ss")));
        });

    connect(btnSendImage, &QPushButton::clicked, this, [=]() {
        emit sigSendImage(m_leImagePath->text()); // 直接读取成员变量
        m_logBrowser->append(QString("[%1] 准备加载图片并发送...").arg(QTime::currentTime().toString("HH:mm:ss")));
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

void CommunicationSoftware::cleanupThreads()
{
    if (mLvdsThread && mLvdsThread->isRunning()) {
        // 通知板卡关闭
        emit sigCloseBoard();

        // 请求线程退出并等待它完成当前任务
        mLvdsThread->quit();
        mLvdsThread->wait(); // 阻塞主线程等待子线程安全结束，防止悬空指针
    }
}