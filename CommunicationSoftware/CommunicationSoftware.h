#ifndef COMMUNICATIONSOFTWARE_H
#define COMMUNICATIONSOFTWARE_H

#include <QtWidgets/QMainWindow>
#include "ui_CommunicationSoftware.h" // 根据你的实际UI头文件名称可能需要调整，例如 #include "ui_communicationsoftware.h"
#include <QThread>

// 前向声明 Qt 控件，减少头文件依赖
class LvdsWorker;
class QLineEdit;
class QGraphicsView;
class QGraphicsScene;
class QTableWidget;
class QTextBrowser;

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

private slots:
    // 用于接收底层 Worker 线程反馈的槽函数，用于更新UI
    void onWorkerLogMessage(const QString &msg);
    void onWorkerError(const QString &errorMsg);
    void onWorkerFinished(const QString &result);

private:
    Ui::CommunicationSoftwareClass ui;

    // 硬件管理相关的线程与工作对象指针
    QThread* mLvdsThread;
    LvdsWorker* mLvdsWorker;

    // === 提升为 private 成员变量的 UI 控件 ===
    QLineEdit* m_leResource;       // 设备资源名输入框
    QLineEdit* m_leImagePath;      // 图像路径输入框
    QGraphicsView* m_imageView;    // 图像展示区 View
    QGraphicsScene* m_imageScene;  // 图像展示区 Scene
    QTableWidget* m_dataTable;     // 数据包解析监控表格
    QTextBrowser* m_logBrowser;    // 系统运行日志区

    // 内部初始化与清理函数
    void initUI();
    void cleanupThreads();
};

#endif // COMMUNICATIONSOFTWARE_H