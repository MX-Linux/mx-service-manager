/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2023 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
 *          MX Linux <http://mxlinux.org>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include "service.h"

#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>

#include <utility>

extern const QString init = Service::getInit();

Service::Service(QString name, bool running, QObject *parent)
    : QObject(parent),
      name {std::move(name)},
      running {running}
{
}

QString Service::getName() const
{
    return name;
}

QString Service::getInfo(const QString &name)
{
    if (init == "systemd") {
        QProcess proc;
        proc.start("service", {name, "status"});
        proc.waitForFinished();
        return proc.readAll();
    } else {
        return getInfoFromFile(name);
    }
}

bool Service::isEnabled(const QString &name)
{
    if (init == "systemd") {
        return (QProcess::execute("systemctl", {"-q", "is-enabled", name}) == 0);
    } else {
        return (QProcess::execute("/bin/bash",
                                  {"-c", R"([[ -e /etc/rc5.d/S*"$file_name" || -e /etc/rcS.d/S*"$file_name" ]])"})
                == 0);
    }
    return false;
}

QString Service::getInit()
{
    QProcess proc;
    proc.start("cat", {"/proc/1/comm"});
    proc.waitForFinished();
    return proc.readAll().trimmed();
}

bool Service::isRunning() const
{
    return running;
}

QString Service::getDescription() const
{
    if (init != "systemd") {
        QRegularExpression regex("\nShort-Description:([^\n]*)");
        QRegularExpressionMatch match = regex.match(getInfo(name));
        if (match.captured(1).isEmpty()) {
            regex.setPattern("\nDescription:\\s*(.*)\n");
            match = regex.match(getInfo(name));
        }
        if (match.hasMatch()) {
            return match.captured(1);
        }
        return {};
    } else {
        QProcess proc;
        proc.start("/bin/bash",
                   {"-c", "systemctl list-units " + name + ".service -o json-pretty | grep description | cut -d: -f2"});
        proc.waitForFinished();
        QString out = proc.readAll().trimmed();
        out = out.mid(1, out.length() - 2);
        if (out.isEmpty()) {
            proc.start("/bin/bash",
                       {"-c", "systemctl status " + name + " | awk -F' - ' 'NR == 1 { print $2 } NR > 1 { exit }'"});
            proc.waitForFinished();
            out = proc.readAll().trimmed();
        }
        if (out.isEmpty()) {
            QRegularExpression regex("\nShort-Description:([^\n]*)");
            QRegularExpressionMatch match = regex.match(getInfo(name));
            if (match.captured(1).isEmpty()) {
                regex.setPattern("\nDescription:\\s*(.*)\n");
                match = regex.match(getInfo(name));
            }
            if (match.hasMatch()) {
                return match.captured(1);
            }
        }
        return out;
    }
}

bool Service::isEnabled() const
{
    return enabled;
}

bool Service::start()
{
    if (QProcess::execute("service", {name, "start"}) == 0) {
        setRunning(true);
        return true;
    }
    return false;
}

bool Service::stop()
{
    if (QProcess::execute("service", {name, "stop"}) == 0) {
        setRunning(false);
        return true;
    }
    return false;
}

void Service::setEnabled(bool enabled)
{
    this->enabled = enabled;
}

void Service::setRunning(bool running)
{
    this->running = running;
}

QString Service::getInfoFromFile(const QString &name)
{
    QFile file("/etc/init.d/" + name);
    if (!file.exists()) {
        qDebug() << "Could not find unit file";
        QProcess proc;
        proc.start("service", {name, "status"});
        proc.waitForFinished();
        return proc.readAll();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Could not open unit file";
        QProcess proc;
        proc.start("service", {name, "status"});
        proc.waitForFinished();
        return proc.readAll();
    }
    QString info;
    bool info_header = false;
    while (!file.atEnd()) {
        QString line = file.readLine().trimmed();
        if (line.startsWith("### END INIT INFO")) {
            info_header = false;
        }
        if (info_header) {
            line.remove(0, 2);
            info.append(line + "\n");
        }
        if (line.startsWith("### BEGIN INIT INFO")) {
            info_header = true;
        }
    }
    return info;
}

bool Service::enable()
{
    if (init == "systemd") {
        QProcess::execute("systemctl", {"unmask", name});
        if (QProcess::execute("systemctl", {"enable", name}) == 0) {
            setEnabled(true);
            return true;
        }
    } else {
        QProcess::execute("update-rc.d", {name, "defaults"});
        if (QProcess::execute("update-rc.d", {name, "enable"}) == 0) {
            setEnabled(true);
            return true;
        }
    }
    return false;
}

bool Service::disable()
{
    if (init == "systemd") {
        if (QProcess::execute("systemctl", {"disable", name}) == 0) {
            QProcess::execute("systemctl", {"mask", name});
            setEnabled(false);
            return true;
        }
    } else {
        if (QProcess::execute("update-rc.d", {name, "remove"}) == 0) {
            setEnabled(false);
            return true;
        }
    }
    return false;
}
