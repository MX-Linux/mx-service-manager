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
#include <QScreen>
#include <QScrollBar>
#include <QTextStream>
#include <QTimer>

#include "about.h"
#include "service.h"

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
    auto init = Service::getInit();
    if (init != "systemd" && init != "init") {
        QMessageBox::warning(
            this, tr("Error"),
            tr("Could not determine the init system. This program is supposed to run either with systemd or sysvinit")
                + "\nINIT:" + init);
    }
    QPalette palette = ui->listServices->palette();
    defaultForeground = palette.color(QPalette::Text);

    ui->listServices->addItem(tr("Loading..."));
    QTimer::singleShot(0, this, [this] {
        listServices();
        displayServices();
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
    listServices();
    displayServices(ui->checkShowRunning->checkState());
    blockSignals(false);
}

void MainWindow::onSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    ui->textBrowser->setText(Service::getInfo(current->text()));
    if (current->data(Qt::UserRole) == true) {
        ui->pushStartStop->setText(tr("Stop"));
        ui->pushStartStop->setIcon(QIcon::fromTheme("stop"));
        current->setForeground(QColor(Qt::darkGreen));
    } else {
        ui->pushStartStop->setText(tr("Start"));
        ui->pushStartStop->setIcon(QIcon::fromTheme("start"));
        current->setForeground(defaultForeground);
    }
    if (Service::isEnabled(current->text())) {
        ui->pushEnableDisable->setText(tr("Disable at boot"));
        ui->pushEnableDisable->setIcon(QIcon::fromTheme("stop"));
    } else {
        ui->pushEnableDisable->setText(tr("Enable at boot"));
        ui->pushEnableDisable->setIcon(QIcon::fromTheme("start"));
    }
}

void MainWindow::cmdDone()
{
    setCursor(QCursor(Qt::ArrowCursor));
}

// set proc and timer connections
void MainWindow::setConnections()
{
}

void MainWindow::setGeneralConnections()
{
    connect(ui->checkShowRunning, &QCheckBox::stateChanged, this, &MainWindow::displayServices);
    connect(ui->listServices, &QListWidget::currentItemChanged, this, &MainWindow::onSelectionChanged);
    connect(ui->pushAbout, &QPushButton::clicked, this, &MainWindow::pushAbout_clicked);
    connect(ui->pushCancel, &QPushButton::pressed, this, &MainWindow::close);
    connect(ui->pushHelp, &QPushButton::clicked, this, &MainWindow::pushHelp_clicked);
    connect(ui->pushEnableDisable, &QPushButton::clicked, this, &MainWindow::pushEnableDisable_clicked);
    connect(ui->pushStartStop, &QPushButton::clicked, this, &MainWindow::pushStartStop_clicked);
}

void MainWindow::listServices()
{
    services.clear();
    const QStringList listServices = cmd.getCmdOut("service --status-all").split("\n");
    services.reserve(listServices.count());
    for (const auto &item : listServices) {
        auto *service = new Service(item.section("]  ", 1), item.trimmed().startsWith("[ + ]"));
        services << QSharedPointer<Service>(service);
    }
}

void MainWindow::displayServices(int checked)
{
    ui->listServices->blockSignals(true);
    ui->listServices->clear();
    uint countActive = 0;
    for (const auto &service : services) {
        auto *item = new QListWidgetItem(service->getName(), ui->listServices);
        if (service->isRunning()) {
            ++countActive;
            item->setData(Qt::UserRole, true);
            item->setForeground(QColor(Qt::darkGreen));
        } else {
            if (checked == Qt::Checked) {
                delete item;
                continue;
            }
        }
        ui->listServices->addItem(item);
    }
    ui->labelCount->setText(tr("%1 total services, %2 currently running").arg(services.count()).arg(countActive));
    ui->listServices->blockSignals(false);
    if (savedRow >= ui->listServices->count()) {
        savedRow = ui->listServices->count() - 1;
    }
    ui->listServices->setCurrentRow(savedRow);
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
    if (ui->pushEnableDisable->text() == tr("Enable at boot")) {
        if (!Service::enable(service)) {
            QMessageBox::warning(this, tr("Error"), tr("Could not enable %1").arg(service));
        }
        itemUpdated();
        emit ui->listServices->currentItemChanged(ui->listServices->currentItem(), ui->listServices->currentItem());
        QMessageBox::information(this, tr("Success"), tr("%1 was enabled at boot time.").arg(service));
    } else {
        if (!Service::disable(ui->listServices->currentItem()->text())) {
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
    const QString url = QStringLiteral("mxlinux.org");
    displayDoc(url, tr("%1 Help").arg(this->windowTitle()));
}

void MainWindow::pushStartStop_clicked()
{
    savedRow = ui->listServices->currentRow();
    auto service = ui->listServices->currentItem()->text();
    if (ui->pushStartStop->text() == tr("Start")) {
        if (!Service::start(service)) {
            QMessageBox::warning(this, tr("Error"), tr("Could not start %1").arg(service));
        } else {
            ui->listServices->currentItem()->setData(Qt::UserRole, true);
            itemUpdated();
            emit ui->listServices->currentItemChanged(ui->listServices->currentItem(), ui->listServices->currentItem());
            QMessageBox::information(this, tr("Success"), tr("%1 was started.").arg(service));
        }
    } else {
        if (!Service::stop(ui->listServices->currentItem()->text())) {
            QMessageBox::warning(this, tr("Error"), tr("Could not stop %1").arg(service));
        } else {
            ui->listServices->currentItem()->setData(Qt::UserRole, false);
            itemUpdated();
            emit ui->listServices->currentItemChanged(ui->listServices->currentItem(), ui->listServices->currentItem());
            QMessageBox::information(this, tr("Success"), tr("%1 was stopped.").arg(service));
        }
    }
}
