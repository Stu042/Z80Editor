#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName("Z80Tools");
  QCoreApplication::setApplicationName("Z80Editor");
  QCoreApplication::setApplicationVersion("1.0");
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Z80 / Z80N assembly editor. Press F5 to run Z80Test.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "Assembly file to open", "[file.asm]");
  parser.process(app);
  if (parser.positionalArguments().size() > 1)
    parser.showHelp(1);
  Window window(nullptr, parser.positionalArguments().isEmpty());
  if (!parser.positionalArguments().isEmpty() &&
      !window.openFile(parser.positionalArguments().first()))
    return 1;
  window.show();
  return app.exec();
}
