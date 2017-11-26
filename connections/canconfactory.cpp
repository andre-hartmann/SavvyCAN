#include <QString>
#include "canconfactory.h"
#include "serialbusconnection.h"
#include "gvretserial.h"

using namespace CANCon;

CANConnection *CanConFactory::create(const QString &type, const QString &portName)
{
    if (type == CANConnection::typeGvret())
        return new GVRetSerial(portName);

    if (type == CANConnection::typeKvaser())
        ; // return new KvaserConnection(portName);

    return new SerialBusConnection(type, portName);
}
