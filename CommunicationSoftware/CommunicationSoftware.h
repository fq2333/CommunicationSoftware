#ifndef COMMUNICATIONSOFTWARE_H
#define COMMUNICATIONSOFTWARE_H

#include <QtWidgets/QMainWindow>
#include "ui_CommunicationSoftware.h" // 根据你的实际UI头文件名称可能需要调整，例如 #include "ui_communicationsoftware.h"
#include <QThread>
#include <QCloseEvent>
#include <QTimer>     // [新增] 定时器
#include <fstream>    // [新增] 文件流
#include <array>

// 注意：因为下面的信号中用到了 RFM2G_NODE，必须在这里引入 RFM 官方头文件
#include <windows.h>
#include "rfm2g_api.h"

// 前向声明 Qt 控件，减少头文件依赖
class LvdsWorker;
class CameraLinkWorker;
class QLineEdit;
class QGraphicsView;
class QGraphicsScene;
class QTableWidget;
class QTextBrowser;
class QRadioButton;
class QSpinBox;
class QPushButton;
class QComboBox;
class Rfm2gWorker;

class CommunicationSoftware : public QMainWindow
{
    Q_OBJECT

public:
    explicit CommunicationSoftware(QWidget* parent = nullptr);
    ~CommunicationSoftware();
    
signals:
    void sigInitLvds(const QString &resourceName);
    void sigSendImage(const QString &imagePath);
    void sigCloseBoard();
    void sigResetBoard();
    void sigReadSelfTestData(const QString& saveFilePath);

    // --- [新增] CameraLink 信号 ---
    // 用于通知子线程执行离线组包和保存
    void sigPackAndSaveCmlk(const QString& imagePath, const QString& savePath);
    // ==========================================
    // [新增] RFM2g 光纤接口跨线程控制信号
    // ==========================================
    void sigInitRfm(const QString& devicePath);
    void sigSendRfmData(const QByteArray& data, quint32 offset, RFM2G_NODE targetNode);
    void sigWaitRfmReturn(quint32 offset, quint32 length, quint32 timeoutMs);


protected:
    // [新增] 拦截窗口关闭事件
    void closeEvent(QCloseEvent* event) override;

private slots:
    // 用于接收底层 Worker 线程反馈的槽函数，用于更新UI
    void onWorkerLogMessage(const QString &msg);
    void onWorkerError(const QString &errorMsg);
    void onWorkerFinished(const QString &result);
    // [新增] 动力学数据发送控制槽函数
    void on_btnStartDynamics_clicked();
    void onDynamicsTimerTimeout(); // 定时器触发时的逐行读取逻辑

private:
    Ui::CommunicationSoftwareClass ui;

    // 硬件管理相关的线程与工作对象指针
    QThread* mLvdsThread;
    LvdsWorker* mLvdsWorker;

    // --- [新增] CameraLink 线程与对象 ---
    QThread* m_cmlkThread;
    CameraLinkWorker* m_cmlkWorker;

    QThread*  m_rfmThread;
    Rfm2gWorker* m_rfmWorker;

    // === 提升为 private 成员变量的 UI 控件 ===
    QLineEdit* m_leResource;       // 设备资源名输入框
    QLineEdit* m_leImagePath;      // 图像路径输入框
    QGraphicsView* m_imageView;    // 图像展示区 View
    QGraphicsScene* m_imageScene;  // 图像展示区 Scene
    QTableWidget* m_dataTable;     // 数据包解析监控表格
    QTextBrowser* m_logBrowser;    // 系统运行日志区

    // --- [新增] CameraLink UI 控件 ---
    QLineEdit* m_leCmlkImagePath; // CMLK输入图像路径
    QLineEdit* m_leCmlkSavePath;  // CMLK输出bin路径

    // === 动力学数据源控制区 UI 控件 ===
    QRadioButton* m_radioLocalFile;
    QRadioButton* m_radioUdpNetwork;

    // 本地文件配置
    QLineEdit* m_leJ2000Path;
    QSpinBox* m_spinIntervalMs; // 发送间隔(毫秒)

    // UDP 网络配置 (预留扩展)
    QLineEdit* m_leUdpLocalPort;
    QLineEdit* m_leUdpRemoteIp;
    QLineEdit* m_leUdpRemotePort;
    QComboBox* m_cmbUdpMode;

    QPushButton* m_btnStartDynamics;

    // === 发送控制核心对象 ===
    QTimer* m_dynamicsTimer;
    std::ifstream m_dynamicsFile;
    int m_currentLineNumber;


    // 内部初始化与清理函数
    void initUI();
    void cleanupThreads();
    void initThreads(); // 添加这个声明
    
};

#endif // COMMUNICATIONSOFTWARE_H