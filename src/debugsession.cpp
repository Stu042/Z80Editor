#include "debugsession.h"
#include <QStringList>
#include <cstring>

QString DebugSession::terminalReason() const {
  if (cpu.stopped || (!cpu.halted && cpu.breakpoints[cpu.pc]))
    return "BREAK";
  if (!cpu.halted && !cpu.loaded[cpu.pc])
    return "end of assembled code";
  if (cpu.steps >= limit)
    return "step limit reached";
  return {};
}
QString DebugSession::results(const QString &reason) const {
  auto hex = [](unsigned value, int width) {
    return QString::number(value, 16).rightJustified(width, '0').toUpper();
  };
  QStringList lines;
  QStringList bytes;
  const unsigned values[] = {cpu.a, cpu.f, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l};
  const char *names[] = {"A", "F", "B", "C", "D", "E", "H", "L"};
  for (int i = 0; i < 8; ++i)
    bytes << QString("%1=%2 (0x%3)").arg(names[i]).arg(values[i]).arg(hex(values[i], 2));
  lines << bytes.join(' ');
  QStringList pairs;
  const unsigned words[] = {unsigned(cpu.a << 8 | cpu.f), unsigned(cpu.b << 8 | cpu.c),
    unsigned(cpu.d << 8 | cpu.e), unsigned(cpu.h << 8 | cpu.l), cpu.ix, cpu.iy, cpu.sp, cpu.pc};
  const char *wordNames[] = {"AF", "BC", "DE", "HL", "IX", "IY", "SP", "PC"};
  for (int i = 0; i < 8; ++i)
    pairs << QString("%1=%2 (0x%3)").arg(wordNames[i]).arg(words[i]).arg(hex(words[i], 4));
  lines << pairs.join(' ');
  lines << QString("AF'=%1 BC'=%2 DE'=%3 HL'=%4 I=%5 R=%6")
    .arg(hex(cpu.a2 << 8 | cpu.f2, 4), hex(cpu.b2 << 8 | cpu.c2, 4),
         hex(cpu.d2 << 8 | cpu.e2, 4), hex(cpu.h2 << 8 | cpu.l2, 4), hex(cpu.i, 2), hex(cpu.r, 2));
  lines << QString("IFF1=%1 IFF2=%2 IM=%3 HALT=%4").arg(cpu.iff1).arg(cpu.iff2).arg(cpu.im).arg(cpu.halted);
  auto flags = [](unsigned f) {
    return QString("S=%1 Z=%2 Y=%3 H=%4 X=%5 P/V=%6 N=%7 C=%8")
      .arg(!!(f & 128)).arg(!!(f & 64)).arg(!!(f & 32)).arg(!!(f & 16))
      .arg(!!(f & 8)).arg(!!(f & 4)).arg(!!(f & 2)).arg(!!(f & 1));
  };
  lines << "Flags: " + flags(cpu.f) << "Alternate flags: " + flags(cpu.f2);
  lines << QString("Instructions: %1\nT-states: %2\nTime: %3 seconds (%4 Hz)")
    .arg(cpu.steps).arg(cpu.cycles).arg(cpu.cycles / clock, 0, 'f', 9).arg(clock, 0, 'f', 0);
  lines << "Stopped: " + reason;
  return lines.join('\n');
}

