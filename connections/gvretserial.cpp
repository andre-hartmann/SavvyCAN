#include <QObject>
#include <QDebug>
#include <QCanBusFrame>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStringBuilder>

#include "gvretserial.h"

GVRetSerial::GVRetSerial(QString portName) :
    CANConnection(typeGvret(), portName, 3, 4000, true),
    mTimer(this) /*NB: set this as parent of timer to manage it from working thread */
{
    qDebug() << "GVRetSerial()";
    debugOutput("GVRetSerial()");

    serial = NULL;
    rx_state = IDLE;
    rx_step = 0;
    gotValidated = true;
    isAutoRestart = false;

    timeBasis = 0;
    lastSystemTimeBasis = 0;
    timeAtGVRETSync = 0;

    readSettings();
}


GVRetSerial::~GVRetSerial()
{
    stop();
    qDebug() << "~GVRetSerial()";
    debugOutput("~GVRetSerial()");
}


void GVRetSerial::piStarted()
{
    connectDevice();

    /* start timer */
    connect(&mTimer, SIGNAL(timeout()), this, SLOT(handleTick()));
    mTimer.setInterval(250); //tick four times per second
    mTimer.setSingleShot(false); //keep ticking
    mTimer.start();
}


void GVRetSerial::piSuspend(bool pSuspend)
{
    /* update capSuspended */
    setCapSuspended(pSuspend);

    /* flush queue if we are suspended */
    if(isCapSuspended())
        getQueue().flush();
}


void GVRetSerial::piStop()
{
    mTimer.stop();
    disconnectDevice();
}


bool GVRetSerial::piGetBusSettings(int pBusIdx, CANBus& pBus)
{
    return getBusConfig(pBusIdx, pBus);
}


void GVRetSerial::piSetBusSettings(int pBusIdx, CANBus bus)
{
    /* sanity checks */
    if( (pBusIdx < 0) || pBusIdx >= getNumBuses())
        return;

    /* copy bus config */
    setBusConfig(pBusIdx, bus);

    qDebug() << "About to update bus " << pBusIdx << " on GVRET";
    if (pBusIdx == 0)
    {
        can0Baud = bus.getSpeed();
        can0Baud |= 0x80000000;
        if (bus.isActive())
        {
            can0Baud |= 0x40000000;
            can0Enabled = true;
        }
        else can0Enabled = false;

        if (bus.isListenOnly())
        {
            can0Baud |= 0x20000000;
            can0ListenOnly = true;
        }
        else can0ListenOnly = false;
    }
    else if (pBusIdx == 1)
    {
        can1Baud = bus.getSpeed();
        can1Baud |= 0x80000000;
        if (bus.isActive())
        {
            can1Baud |= 0x40000000;
            can1Enabled = true;
        }
        else can1Enabled = false;

        if (bus.isListenOnly())
        {
            can1Baud |= 0x20000000;
            can1ListenOnly = true;
        }
        else can1ListenOnly = false;

        if (bus.isSingleWire())
        {
            can1Baud |= 0x10000000;
            deviceSingleWireMode = 1;
        }
        else deviceSingleWireMode = 0;
    }
    else if (pBusIdx == 2)
    {
        swcanBaud = bus.getSpeed();
        swcanBaud |= 0x80000000;
        if (bus.isActive())
        {
            swcanBaud |= 0x40000000;
            swcanEnabled = true;
        }
        else swcanEnabled = false;

        if (bus.isListenOnly())
        {
            swcanBaud |= 0x20000000;
            swcanListenOnly = true;
        }
        else swcanListenOnly = false;

    }

    if (pBusIdx < 2) {
        /* update baud rates */
        QByteArray buffer;
        qDebug() << "Got signal to update bauds. 1: " << (can0Baud & 0xFFFFFFF) <<" 2: " << (can1Baud & 0xFFFFFFF);
        debugOutput("Got signal to update bauds. 1: " + QString::number((can0Baud & 0xFFFFFFF)) + " 2: " + QString::number((can1Baud & 0xFFFFFFF)));
        buffer[0] = (char)0xF1; //start of a command over serial
        buffer[1] = 5; //setup canbus
        buffer[2] = (unsigned char)(can0Baud & 0xFF); //four bytes of ID LSB first
        buffer[3] = (unsigned char)(can0Baud >> 8);
        buffer[4] = (unsigned char)(can0Baud >> 16);
        buffer[5] = (unsigned char)(can0Baud >> 24);
        buffer[6] = (unsigned char)(can1Baud & 0xFF); //four bytes of ID LSB first
        buffer[7] = (unsigned char)(can1Baud >> 8);
        buffer[8] = (unsigned char)(can1Baud >> 16);
        buffer[9] = (unsigned char)(can1Baud >> 24);
        buffer[10] = 0;
        if (serial == NULL) return;
        if (!serial->isOpen()) return;
        serial->write(buffer);
    }
    else
    {
        /* update baud rates */
        QByteArray buffer;
        qDebug() << "Got signal to update extended bus speeds SWCAN: " << swcanBaud <<" LIN1: " << lin1Baud << " LIN2: " << lin2Baud;
        debugOutput("Got signal to update extended bus speeds SWCAN: " + QString::number(swcanBaud) + " LIN1: " + QString::number(lin1Baud) + " LIN2: " + QString::number(lin2Baud));
        buffer[0] = (char)0xF1; //start of a command over serial
        buffer[1] = 14; //setup extended buses
        buffer[2] = (unsigned char)(swcanBaud & 0xFF); //four bytes of ID LSB first
        buffer[3] = (unsigned char)(swcanBaud >> 8);
        buffer[4] = (unsigned char)(swcanBaud >> 16);
        buffer[5] = (unsigned char)(swcanBaud >> 24);
        buffer[6] = (unsigned char)(lin1Baud & 0xFF); //four bytes of ID LSB first
        buffer[7] = (unsigned char)(lin1Baud >> 8);
        buffer[8] = (unsigned char)(lin1Baud >> 16);
        buffer[9] = (unsigned char)(lin1Baud >> 24);
        buffer[10] = (unsigned char)(lin2Baud & 0xFF); //four bytes of ID LSB first
        buffer[11] = (unsigned char)(lin2Baud >> 8);
        buffer[12] = (unsigned char)(lin2Baud >> 16);
        buffer[13] = (unsigned char)(lin2Baud >> 24);
        buffer[14] = 0;
        if (serial == NULL) return;
        if (!serial->isOpen()) return;
        serial->write(buffer);
    }
}


