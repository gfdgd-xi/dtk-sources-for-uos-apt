// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "dfiledragclient.h"
#include "private/dfiledragcommon_p.h"

#include <DObjectPrivate>

#include <QMimeData>
#include <QUrl>
#include <QUuid>
#include <QSharedPointer>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>

DGUI_BEGIN_NAMESPACE

class DDndClientSignalRelay : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    void progressChanged(QString uuid, int progress);
    void stateChanged(QString uuid, int state);
    void serverDestroyed(QString uuid);

private:
    static QWeakPointer<DDndClientSignalRelay> relayref;
    friend class DFileDragClient;
};

class DFileDragClientPrivate : DCORE_NAMESPACE::DObjectPrivate
{
    explicit DFileDragClientPrivate(DFileDragClient *q)
        : DCORE_NAMESPACE::DObjectPrivate(q) {}
    QUuid uuid;
    QString service;
    QSharedPointer<QDBusInterface> iface;
    QSharedPointer<DDndClientSignalRelay> relay;

    static QHash<QString, DFileDragClient*> connectionmap;
    static QHash<QString, QWeakPointer<QDBusInterface>> ifacemap;

    D_DECLARE_PUBLIC(DFileDragClient)
    friend class DDndClientSignalRelay;
};

QHash<QString, DFileDragClient*> DFileDragClientPrivate::connectionmap;
QHash<QString, QWeakPointer<QDBusInterface>> DFileDragClientPrivate::ifacemap;
QWeakPointer<DDndClientSignalRelay> DDndClientSignalRelay::relayref;

void DDndClientSignalRelay::progressChanged(QString uuid, int progress)
{
    if (DFileDragClientPrivate::connectionmap.contains(uuid)) {
        Q_EMIT DFileDragClientPrivate::connectionmap[uuid]->progressChanged(progress);
    }
}

void DDndClientSignalRelay::stateChanged(QString uuid, int state)
{
    if (DFileDragClientPrivate::connectionmap.contains(uuid)) {
        Q_EMIT DFileDragClientPrivate::connectionmap[uuid]->stateChanged(static_cast<DFileDragState>(state));
    }
}

void DDndClientSignalRelay::serverDestroyed(QString uuid)
{
    if (DFileDragClientPrivate::connectionmap.contains(uuid)) {
        DFileDragClientPrivate::connectionmap[uuid]->deleteLater();
        DFileDragClientPrivate::connectionmap.remove(uuid);
    }
}

/*!
  \class Dtk::Gui::DFileDragClient
  \inmodule dtkgui
  \brief ??????????????????????????????????????????????????????.
 */

/*!
  \fn void DFileDragClient::progressChanged(int progress)
  \a progress ????????????
  \brief ??????????????????????????????????????????.
 */

/*!
  \fn void DFileDragClient::stateChanged(DFileDragState state)
  \a state ?????????????????????
  \brief ??????????????????????????????????????????.
 */

/*!
  \fn void DFileDragClient::serverDestroyed()
  \brief ?????????????????????????????????????????????.

  \note DFileDragClient ?????????????????????(deletelater)???????????????????????? new ????????? DFileDragClient
 */

