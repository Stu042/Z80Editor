#include "window.h"
#include "editor.h"
#include "debugsession.h"
#include <QTimer>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QToolBar>
#include <QVBoxLayout>

Window::Window(QWidget *parent, bool restoreLastFile)
    : QMainWindow(parent), editor_(new Editor), output_(new QPlainTextEdit) {
  resize(1150, 800);
  setObjectName("Z80EditorWindow");
  auto *file = menuBar()->addMenu("&File");
  auto *newAction =
      file->addAction("&New", QKeySequence::New, this, &Window::newFile);
  auto *openAction =
      file->addAction("&Open…", QKeySequence::Open, this, [this] {
        if (!mayDiscard())
          return;
        auto path = QFileDialog::getOpenFileName(
            this, "Open assembly", filePath_,
            "Assembly (*.asm *.s *.z80);;All files (*)");
        if (!path.isEmpty())
          openFile(path);
      });
  auto *saveAction =
      file->addAction("&Save", QKeySequence::Save, this, [this] { save(); });
  file->addAction("Save &As…", QKeySequence::SaveAs, this,
                  [this] { save(true); });
  file->addSeparator();
  file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);
  auto *edit = menuBar()->addMenu("&Edit");
  edit->addAction("&Undo", QKeySequence::Undo, editor_, &QPlainTextEdit::undo);
  edit->addAction("&Redo", QKeySequence::Redo, editor_, &QPlainTextEdit::redo);
  edit->addSeparator();
  edit->addAction("Cu&t", QKeySequence::Cut, editor_, &QPlainTextEdit::cut);
  edit->addAction("&Copy", QKeySequence::Copy, editor_, &QPlainTextEdit::copy);
  edit->addAction("&Paste", QKeySequence::Paste, editor_,
                  &QPlainTextEdit::paste);
  edit->addAction("Select &All", QKeySequence::SelectAll, editor_,
                  &QPlainTextEdit::selectAll);
  edit->addSeparator();
  edit->addAction("&Find…", QKeySequence::Find, this, &Window::findText);
  edit->addAction("Find &Next", QKeySequence::FindNext, this, [this] {
    if (lastSearch_.isEmpty()) {
      findText();
      return;
    }
    if (!editor_->find(lastSearch_)) {
      editor_->moveCursor(QTextCursor::Start);
      editor_->find(lastSearch_);
    }
  });
  auto *view = menuBar()->addMenu("&View");
  auto *tabWidthMenu = view->addMenu("Tab &width");
  auto *tabWidths = new QActionGroup(this);
  const int tabWidth = QSettings().value("tabWidth", 4).toInt() == 8 ? 8 : 4;
  editor_->setTabStopDistance(editor_->fontMetrics().horizontalAdvance(' ') * tabWidth);
  for (int width : {4, 8}) {
    auto *action = tabWidthMenu->addAction(QString("%1 characters").arg(width));
    action->setObjectName(QString("tabWidth%1Action").arg(width));
    action->setCheckable(true);
    tabWidths->addAction(action);
    action->setChecked(width == tabWidth);
    connect(action, &QAction::triggered, this, [this, width] {
      editor_->setTabStopDistance(editor_->fontMetrics().horizontalAdvance(' ') * width);
      QSettings().setValue("tabWidth", width);
    });
  }
  auto *execution = menuBar()->addMenu("&Run");
  run_ = execution->addAction("Run Z80Test", QKeySequence(Qt::Key_F5), this,
                              &Window::run);
  run_->setObjectName("runAction");
  stop_ = execution->addAction("Stop", QKeySequence(Qt::SHIFT | Qt::Key_F5),
                               this, &Window::stop);
  stop_->setEnabled(false);
  stepOver_ = execution->addAction("Step over", QKeySequence(Qt::Key_F10), this, &Window::stepOver);
  stepOver_->setObjectName("stepOverAction");
  stepInto_ = execution->addAction("Step into", QKeySequence(Qt::Key_F11), this, &Window::stepInto);
  stepInto_->setObjectName("stepIntoAction");
  debugTimer_ = new QTimer(this);
  connect(debugTimer_, &QTimer::timeout, this, &Window::debugTick);
  auto *toolbar = addToolBar("Main");
  toolbar->setMovable(false);
  toolbar->addAction(newAction);
  toolbar->addAction(openAction);
  toolbar->addAction(saveAction);
  toolbar->addSeparator();
  run_->setText("Run · F5");
  toolbar->addAction(run_);
  toolbar->addAction(stop_);
  toolbar->addAction(stepOver_);
  toolbar->addAction(stepInto_);

  auto *central = new QWidget;
  auto *layout = new QVBoxLayout(central);
  layout->setContentsMargins(10, 8, 10, 8);
  auto *options = new QGroupBox("Simulation");
  auto *form = new QFormLayout(options);
  auto *backendRow = new QWidget;
  auto *backendLayout = new QHBoxLayout(backendRow);
  backendLayout->setContentsMargins(0, 0, 0, 0);
  simulator_ = new QLineEdit;
  simulator_->setObjectName("simulatorPath");
  simulator_->setPlaceholderText("Path to Z80Test executable");
  auto *browse = new QPushButton("Browse…");
  backendLayout->addWidget(simulator_, 1);
  backendLayout->addWidget(browse);
  connect(browse, &QPushButton::clicked, this, [this] {
    auto path = QFileDialog::getOpenFileName(this, "Select Z80Test executable",
                                             simulator_->text());
    if (!path.isEmpty())
      simulator_->setText(path);
  });
  form->addRow("Simulator", backendRow);
  registers_ = new QLineEdit;
  registers_->setObjectName("initialRegisters");
  registers_->setPlaceholderText(
      "A=0 BC=0x1234 SP=0xfffe  (omitted registers default to zero)");
  registers_->setToolTip("Space-separated register assignments. No shell is "
                         "used. Example: AF'=0x1234 PC=0x8000");
  form->addRow("Initial registers", registers_);
  auto *timing = new QWidget;
  auto *timingLayout = new QHBoxLayout(timing);
  timingLayout->setContentsMargins(0, 0, 0, 0);
  clock_ = new QDoubleSpinBox;
  clock_->setObjectName("clockHz");
  clock_->setRange(1, 1000000000);
  clock_->setDecimals(0);
  clock_->setSingleStep(3500000);
  clock_->setSuffix(" Hz");
  maxSteps_ = new QSpinBox;
  maxSteps_->setObjectName("maxSteps");
  maxSteps_->setRange(1, 1000000000);
  timingLayout->addWidget(clock_);
  timingLayout->addSpacing(20);
  timingLayout->addWidget(new QLabel("Step limit"));
  timingLayout->addWidget(maxSteps_);
  timingLayout->addStretch();
  timingLayout->addWidget(new QLabel("End your program with BREAK"));
  form->addRow("Clock", timing);
  layout->addWidget(options);
  auto *splitter = new QSplitter(Qt::Vertical);
  splitter->setObjectName("editorSplitter");
  splitter->addWidget(editor_);
  auto *resultPanel = new QWidget;
  auto *resultLayout = new QVBoxLayout(resultPanel);
  resultLayout->setContentsMargins(0, 5, 0, 0);
  auto *resultHeader = new QHBoxLayout;
  resultHeader->addWidget(new QLabel("Results — registers, flags and timing"));
  resultHeader->addStretch();
  auto *copy = new QPushButton("Copy results");
  connect(copy, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(output_->toPlainText());
  });
  resultHeader->addWidget(copy);
  resultLayout->addLayout(resultHeader);
  output_->setObjectName("resultsOutput");
  output_->setReadOnly(true);
  output_->setLineWrapMode(QPlainTextEdit::NoWrap);
  output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  output_->setPlaceholderText("Press F5 to run the current assembly. Results "
                              "and any source errors appear here.");
  resultLayout->addWidget(output_);
  splitter->addWidget(resultPanel);
  splitter->setSizes({360, 300});
  layout->addWidget(splitter, 1);
  setCentralWidget(central);
  position_ = new QLabel;
  runStatus_ = new QLabel("Ready · F5 to run");
  runStatus_->setObjectName("runStatus");
  statusBar()->addWidget(runStatus_, 1);
  statusBar()->addPermanentWidget(position_);
  connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, [this] {
    const auto cursor = editor_->textCursor();
    position_->setText(QString("Ln %1, Col %2")
                           .arg(cursor.blockNumber() + 1)
                           .arg(cursor.positionInBlock() + 1));
  });
  connect(editor_->document(), &QTextDocument::modificationChanged, this,
          &Window::updateTitle);
  process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(&process_, &QProcess::readyReadStandardOutput, this,
          [this] { captured_ += process_.readAllStandardOutput(); });
  connect(&process_, &QProcess::finished, this, &Window::finish);
  connect(&process_, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
              captured_ =
                  ("Could not start Z80Test: " + process_.errorString() +
                   "\nCheck the Simulator path above.")
                      .toUtf8();
              finish(-1, QProcess::CrashExit);
            }
          });
  connect(editor_, &QPlainTextEdit::textChanged, this, &Window::resetDebug);
  connect(registers_, &QLineEdit::textChanged, this, &Window::resetDebug);
  connect(clock_, &QDoubleSpinBox::valueChanged, this, &Window::resetDebug);
  connect(maxSteps_, &QSpinBox::valueChanged, this, &Window::resetDebug);
  readSettings();
  newFile();
  if (restoreLastFile) {
    const QString lastFile = QSettings().value("lastFile").toString();
    if (!lastFile.isEmpty() && !openFile(lastFile, false))
      statusBar()->showMessage("Could not reopen last file: " + lastFile, 6000);
  }
}
Window::~Window() {
  disconnect(&process_, nullptr, this, nullptr);
  if (process_.state() != QProcess::NotRunning) {
    process_.kill();
    process_.waitForFinished(3000);
  }
}
void Window::readSettings() {
  QSettings settings;
  simulator_->setText(
      settings.value("simulator", QStringLiteral(Z80TEST_DEFAULT_EXECUTABLE))
          .toString());
  registers_->setText(settings.value("registers").toString());
  clock_->setValue(settings.value("clock", 3500000).toDouble());
  maxSteps_->setValue(settings.value("maxSteps", 1000000).toInt());
  if (settings.contains("geometry"))
    restoreGeometry(settings.value("geometry").toByteArray());
  if (settings.contains("splitter"))
    findChild<QSplitter *>("editorSplitter")
        ->restoreState(settings.value("splitter").toByteArray());
}
void Window::writeSettings() {
  QSettings settings;
  settings.setValue("simulator", simulator_->text());
  settings.setValue("registers", registers_->text());
  settings.setValue("clock", clock_->value());
  settings.setValue("maxSteps", maxSteps_->value());
  settings.setValue("geometry", saveGeometry());
  settings.setValue("splitter",
                    findChild<QSplitter *>("editorSplitter")->saveState());
}
void Window::updateTitle() {
  setWindowModified(editor_->document()->isModified());
  setWindowTitle(
      (filePath_.isEmpty() ? "Untitled.asm" : QFileInfo(filePath_).fileName()) +
      "[*] — Z80Editor");
}
bool Window::mayDiscard() {
  if (!editor_->document()->isModified())
    return true;
  auto choice = QMessageBox::warning(
      this, "Unsaved assembly", "Save changes before continuing?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);
  return choice == QMessageBox::Discard ||
         (choice == QMessageBox::Save && save());
}
void Window::newFile() {
  if (!mayDiscard())
    return;
  filePath_.clear();
  editor_->setPlainText("; Z80 / Z80N assembly — press F5 to run\nCameraMinX: "
                        "equ 100\n\n    ld a,CameraMinX\n    ld b,10\nloop:\n  "
                        "  add a,b\n    djnz loop\n    break\n");
  editor_->document()->setModified(false);
  editor_->moveCursor(QTextCursor::Start);
  output_->clear();
  updateTitle();
  editor_->setFocus();
}
bool Window::openFile(const QString &path, bool reportErrors) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (reportErrors)
      QMessageBox::critical(this, "Open failed", file.errorString());
    return false;
  }
  auto decoder = QStringDecoder(QStringDecoder::Utf8);
  const QString text = decoder(file.readAll());
  if (decoder.hasError()) {
    if (reportErrors)
      QMessageBox::critical(this, "Open failed",
                            "The source file must be UTF-8 text.");
    return false;
  }
  filePath_ = QFileInfo(path).absoluteFilePath();
  QSettings().setValue("lastFile", filePath_);
  editor_->setPlainText(text);
  editor_->document()->setModified(false);
  editor_->moveCursor(QTextCursor::Start);
  output_->clear();
  updateTitle();
  editor_->setFocus();
  return true;
}
bool Window::save(bool saveAs) {
  QString path = filePath_;
  if (path.isEmpty() || saveAs)
    path = QFileDialog::getSaveFileName(this, "Save assembly",
                                        path.isEmpty() ? "Untitled.asm" : path,
                                        "Assembly (*.asm);;All files (*)");
  if (path.isEmpty())
    return false;
  QSaveFile file(path);
  const auto bytes = editor_->toPlainText().toUtf8();
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit()) {
    QMessageBox::critical(this, "Save failed", file.errorString());
    return false;
  }
  filePath_ = QFileInfo(path).absoluteFilePath();
  QSettings().setValue("lastFile", filePath_);
  editor_->document()->setModified(false);
  updateTitle();
  return true;
}
void Window::findText() {
  bool accepted;
  auto text = QInputDialog::getText(this, "Find", "Find in assembly",
                                    QLineEdit::Normal, lastSearch_, &accepted);
  if (!accepted || text.isEmpty())
    return;
  lastSearch_ = text;
  if (!editor_->find(text)) {
    editor_->moveCursor(QTextCursor::Start);
    if (!editor_->find(text))
      statusBar()->showMessage("Text not found", 3000);
  }
}
void Window::run() {
  if (running_)
    return;
  if (debug_) {
    debugContinue_ = true;
    running_ = true;
    run_->setEnabled(false);
    stepInto_->setEnabled(false);
    stepOver_->setEnabled(false);
    debugTimer_->start(0);
    return;
  }
  editor_->clearAnnotations();
  const QString executable =
      QFileInfo(simulator_->text().trimmed()).absoluteFilePath();
  if (!QFileInfo(executable).isFile() ||
      !QFileInfo(executable).isExecutable()) {
    output_->setPlainText(
        "Cannot run Z80Test. Select a valid executable in Simulator above.\n" +
        executable);
    runStatus_->setText("Simulator unavailable");
    return;
  }
  const QStringList assignments = QProcess::splitCommand(registers_->text());
  static const QRegularExpression assignment(
      R"(^[A-Za-z][A-Za-z0-9]*'?=[^\s=]+$)");
  for (const auto &value : assignments) {
    if (!assignment.match(value).hasMatch()) {
      output_->setPlainText(
          "Invalid initial register assignment: " + value +
          "\nUse space-separated values such as A=10 BC=0x1234 SP=0xfffe.");
      runStatus_->setText("Check initial registers");
      return;
    }
  }
  snapshot_ =
      std::make_unique<QTemporaryDir>(QDir::tempPath() + "/Z80Editor-XXXXXX");
  if (!snapshot_->isValid()) {
    output_->setPlainText("Could not create temporary source directory.");
    return;
  }
  QFile source(snapshot_->filePath("program.asm"));
  runSource_ = editor_->toPlainText();
  const auto bytes = runSource_.toUtf8();
  if (!source.open(QIODevice::WriteOnly) ||
      source.write(bytes) != bytes.size() || !source.flush()) {
    output_->setPlainText("Could not write temporary assembly: " +
                          source.errorString());
    snapshot_.reset();
    return;
  }
  source.close();
  QStringList arguments{source.fileName()};
  arguments.append(assignments);
  arguments << "--max-steps" << QString::number(maxSteps_->value()) << "--clock"
            << QString::number(clock_->value(), 'f', 0);
  captured_.clear();
  cancelled_ = false;
  running_ = true;
  output_->setPlainText("Running Z80Test…");
  runStatus_->setText("Running… · Shift+F5 to stop");
  run_->setEnabled(false);
  stepInto_->setEnabled(false);
  stepOver_->setEnabled(false);
  stop_->setEnabled(true);
  process_.setWorkingDirectory(filePath_.isEmpty()
                                   ? QDir::currentPath()
                                   : QFileInfo(filePath_).absolutePath());
  if (executable == QFileInfo(QStringLiteral(Z80TEST_DEFAULT_EXECUTABLE)).absoluteFilePath()) {
    // Use the same core as stepping so each executed statement can be annotated.
    running_ = false;
    stepInto();
    if (debug_)
      run();
    else if (!running_) {
      run_->setEnabled(true);
      stepInto_->setEnabled(true);
      stepOver_->setEnabled(true);
      stop_->setEnabled(false);
    }
    snapshot_.reset();
  } else {
    process_.start(executable, arguments);
  }
}
void Window::stop() {
  if (debug_) {
    showDebug("Cancelled", true);
    return;
  }
  if (!running_)
    return;
  cancelled_ = true;
  process_.kill();
}
void Window::finish(int exitCode, QProcess::ExitStatus status) {
  if (!running_)
    return;
  captured_ += process_.readAllStandardOutput();
  if (cancelled_)
    captured_ += "\nRun cancelled.\n";
  else if (status == QProcess::CrashExit && exitCode != -1)
    captured_ += "\nZ80Test terminated unexpectedly.\n";
  const QString output = QString::fromUtf8(captured_);
  output_->setPlainText(output);
  output_->moveCursor(QTextCursor::Start);
  QString label = cancelled_      ? "Cancelled"
                  : exitCode == 0 ? "Finished"
                  : exitCode == 2 ? "Step limit reached"
                                  : "Run failed";
  const bool changed = editor_->toPlainText() != runSource_;
  if (changed)
    label += " · results are for the source before your latest edits";
  runStatus_->setText(label);
  if (exitCode != 0 && !changed && !cancelled_) {
    static const QRegularExpression line(R"(\bline (\d+):)");
    const auto match = line.match(output);
    if (match.hasMatch())
      editor_->goToLine(match.captured(1).toInt(), true);
  }
  running_ = false;
  stepInto_->setEnabled(true);
  stepOver_->setEnabled(true);
  run_->setEnabled(true);
  stop_->setEnabled(false);
  snapshot_.reset();
  emit runFinished(exitCode);
}
void Window::closeEvent(QCloseEvent *event) {
  if (!mayDiscard()) {
    event->ignore();
    return;
  }
  writeSettings();
  if (running_)
    stop();
  event->accept();
}

