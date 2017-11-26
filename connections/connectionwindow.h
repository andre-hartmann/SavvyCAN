#ifndef CONNECTIONWINDOW_H
#define CONNECTIONWINDOW_H



#include <QSerialPortInfo>
#include <QDialog>
#include <QDebug>
#include <QSettings>
#include <QTimer>
#include <QItemSelection>
#include "canconnectionmodel.h"
#include "connections/canconnection.h"


class CANConnectionModel;

namespace Ui {
class ConnectionWindow;
}


class ConnectionWindow : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionWindow(QWidget *parent = 0);
    ~ConnectionWindow();

    bool getSWMode();

signals:
    void updateBusSettings(CANBus *bus);
    void updatePortName(QString port);
    void sendDebugData(QByteArray bytes);

public slots:
    void setSpeed(int speed0);
    void setSWMode(bool mode);

    void setSuspendAll(bool pSuspend);

    void getDebugText(QString debugText);

private slots:
    void handleOKButton();
    void handleConnectionTypeChanged(const QString &type);
    void currentRowChanged(const QModelIndex &current, const QModelIndex &previous);
    void consoleEnableChanged(bool checked);
    void handleRemoveConn();
    void handleEnableAll();
    void handleDisableAll();
    void handleRevert();
    void handleNewConn();
    void handleClearDebugText();
    void handleSendHex();
    void handleSendText();
    void connectionStatus(CANConStatus);

private:
    Ui::ConnectionWindow *ui;
    QList<QSerialPortInfo> ports;
    QSettings *settings;
    CANConnectionModel *connModel;

    void selectSerial();
    void selectKvaser();
    void selectSerialBus();
    int getSpeed();
    void setPortName(const QString &typeName, const QString &portName);

    void setActiveAll(bool pActive);
    CANConnection *create(const QString &typeName, const QString &portName);
    void loadConnections();
    void saveConnections();
    void showEvent(QShowEvent *);
    void closeEvent(QCloseEvent *event);
    void readSettings();
    void writeSettings();
};

#endif // CONNECTIONWINDOW_H
