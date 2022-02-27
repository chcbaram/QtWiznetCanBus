#include "WiznetCanBackend.h"
#include <QNetworkDatagram>
#include <QTimerEvent>

namespace
{
const unsigned long MAX_BYTES_PER_SEC = 38400;
const unsigned long OUTGOING_QUEUE_TIMEOUT_MSEC = 50;
const unsigned long MAX_BYTES_PER_TIMEOUT =
    MAX_BYTES_PER_SEC / (1000 / OUTGOING_QUEUE_TIMEOUT_MSEC);

#if 0
QCanBusFrame convert(const canfd_frame& frame)
{
    QCanBusFrame f;
    if (frame.can_id & CAN_ERR_FLAG)
        f.setFrameType(QCanBusFrame::ErrorFrame);
    else if (frame.can_id & CAN_RTR_FLAG)
        f.setFrameType(QCanBusFrame::RemoteRequestFrame);

    if (frame.can_id & CAN_EFF_FLAG)
        f.setExtendedFrameFormat(true);
    else
        f.setExtendedFrameFormat(false);

    f.setFrameId(frame.can_id);
    f.setFlexibleDataRateFormat(true);

    QByteArray a;
    a.append(reinterpret_cast<const char*>(frame.data), frame.len);
    f.setPayload(a);
    return f;
}

canfd_frame convert(const QCanBusFrame& qtFrame)
{
    canfd_frame frame;
    frame.can_id = qtFrame.frameId();

    switch (qtFrame.frameType())
    {
    case QCanBusFrame::ErrorFrame:
        frame.can_id |= CAN_ERR_FLAG;
        break;

    case QCanBusFrame::RemoteRequestFrame:
        frame.can_id |= CAN_RTR_FLAG;
        break;

    default:
        break;
    }

    if (qtFrame.hasExtendedFrameFormat())
    {
        frame.can_id |= CAN_EFF_FLAG;
    }
    else
    {
        frame.can_id &= ~(CAN_EFF_FLAG);
    }
    auto payload = qtFrame.payload();

    // frames with a larger length should be discarded by base QCanBusFrame code
    // as invalid.
    Q_ASSERT(payload.size() <= sizeof(frame.data));

    memcpy(frame.data, payload.data(), payload.size());
    frame.len = payload.size();

    return frame;
}
#endif
} // namespace

WiznetCanBackend::WiznetCanBackend(quint16 localPort,
                                           const QHostAddress& remoteAddr,
                                           quint16 remotePort)
    : localPort_(localPort), remoteAddr_(remoteAddr), remotePort_(remotePort),
      timerId_(0)
{
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
#if 1
    Q_ASSERT(!sock_.isOpen());
    if (!sock_.bind(QHostAddress::LocalHost, localPort_))
    {
        setState(QCanBusDevice::UnconnectedState);
        return false;
    }
    qDebug() << "sock_.bind(QHostAddress::LocalHost, localPort_)";

    connect(&sock_, &QAbstractSocket::readyRead, this,
            &WiznetCanBackend::dataAvailable);
    setState(QCanBusDevice::ConnectedState);
    timerId_ = startTimer(OUTGOING_QUEUE_TIMEOUT_MSEC);

    qDebug() << "connect()";

    sock_.writeDatagram("open", 5, remoteAddr_, remotePort_);
#endif
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
        const char *send_str = "send can\n";

        sock_.writeDatagram(send_str, strlen(send_str)+1,
                            remoteAddr_, remotePort_);

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
    qDebug() << "Received valid datagram, size =" << data.size();

    QVector<QCanBusFrame> newFrames;

    QCanBusFrame frame;
    frame.setFrameId(0x11);
    frame.setPayload(data);
    newFrames.push_back(frame);

    qDebug() << "Received" << newFrames.size() << "new frames";
    enqueueReceivedFrames(newFrames);
#endif
}
