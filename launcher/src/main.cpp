// split2 launcher: TimeSplitters 2's settings, controls and build, then Play.
#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TimeSplitters 2"));
    QApplication::setOrganizationName(QStringLiteral("xboxrecomp"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/split2.png")));
    // The native look: Windows 11's style where Qt has it.
    if (QStyleFactory::keys().contains(QStringLiteral("windows11"), Qt::CaseInsensitive))
        QApplication::setStyle(QStringLiteral("windows11"));
    MainWindow w;
    w.show();
    const QStringList args = QApplication::arguments();
    if (args.size() >= 3 && args[1] == QLatin1String("--shots")) {
        w.saveTabShots(args[2]);
        return 0;
    }
    w.setupIfNeeded();
    return app.exec();
}
