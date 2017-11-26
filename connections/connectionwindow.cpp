#include <QCanBus>
#include <QComboBox>
#include <QThread>

#include "connectionwindow.h"
#include "mainwindow.h"
#include "ui_connectionwindow.h"
#include "connections/canconfactory.h"
#include "connections/canconmanager.h"
#include "canbus.h"



ConnectionWindow::ConnectionWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ConnectionWindow)
{
    ui->setupUi(this);

    QSettings settings;

    qRegisterMetaType<CANBus>("CANBus");
    qRegisterMetaType<const CANFrame *>("const CANFrame *");
    qRegisterMetaType<const QList<CANFrame> *>("const QList<CANFrame> *");

    connModel = new CANConnectionModel(this);
    ui->tableConnections->setModel(connModel);
    ui->tableConnections->setColumnWidth(0, 40);
    ui->tableConnections->setColumnWidth(1, 70);
    ui->tableConnections->setColumnWidth(2, 70);
    ui->tableConnections->setColumnWidth(3, 70);
    ui->tableConnections->setColumnWidth(4, 70);
    ui->tableConnections->setColumnWidth(5, 70);
    ui->tableConnections->setColumnWidth(6, 70);
    ui->tableConnections->setColumnWidth(7, 90);
    QHeaderView *HorzHdr = ui->tableConnections->horizontalHeader();
    HorzHdr->setStretchLastSection(true); //causes the data column to automatically fill the tableview

    ui->textConsole->setEnabled(false);
    ui->btnClearDebug->setEnabled(false);
    ui->btnSendHex->setEnabled(false);
    ui->btnSendText->setEnabled(false);
    ui->lineSend->setEnabled(false);

    /* load connection configuration */
    loadConnections();

    connect(ui->btnOK, &QAbstractButton::clicked, this, &ConnectionWindow::handleOKButton);

    connect(ui->cbType, static_cast<void(QComboBox::*)(const QString &)>(&QComboBox::currentIndexChanged),
            this, &ConnectionWindow::handleConnectionTypeChanged);
    connect(ui->tableConnections->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &ConnectionWindow::currentRowChanged);
    connect(ui->btnActivateAll, &QPushButton::clicked, this, &ConnectionWindow::handleEnableAll);
    connect(ui->btnDeactivateAll, &QPushButton::clicked, this, &ConnectionWindow::handleDisableAll);
    connect(ui->btnRemoveBus, &QPushButton::clicked, this, &ConnectionWindow::handleRemoveConn);
    connect(ui->btnClearDebug, &QPushButton::clicked, this, &ConnectionWindow::handleClearDebugText);
    connect(ui->btnSendHex, &QPushButton::clicked, this, &ConnectionWindow::handleSendHex);
    connect(ui->btnSendText, &QPushButton::clicked, this, &ConnectionWindow::handleSendText);
    connect(ui->ckEnableConsole, &QCheckBox::toggled, this, &ConnectionWindow::consoleEnableChanged);

    ui->cbType->addItem(CANConnection::typeGvret());
#ifdef Q_OS_WIN
    ui->cbType->addItem(CANConnection::typeKvaser());
#endif
    ui->cbType->addItems(QCanBus::instance()->plugins());
}

ConnectionWindow::~ConnectionWindow()
{
    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();
    CANConnection* conn_p;

    /* save configuration */
    saveConnections();

    /* delete connections */
    while(!conns.isEmpty())
    {
        conn_p = conns.takeFirst();
        conn_p->stop();
        delete conn_p;
    }

    delete ui;
}


void ConnectionWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    qDebug() << "Show connectionwindow";
    readSettings();
    ui->tableConnections->selectRow(0);
    currentRowChanged(ui->tableConnections->currentIndex(), ui->tableConnections->currentIndex());
}

void ConnectionWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event);
    writeSettings();
}

void ConnectionWindow::readSettings()
{
    QSettings settings;
    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        resize(settings.value("ConnWindow/WindowSize", QSize(956, 665)).toSize());
        move(settings.value("ConnWindow/WindowPos", QPoint(100, 100)).toPoint());
    }
}

void ConnectionWindow::writeSettings()
{
    QSettings settings;

    if (settings.value("Main/SaveRestorePositions", false).toBool())
    {
        settings.setValue("ConnWindow/WindowSize", size());
        settings.setValue("ConnWindow/WindowPos", pos());
    }
}

