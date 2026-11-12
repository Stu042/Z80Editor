#include "editor.h"
#include <QFontDatabase>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>
#include <QToolTip>
LineNumbers::LineNumbers(Editor *editor) : QWidget(editor), editor_(editor) {}
QSize LineNumbers::sizeHint() const { return {editor_->numberWidth(), 0}; }
void LineNumbers::paintEvent(QPaintEvent *event) {
  editor_->paintNumbers(event);
}
Editor::Editor(QWidget *parent)
    : QPlainTextEdit(parent), numbers_(new LineNumbers(this)) {
  setObjectName("sourceEditor");
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  setLineWrapMode(QPlainTextEdit::NoWrap);
  setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
  connect(this, &QPlainTextEdit::blockCountChanged, this,
          [this] { updateMargins(); });
  connect(this, &QPlainTextEdit::updateRequest, this,
          [this](const QRect &rect, int dy) {
            if (dy)
              numbers_->scroll(0, dy);
            else
              numbers_->update(0, rect.y(), numbers_->width(), rect.height());
            if (rect.contains(viewport()->rect()))
              updateMargins();
          });
  connect(this, &QPlainTextEdit::cursorPositionChanged, this,
          &Editor::highlightLine);
  connect(this, &QPlainTextEdit::textChanged, this, [this] {
    clearAnnotations();
    errorLine_ = -1;
    highlightLine();
  });
  new AssemblyHighlighter(document());
  updateMargins();
  highlightLine();
}
int Editor::numberWidth() const {
  int digits = 1;
  for (int n = blockCount(); n >= 10; n /= 10)
    ++digits;
  return 20 + fontMetrics().horizontalAdvance('9') * digits;
}
void Editor::updateMargins() { setViewportMargins(numberWidth(), 0, 0, 0); }
void Editor::resizeEvent(QResizeEvent *event) {
  QPlainTextEdit::resizeEvent(event);
  const auto r = contentsRect();
  numbers_->setGeometry(r.left(), r.top(), numberWidth(), r.height());
}
void Editor::paintNumbers(QPaintEvent *event) {
  QPainter painter(numbers_);
  painter.fillRect(event->rect(), palette().alternateBase());
  auto block = firstVisibleBlock();
  int top =
      qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
  while (block.isValid() && top <= event->rect().bottom()) {
    const int height = qRound(blockBoundingRect(block).height());
    if (block.isVisible() && top + height >= event->rect().top()) {
      painter.setPen(palette().color(QPalette::PlaceholderText));
      painter.drawText(0, top, numbers_->width() - 9, fontMetrics().height(),
                       Qt::AlignRight,
                       QString::number(block.blockNumber() + 1));
    }
    top += height;
    block = block.next();
  }
}
void Editor::highlightLine() {
  QList<QTextEdit::ExtraSelection> selections;
  QTextEdit::ExtraSelection current;
  current.cursor = textCursor();
  current.cursor.clearSelection();
  QColor color = palette().color(QPalette::Highlight);
  color.setAlpha(25);
  current.format.setBackground(color);
  current.format.setProperty(QTextFormat::FullWidthSelection, true);
  selections.append(current);
  if (errorLine_ >= 0) {
    const auto block = document()->findBlockByNumber(errorLine_);
    if (block.isValid()) {
      QTextEdit::ExtraSelection error;
      error.cursor = QTextCursor(block);
      error.format.setBackground(QColor(220, 60, 60, 55));
      error.format.setProperty(QTextFormat::FullWidthSelection, true);
      selections.append(error);
    }
  }
  if (executionLine_ >= 0) {
    const auto block = document()->findBlockByNumber(executionLine_);
    if (block.isValid()) {
      QTextEdit::ExtraSelection execution;
      execution.cursor = QTextCursor(block);
      execution.format.setBackground(QColor(230, 180, 30, 80));
      execution.format.setProperty(QTextFormat::FullWidthSelection, true);
      selections.append(execution);
    }
  }
  setExtraSelections(selections);
}
void Editor::goToLine(int number, bool error) {
  auto block = document()->findBlockByNumber(number - 1);
  if (!block.isValid())
    return;
  errorLine_ = error ? number - 1 : -1;
  setTextCursor(QTextCursor(block));
  centerCursor();
  highlightLine();
}
void Editor::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Tab && !textCursor().hasSelection()) {
    insertPlainText(QString(4 - textCursor().positionInBlock() % 4, ' '));
    return;
  }
  QPlainTextEdit::keyPressEvent(event);
}
AssemblyHighlighter::AssemblyHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document) {}
void AssemblyHighlighter::highlightBlock(const QString &text) {
  static const QRegularExpression tokens(
      R"(("[^"\n]*"|'[^'\n]'|;[^\n]*|\b[a-zA-Z_.][a-zA-Z0-9_.]*'?|\$[0-9a-fA-F]+|%[01]+|\b[0-9][a-zA-Z0-9]*))");
  static const QStringList instructions =
      QStringLiteral(
          "LD INC DEC ADD ADC SUB SBC AND XOR OR CP PUSH POP JP JR DJNZ CALL "
          "RET RST "
          "EX EXX BIT RES SET RLC RRC RL RR SLA SRA SLL SRL NOP HALT RLCA RRCA "
          "RLA RRA "
          "DAA CPL SCF CCF DI EI NEG RETN RETI RRD RLD LDI LDIR LDD LDDR CPI "
          "CPIR CPD CPDR "
          "IN OUT INI INIR IND INDR OUTI OTIR OUTD OTDR IM SWAPNIB SWAP MIRROR "
          "MIRR TEST "
          "BSLA BSRA BSRL BSRF BRLC MUL OUTINB OTIB NEXTREG NREG PIXELDN PXDN "
          "PIXELAD PXAD "
          "SETAE STAE LDIX LDWS LDDX LDIRX LIRX LDPIRX LPRX LDDRX LDRX")
          .split(' ');
  static const QStringList directives =
      QStringLiteral(
          "BREAK EQU ORG .ORG DB DEFB .BYTE DW DEFW .WORD DS DEFS .SPACE END")
          .split(' ');
  static const QStringList registers =
      QStringLiteral("A F B C D E H L AF BC DE HL IX IY SP PC I R AF' BC' DE' "
                     "HL' NZ Z NC PO PE P M")
          .split(' ');
  auto matches = tokens.globalMatch(text);
  while (matches.hasNext()) {
    const auto match = matches.next();
    const auto token = match.captured();
    const auto upper = token.toUpper();
    QTextCharFormat format;
    if (token.startsWith(';')) {
      format.setForeground(QColor("#728675"));
      format.setFontItalic(true);
    } else if (token.startsWith('"') || token.startsWith('\''))
      format.setForeground(QColor("#a16a22"));
    else if (directives.contains(upper)) {
      format.setForeground(QColor("#a264b6"));
      format.setFontWeight(QFont::Bold);
    } else if (instructions.contains(upper)) {
      format.setForeground(QColor("#3886c7"));
      format.setFontWeight(QFont::Bold);
    } else if (registers.contains(upper))
      format.setForeground(QColor("#299b91"));
    else if (token.at(0).isDigit() || token.startsWith('$') ||
             token.startsWith('%'))
      format.setForeground(QColor("#bf7b35"));
    else if (text.mid(match.capturedEnd()).trimmed().startsWith(':')) {
      format.setForeground(QColor("#a16a22"));
      format.setFontWeight(QFont::Bold);
    } else
      continue;
    setFormat(match.capturedStart(), match.capturedLength(), format);
  }
}