bool GVRetSerial::piSendFrame(const CANFrame& frame)
{
    QByteArray buffer;
    unsigned int c;
    int ID;

    //qDebug() << "Sending out GVRET frame with id " << frame.ID << " on bus " << frame.bus;

    framesRapid++;

    if (serial == NULL) return false;
    if (!serial->isOpen()) return false;
    //if (!isConnected) return false;

    ID = frame.ID;
    if (frame.extended) ID |= 1 << 31;

    buffer[0] = (char)0xF1; //start of a command over serial
    buffer[1] = 0; //command ID for sending a CANBUS frame
    buffer[2] = (unsigned char)(ID & 0xFF); //four bytes of ID LSB first
    buffer[3] = (unsigned char)(ID >> 8);
    buffer[4] = (unsigned char)(ID >> 16);
    buffer[5] = (unsigned char)(ID >> 24);
    buffer[6] = (unsigned char)((frame.bus) & 3);
    buffer[7] = (unsigned char)frame.len;
    for (c = 0; c < frame.len; c++)
    {
        buffer[8 + c] = frame.data[c];
    }
    buffer[8 + frame.len] = 0;

    //qDebug() << "writing " << buffer.length() << " bytes to serial port";
    debugOutput("writing " + QString::number(buffer.length()) + " bytes to serial port");
    serial->write(buffer);

    return true;
}



/****************************************************************/

void GVRetSerial::readSettings()
{
    QSettings settings;

    if (settings.value("Main/ValidateComm", true).toBool())
    {
        doValidation = true;
    }
    else doValidation = false;
}


