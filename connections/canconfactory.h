#ifndef CANCONFACTORY_H
#define CANCONFACTORY_H

#include "canconnection.h"

class CanConFactory
{
public:
    static CANConnection *create(const QString &type, const QString &portName);
};

#endif // CANCONFACTORY_H
