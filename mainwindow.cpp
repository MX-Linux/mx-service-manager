/**********************************************************************
 *  mainwindow.cpp
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
#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QScrollBar>
#include <QTextStream>
#include <QTimer>

#include "about.h"
#include "service.h"
#include <chrono>

using namespace std::chrono_literals;

extern const QString init;

MainWindow::MainWindow(QWidget *parent)
    : QDialog(parent),
      ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    setGeneralConnections();

    QSize size = this->size();
    if (settings.contains(QStringLiteral("geometry"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
        if (this->isMaximized()) { // add option to resize if maximized
            this->resize(size);
            centerWindow();
        }
    }
    if (init != "systemd" && !init.startsWith("init")) { // can be "init(mxlinux)" when running in WSL for example
        QMessageBox::warning(
            this, tr("Error"),
            tr("Could not determine the init system. This program is supposed to run either with systemd or sysvinit")
                + "\nINIT:" + init);
    }
    QPalette palette = ui->listServices->palette();
    defaultForeground = palette.color(QPalette::Text);
    ui->listServices->addItem(tr("Loading..."));

    dependTargets
        = cmd.getCmdOut("grep --no-filename \"TARGETS = \" /etc/init.d/.depend.start /etc/init.d/.depend.boot |  "
                        "sed  -e ':a;N;$!ba;s/\\n/ /' -e 's/TARGETS = //g'",
                        true)
              .split(" ");

    QTimer::singleShot(0, this, [this] {
        QTimer timer;
        timer.start(300ms);
        connect(&timer, &QTimer::timeout, this, [this] {
            static auto i = 0;
            (i % 2 == 0) ? ui->labelCount->setText(tr("Loading...")) : ui->labelCount->clear();
            ++i;
        });
        listServices();
        displayServices();
        ui->listServices->setFocus();
    });
    connect(ui->listServices, &QListWidget::itemEntered, this, [this](QListWidgetItem *item) {
        if (item->data(Qt::UserRole).value<Service *>()) {
            if (!item->toolTip().isEmpty()) {
                return;
            }
            ui->lineSearch->blockSignals(true);
            item->setToolTip(item->data(Qt::UserRole).value<Service *>()->getDescription());
            ui->lineSearch->blockSignals(false);
        }
    });
}

MainWindow::~MainWindow()
{
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    delete ui;
}

void MainWindow::centerWindow()
{
    QRect screenGeometry = QApplication::primaryScreen()->geometry();
    int x = (screenGeometry.width() - this->width()) / 2;
    int y = (screenGeometry.height() - this->height()) / 2;
    this->move(x, y);
}

void MainWindow::cmdStart()
{
    setCursor(QCursor(Qt::BusyCursor));
}

void MainWindow::itemUpdated()
{
    blockSignals(true);
    displayServices();
    blockSignals(false);
}

void MainWindow::onSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current) {
        return;
    }
    ui->textBrowser->setText(Service::getInfo(current->text()));
    bool running = current->data(Qt::UserRole).value<Service *>()->isRunning();
    bool enabled = current->data(Qt::UserRole).value<Service *>()->isEnabled();
    if (running) {
        ui->pushStartStop->setText(tr("&Stop"));
        ui->pushStartStop->setIcon(QIcon::fromTheme("stop"));
        current->setForeground(runningColor);
    } else {
        ui->pushStartStop->setIcon(QIcon::fromTheme("start"));
        ui->pushStartStop->setText(tr("S&tart"));
        if (!enabled) {
            current->setForeground(defaultForeground);
        }
    }
    if (enabled) {
        ui->pushEnableDisable->setText(tr("&Disable at boot"));
        ui->pushEnableDisable->setIcon(QIcon::fromTheme("stop"));
        if (!running) {
            current->setForeground(enabledColor);
        }
    } else {
        ui->pushEnableDisable->setIcon(QIcon::fromTheme("start"));
        ui->pushEnableDisable->setText(tr("&Enable at boot"));
    }
}

void MainWindow::cmdDone()
{
    setCursor(QCursor(Qt::ArrowCursor));
}

void MainWindow::setGeneralConnections()
{
    connect(ui->comboFilter, &QComboBox::currentTextChanged, this, &MainWindow::displayServices);
    connect(ui->lineSearch, &QLineEdit::textChanged, this, &MainWindow::displayServices);
    connect(ui->listServices, &QListWidget::currentItemChanged, this, &MainWindow::onSelectionChanged);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushCancel, &QPushButton::pressed, this, &MainWindow::close);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->pushEnableDisable, &QPushButton::clicked, this, &MainWindow::pushEnableDisable_clicked);
    connect(ui->pushStartStop, &QPushButton::clicked, this, &MainWindow::pushStartStop_clicked);
}

QString MainWindow::getHtmlColor(const QColor &color)
{
    return QString("#%1%2%3")
        .arg(color.red(), 2, 16, QChar('0'))
        .arg(color.green(), 2, 16, QChar('0'))
        .arg(color.blue(), 2, 16, QChar('0'));
}

void MainWindow::listServices()
{
    services.clear();
    if (init != "systemd") {
        const auto list = cmd.getCmdOut("service --status-all", true).split("\n");
        services.reserve(list.count());
        QRegularExpression re("dpkg-.*$");
        QString name;
        for (const auto &item : list) {
            if (item.trimmed().contains(re)) {
                continue;
            }
            name = item.section("]  ", 1);
            if (name == "debian") {
                continue;
            }
            auto *service = new Service(name, item.trimmed().startsWith("[ + ]"));
            service->setEnabled(Service::isEnabled(name));
            if (dependTargets.contains(name)) {
                service->setEnabled(true);
            }
            services << QSharedPointer<Service>(service);
        }
    } else {
        const auto list = cmd.getCmdOut("systemctl list-units --type=service --all -o json");
        auto doc = QJsonDocument::fromJson(list.toUtf8());
        if (!doc.isArray()) {
            qDebug() << "JSON data is not an array.";
            return;
        }
        auto jsonArray = doc.array();
        QStringList names;
        names.reserve(jsonArray.size() * 2);
        for (const auto &value : jsonArray) {
            auto obj = value.toObject();
            if (value.isObject()) {
                QString name = obj.value("unit").toString().section(".", 0, 0);
                QString status = obj.value("sub").toString();
                QString load = obj.value("load").toString();
                if (load == "not-found") {
                    continue;
                }
                auto *service = new Service(name, status == "running");
                names << name;
                service->setEnabled(Service::isEnabled(name));
                if (dependTargets.contains(name)) {
                    service->setEnabled(true);
                }
                services << QSharedPointer<Service>(service);
            }
        }
        const auto masked = cmd.getCmdOut("systemctl list-unit-files --type=service --state=masked -o json");
        doc = QJsonDocument::fromJson(masked.toUtf8());
        if (!doc.isArray()) {
            qDebug() << "JSON data is not an array.";
            return;
        }
        jsonArray = doc.array();
        for (const auto &value : jsonArray) {
            auto obj = value.toObject();
            if (value.isObject()) {
                QString name = obj.value("unit_file").toString().section(".", 0, 0);
                if (!names.contains(name)) {
                    auto *service = new Service(name, false);
                    service->setEnabled(false);
                    services << QSharedPointer<Service>(service);
                }
            }
        }
    }
}

void MainWindow::displayServices()
{
    ui->listServices->blockSignals(true);
    ui->listServices->clear();
    uint countActive = 0;
    uint countEnabled = 0;
    for (const auto &service : services) {
        QString serviceName = service->getName();
        QString searchText = ui->lineSearch->text();
        if (!searchText.isEmpty() && !serviceName.startsWith(searchText)
            && !(serviceName == "smbd"
                 && (searchText == "s" || searchText == "sa" || searchText == "sam" || searchText == "samb"
                     || searchText == "samba"))) {
            continue;
        }
        auto *item = new QListWidgetItem(service->getName(), ui->listServices);
        item->setData(Qt::UserRole, QVariant::fromValue(service.get()));
        if (service->isRunning()) {
            ++countActive;
            item->setForeground(runningColor);
        } else {
            if (service->isEnabled()) {
                ++countEnabled;
                item->setForeground(enabledColor);
            }
            if (ui->comboFilter->currentText() == tr("Running services")) {
                delete item;
                continue;
            }
        }
        if ((!service->isEnabled() && (ui->comboFilter->currentText() == tr("Services enabled at boot")))
            || (service->isEnabled() && (ui->comboFilter->currentText() == tr("Services disabled at boot")))) {
            delete item;
        } else {
            ui->listServices->addItem(item);
        }
    }

    QString fontTagStart = QString("<font color='%1'>").arg(getHtmlColor(runningColor));
    const QString fontTagEnd = "</font>";
    ui->labelCount->setText(
        tr("%1 total services, %2 currently %3running%4")
            .arg(QString::number(services.count()), QString::number(countActive), fontTagStart, fontTagEnd));
    fontTagStart = QString("<font color='%1'>").arg(getHtmlColor(enabledColor));
    ui->labelEnabledAtBoot->setText(
        tr("%1 %2enabled%3 at boot, but not running").arg(QString::number(countEnabled), fontTagStart, fontTagEnd));
    ui->listServices->blockSignals(false);
    if (savedRow >= ui->listServices->count()) {
        savedRow = ui->listServices->count() - 1;
    }
    ui->listServices->setCurrentRow(savedRow);
    ui->listServices->sortItems();
}

void MainWindow::pushAbout_clicked()
{
    this->hide();
    displayAboutMsgBox(
        tr("About %1") + tr("MX Service Manager"),
        R"(<p align="center"><b><h2>MX Service Manager</h2></b></p><p align="center">)" + tr("Version: ")
            + QApplication::applicationVersion() + "</p><p align=\"center\"><h3>" + tr("Service and daemon manager")
            + R"(</h3></p><p align="center"><a href="http://mxlinux.org">http://mxlinux.org</a><br /></p><p align="center">)"
            + tr("Copyright (c) MX Linux") + "<br /><br /></p>",
        QStringLiteral("/usr/share/doc/mx-service-manager/license.html"), tr("%1 License").arg(this->windowTitle()));

    this->show();
}

void MainWindow::pushEnableDisable_clicked()
{
    savedRow = ui->listServices->currentRow();
    auto service = ui->listServices->currentItem()->text();
    auto *ptrService = ui->listServices->currentItem()->data(Qt::UserRole).value<Service *>();
    if (ui->pushEnableDisable->text() == tr("&Enable at boot")) {
        if (!ptrService->enable()) {
            QMessageBox::warning(this, tr("Error"), tr("Could not enable %1").arg(service));
        }
        itemUpdated();
        emit ui->listServices->currentItemChanged(ui->listServices->currentItem(), ui->listServices->currentItem());
        QMessageBox::information(this, tr("Success"), tr("%1 was enabled at boot time.").arg(service));
    } else {
        if (!ptrService->disable()) {
            QMessageBox::warning(this, tr("Error"), tr("Could not disable %1").arg(service));
        }
        itemUpdated();
        emit ui->listServices->currentItemChanged(ui->listServices->currentItem(), ui->listServices->currentItem());
        QMessageBox::information(this, tr("Success"), tr("%1 was disabled.").arg(service));
    }
}

// Help button clicked
void MainWindow::pushHelp_clicked()
{
    const QString url = QStringLiteral("https://mxlinux.org/wiki/help-service-manager/");
    displayDoc(url, tr("%1 Help").arg(this->windowTitle()));
}

void MainWindow::pushStartStop_clicked()
{
    savedRow = ui->listServices->currentRow();
    auto service = ui->listServices->currentItem()->text();
    auto *ptrService = ui->listServices->currentItem()->data(Qt::UserRole).value<Service *>();
    if (ui->pushStartStop->text() == tr("S&tart")) {
        if (!ptrService->start()) {
            QMessageBox::warning(this, tr("Error"), tr("Could not start %1").arg(service));
        } else {
            ptrService->setRunning(true);
            itemUpdated();
            emit ui->listServices->currentItemChanged(ui->listServices->currentItem(), ui->listServices->currentItem());
            QMessageBox::information(this, tr("Success"), tr("%1 was started.").arg(service));
        }
    } else {
        if (!ptrService->stop()) {
            QMessageBox::warning(this, tr("Error"), tr("Could not stop %1").arg(service));
        } else {
            ptrService->setRunning(false);
            itemUpdated();
            emit ui->listServices->currentItemChanged(ui->listServices->currentItem(), ui->listServices->currentItem());
            QMessageBox::information(this, tr("Success"), tr("%1 was stopped.").arg(service));
        }
    }
}