void GVRetSerial::connectDevice()
{
    QSettings settings;

    /* disconnect device */
    if(serial)
        disconnectDevice();

    /* open new device */
    serial = new QSerialPort(QSerialPortInfo(getPort()));
    if(!serial) {
        qDebug() << "can't open serial port " << getPort();
        debugOutput("can't open serial port " + getPort());
        return;
    }

    /* configure */
    serial->setDataBits(serial->Data8);
    serial->setFlowControl(serial->HardwareControl); //this is important though
    if (!serial->open(QIODevice::ReadWrite))
    {
        qDebug() << serial->errorString();
    }
    serial->setDataTerminalReady(true); //you do need to set these or the fan gets dirty
    serial->setRequestToSend(true);


    QByteArray output;
    output.append((char)0xE7); //this puts the device into binary comm mode
    output.append((char)0xE7);

    output.append((char)0xF1);
    output.append((char)0x0C); //get number of actually implemented buses. Not implemented except on M2RET
    mNumBuses = 2; //the proper number if C/12 is not implemented

    output.append((char)0xF1); //signal we want to issue a command
    output.append((char)0x06); //request canbus stats from the board

    output.append((char)0xF1); //another command to the GVRET
    output.append((char)0x07); //request device information

    /*output.append((char)0xF1);
    output.append((char)0x08); //setting singlewire mode
    if (settings.value("Main/SingleWireMode", false).toBool())
    {
        output.append((char)0x10); //signal that we do want single wire mode
    }
    else
    {
        output.append((char)0xFF); //signal we don't want single wire mode
    }*/

    output.append((char)0xF1); //and another command
    output.append((char)0x01); //Time Sync - Not implemented until 333 but we can try

    output.append((char)0xF1); //yet another command
    output.append((char)0x09); //comm validation command

    continuousTimeSync = true;

    serial->write(output);
    QString buildDebug;
    buildDebug = "Write to serial -> ";
    foreach (int byt, output) {
        buildDebug = buildDebug % QString::number(byt, 16) % " ";
    }
    debugOutput(buildDebug);

    if(doValidation) {
        QTimer::singleShot(1000, this, SLOT(connectionTimeout()));
    }
    else {
        setStatus(CANCon::CONNECTED);
        CANConStatus stats;
        stats.conStatus = getStatus();
        stats.numHardwareBuses = mNumBuses;
        emit status(stats);
    }

    /* connect reading event */
    connect(serial, SIGNAL(readyRead()), this, SLOT(readSerialData()));
}


void GVRetSerial::disconnectDevice() {
    if (serial != NULL)
    {
        if (serial->isOpen())
        {
            serial->clear();
            serial->close();

        }
        serial->disconnect(); //disconnect all signals
        delete serial;
        serial = NULL;
    }
}


void GVRetSerial::connectionTimeout()
{
    //one second after trying to connect are we actually connected?
    //if (CANCon::NOT_CONNECTED==getStatus()) //no?
    if (!gotValidated)
    {
        //then emit the the failure signal and see if anyone cares
        qDebug() << "Failed to connect to GVRET at that com port";

        disconnectDevice();
    }
}


void GVRetSerial::readSerialData()
{
    QByteArray data = serial->readAll();
    unsigned char c;
    QString debugBuild;
    debugOutput("Got data from serial. Len = " % QString::number(data.length()));
    //qDebug() << (tr("Got data from serial. Len = %0").arg(data.length()));
    for (int i = 0; i < data.length(); i++)
    {
        c = data.at(i);
        //qDebug() << c << "    " << QString::number(c, 16) << "     " << QString(c);
        debugBuild = debugBuild % QString::number(c, 16) % " ";
        procRXChar(c);
    }
    debugOutput(debugBuild);
}

//Debugging data sent from connection window. Inject it into Comm traffic.
void GVRetSerial::debugInput(QByteArray bytes) {
   serial->write(bytes);
}

