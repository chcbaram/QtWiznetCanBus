#include "WiznetCanBackend.h"
#include <QNetworkDatagram>
#include <QTimerEvent>
#include <QDateTime>


namespace
{
const unsigned long MAX_BYTES_PER_SEC = 38400;
const unsigned long OUTGOING_QUEUE_TIMEOUT_MSEC = 50;
const unsigned long MAX_BYTES_PER_TIMEOUT =
    MAX_BYTES_PER_SEC / (1000 / OUTGOING_QUEUE_TIMEOUT_MSEC);

#if 1
QCanBusFrame convert(const can_msg_t& frame)
{
    QCanBusFrame f;

    if (frame.frame_type == CAN_FRAME_TYPE_ERROR)
        f.setFrameType(QCanBusFrame::ErrorFrame);
    else if (frame.frame_type & CAN_FRAME_TYPE_REMOTE)
        f.setFrameType(QCanBusFrame::RemoteRequestFrame);
    else
        f.setFrameType(QCanBusFrame::DataFrame);


    if (frame.id_type == CAN_EXT)
        f.setExtendedFrameFormat(true);
    else
        f.setExtendedFrameFormat(false);

    f.setFrameId(frame.id);
    f.setFlexibleDataRateFormat(false);

    QByteArray a;
    a.append(reinterpret_cast<const char*>(frame.data), frame.length);
    f.setPayload(a);
    return f;
}

can_msg_t convert(const QCanBusFrame& qtFrame)
{
    can_msg_t frame;
    frame.id = qtFrame.frameId();


    switch (qtFrame.frameType())
    {
    case QCanBusFrame::ErrorFrame:
        frame.frame_type = CAN_FRAME_TYPE_ERROR;
        break;

    case QCanBusFrame::RemoteRequestFrame:
        frame.frame_type = CAN_FRAME_TYPE_REMOTE;
        break;

    default:
        frame.frame_type = CAN_FRAME_TYPE_DATA;
        break;
    }


    if (qtFrame.hasExtendedFrameFormat())
    {
        frame.id_type = CAN_EXT;
    }
    else
    {
        frame.id_type = CAN_STD;
    }
    auto payload = qtFrame.payload();

    // frames with a larger length should be discarded by base QCanBusFrame code
    // as invalid.
    Q_ASSERT(payload.size() <= (int)sizeof(frame.data));

    memcpy(frame.data, payload.data(), payload.size());
    frame.length = payload.size();

    return frame;
}
#endif

} // namespace


uint32_t canDriverAvailable(void *p_arg) { return ((WiznetCanBackend *)p_arg)->canDriverAvailable(); }
bool     canDriverFlush(void *p_arg)     { return ((WiznetCanBackend *)p_arg)->canDriverFlush(); }
uint8_t  canDriverRead(void *p_arg)      { return ((WiznetCanBackend *)p_arg)->canDriverRead(); }
uint32_t canDriverWrite(void *p_arg, uint8_t *p_data, uint32_t length) { return ((WiznetCanBackend *)p_arg)->canDriverWrite(p_data, length); }



QList<QCanBusDeviceInfo> WiznetCanBackend::interfaces()
{
    QList<QCanBusDeviceInfo> result;
    result.append(createDeviceInfo(QStringLiteral("127.0.0.1:30000")));
    result.append(createDeviceInfo(QStringLiteral("172.30.1.54:30000")));
    return result;
}


WiznetCanBackend::WiznetCanBackend(quint16 localPort,
                                           const QHostAddress& remoteAddr,
                                           quint16 remotePort)
    : localPort_(localPort), remoteAddr_(remoteAddr), remotePort_(remotePort),
      timerId_(0)
{
    cmd_can_driver.p_arg = (void *)this;
    cmd_can_driver.available = ::canDriverAvailable;
    cmd_can_driver.flush = ::canDriverFlush;
    cmd_can_driver.read = ::canDriverRead;
    cmd_can_driver.write = ::canDriverWrite;

    cmdCanInit(&cmd_can, &cmd_can_driver);
    cmdCanOpen(&cmd_can);
    qbufferCreate(&cmd_can_q, cmd_can_buf, 512*1024);

    offset_time = -1;
}

bool WiznetCanBackend::writeFrame(const QCanBusFrame& frame)
{
    enqueueOutgoingFrame(frame);
    return true;
}

QString WiznetCanBackend::interpretErrorFrame(const QCanBusFrame&)
{
    return "Error frame received";
}

bool WiznetCanBackend::open()
{
    Q_ASSERT(!sock_.isOpen());
    if (!sock_.bind(QHostAddress::AnyIPv4, localPort_))
    {
        setState(QCanBusDevice::UnconnectedState);
        return false;
    }
    qDebug() << "sock_.bind(QHostAddress::LocalHost, localPort_)";

    connect(&sock_, &QAbstractSocket::readyRead, this,
            &WiznetCanBackend::dataAvailable);
    setState(QCanBusDevice::ConnectedState);
    timerId_ = startTimer(OUTGOING_QUEUE_TIMEOUT_MSEC);

    sock_.connectToHost(remoteAddr_,remotePort_);

    qDebug() << "connect()";


    cmdCanSendType(&cmd_can, PKT_TYPE_PING, NULL, 0);

    return true;
}

void WiznetCanBackend::close()
{
    Q_ASSERT(sock_.isOpen());
    disconnect(&sock_);
    sock_.close();
    killTimer(timerId_);
    timerId_ = 0;
}

