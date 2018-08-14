#include "vchar64application.h"

#include <QFileOpenEvent>

VChar64Application::VChar64Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
}

bool VChar64Application::event(QEvent *event)
{
        switch (event->type())
        {
        case QEvent::FileOpen:
            emit fileOpenRequest(dynamic_cast<QFileOpenEvent*>(event)->file());
            return true;
        default:
            return QApplication::event(event);
    }
}