void ConnectionWindow::setSuspendAll(bool pSuspend)
{
    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();

    foreach(CANConnection* conn_p, conns)
        conn_p->suspend(pSuspend);

    connModel->refresh();
}


void ConnectionWindow::setActiveAll(bool pActive)
{
    CANBus bus;
    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();

    foreach(CANConnection* conn_p, conns)
    {
        for(int i=0 ; i<conn_p->getNumBuses() ; i++) {
            if( conn_p->getBusSettings(i, bus) ) {
                bus.active = pActive;
                conn_p->setBusSettings(i, bus);
            }
        }
    }

    connModel->refresh();
}

void ConnectionWindow::consoleEnableChanged(bool checked) {
    int busId;

    CANConnection* conn_p = connModel->getAtIdx(ui->tableConnections->currentIndex().row(), busId);

    ui->textConsole->setEnabled(checked);
    ui->btnClearDebug->setEnabled(checked);
    ui->btnSendHex->setEnabled(checked);
    ui->btnSendText->setEnabled(checked);
    ui->lineSend->setEnabled(checked);

    if(!conn_p) return;

    if (checked) { //enable console
        connect(conn_p, SIGNAL(debugOutput(QString)), this, SLOT(getDebugText(QString)));
        connect(this, SIGNAL(sendDebugData(QByteArray)), conn_p, SLOT(debugInput(QByteArray)));
    }
    else { //turn it off
        disconnect(conn_p, SIGNAL(debugOutput(QString)), 0, 0);
        disconnect(this, SIGNAL(sendDebugData(QByteArray)), conn_p, SLOT(debugInput(QByteArray)));
    }
}

void ConnectionWindow::handleNewConn()
{
    ui->tableConnections->setCurrentIndex(QModelIndex());
    currentRowChanged(ui->tableConnections->currentIndex(), ui->tableConnections->currentIndex());
}


void ConnectionWindow::handleEnableAll()
{
    setActiveAll(true);
}

void ConnectionWindow::handleDisableAll()
{
    setActiveAll(false);
}

void ConnectionWindow::handleConnectionTypeChanged(const QString &type)
{
    if (type == CANConnection::typeGvret())
        selectSerial();
    else if (type == CANConnection::typeKvaser())
        selectKvaser();
    else
        selectSerialBus();
}


/* status */
void ConnectionWindow::connectionStatus(CANConStatus pStatus)
{
    Q_UNUSED(pStatus);

    qDebug() << "Connectionstatus changed";
    connModel->refresh();
}


void ConnectionWindow::handleOKButton()
{
    CANConnection* conn_p = NULL;

    if( ! CANConManager::getInstance()->getByName(ui->cbPort->currentText()) )
    {
        /* create connection */
        conn_p = create(ui->cbType->currentText(), ui->cbPort->currentText());
        if(!conn_p)
            return;
        /* add connection to model */
        connModel->add(conn_p);
    }
}

void ConnectionWindow::currentRowChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    int selIdx = current.row();

    int busId;

    disconnect(connModel->getAtIdx(previous.row(), busId), SIGNAL(debugOutput(QString)), 0, 0);
    disconnect(this, SIGNAL(sendDebugData(QByteArray)), connModel->getAtIdx(previous.row(), busId), SLOT(debugInput(QByteArray)));
return;

    /* set parameters */
    if (selIdx == -1)
    {
        ui->btnOK->setText(tr("Create New Connection"));

        ui->cbType->setCurrentText(CANConnection::typeGvret());

        setSpeed(0);
        setPortName(CANConnection::typeGvret(), QString());
    }
    else
    {
        bool ret;
        CANBus bus;
        CANConnection* conn_p = connModel->getAtIdx(selIdx, busId);
        if(!conn_p) return;

        if (ui->ckEnableConsole->isChecked()) { //only connect if console is actually enabled
            connect(conn_p, SIGNAL(debugOutput(QString)), this, SLOT(getDebugText(QString)));
            connect(this, SIGNAL(sendDebugData(QByteArray)), conn_p, SLOT(debugInput(QByteArray)));
        }

        ret = conn_p->getBusSettings(busId, bus);
        if(!ret) return;

        ui->btnOK->setText(tr("Update Connection Settings"));
        setSpeed(bus.getSpeed());
        setPortName(conn_p->getType(), conn_p->getPort());
    }
}