void WiznetCanBackend::dataAvailable()
{
    while (sock_.hasPendingDatagrams())
    {
        auto dgram = sock_.receiveDatagram();
        if (!dgram.isValid())
        {
            qDebug() << "Received invalid datagram";
            continue;
        }
        if (dgram.senderAddress() != remoteAddr_ ||
            dgram.senderPort() != remotePort_)
        {
            qDebug() << "Received datagram from"
                     << dgram.senderAddress().toString() << ':'
                     << dgram.senderPort() << ", expected"
                     << remoteAddr_.toString() << ':' << remotePort_;
            continue;
        }
        handlePacket(dgram.data());
    }
}

void WiznetCanBackend::timerEvent(QTimerEvent* ev)
{
    Q_ASSERT(ev->timerId() == timerId_);
    //qDebug() << "WiznetCanBackend output timer fired";

    auto f = dequeueOutgoingFrame();
    // send every CAN frame as a separate Cannelloni packet. CAN frames can
    // only be at most 64 bytes in length (in case of CANFD), so let's give the
    // Cannelloni headers enough space...
    // we theoretically could stuff more frames into one Cannelloni packet, but
    // datagrams are generally meant to be small in order not to be fragmented.
    qint64 framesSent = 0;
    unsigned long totalBytes = 0;
    while (f.isValid() && totalBytes < MAX_BYTES_PER_TIMEOUT)
    {
#if 0
        auto rawFrame = convert(f);
        std::list<canfd_frame*> frames = {&rawFrame};
        uint8_t buf[128];
        auto end = buildPacket(sizeof(buf), buf, frames, 0, [](auto, auto) {
            // the packet should not overflow : the buffer is supposed to be
            // large enough to handle stuffing a CAN frame inside it. if that's
            // not the case, there is a bug or some other problem.
            Q_ASSERT(0 && "buildPacket() overflow callback called");
        });
        sock_.writeDatagram(reinterpret_cast<char*>(buf), end - buf,
                            remoteAddr_, remotePort_);

        f = dequeueOutgoingFrame();
        ++framesSent;
        totalBytes += (end - buf);
#else
        //const char *send_str = "send can\n";
        //sock_.writeDatagram(send_str, strlen(send_str)+1,remoteAddr_, remotePort_);

        can_msg_t can_msg;

        can_msg = convert(f);
        cmdCanSendType(&cmd_can, PKT_TYPE_CAN, (uint8_t *)&can_msg, sizeof(can_msg));

        f = dequeueOutgoingFrame();
        ++framesSent;
#endif
    }
    if (framesSent)
    {
        // TODO : this should really be emitted from the socket's
        // bytesWritten()...
        emit framesWritten(framesSent);
    }
    //qDebug() << "Wrote" << totalBytes << "bytes and" << framesSent << "frames";
}

void WiznetCanBackend::handlePacket(const QByteArray& data)
{
#if 0
    qDebug() << "Received valid datagram, size =" << data.size();
    auto allocator = []() {
        return new canfd_frame; // FIXME: preallocate some buffer for frames
    };

    QVector<QCanBusFrame> newFrames;
    auto receiver = [&](canfd_frame* frame, bool) {
        newFrames.push_back(convert(*frame));
        delete frame;
    };

    parseFrames(static_cast<uint16_t>(data.length()),
                reinterpret_cast<const uint8_t*>(data.constData()), allocator,
                receiver);

    qDebug() << "Received" << newFrames.size() << "new frames";
    enqueueReceivedFrames(newFrames);
#else
    //qDebug() << "Received valid datagram, size =" << data.size();

    qbufferWrite(&cmd_can_q, (uint8_t *)data.data(), data.size());

    while(qbufferAvailable(&cmd_can_q) > 0)
    {
        if (cmdCanReceivePacket(&cmd_can) == true)
        {
            if (cmd_can.rx_packet.type == PKT_TYPE_CAN)
            {
                QVector<QCanBusFrame> newFrames;

                QCanBusFrame frame;
                can_msg_t can_msg;

                memcpy(&can_msg, cmd_can.rx_packet.data, sizeof(can_msg));


                QByteArray frame_data = QByteArray(reinterpret_cast<char *>(can_msg.data), can_msg.length);
                QDateTime current_time = QDateTime::currentDateTime();

                if (offset_time < 0)
                {
                    offset_time = current_time.currentMSecsSinceEpoch();
                }
                qint64 time_msec = current_time.currentMSecsSinceEpoch() - offset_time;

                frame.setTimeStamp(QCanBusFrame::TimeStamp(time_msec/1000, (time_msec%1000)*1000));
                frame.setFrameId(can_msg.id);
                frame.setPayload(frame_data);
                newFrames.push_back(frame);
                enqueueReceivedFrames(newFrames);
            }
        }
    }
#endif
}

uint32_t WiznetCanBackend::canDriverAvailable(void)
{
    return qbufferAvailable(&cmd_can_q);
}

bool WiznetCanBackend::canDriverFlush(void)
{
    qbufferFlush(&cmd_can_q);
    return true;
}

uint8_t WiznetCanBackend::canDriverRead(void)
{
    uint8_t ret;

    qbufferRead(&cmd_can_q, &ret, 1);
    return ret;
}

uint32_t WiznetCanBackend::canDriverWrite(uint8_t *p_data, uint32_t length)
{
    uint32_t ret = 0;

    ret = sock_.writeDatagram((const char *)p_data, length, remoteAddr_, remotePort_);

    return ret;
}

