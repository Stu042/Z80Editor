#include "editor.h"
#include "debugsession.h"
#include "window.h"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QDialog>
#include <QHelpEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolTip>
class EditorTests : public QObject {
  Q_OBJECT
private slots:
  void registerPairAnnotations() {
    auto check = [](std::initializer_list<uint8_t> instruction, unsigned h, unsigned l) {
      Z80 before{};
      before.h = h;
      before.l = l;
      unsigned address = 0;
      for (auto byte : instruction)
        before.mem[address++] = byte;
      Z80 after = before;
      char error[512]{};
      if (!z80_step(&after, error, sizeof error))
        return QString::fromUtf8(error);
      return DebugSession::changes(before, after);
    };
    const auto load = check({0x21, 0x34, 0x12}, 0, 0); // LD HL,1234
    QVERIFY(load.contains("HL=1234"));
    QVERIFY(!load.contains("  L="));
    QVERIFY(check({0x23}, 0x12, 0).contains("HL=1201")); // INC HL, low byte only
    QVERIFY(check({0x23}, 0x12, 0xff).contains("HL=1300")); // Carry into H
    const auto byte = check({0x2c}, 0x12, 0); // INC L
    QVERIFY(byte.contains("L=01"));
    QVERIFY(!byte.contains("HL="));
    QVERIFY(check({0xed, 0x6b, 0x10, 0}, 0x12, 0x34).contains("HL=0000"));
    QVERIFY(check({0xdd, 0x21, 0x34, 0x12}, 0, 0).contains("IX=1234"));
    QVERIFY(check({0xeb}, 0x12, 0x34).contains("DE=1234"));
    QVERIFY(check({0x01, 0x34, 0x12}, 0, 0).contains("BC=1234"));
    QVERIFY(check({0x11, 0x34, 0x12}, 0, 0).contains("DE=1234"));
    QVERIFY(check({0x29}, 0x12, 0).contains("HL=2400")); // ADD HL,HL
    QVERIFY(check({0xe1}, 0, 0).contains("HL=00E1")); // POP HL from address zero
  }
  void initTestCase() {
    QCoreApplication::setOrganizationName("Z80EditorTests");
    QCoreApplication::setApplicationName("Integration");
    QVERIFY(settings_.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settings_.path());
  }
  void f5RunsUnsavedSource() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    auto *output = window.findChild<QPlainTextEdit *>("resultsOutput");
    QVERIFY(editor);
    QVERIFY(output);
    editor->setPlainText("ld a,42\nbreak\ninc a\n");
    editor->document()->setModified(true);
    window.show();
    window.activateWindow();
    editor->setFocus();
    QTest::qWait(30);
    QSignalSpy finished(&window, &Window::runFinished);
    QTest::keyClick(editor, Qt::Key_F5);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().first().toInt(), 0);
    QVERIFY(output->toPlainText().contains("A=42 (0x2A)"));
    QVERIFY(output->toPlainText().contains("T-states: 7"));
    QVERIFY(output->toPlainText().contains("Stopped: BREAK"));
    QVERIFY(editor->document()->isModified());
    QVERIFY(window.findChild<QAction *>("runAction")->isEnabled());
  }
  void registerInputsReachSimulator() {
    Window window;
    window.findChild<Editor *>("sourceEditor")->setPlainText("inc a\nbreak\n");
    window.findChild<QLineEdit *>("initialRegisters")
        ->setText("A=10 BC=0x1234 SP=0xfffe AF'=0x1234");
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    const auto text =
        window.findChild<QPlainTextEdit *>("resultsOutput")->toPlainText();
    QCOMPARE(finished.first().first().toInt(), 0);
    QVERIFY(text.contains("A=11 (0x0B)"));
    QVERIFY(text.contains("BC=4660 (0x1234)"));
    QVERIFY(text.contains("AF'=1234"));
    QVERIFY(text.contains("Time:"));
  }
  void errorSelectsOriginalLine() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    editor->setPlainText(
        "; heading\n    ld a,MissingSymbol ; Original Comment\nbreak\n");
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    const auto text =
        window.findChild<QPlainTextEdit *>("resultsOutput")->toPlainText();
    QCOMPARE(finished.first().first().toInt(), 1);
    QVERIFY(text.contains("line 2:"));
    QVERIFY(text.contains("    ld a,MissingSymbol ; Original Comment"));
    QCOMPARE(editor->textCursor().blockNumber(), 1);
  }
  void stepLimitIsVisible() {
    Window window;
    window.findChild<Editor *>("sourceEditor")->setPlainText("loop: jr loop\n");
    window.findChild<QSpinBox *>("maxSteps")->setValue(5);
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QCOMPARE(finished.first().first().toInt(), 2);
    QVERIFY(window.findChild<QPlainTextEdit *>("resultsOutput")
                ->toPlainText()
                .contains("T-states: 60"));
    QCOMPARE(window.findChild<QLabel *>("runStatus")->text(),
             QString("Step limit reached"));
  }
  void cancellationAndEditedSnapshot() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    editor->setPlainText("loop: jr loop\n");
    window.findChild<QSpinBox *>("maxSteps")->setValue(1000000000);
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    editor->setPlainText("break\n");
    QTimer::singleShot(30, &window, &Window::stop);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(window.findChild<QPlainTextEdit *>("resultsOutput")
                ->toPlainText()
                .contains("Run cancelled"));
    QVERIFY(window.findChild<QLabel *>("runStatus")
                ->text()
                .startsWith("Cancelled"));
  }
  void savedFileIsNotOverwrittenByRun() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath("source.asm"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray original = "ld a,1\nbreak\n";
    QCOMPARE(file.write(original), original.size());
    file.close();
    Window window;
    QVERIFY(window.openFile(file.fileName()));
    window.findChild<Editor *>("sourceEditor")->setPlainText("ld a,2\nbreak\n");
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    QVERIFY(window.findChild<QPlainTextEdit *>("resultsOutput")
                ->toPlainText()
                .contains("A=2 (0x02)"));
  }
  void invalidOptionsStayInEditor() {
    Window window;
    window.findChild<QLineEdit *>("initialRegisters")
        ->setText("--max-steps 50");
    window.run();
    QVERIFY(window.findChild<QPlainTextEdit *>("resultsOutput")
                ->toPlainText()
                .contains("Invalid initial register"));
    window.findChild<QLineEdit *>("initialRegisters")->clear();
    window.findChild<QLineEdit *>("simulatorPath")->setText("/missing/Z80Test");
    window.run();
    QVERIFY(window.findChild<QPlainTextEdit *>("resultsOutput")
                ->toPlainText()
                .contains("Select a valid executable"));
  }
  void steppingCallsAndContinuation() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    auto *output = window.findChild<QPlainTextEdit *>("resultsOutput");
    editor->setPlainText("ld a,1\ncall outer\ninc a\nbreak\nnop\nouter: call inner\ninc a\nret\ninner: inc a\nret\n");
    window.show();
    window.activateWindow();
    editor->setFocus();
    QTest::qWait(30);
    QTest::keyClick(editor, Qt::Key_F11);
    QVERIFY(output->toPlainText().contains("A=1 (0x01)"));
    QVERIFY(output->toPlainText().contains("T-states: 7"));
    QCOMPARE(editor->textCursor().blockNumber(), 1);
    QTest::keyClick(editor, Qt::Key_F11);
    QCOMPARE(editor->textCursor().blockNumber(), 5);
    QVERIFY(output->toPlainText().contains("T-states: 24"));
    QTest::keyClick(editor, Qt::Key_F10);
    QCOMPARE(editor->textCursor().blockNumber(), 6);
    QVERIFY(output->toPlainText().contains("A=2 (0x02)"));
    QVERIFY(output->toPlainText().contains("T-states: 55"));
    window.run();
    QTRY_VERIFY(output->toPlainText().contains("Stopped: BREAK"));
    QVERIFY(output->toPlainText().contains("A=4 (0x04)"));
    QVERIFY(output->toPlainText().contains("T-states: 73"));
  }
  void callRoutineImmediatelyAfterBreak() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    editor->setPlainText("ld b,10\ncall routine\nbreak\nroutine: ld a,b\ninc a\nret\n");
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(finished.first().first().toInt(), 0);
    QVERIFY(editor->annotation(4).contains("A=0A"));
    QVERIFY(editor->annotation(5).contains("A=0B"));
    QVERIFY(!editor->annotation(6).isEmpty());
    const auto output = window.findChild<QPlainTextEdit *>("resultsOutput")->toPlainText();
    QVERIFY(output.contains("A=11 (0x0B)"));
    QVERIFY(output.contains("Stopped: BREAK"));
    QCOMPARE(editor->textCursor().blockNumber(), 2);
  }
  void stepOverNestedAndConditionalCalls() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    auto *output = window.findChild<QPlainTextEdit *>("resultsOutput");
    editor->setPlainText("call outer\ncall z,outer\nbreak\nnop\nouter: call inner\nret\ninner: inc a\nret\n");
    window.stepOver();
    QCOMPARE(editor->textCursor().blockNumber(), 1);
    QVERIFY(output->toPlainText().contains("A=1 (0x01)"));
    QVERIFY(output->toPlainText().contains("T-states: 58"));
    window.stepOver();
    QVERIFY(output->toPlainText().contains("Stopped: BREAK"));
    QVERIFY(output->toPlainText().contains("A=1 (0x01)"));
    QVERIFY(output->toPlainText().contains("T-states: 68"));
  }
  void debugMemoryResetAndLimits() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    auto *output = window.findChild<QPlainTextEdit *>("resultsOutput");
    editor->setPlainText("ld a,42\nld ($8000),a\nld a,0\nld a,($8000)\nbreak\n");
    for (int i = 0; i < 4; ++i)
      window.stepInto();
    QVERIFY(output->toPlainText().contains("A=42 (0x2A)"));
    editor->setPlainText("inc a\nbreak\n");
    window.stepInto();
    QVERIFY(output->toPlainText().contains("A=1 (0x01)"));
    window.findChild<QSpinBox *>("maxSteps")->setValue(10);
    editor->setPlainText("call forever\nbreak\nnop\nforever: jr forever\n");
    window.stepOver();
    QVERIFY(output->toPlainText().contains("Stopped: step limit reached"));
    window.findChild<QSpinBox *>("maxSteps")->setValue(1000000000);
    window.stepOver();
    window.stop();
    QVERIFY(output->toPlainText().contains("Stopped: Cancelled"));
  }
  void executionAnnotations() {
    Window window;
    auto *editor = window.findChild<Editor *>("sourceEditor");
    const QString source = "ld a,42\nld ($8000),a\ninc a\nbreak\n";
    editor->setPlainText(source);
    window.stepInto();
    QVERIFY(editor->annotation(1).contains("A=2A"));
    QVERIFY(editor->annotation(1).contains("+7 T"));
    QVERIFY(editor->annotation(2).isEmpty());
    QCOMPARE(editor->toPlainText(), source);
    window.stepInto();
    QVERIFY(editor->annotation(2).contains("[8000]=2A"));
    window.run();
    QTRY_VERIFY(editor->annotation(3).contains("A=2B"));
    QCOMPARE(editor->toPlainText(), source);
    window.stepInto(); // New session clears the previous run's annotations.
    QVERIFY(editor->annotation(2).isEmpty());
    QVERIFY(editor->annotation(3).isEmpty());
    editor->insertPlainText("; edited");
    QVERIFY(editor->annotation(1).isEmpty());
    editor->setPlainText(source);
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(editor->annotation(1).contains("A=2A"));
    QVERIFY(editor->annotation(2).contains("[8000]=2A"));
    QVERIFY(editor->annotation(3).contains("A=2B"));
    QCOMPARE(editor->toPlainText(), source);
  }
  void bundledSelectionRestoresF5Overlays() {
    Window window;
    auto *path = window.findChild<QLineEdit *>("simulatorPath");
    auto *hint = window.findChild<QLabel *>("simulatorOverlayHint");
    const auto bundledPath = path->text();
    path->setText("/custom/Z80Test");
    QVERIFY(hint->text().contains("final results only"));
    window.findChild<QPushButton *>("useBundledSimulator")->click();
    QCOMPARE(path->text(), bundledPath);
    QVERIFY(hint->text().contains("overlays enabled"));
    auto *editor = window.findChild<Editor *>("sourceEditor");
    editor->setPlainText("ld a,42\nbreak\n");
    QSignalSpy finished(&window, &Window::runFinished);
    window.findChild<QAction *>("runAction")->trigger();
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(editor->annotation(1).contains("A=2A"));
  }
  void preferencesAndOverlayRegisters() {
    QSettings().remove("overlayRegisters");
    Window window;
    auto *preferences = window.findChild<QDialog *>("preferencesDialog");
    auto *path = window.findChild<QLineEdit *>("simulatorPath");
    QVERIFY(preferences);
    QVERIFY(preferences->isAncestorOf(path));
    QVERIFY(!preferences->isVisible());
    window.findChild<QAction *>("preferencesAction")->trigger();
    QVERIFY(preferences->isVisible());
    preferences->hide();
    const QStringList defaults = {"A", "BC", "DE", "HL", "F"};
    for (auto *button : window.findChildren<QPushButton *>())
      if (button->objectName().startsWith("overlayRegister_"))
        QCOMPARE(button->isChecked(), defaults.contains(button->text()));
    auto *editor = window.findChild<Editor *>("sourceEditor");
    editor->setPlainText("ld b,1\nld a,2\nbreak\n");
    window.stepInto();
    QVERIFY(editor->annotation(1).contains("B=01"));
    QVERIFY(!editor->annotation(1).contains("PC="));
    QVERIFY(!editor->annotation(1).contains("R="));
    auto *bc = window.findChild<QPushButton *>("overlayRegister_BC");
    bc->click();
    QVERIFY(!editor->annotation(1).contains("B="));
    window.findChild<QPushButton *>("overlayRegister_PC")->click();
    QVERIFY(editor->annotation(1).contains("PC="));
    Window restored;
    QVERIFY(!restored.findChild<QPushButton *>("overlayRegister_BC")->isChecked());
    QVERIFY(restored.findChild<QPushButton *>("overlayRegister_PC")->isChecked());
    bc->click();
    window.findChild<QPushButton *>("overlayRegister_PC")->click();
    QVERIFY(editor->annotation(1).contains("B=01"));
    QVERIFY(editor->annotation(1).contains("+7 T"));
    QSettings().remove("overlayRegisters");
  }
  void annotationsPaintAtColumn60() {
    Editor editor;
    editor.resize(1100, 160);
    editor.setPlainText("ld a,42\n");
    editor.show();
    QTest::qWait(20);
    const int column60 = qRound(editor.document()->documentMargin()) +
                         60 * editor.fontMetrics().horizontalAdvance(' ');
    const QRect overlayArea(column60, 0, 130, editor.fontMetrics().height() + 10);
    const auto before = editor.viewport()->grab(overlayArea).toImage();
    editor.annotate(1, "A=2A");
    const auto after = editor.viewport()->grab(overlayArea).toImage();
    QVERIFY2(before != after, "Annotation must be visible at column 60");
    editor.clearAnnotations();
    QCOMPARE(editor.viewport()->grab(overlayArea).toImage(), before);
  }
  void annotationHexTooltips() {
    Editor editor;
    editor.resize(1400, 160);
    editor.setPlainText("ld hl,$ffff\n");
    editor.show();
    QTest::qWait(20);
    const QString annotation = "A=FF  HL=FFFF  F: C=1  [8000]=2A  +10 T";
    editor.annotate(1, annotation);
    const auto metrics = editor.fontMetrics();
    const int start = qRound(editor.document()->documentMargin()) +
                      60 * metrics.horizontalAdvance(' ');
    auto hover = [&](int index) {
      const QPoint pos(start + metrics.horizontalAdvance(annotation.left(index)) + 2,
                       editor.cursorRect().center().y());
      QHelpEvent event(QEvent::ToolTip, pos, editor.viewport()->mapToGlobal(pos));
      QApplication::sendEvent(editor.viewport(), &event);
    };
    hover(annotation.indexOf("FF"));
    QCOMPARE(QToolTip::text(), QString("255 (unsigned decimal)\n-1 (signed decimal)"));
    hover(annotation.indexOf("FFFF"));
    QCOMPARE(QToolTip::text(), QString("65535 (unsigned decimal)\n-1 (signed decimal)"));
    hover(annotation.indexOf("8000"));
    QCOMPARE(QToolTip::text(), QString("32768 (unsigned decimal)\n-32768 (signed decimal)"));
    hover(annotation.indexOf("2A"));
    QCOMPARE(QToolTip::text(), QString("42 (unsigned decimal)\n42 (signed decimal)"));
    hover(annotation.indexOf("C=1") + 2);
    QTRY_VERIFY(!QToolTip::isVisible());
    hover(annotation.indexOf("10 T"));
    QVERIFY(!QToolTip::isVisible());
  }
  void reopensLastFile() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("last.asm");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("ld a,23\nbreak\n");
    file.close();
    {
      Window window;
      QVERIFY(window.openFile(path));
    }
    {
      Window restored;
      auto *editor = restored.findChild<Editor *>("sourceEditor");
      QCOMPARE(editor->toPlainText(), QString("ld a,23\nbreak\n"));
      QVERIFY(!editor->document()->isModified());
      Window explicitFile(nullptr, false);
      QVERIFY(explicitFile.findChild<Editor *>("sourceEditor")->toPlainText() != editor->toPlainText());
    }
    QVERIFY(QFile::remove(path));
    {
      Window missing;
      QVERIFY(missing.windowTitle().contains("Untitled.asm"));
    }
    QSettings().remove("lastFile");
  }
  void preview() {
    if (qEnvironmentVariableIsEmpty("Z80EDITOR_SCREENSHOT"))
      return;
    Window window;
    window.show();
    QSignalSpy finished(&window, &Window::runFinished);
    window.run();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
    QTest::qWait(50);
    QVERIFY(window.grab().save(qEnvironmentVariable("Z80EDITOR_SCREENSHOT")));
  }

private:
  QTemporaryDir settings_;
};
QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