DFileDragClient::DFileDragClient(const QMimeData *data, QObject *parent)
    : QObject(parent)
    , DCORE_NAMESPACE::DObject(*new DFileDragClientPrivate(this))
{
    D_D(DFileDragClient);
    Q_ASSERT(checkMimeData(data));

    d->service = data->data(DND_MIME_SERVICE);
    d->uuid = QUuid(data->data(DND_MIME_UUID));
    d->connectionmap[d->uuid.toString()] = this;

    if (DDndClientSignalRelay::relayref.isNull()) {
        d->relay = QSharedPointer<DDndClientSignalRelay>(new DDndClientSignalRelay);
        DDndClientSignalRelay::relayref = d->relay.toWeakRef();
    } else {
        d->relay = DDndClientSignalRelay::relayref.toStrongRef();
    }

    if (!DFileDragClientPrivate::ifacemap.contains(d->service)) {
        QDBusConnection sessionBus(QDBusConnection::sessionBus());
        d->iface = QSharedPointer<QDBusInterface>(new QDBusInterface(d->service, DND_OBJPATH, DND_INTERFACE, sessionBus), [d](QDBusInterface* intf){
            QDBusConnection sessionBus(QDBusConnection::sessionBus());
            sessionBus.disconnect(d->service, DND_OBJPATH, DND_INTERFACE, "progressChanged", "si", d->relay.data(), SLOT(progressChanged(QString, int)));
            sessionBus.disconnect(d->service, DND_OBJPATH, DND_INTERFACE, "stateChanged", "si", d->relay.data(), SLOT(stateChanged(QString, int)));
            sessionBus.disconnect(d->service, DND_OBJPATH, DND_INTERFACE, "serverDestroyed", "s", d->relay.data(), SLOT(serverDestroyed(QString)));
            intf->deleteLater();
            DFileDragClientPrivate::ifacemap.remove(d->service);
        });
        DFileDragClientPrivate::ifacemap[d->service] = d->iface.toWeakRef();
        sessionBus.connect(d->service, DND_OBJPATH, DND_INTERFACE, "progressChanged", "si", d->relay.data(), SLOT(progressChanged(QString, int)));
        sessionBus.connect(d->service, DND_OBJPATH, DND_INTERFACE, "stateChanged", "si", d->relay.data(), SLOT(stateChanged(QString, int)));
        sessionBus.connect(d->service, DND_OBJPATH, DND_INTERFACE, "serverDestroyed", "s", d->relay.data(), SLOT(serverDestroyed(QString)));
    } else {
        d->iface = DFileDragClientPrivate::ifacemap[d->service].toStrongRef();
    }

}

/*!
  \brief DFileDragClient::progress
  \return ???????????????????????????
 */
int DFileDragClient::progress() const
{
    D_D(const DFileDragClient);

    return QDBusReply<int>(d->iface->call("progress", d->uuid.toString())).value();
}

/*!
  \brief DFileDragClient::state
  \return ??????????????????,??? DFileDragState
 */
DFileDragState DFileDragClient::state() const
{
    D_D(const DFileDragClient);

    return static_cast<DFileDragState>(QDBusReply<int>(d->iface->call("state", d->uuid.toString())).value());
}

/*!
  \brief DFileDragClient::checkMimeData
  \a data
  \return ?????? DND_MIME_PID ???????????????????????? true??????????????? false
  \note ???????????????????????????????????????dropEvent(QDropEvent *event)????????????????????? event->mimeData() ????????? DFileDrag
 */
bool DFileDragClient::checkMimeData(const QMimeData *data)
{
    return data->hasFormat(DND_MIME_SERVICE) && data->hasFormat(DND_MIME_PID);
}

/*!
  \brief DFileDragClient::setTargetData
  \a data ??????????????????data,????????????????????????????????????dbus???????????????????????????
  \a key
  \a value
  \note ???????????????????????????????????????
 */
void DFileDragClient::setTargetData(const QMimeData *data, QString key, QVariant value)
{
    Q_ASSERT(checkMimeData(data));
    QString service(data->data(DND_MIME_SERVICE));
    QString uuid(data->data(DND_MIME_UUID));
    QDBusInterface iface(service, DND_OBJPATH, DND_INTERFACE, QDBusConnection::sessionBus());

    QDBusReply<uint> pid = QDBusConnection::sessionBus().interface()->servicePid(service);
    if (QString::number(pid).toUtf8() != data->data(DND_MIME_PID)) {
        return;
    }
    iface.call("setData", uuid, key, value.toString());
}

/*!
  \brief DFileDragClient::setTargetUrl
  \a data
  \a url
  \note ???????????????????????????????????????
 */
void DFileDragClient::setTargetUrl(const QMimeData *data, QUrl url)
{
    setTargetData(data, DND_TARGET_URL_KEY, QVariant::fromValue(url.toString()));
}

DGUI_END_NAMESPACE

#include "dfiledragclient.moc"