void GVRetSerial::procRXChar(unsigned char c)
{
    CANConStatus stats;
    int oldBuses;
    QByteArray output;

    switch (rx_state)
    {
    case IDLE:
        if (c == 0xF1) rx_state = GET_COMMAND;
        break;
    case GET_COMMAND:
        switch (c)
        {
        case 0: //receiving a can frame
            rx_state = BUILD_CAN_FRAME;
            rx_step = 0;
            break;
        case 1: //time sync
            rx_state = TIME_SYNC;
            rx_step = 0;
            break;
        case 2: //process a return reply for digital input states.
            rx_state = GET_DIG_INPUTS;
            rx_step = 0;
            break;
        case 3: //process a return reply for analog inputs
            rx_state = GET_ANALOG_INPUTS;
            break;
        case 4: //we set digital outputs we don't accept replies so nothing here.
            rx_state = IDLE;
            break;
        case 5: //we set canbus specs we don't accept replies.
            rx_state = IDLE;
            break;
        case 6: //get canbus parameters from GVRET
            rx_state = GET_CANBUS_PARAMS;
            rx_step = 0;
            break;
        case 7: //get device info
            rx_state = GET_DEVICE_INFO;
            rx_step = 0;
            break;
        case 9:
            gotValidated = true;
            //qDebug() << "Got validated";
            rx_state = IDLE;
            break;
        case 12:
            rx_state = GET_NUM_BUSES;
            qDebug() << "Got num buses reply";
            rx_step = 0;
            break;
        case 13:
            rx_state = GET_EXT_BUSES;
            qDebug() << "Got extended buses info reply";
            rx_step = 0;
            break;
        }
        break;
    case BUILD_CAN_FRAME:
        switch (rx_step)
        {
        case 0:
            buildFrame.timestamp = c;
            break;
        case 1:
            buildFrame.timestamp |= (uint)(c << 8);
            break;
        case 2:
            buildFrame.timestamp |= (uint)c << 16;
            break;
        case 3:
            buildFrame.timestamp |= (uint)c << 24;

            buildFrame.timestamp += timeBasis;
            if (useSystemTime)
            {
                buildFrame.timestamp = QDateTime::currentMSecsSinceEpoch() * 1000l;
            }
            break;
        case 4:
            buildFrame.ID = c;
            break;
        case 5:
            buildFrame.ID |= c << 8;
            break;
        case 6:
            buildFrame.ID |= c << 16;
            break;
        case 7:
            buildFrame.ID |= c << 24;
            if ((buildFrame.ID & 1 << 31) == 1u << 31)
            {
                buildFrame.ID &= 0x7FFFFFFF;
                buildFrame.extended = true;
            }
            else buildFrame.extended = false;
            break;
        case 8:
            buildFrame.len = c & 0xF;
            if (buildFrame.len > 8) buildFrame.len = 8;
            buildFrame.bus = (c & 0xF0) >> 4;
            break;
        default:
            if (rx_step < buildFrame.len + 9)
            {
                buildFrame.data[rx_step - 9] = c;
            }
            else
            {
                rx_state = IDLE;
                rx_step = 0;
                buildFrame.isReceived = true;

                if (!isCapSuspended())
                {
                    /* get frame from queue */
                    CANFrame* frame_p = getQueue().get();
                    if(frame_p) {
                        //qDebug() << "GVRET got frame on bus " << frame_p->bus;
                        /* copy frame */
                        *frame_p = buildFrame;
                        checkTargettedFrame(buildFrame);
                        /* enqueue frame */
                        getQueue().queue();
                    }
                    else
                        qDebug() << "can't get a frame, ERROR";

                    //take the time the frame came in and try to resync the time base.
                    //if (continuousTimeSync) txTimestampBasis = QDateTime::currentMSecsSinceEpoch() - (buildFrame.timestamp / 1000);
                }
            }
            break;
        }
        rx_step++;
        break;
    case TIME_SYNC: //gives a pretty good base guess for the proper timestamp. Can be refined when traffic starts to flow (if wanted)
        switch (rx_step)
        {
        case 0:
            buildTimeBasis = c;
            break;
        case 1:
            buildTimeBasis += ((uint32_t)c << 8);
            break;
        case 2:
            buildTimeBasis += ((uint32_t)c << 16);
            break;
        case 3:
            buildTimeBasis += ((uint32_t)c << 24);
            qDebug() << "GVRET firmware reports timestamp of " << buildTimeBasis;
            timeAtGVRETSync = QDateTime::currentMSecsSinceEpoch() * 1000;

            rebuildLocalTimeBasis();

            continuousTimeSync = false;
            rx_state = IDLE;
            break;
        }
        rx_step++;
        break;

    case GET_ANALOG_INPUTS: //get 9 bytes - 2 per analog input plus checksum
        switch (rx_step)
        {
        case 0:
            break;
        }
        rx_step++;
        break;
    case GET_DIG_INPUTS: //get two bytes. One for digital in status and one for checksum.
        switch (rx_step)
        {
        case 0:
            break;
        case 1:
            rx_state = IDLE;
            break;
        }
        rx_step++;
        break;
    case GET_CANBUS_PARAMS:
        switch (rx_step)
        {
        case 0:
            can0Enabled = (c & 0xF);
            can0ListenOnly = (c >> 4);
            break;
        case 1:
            can0Baud = c;
            break;
        case 2:
            can0Baud |= c << 8;
            break;
        case 3:
            can0Baud |= c << 16;
            break;
        case 4:
            can0Baud |= c << 24;
            break;
        case 5:
            can1Enabled = (c & 0xF);
            can1ListenOnly = (c >> 4);
            deviceSingleWireMode = (c >> 6);
            break;
        case 6:
            can1Baud = c;
            break;
        case 7:
            can1Baud |= c << 8;
            break;
        case 8:
            can1Baud |= c << 16;
            break;
        case 9:
            can1Baud |= c << 24;
            rx_state = IDLE;
            qDebug() << "Baud 0 = " << can0Baud;
            qDebug() << "Baud 1 = " << can1Baud;
            mBusData[0].mBus.setSpeed(can0Baud);
            mBusData[1].mBus.setSpeed(can1Baud);
            mBusData[0].mBus.setEnabled(can0Enabled);
            mBusData[1].mBus.setEnabled(can1Enabled);
            mBusData[0].mConfigured = true;
            mBusData[1].mConfigured = true;

            can0Baud |= 0x80000000;
            if (can0Enabled) can0Baud |= 0x40000000;
            if (can0ListenOnly) can0Baud |= 0x20000000;

            can1Baud |= 0x80000000;
            if (can1Enabled) can1Baud |= 0x40000000;
            if (can1ListenOnly) can1Baud |= 0x20000000;
            if (deviceSingleWireMode > 0) can1Baud |= 0x10000000;

            setStatus(CANCon::CONNECTED);
            stats.conStatus = getStatus();
            stats.numHardwareBuses = mNumBuses;
            emit status(stats);

            int can0Status = 0x78; //updating everything we can update
            int can1Status = 0x78;
            if (can0Enabled) can0Status +=1;
            if (can0ListenOnly) can0Status += 4;
            if (can1Enabled) can1Status += 1;
            if (deviceSingleWireMode > 0) can1Status += 2;
            if (can1ListenOnly) can1Status += 4;
            //emit busStatus(busBase, can0Baud & 0xFFFFF, can0Status);
            //emit busStatus(busBase + 1, can1Baud & 0xFFFFF, can1Status);
            break;
        }
        rx_step++;
        break;
    case GET_DEVICE_INFO:
        switch (rx_step)
        {
        case 0:
            deviceBuildNum = c;
            break;
        case 1:
            deviceBuildNum |= c << 8;
            break;
        case 2:
            break; //don't care about eeprom version
        case 3:
            break; //don't care about file type
        case 4:
            break; //don't care about whether it auto logs or not
        case 5:
            deviceSingleWireMode = c;
            rx_state = IDLE;
            qDebug() << "build num: " << deviceBuildNum;
            qDebug() << "single wire can: " << deviceSingleWireMode;
            emit deviceInfo(deviceBuildNum, deviceSingleWireMode);
            break;
        }
        rx_step++;
        break;
    case SET_DIG_OUTPUTS:
        rx_state = IDLE;
        break;
    case SETUP_CANBUS:
        rx_state = IDLE;
        break;
    case SET_SINGLEWIRE_MODE:
        rx_state = IDLE;
        break;
    case GET_NUM_BUSES:
        oldBuses = mNumBuses;
        mNumBuses = c;
        rx_state = IDLE;
        qDebug() << "Get number of buses = " << mNumBuses;
        stats.conStatus = getStatus();
        stats.numHardwareBuses = mNumBuses;
        mBusData.resize(mNumBuses);
        if (mNumBuses > oldBuses)
        {
            for (int i = oldBuses; i < mNumBuses; i++)
            {
                mBusData[i].mConfigured = true;
                mBusData[i].mBus = mBusData[0].mBus;
            }
        }

        output.append((char)0xF1); //start a new command
        output.append((char)13); //get extended buses
        serial->write(output);

        emit status(stats);
        break;
    case GET_EXT_BUSES:
        switch (rx_step)
        {
        case 0:
            swcanEnabled = (c & 0xF);
            swcanListenOnly = (c >> 4);
            break;
        case 1:
            swcanBaud = c;
            break;
        case 2:
            swcanBaud |= c << 8;
            break;
        case 3:
            swcanBaud |= c << 16;
            break;
        case 4:
            swcanBaud |= c << 24;
            break;
        case 5:
            lin1Enabled = (c & 0xF);
            break;
        case 6:
            lin1Baud = c;
            break;
        case 7:
            lin1Baud |= c << 8;
            break;
        case 8:
            lin1Baud |= c << 16;
            break;
        case 9:
            lin1Baud |= c << 24;
        case 10:
            lin2Enabled = (c & 0xF);
            break;
        case 11:
            lin2Baud = c;
            break;
        case 12:
            lin2Baud |= c << 8;
            break;
        case 13:
            lin2Baud |= c << 16;
            break;
        case 14:
            lin2Baud |= c << 24;
            rx_state = IDLE;
            qDebug() << "SWCAN Baud = " << swcanBaud;
            qDebug() << "LIN1 Baud = " << lin1Baud;
            qDebug() << "LIN2 Baud = " << lin2Baud;
            mBusData[2].mBus.setSpeed(swcanBaud);
            mBusData[2].mBus.setEnabled(swcanEnabled);

            setStatus(CANCon::CONNECTED);
            stats.conStatus = getStatus();
            stats.numHardwareBuses = mNumBuses;
            emit status(stats);
            break;
        }
        rx_step++;
        break;
    }
}