void Window::resetDebug() {
  editor_->clearAnnotations();
  // A running simulation continues using its immutable source snapshot.
  if (debug_ && running_) {
    editor_->setExecutionLine(0);
    return;
  }
  if (!debug_)
    return;
  debugTimer_->stop();
  debug_.reset();
  editor_->setExecutionLine(0);
  running_ = false;
  run_->setEnabled(true);
  stepInto_->setEnabled(true);
  stepOver_->setEnabled(true);
  stop_->setEnabled(false);
  runStatus_->setText("Debug session reset · F10/F11 to restart");
}
void Window::stepInto() { step(false); }
void Window::stepOver() { step(true); }
void Window::step(bool over) {
  if (running_)
    return;
  if (!debug_) {
    editor_->clearAnnotations();
    auto session = std::make_unique<DebugSession>();
    session->source = editor_->toPlainText();
    session->clock = clock_->value();
    session->limit = maxSteps_->value();
    QTemporaryDir directory;
    QFile source(directory.filePath("debug.asm"));
    const auto bytes = session->source.toUtf8();
    if (!directory.isValid() || !source.open(QIODevice::WriteOnly) ||
        source.write(bytes) != bytes.size() || !source.flush()) {
      output_->setPlainText("Could not write temporary debug source.");
      return;
    }
    source.close();
    char error[512]{};
    uint16_t entry;
    if (!assemble(&session->cpu, source.fileName().toUtf8().constData(), &entry, error, sizeof error)) {
      output_->setPlainText("Assembly error: " + QString::fromUtf8(error) + "\n" +
          session->source.section('\n', int(session->cpu.error_line) - 1, int(session->cpu.error_line) - 1));
      runStatus_->setText("Assembly failed");
      emit runFinished(1);
      if (session->cpu.error_line)
        editor_->goToLine(session->cpu.error_line, true);
      return;
    }
    for (const auto &assignment : QProcess::splitCommand(registers_->text())) {
      if (!set_register(&session->cpu, assignment.toUtf8().constData())) {
        output_->setPlainText("Invalid initial register assignment: " + assignment);
        return;
      }
    }
    debug_ = std::move(session);
  }
  auto reason = debug_->terminalReason();
  if (!reason.isEmpty()) {
    showDebug(reason, true);
    return;
  }
  auto &cpu = debug_->cpu;
  // Index prefixes are ignored by CALL, but still contribute to its length.
  quint16 opcodePc = cpu.pc;
  unsigned prefixes = 0;
  while ((cpu.mem[opcodePc] == 0xdd || cpu.mem[opcodePc] == 0xfd) && prefixes++ < 65536)
    ++opcodePc;
  const auto opcode = cpu.mem[opcodePc];
  debugOver_ = over && !cpu.halted && (opcode == 0xcd || (opcode & 0xc7) == 0xc4);
  returnPc_ = quint16(opcodePc + 3);
  returnSp_ = cpu.sp;
  debugContinue_ = false;
  running_ = true;
  run_->setEnabled(false);
  stepInto_->setEnabled(false);
  stepOver_->setEnabled(false);
  stop_->setEnabled(true);
  debugTick();
}
void Window::debugTick() {
  if (!debug_)
    return;
  for (int count = 0; count < 2000; ++count) {
    auto reason = debug_->terminalReason();
    if (!reason.isEmpty()) {
      showDebug(reason, true);
      return;
    }
    auto &cpu = debug_->cpu;
    const auto line = cpu.source_lines[cpu.pc];
    const Z80 before = cpu;
    char error[512]{};
    if (!z80_step(&cpu, error, sizeof error)) {
      showDebug("Simulation error: " + QString::fromUtf8(error), true);
      if (line)
        editor_->goToLine(line, true);
      return;
    }
    if (editor_->toPlainText() == debug_->source)
      editor_->annotate(line, DebugSession::changes(before, cpu));
    reason = debug_->terminalReason();
    if (!reason.isEmpty()) {
      showDebug(reason, true);
      return;
    }
    if (!debugContinue_ && (!debugOver_ || (cpu.pc == returnPc_ && cpu.sp == returnSp_))) {
      showDebug("Paused");
      return;
    }
  }
  output_->setPlainText(debug_->results("Running…"));
  runStatus_->setText("Running… · Shift+F5 to stop");
  debugTimer_->start(0);
}
void Window::showDebug(const QString &reason, bool terminal) {
  debugTimer_->stop();
  QString results = debug_->results(reason);
  if (reason == "Cancelled")
    results += "\nRun cancelled.\n";
  output_->setPlainText(results);
  const auto line = debug_->cpu.source_lines[debug_->cpu.pc];
  const bool changed = editor_->toPlainText() != debug_->source;
  editor_->setExecutionLine(terminal || changed ? 0 : line);
  if (line && !changed)
    editor_->goToLine(line);
  runStatus_->setText(terminal ? reason : "Paused · F10 step over · F11 step into · F5 continue");
  if (reason == "step limit reached")
    runStatus_->setText("Step limit reached");
  if (changed)
    runStatus_->setText(runStatus_->text() + " · results are for the source before your latest edits");
  running_ = false;
  run_->setEnabled(true);
  stepInto_->setEnabled(true);
  stepOver_->setEnabled(true);
  stop_->setEnabled(!terminal);
  if (terminal) {
    debug_.reset();
    const int code = reason == "step limit reached" ? 2 : reason.startsWith("Simulation error") ? 1 : 0;
    emit runFinished(code);
  }
}
