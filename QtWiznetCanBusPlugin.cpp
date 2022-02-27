#include "WiznetCanBackend.h"

#include <QCanBusFactory>
#include <QObject>
#include <QStringList>
#include <limits>
#include <iostream>


class QtWiznetCanBusPlugin : public QObject, public QCanBusFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QCanBusFactory" FILE "plugin.json")
    Q_INTERFACES(QCanBusFactory)

public:
    QCanBusDevice* createDevice(const QString& interfaceName,
                                QString* errorMessage) const override
    {

        std::cout << "interfaceName " << interfaceName.toStdString().c_str() << std::endl;

        auto tokens = interfaceName.split(QChar(':'));
        if (tokens.size() != 2)
        {
            *errorMessage = "Invalid interface name format";
            return nullptr;
        }
        bool ok;

        QHostAddress remoteAddr(tokens[0]);
        if (remoteAddr.isNull())
        {
            *errorMessage = "Invalid remote address format";
            return nullptr;
        }

        const auto quint16max = std::numeric_limits<quint16>::max();
        auto remotePort = tokens[1].toUInt(&ok);
        if (!ok || remotePort > quint16max)
        {
            *errorMessage = "Invalid remote port format";
            return nullptr;
        }

        auto localPort = remotePort + 1;

        qDebug() << remoteAddr.toString();

        return new WiznetCanBackend(localPort, remoteAddr, remotePort);
    }
};

#include "QtWiznetCanBusPlugin.moc"
