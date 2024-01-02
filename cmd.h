#pragma once

#include <QProcess>

class QTextStream;

class Cmd : public QProcess
{
    Q_OBJECT

public:
    explicit Cmd(QObject *parent = nullptr);

    [[nodiscard]] QString getOut(const QString &cmd, bool quiet = false, bool asRoot = false,
                                 bool waitForFinish = false);
    [[nodiscard]] QString getOutAsRoot(const QString &cmd, bool quiet = false, bool waitForFinish = false);
    bool run(const QString &cmd, bool quiet = false, bool asRoot = false, bool waitForFinish = false);
    bool runAsRoot(const QString &cmd, bool quiet = false);

signals:
    void done();

private:
    static const QString elevateCommand;
    static const QString helperCommand;
    QString elevate;
    QString helper;
};
