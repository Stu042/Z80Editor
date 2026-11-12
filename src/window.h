#pragma once
#include <QMainWindow>
#include <QProcess>
#include <memory>
class DebugSession;
class QTimer;
class Editor;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QPlainTextEdit;
class QTemporaryDir;
class QAction;
class QLabel;
class QCloseEvent;
class Window final : public QMainWindow {
  Q_OBJECT
public:
  explicit Window(QWidget *parent = nullptr, bool restoreLastFile = true);
  ~Window() override;
  bool openFile(const QString &path, bool reportErrors = true);
signals:
  void runFinished(int exitCode);
public slots:
  void run();
  void stop();
  void stepInto();
  void stepOver();

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void step(bool over);
  void debugTick();
  void showDebug(const QString &reason, bool terminal = false);
  void resetDebug();
  void newFile();
  bool save(bool saveAs = false);
  bool mayDiscard();
  void updateTitle();
  void finish(int exitCode, QProcess::ExitStatus status);
  void findText();
  void readSettings();
  void writeSettings();
  Editor *editor_;
  QPlainTextEdit *output_;
  QLineEdit *registers_, *simulator_;
  QDoubleSpinBox *clock_;
  QSpinBox *maxSteps_;
  QAction *run_, *stop_, *stepInto_, *stepOver_;
  QTimer *debugTimer_;
  std::unique_ptr<DebugSession> debug_;
  bool debugContinue_ = false, debugOver_ = false;
  quint16 returnPc_ = 0, returnSp_ = 0;
  QLabel *position_, *runStatus_;
  QProcess process_;
  std::unique_ptr<QTemporaryDir> snapshot_;
  QString filePath_, lastSearch_, runSource_;
  QByteArray captured_;
  bool cancelled_ = false, running_ = false;
};
