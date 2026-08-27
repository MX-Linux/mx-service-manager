#define QT_USE_QSTRINGBUILDER
#include "cmd.h"

#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QMetaObject>

#include <unistd.h>

Cmd::Cmd(QObject *parent)
    : QProcess(parent),
      elevate {QFile::exists("/usr/bin/pkexec") ? "/usr/bin/pkexec" : "/usr/bin/gksu"},
      helper {QStringLiteral(HELPER_PATH)}
{
}

QString Cmd::getOut(const QString &cmd, bool quiet, bool waitForFinish)
{
    run(cmd, quiet, waitForFinish);
    return readAll();
}

QString Cmd::getOutAsRoot(const QStringList &helperArgs, bool quiet, bool waitForFinish)
{
    runAsRoot(helperArgs, quiet, waitForFinish);
    return readAll();
}

bool Cmd::run(const QString &cmd, bool quiet, bool waitForFinish)
{
    if (state() != QProcess::NotRunning) {
        qDebug() << "Process already running:" << program() << arguments();
        return false;
    }
    if (!quiet) {
        qDebug().noquote() << cmd;
    }
    QEventLoop loop;
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    start(QStringLiteral("/bin/bash"), {QStringLiteral("-c"), cmd});
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
    if (getuid() != 0
        && (exitCode() == EXIT_CODE_PERMISSION_DENIED || exitCode() == EXIT_CODE_COMMAND_NOT_FOUND)) {
        // pkexec exits 126 when the user dismisses/fails the auth dialog and 127 when it
        // can't run the helper at all (also what our own helper returns for a rejected
        // command, relayed verbatim through a successful pkexec run). Either way, elevation
        // itself didn't work, so there's no path forward for any further privileged
        // operation -- unlike a command that ran with valid elevation but failed on its own
        // merits (e.g. `systemctl status` on an inactive unit returns a nonzero exit code
        // while still printing valid data to stdout), which is left to the caller's own
        // per-action error handling instead.
        // Only peek at stderr here, never consume stdout: getOutAsRoot() still needs to
        // readAll() it afterwards.
        qWarning().noquote() << "Elevation failed for" << program() << arguments()
                             << "exitCode:" << exitCode()
                             << "stderr:" << QString::fromUtf8(readAllStandardError()).trimmed();
        handleElevationError();
    }
    emit done();
    return (exitStatus() == QProcess::NormalExit && exitCode() == 0);
}

void Cmd::handleElevationError()
{
    // runAsRoot() can be called from a background thread (e.g. QtConcurrent service
    // discovery), but QMessageBox must only be created on the GUI thread. Compute the
    // translated text here (translation lookup itself is thread-safe) and marshal the
    // actual dialog/quit onto the main thread; QMetaObject::invokeMethod runs it
    // synchronously if we're already there.
    const QString title = tr("Administrator Access Required");
    const QString message = tr("This operation requires administrator privileges. Please restart the application "
                                "and enter your password when prompted.");
    QMetaObject::invokeMethod(qApp, [title, message] {
        QMessageBox::critical(nullptr, title, message);
        exit(EXIT_FAILURE);
    });
}
