#ifndef COMMUNICATIONSOFTWARE_H
#define COMMUNICATIONSOFTWARE_H

#include <QtWidgets/QMainWindow>
#include "ui_CommunicationSoftware.h" 
#include <QThread>
#include <QCloseEvent>
#include <QTimer>     
#include <fstream>    
#include <array>
#include <windows.h>
#include "rfm2g_api.h"
#include <QSettings>
#include <QCheckBox> // 在顶部添加头文件
#include <QRadioButton> // 顶部加入头文件
#include <QTableWidgetItem>
#include <QGraphicsPixmapItem> // 在顶部区域添加头文件
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
    void sigInitLvds(const QString& resourceName);
    void sigSendImage(const QString& imagePath);
    void sigCloseBoard();
    void sigResetBoard();
    void sigReadSelfTestData(const QString& saveFilePath);
    void sigPackAndSaveCmlk(const QString& imagePath, const QString& savePath);

    // ==========================================
    // RFM2g 光纤接口跨线程控制信号
    // ==========================================
    void sigInitRfm(const QString& devicePath);
    // 【请在这里补上这行】：开环测试用的单向发送信号
    void sigSendRfmData(const QByteArray& data, quint32 offset, RFM2G_NODE targetNode);
    // 闭环自动化：原子化的发送与等待信号
    void sigSendAndWaitRfm(const QByteArray& data, quint32 txOffset, RFM2G_NODE targetNode, quint32 rxOffset, quint32 timeoutMs);
    // ==========================================
    // [新增] 控制独立接收线程的信号
    // ==========================================
    void sigStartRxListen(quint32 rxOffset);
    void sigStopRxListen();

    // ==========================================
    // [新增] 动态路由专用转发信号
    // ==========================================
    void sigSendToLvdsMem(const QByteArray& imageData, quint32 width, quint32 height, quint8 bitDepth);
    void sigSendToCmlkMem(const QByteArray& imageData, quint32 width, quint32 height, quint8 bitDepth);

private:
    QLineEdit* m_leRfmRxOffset;

    // 标志位，用于拦截未完成的定时器事件，防止数据积压
    bool m_isWaitingForAck = false;

    // 替换为这组复选框：
    QCheckBox* m_chkRouteLvds;
    QCheckBox* m_chkRouteCmlk;
    // [新增] 预留的 2711 与 RS422 接口路由
    QCheckBox* m_chkRoute2711;
    QCheckBox* m_chkRouteRS422;
    // [新增] 专门用于监控接收图像的表格
    QTableWidget* m_rxImageTable;

    // [新增] 进制切换单选按钮
    QRadioButton* m_radioHex;
    QRadioButton* m_radioDec;
    // [新增] 浮点数还原视图按钮
    QRadioButton* m_radioDouble;

    // [新增] 闭环/开环模式切换复选框
    QCheckBox* m_chkWaitAck;

    // ==========================================
    // [新增] 矩阵表格相关的缓存与函数
    // ==========================================
    QByteArray m_latestTxData; // 缓存最新一帧的 Tx 裸数据
    QByteArray m_latestRxData; // 缓存最新一帧的 Rx 裸数据

    // 辅助函数：初始化 Mx16 的空矩阵
    void initMatrixTable(QTableWidget* table, int maxRows);

    // 辅助函数：将裸数据刷入矩阵
    void updateMatrixTable(QTableWidget* table, const QByteArray& data);



protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void routeImageToVideo(const QByteArray& imageData, quint32 width, quint32 height, quint8 bitDepth);
    void onSimulationStepFinished();

    void onWorkerLogMessage(const QString& msg);
    void onWorkerError(const QString& errorMsg);
    void onWorkerFinished(const QString& result);

    void handleStartDynamicsClicked();
    void onDynamicsTimerTimeout();
    // [新增] 用于响应 16进制/10进制 切换的槽函数
    void onFormatToggled();

private:
    Ui::CommunicationSoftwareClass ui;

    QThread* mLvdsThread;
    LvdsWorker* mLvdsWorker;
    QThread* m_cmlkThread;
    CameraLinkWorker* m_cmlkWorker;
    //QThread* m_rfmThread;
    //Rfm2gWorker* m_rfmWorker;
    // 【替换为以下四行双核架构】：
    QThread* m_rfmTxThread;
    Rfm2gWorker* m_rfmTxWorker;

    QThread* m_rfmRxThread;
    Rfm2gWorker* m_rfmRxWorker;


    QLineEdit* m_leResource;
    QLineEdit* m_leImagePath;
    QGraphicsView* m_imageView;
    QGraphicsScene* m_imageScene;
    // ==========================================
    // [新增] 图像显示图元与降频计数器
    // ==========================================
    QGraphicsPixmapItem* m_pixmapItem; // 预先分配图元，避免反复 clear 造成的内存泄漏
    int m_displayFrameCounter = 0;     // 降频刷新计数器



    QTableWidget* m_dataTable;
    QTextBrowser* m_logBrowser;

    QLineEdit* m_leCmlkImagePath;
    QLineEdit* m_leCmlkSavePath;

    QRadioButton* m_radioLocalFile;
    QRadioButton* m_radioUdpNetwork;
    QLineEdit* m_leJ2000Path;
    QSpinBox* m_spinIntervalMs;

    QLineEdit* m_leUdpLocalPort;
    QLineEdit* m_leUdpRemoteIp;
    QLineEdit* m_leUdpRemotePort;
    QComboBox* m_cmbUdpMode;

    QPushButton* m_btnStartDynamics;
    QTimer* m_dynamicsTimer;
    std::ifstream m_dynamicsFile;
    int m_currentLineNumber;

    QLineEdit* m_leRfmTargetNode;
    QLineEdit* m_leRfmTxOffset;

    void initUI();
    void cleanupThreads();
    void initThreads();
};

#endif // COMMUNICATIONSOFTWARE_H