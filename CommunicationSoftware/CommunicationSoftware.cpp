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

    m_leImagePath = new QLineEdit("C:/test_image.bmp", lvdsPage);
    QPushButton* btnSelectImage = new QPushButton(QString::fromLocal8Bit("选择本地图像..."), lvdsPage);
    QPushButton* btnSendImage = new QPushButton(QString::fromLocal8Bit("写入板卡并发送"), lvdsPage);

    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("设备资源名:")));
    lvdsLayout->addWidget(m_leResource);
    lvdsLayout->addWidget(btnInitLvds);
    lvdsLayout->addWidget(new QLabel(QString::fromLocal8Bit("图像文件路径:")));
    lvdsLayout->addWidget(m_leImagePath);
    lvdsLayout->addWidget(btnSelectImage);
    lvdsLayout->addWidget(btnSendImage);
    lvdsLayout->addStretch();

    configToolBox->addItem(lvdsPage, QString::fromLocal8Bit("1. LVDS接口配置"));

    // (... 省略 UDP/RS422 页面的添加逻辑，与之前一致 ...)

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