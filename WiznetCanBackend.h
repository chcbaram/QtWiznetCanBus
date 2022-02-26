#ifndef WIZNET_CAN_BACKEND_H_
#define WIZNET_CAN_BACKEND_H_

#include <QCanBusDevice>
#include <QUdpSocket>

class WiznetCanBackend : public QCanBusDevice
{
    Q_OBJECT
public:
    WiznetCanBackend(quint16 localPort, const QHostAddress& remoteAddr,
                     quint16 remotePort);

    bool writeFrame(const QCanBusFrame& frame) override;
    QString interpretErrorFrame(const QCanBusFrame& errorFrame) override;

protected:
    bool open() override;
    void close() override;
    void timerEvent(QTimerEvent*) override;

private:
    quint16 localPort_;
    QHostAddress remoteAddr_;
    quint16 remotePort_;
    QUdpSocket sock_;
    int timerId_;
    void handlePacket(const QByteArray& data);

private slots:
    void dataAvailable();
//    void outQueueTimer();
};

#endif 
