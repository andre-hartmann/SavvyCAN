#ifndef SCRIPTINGWINDOW_H
#define SCRIPTINGWINDOW_H

#include "scriptcontainer.h"
#include "can_structs.h"
#include "connections/canconnection.h"
#include "jsedit.h"

#include <QDialog>
#include <QJSEngine>

class ScriptContainer;

namespace Ui {
class ScriptingWindow;
}

class ScriptingWindow : public QDialog
{
    Q_OBJECT

public:
    explicit ScriptingWindow(const QVector<CANFrame> *frames, QWidget *parent = 0);
    void showEvent(QShowEvent*);
    ~ScriptingWindow();

public slots:
    void log(QString text);

private slots:
    void loadNewScript();
    void createNewScript();
    void deleteCurrentScript();
    void refreshSourceWindow();
    void saveScript();
    void revertScript();
    void recompileScript();
    void changeCurrentScript();
    void newFrames(const CANConnection*, const QVector<CANFrame>&);
    void clickedLogClear();

private:
    void closeEvent(QCloseEvent *event);
    void readSettings();
    void writeSettings();

    Ui::ScriptingWindow *ui;
    JSEdit *editor;
    QList<ScriptContainer *> scripts;
    ScriptContainer *currentScript;
    const QVector<CANFrame> *modelFrames;
    QElapsedTimer elapsedTime;
};

#endif // SCRIPTINGWINDOW_H