void GVRetSerial::rebuildLocalTimeBasis()
{
    qDebug() << "Rebuilding GVRET time base. GVRET local base = " << buildTimeBasis;

    /*
      our time basis is the value we have to modulate the main system basis by in order
      to sync the GVRET timestamps to the rest of the system.
      The rest of the system uses CANConManager::getInstance()->getTimeBasis as the basis.
      GVRET returns to us the current time since boot up in microseconds.
      timeAtGVRETSync stores the "system" timestamp when the GVRET timestamp was retrieved.
    */
    lastSystemTimeBasis = CANConManager::getInstance()->getTimeBasis();
    int64_t systemDelta = timeAtGVRETSync - lastSystemTimeBasis;
    int32_t localDelta = buildTimeBasis - systemDelta;
    timeBasis = -localDelta;
}

void GVRetSerial::handleTick()
{
    if (lastSystemTimeBasis != CANConManager::getInstance()->getTimeBasis()) rebuildLocalTimeBasis();
    //qDebug() << "Tick!";
/*
    if( CANCon::CONNECTED == getStatus() )
    {
        if (!gotValidated && doValidation)
        {
            if (serial == NULL) return;
            if (serial->isOpen()) //if it's still false we have a problem...
            {
                qDebug() << "Comm validation failed. ";

                setStatus(CANCon::NOT_CONNECTED);
                emit status(getStatus());

                disconnectDevice(); //start by stopping everything.
                //Then wait 500ms and restart the connection automatically
                QTimer::singleShot(500, this, SLOT(connectDevice()));
                return;
            }
        }
    }
*/
    if (doValidation && serial && serial->isOpen()) sendCommValidation();
}


void GVRetSerial::sendCommValidation()
{
    QByteArray output;

    gotValidated = false;
    output.append((char)0xF1); //another command to the GVRET
    output.append((char)0x09); //request a reply to get validation
    //send it twice for good measure.
    output.append((char)0xF1); //another command to the GVRET
    output.append((char)0x09); //request a reply to get validation

    serial->write(output);
}



