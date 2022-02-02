/*
    SPDX-FileCopyrightText: 2021 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include "ksignalhandler.h"
#include "kcoreaddons_debug.h"
#include <QSocketNotifier>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

class KSignalHandlerPrivate : public QObject
{
public:
    static void signalHandler(int signal);
    void handleSignal();

    QSet<int> m_signalsRegistered;
    static int signalFd[2];
    QSocketNotifier *m_handler = nullptr;

    KSignalHandler *q;
};
int KSignalHandlerPrivate::signalFd[2];

KSignalHandler::KSignalHandler()
    : d(new KSignalHandlerPrivate)
{
    d->q = this;
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, KSignalHandlerPrivate::signalFd)) {
        qCWarning(KCOREADDONS_DEBUG) << "Couldn't create a socketpair";
        return;
    }

    d->m_handler = new QSocketNotifier(KSignalHandlerPrivate::signalFd[1], QSocketNotifier::Read, this);
    connect(d->m_handler, &QSocketNotifier::activated, d.get(), &KSignalHandlerPrivate::handleSignal);
}

KSignalHandler::~KSignalHandler()
{
    for (int sig : std::as_const(d->m_signalsRegistered)) {
        signal(sig, nullptr);
    }
    close(KSignalHandlerPrivate::signalFd[0]);
    close(KSignalHandlerPrivate::signalFd[1]);
}

void KSignalHandler::watchSignal(int signalToTrack)
{
    d->m_signalsRegistered.insert(signalToTrack);
    signal(signalToTrack, KSignalHandlerPrivate::signalHandler);
}

void KSignalHandlerPrivate::signalHandler(int signal)
{
    ::write(signalFd[0], &signal, sizeof(signal));
}

void KSignalHandlerPrivate::handleSignal()
{
    m_handler->setEnabled(false);
    int signal;
    ::read(KSignalHandlerPrivate::signalFd[1], &signal, sizeof(signal));
    m_handler->setEnabled(true);

    Q_EMIT q->signalReceived(signal);
}

KSignalHandler *KSignalHandler::self()
{
    static KSignalHandler s_self;
    return &s_self;
}
