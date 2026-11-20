#define QT_USE_QSTRINGBUILDER
#include "cmd.h"

#include <QDebug>
#include <QEventLoop>
#include <QFile>

#include <unistd.h>

Cmd::Cmd(QObject *parent)
    : QProcess(parent),
      elevate {QFile::exists("/usr/bin/pkexec") ? "/usr/bin/pkexec" : "/usr/bin/gksu"},
      helper {QStringLiteral(HELPER_PATH)}
{
}

QString Cmd::getOut(const QStringList &command, bool quiet, bool waitForFinish)
{
    run(command, quiet, waitForFinish);
    return readAll();
}

QString Cmd::getOutAsRoot(const QStringList &helperArgs, bool quiet, bool waitForFinish)
{
    runAsRoot(helperArgs, quiet, waitForFinish);
    return readAll();
}

bool Cmd::run(const QStringList &command, bool quiet, bool waitForFinish)
{
    if (state() != QProcess::NotRunning) {
        qDebug() << "Process already running:" << program() << arguments();
        return false;
    }
    if (!quiet) {
        qDebug().noquote() << command;
    }
    QEventLoop loop;
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    start(command.first(), command.mid(1));
    if (!waitForFinish) {
        loop.exec();
    } else {
        waitForFinished();
    }
    emit done();
    return (exitStatus() == QProcess::NormalExit && exitCode() == 0);
}

bool Cmd::runAsRoot(const QStringList &helperArgs, bool quiet, bool waitForFinish)
{
    if (state() != QProcess::NotRunning) {
        qDebug() << "Process already running:" << program() << arguments();
        return false;
    }
    if (!quiet) {
        qDebug().noquote() << helperArgs.join(QLatin1Char(' '));
    }
    QEventLoop loop;
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    if (getuid() != 0) {
        start(elevate, QStringList{helper} + helperArgs);
    } else {
        start(helper, helperArgs);
    }
    if (!waitForFinish) {
        loop.exec();
    } else {
        waitForFinished();
    }
    if (elevationUnavailable()) {
        qWarning().noquote() << "Elevation failed for" << program() << arguments()
                             << "exitCode:" << exitCode()
                             << "stderr:" << QString::fromUtf8(readAllStandardError()).trimmed();
    }
    emit done();
    return (exitStatus() == QProcess::NormalExit && exitCode() == 0);
}

bool Cmd::elevationUnavailable() const
{
    return program() == elevate && exitStatus() == QProcess::NormalExit
           && (exitCode() == EXIT_CODE_PERMISSION_DENIED || exitCode() == EXIT_CODE_COMMAND_NOT_FOUND);
}
