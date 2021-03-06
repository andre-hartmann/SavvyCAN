#ifndef SERIALBUSCONNECTION_H
#define SERIALBUSCONNECTION_H

#include "canconnection.h"
#include "canframemodel.h"

#include <QCanBusDevice>
#include <QTimer>

class SerialBusConnection : public CANConnection
{
    Q_OBJECT

public:
    SerialBusConnection(const QString &pluginName, const QString &portName);
    virtual ~SerialBusConnection();

protected:

    virtual void piStarted();
    virtual void piStop();
    virtual void piSetBusSettings(int pBusIdx, CANBus pBus);
    virtual bool piGetBusSettings(int pBusIdx, CANBus& pBus);
    virtual void piSuspend(bool pSuspend);
    virtual bool piSendFrame(const CANFrame&);

    void disconnectDevice();

private slots:
    void errorReceived(QCanBusDevice::CanBusError) const;
    void framesWritten(qint64 count);
    void framesReceived();
    void testConnection();

protected:
    QCanBusDevice*     mDev_p;
    QTimer             mTimer;
};


#endif // SERIALBUSCONNECTION_H