QString DebugSession::changes(const Z80 &before, const Z80 &after) {
  // Use the executed instruction, since a word operation may change only one byte.
  unsigned wordPairs = 0;
  quint16 pc = before.pc;
  unsigned prefix = 0;
  unsigned op = before.mem[pc++];
  for (unsigned count = 0; (op == 0xdd || op == 0xfd) && count < 65536; ++count) {
    prefix = op;
    op = before.mem[pc++];
  }
  auto pair = [&](unsigned p, bool indexed = true) {
    if (p < 3 && !(p == 2 && indexed && prefix))
      wordPairs |= 1u << (p + 1); // AF, BC, DE, HL
  };
  if (op == 0xed) {
    op = before.mem[pc];
    if ((op & 0xcf) == 0x4b)
      pair((op >> 4) & 3, false);
    if ((op & 0xcf) == 0x42 || (op & 0xcf) == 0x4a)
      pair(2, false);
    if ((op & 0xe6) == 0xa2) // Block I/O changes B as a byte and HL as a word.
      pair(2, false);
    if ((op & 0xe6) == 0xa0) { // Block transfers and comparisons.
      pair(0, false);
      pair(2, false);
      if (!(op & 1))
        pair(1, false);
    }
    if (op == 0x94)
      pair(2, false);
    if ((op >= 0x28 && op <= 0x2c) || op == 0x30)
      pair(1, false);
    if (op >= 0x31 && op <= 0x36)
      pair(2 - (op - (op >= 0x34 ? 0x34 : 0x31)), false);
    if (op == 0xa4 || op == 0xac || op == 0xb4 || op == 0xbc || op == 0xb7) {
      pair(0, false);
      pair(1, false);
      if (op != 0xb7)
        pair(2, false);
    }
  } else {
    if ((op & 0xcf) == 0x01 || (op & 0xcf) == 0x03 || (op & 0xcf) == 0x0b)
      pair((op >> 4) & 3);
    if ((op & 0xcf) == 0x09 || op == 0x2a || op == 0xe3)
      pair(2);
    if ((op & 0xcf) == 0xc1) {
      if (op == 0xf1)
        wordPairs |= 1;
      else
        pair((op >> 4) & 3);
    }
    if (op == 0xeb) {
      pair(1, false);
      pair(2, false);
    }
    if (op == 0x08)
      wordPairs |= 0x11; // AF and AF'.
    if (op == 0xd9)
      wordPairs |= 0xee; // BC, DE, HL and their alternate pairs.
  }
  QStringList values;
  auto add = [&](const QString &name, unsigned a, unsigned b, int width) {
    if (a != b)
      values << name + "=" + QString::number(b, 16).rightJustified(width, '0').toUpper();
  };
  auto addFlags = [&](const QString &name, unsigned a, unsigned b) {
    const char *names[] = {"S", "Z", "Y", "H", "X", "P/V", "N", "C"};
    QStringList bits;
    for (int i = 0; i < 8; ++i) {
      const unsigned mask = 128u >> i;
      if ((a ^ b) & mask)
        bits << QString("%1=%2").arg(names[i]).arg(!!(b & mask));
    }
    if (!bits.isEmpty())
      values << name + ": " + bits.join(' ');
  };
#define REG(r, name, width) add(name, before.r, after.r, width)
  auto addPair = [&](unsigned bit, const QString &name, const QString &highName,
                     const QString &lowName, unsigned ah, unsigned al, unsigned bh, unsigned bl) {
    if (wordPairs & (1u << bit))
      add(name, (ah << 8) | al, (bh << 8) | bl, 4);
    else {
      add(highName, ah, bh, 2);
      if (bit % 4 != 0)
        add(lowName, al, bl, 2);
    }
  };
#define PAIR(bit, name, hi, lo, hn, ln) \
  addPair(bit, name, hn, ln, before.hi, before.lo, after.hi, after.lo)
  PAIR(0, "AF", a, f, "A", "F");
  PAIR(1, "BC", b, c, "B", "C");
  PAIR(2, "DE", d, e, "D", "E");
  PAIR(3, "HL", h, l, "H", "L");
  addFlags("F", before.f, after.f);
  addFlags("F'", before.f2, after.f2);
  PAIR(4, "AF'", a2, f2, "A'", "F'");
  PAIR(5, "BC'", b2, c2, "B'", "C'");
  PAIR(6, "DE'", d2, e2, "D'", "E'");
  PAIR(7, "HL'", h2, l2, "H'", "L'");
#undef PAIR
  REG(ix, "IX", 4); REG(iy, "IY", 4); REG(sp, "SP", 4); REG(pc, "PC", 4);
  REG(i, "I", 2); REG(r, "R", 2); REG(iff1, "IFF1", 1); REG(iff2, "IFF2", 1);
  REG(im, "IM", 1); REG(halted, "HALT", 1);
#undef REG
  if (std::memcmp(before.mem, after.mem, sizeof before.mem) != 0)
  for (unsigned address = 0; address < 65536; ++address)
    if (before.mem[address] != after.mem[address])
      add("[" + QString::number(address, 16).rightJustified(4, '0').toUpper() + "]",
          before.mem[address], after.mem[address], 2);
  values << QString("+%1 T").arg(after.cycles - before.cycles);
  return values.join("  ");
}