void ConnectionWindow::getDebugText(QString debugText) {
    ui->textConsole->append(debugText);
}

void ConnectionWindow::handleClearDebugText() {
    ui->textConsole->clear();
}

void ConnectionWindow::handleSendHex() {
    QByteArray bytes;
    QStringList tokens = ui->lineSend->text().split(' ');
    foreach (QString token, tokens) {
        bytes.append(token.toInt(nullptr, 16));
    }
    emit sendDebugData(bytes);
}

void ConnectionWindow::handleSendText() {
    QByteArray bytes;
    bytes = ui->lineSend->text().toLatin1();
    bytes.append('\r'); //add carriage return for line ending
    emit sendDebugData(bytes);
}

void ConnectionWindow::selectSerial()
{
    ui->cbPort->clear();
    ports = QSerialPortInfo::availablePorts();

    for (int i = 0; i < ports.count(); i++)
        ui->cbPort->addItem(ports[i].portName());
}

void ConnectionWindow::selectKvaser()
{
}

void ConnectionWindow::selectSerialBus()
{
    const QString plugin = ui->cbType->currentText();

    ui->cbPort->clear();

#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    QString error;
    const QList<QCanBusDeviceInfo> devices = QCanBus::instance()->availableDevices(plugin, &error);

    for (const auto &device : devices)
        ui->cbPort->addItem(device.name());
#endif
}

void ConnectionWindow::setSpeed(int speed0)
{
}

void ConnectionWindow::setPortName(const QString &typeName, const QString &portName)
{
    ui->cbType->setCurrentText(typeName);

    if (typeName != CANConnection::typeKvaser())
        ui->cbPort->setCurrentText(portName);
}


//-1 means leave it at whatever it booted up to. 0 means disable. Otherwise the actual rate we want.
int ConnectionWindow::getSpeed()
{
    return -1;
}

void ConnectionWindow::setSWMode(bool mode)
{
}

bool ConnectionWindow::getSWMode()
{
    return false;
}

void ConnectionWindow::handleRemoveConn()
{
    int selIdx = ui->tableConnections->selectionModel()->currentIndex().row();
    if (selIdx <0) return;

    qDebug() << "remove connection at index: " << selIdx;

    int busId;
    CANConnection* conn_p = connModel->getAtIdx(selIdx, busId);
    if(!conn_p) return;

    /* remove connection from model & manager */
    connModel->remove(conn_p);

    /* stop and delete connection */
    conn_p->stop();
    delete conn_p;

    /* select first connection in list */
    ui->tableConnections->selectRow(0);
}

void ConnectionWindow::handleRevert()
{

}

CANConnection *ConnectionWindow::create(const QString &typeName, const QString &portName)
{
    CANConnection* conn_p;

    /* create connection */
    conn_p = CanConFactory::create(typeName, portName);
    if(conn_p)
    {
        /* connect signal */
        connect(conn_p, SIGNAL(status(CANConStatus)),
                this, SLOT(connectionStatus(CANConStatus)));

        /*TODO add return value and checks */
        conn_p->start();
    }
    return conn_p;
}


void ConnectionWindow::loadConnections()
{
    qRegisterMetaTypeStreamOperators<CANBus>();
    qRegisterMetaTypeStreamOperators<QList<CANBus>>();

    QSettings settings;

    /* fill connection list */
    QStringList portNames = settings.value("connections/portNames").value<QStringList>();
    QStringList devTypes = settings.value("connections/typeNames").value<QStringList>();

    for(int i=0 ; i<portNames.count() ; i++)
    {
        CANConnection *conn_p = create(devTypes.at(i), portNames.at(i));
        /* add connection to model */
        connModel->add(conn_p);
    }

    if (connModel->rowCount() > 0) {
        ui->tableConnections->selectRow(0);
    }
}

void ConnectionWindow::saveConnections()
{
    QList<CANConnection*>& conns = CANConManager::getInstance()->getConnections();

    QSettings settings;
    QStringList portNames;
    QStringList devTypes;

    /* save connections */
    foreach(CANConnection* conn_p, conns)
    {
        portNames.append(conn_p->getPort());
        devTypes.append(conn_p->getType());
    }

    settings.setValue("connections/portNames", QVariant::fromValue(portNames));
    settings.setValue("connections/typeNames", QVariant::fromValue(devTypes));
}
