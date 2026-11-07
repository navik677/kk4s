/*
  rlBabel: Japanese (CP932 / Shift_JIS) settings

  Copyright (c) 2006 Peter Jolly.

  This library is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 2.1 of the License, or (at your option)
  any later version.

  This library is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
  details.

  You should have received a copy of the GNU Lesser General Public License
  along with this library; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

  As a special exception to the GNU Lesser General Public License (LGPL), you
  may include a publicly distributed version of the library alongside a "work
  that uses the Library" to produce a composite work that includes the library,
  and distribute that work under terms of your choice, without any of the
  additional requirements listed in clause 6 of the LGPL.

  A "publicly distributed version of the library" means either an unmodified
  binary as distributed by Haeleth, or a modified version of the library that is
  distributed under the conditions defined in clause 2 of the LGPL, and a
  "composite work that includes the library" means a RealLive program which
  links to the library, either through the LoadDLL() interface or a #DLL
  directive, and/or includes code from the library's Kepago header.

  Note that this exception does not invalidate any other reasons why any part of
  the work might be covered by the LGPL.
*/

#ifndef SRC_ENCODINGS_CP932_H_
#define SRC_ENCODINGS_CP932_H_

#include <string>

#include "encodings/codepage.h"

struct Cp932 : public Codepage {
  virtual unsigned short Convert(unsigned short ch) const;
  virtual std::wstring ConvertString(const std::string& s) const;
  Cp932();
};

// kk4s: some fan translations keep the game in CP932 and hook the Windows
// text functions (a dsound.dll in the game folder) so that the one-byte codes
// 0xA1-0xDF, half-width katakana, are drawn as other letters (the Russian
// CLANNAD: Cyrillic). The hook carries a table of 63 UTF-16 characters for
// those codes. LoadCp932HalfwidthTable() looks for such a table in |dll| and,
// when found, Cp932::Convert() uses it. Returns the number of characters
// that differ from half-width katakana, or 0 if no table was found.
int LoadCp932HalfwidthTable(const std::string& dll);

// kk4s: true if |codepoint| is one of the characters of a loaded table. They
// stand for one-byte codes, so the text window gives them their own width
// instead of a full-width cell.
bool IsCp932HalfwidthTableChar(int codepoint);

#endif  // SRC_ENCODINGS_CP932_H_