void Editor::setExecutionLine(int number) {
  executionLine_ = number - 1;
  highlightLine();
}

void Editor::clearAnnotations() {
  annotations_.clear();
  viewport()->update();
}
void Editor::annotate(int line, const QString &text) {
  if (line > 0) {
    annotations_[line] = text;
    viewport()->update();
  }
}
QString Editor::annotation(int line) const { return annotations_.value(line); }
QRectF Editor::annotationRect(const QTextBlock &block, const QString &text) const {
  const auto rect = blockBoundingGeometry(block).translated(contentOffset());
  const qreal column60 = document()->documentMargin() +
                         60 * fontMetrics().horizontalAdvance(' ');
  const auto layout = block.layout();
  const qreal codeEnd = layout->lineCount() > 0
                            ? layout->lineAt(0).naturalTextRect().right() + 24
                            : document()->documentMargin();
  const qreal x = rect.left() + qMax(column60, codeEnd) - 6;
  return {x, rect.top(), qreal(fontMetrics().horizontalAdvance(text) + 12), rect.height()};
}
bool Editor::viewportEvent(QEvent *event) {
  if (event->type() == QEvent::ToolTip) {
    const auto *help = static_cast<QHelpEvent *>(event);
    auto block = firstVisibleBlock();
    while (block.isValid()) {
      const auto text = annotations_.value(block.blockNumber() + 1);
      const auto rect = annotationRect(block, text);
      if (rect.top() > viewport()->height())
        break;
      if (!text.isEmpty() && rect.contains(help->pos())) {
        // Register/memory values have two or four digits; flag bits and timing are decimal.
        static const QRegularExpression hex(R"((?<=\[)[0-9A-F]{4}(?=\])|(?<==)(?:[0-9A-F]{4}|[0-9A-F]{2})(?![0-9A-F]))");
        auto matches = hex.globalMatch(text);
        while (matches.hasNext()) {
          const auto match = matches.next();
          const qreal left = rect.left() + 6 +
              fontMetrics().horizontalAdvance(text.left(match.capturedStart()));
          const QRectF target(left, rect.top(), fontMetrics().horizontalAdvance(match.captured()), rect.height());
          if (target.contains(help->pos())) {
            const auto digits = match.captured();
            const unsigned value = digits.toUInt(nullptr, 16);
            const unsigned range = 1u << (digits.size() * 4);
            const int signedValue = value >= range / 2 ? int(value) - int(range) : int(value);
            QToolTip::showText(help->globalPos(),
                QString("%1 (unsigned decimal)\n%2 (signed decimal)").arg(value).arg(signedValue),
                viewport(), target.toAlignedRect());
            return true;
          }
        }
      }
      block = block.next();
    }
    QToolTip::hideText();
    event->ignore();
    return true;
  }
  return QPlainTextEdit::viewportEvent(event);
}
void Editor::paintEvent(QPaintEvent *event) {
  QPlainTextEdit::paintEvent(event);
  QPainter painter(viewport());
  painter.setFont(font());
  auto block = firstVisibleBlock();
  while (block.isValid()) {
    const auto rect = blockBoundingGeometry(block).translated(contentOffset());
    if (rect.top() > viewport()->height())
      break;
    const auto text = annotations_.value(block.blockNumber() + 1);
    if (!text.isEmpty() && rect.bottom() >= 0) {
      const auto label = annotationRect(block, text);
      painter.fillRect(label, palette().alternateBase());
      painter.setPen(palette().color(QPalette::Link));
      painter.drawText(label.adjusted(6, 0, -6, 0), Qt::AlignVCenter, text);
    }
    block = block.next();
  }
}
