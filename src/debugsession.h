#pragma once
#include <QString>
extern "C" {
#include "z80.h"
}

// Owns the complete machine, including RAM, across debugger commands.
class DebugSession {
public:
  Z80 cpu{};
  QString source;
  double clock = 3500000;
  quint64 limit = 1000000;
  static QString changes(const Z80 &before, const Z80 &after);
  QString terminalReason() const;
  QString results(const QString &reason) const;
};
