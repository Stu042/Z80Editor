#pragma once
#include <QPlainTextEdit>
#include <QMap>
#include <QSyntaxHighlighter>
class Editor;
class LineNumbers final : public QWidget {
public:
  explicit LineNumbers(Editor *editor);
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  Editor *editor_;
};
class Editor final : public QPlainTextEdit {
  Q_OBJECT
public:
  explicit Editor(QWidget *parent = nullptr);
  int numberWidth() const;
  void setExecutionLine(int number);
  void clearAnnotations();
  void annotate(int line, const QString &text);
  QString annotation(int line) const;
  void paintNumbers(QPaintEvent *event);
  void goToLine(int number, bool error = false);

protected:
  bool viewportEvent(QEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;

private:
  QRectF annotationRect(const QTextBlock &block, const QString &text) const;
  void updateMargins();
  void highlightLine();
  QMap<int, QString> annotations_;
  LineNumbers *numbers_;
  int errorLine_ = -1;
  int executionLine_ = -1;
};
class AssemblyHighlighter final : public QSyntaxHighlighter {
public:
  explicit AssemblyHighlighter(QTextDocument *document);

protected:
  void highlightBlock(const QString &text) override;
};
