// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of libreallive, a dependency of RLVM.
//
// -----------------------------------------------------------------------
//
// Copyright (c) 2006 Peter Jolly
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// -----------------------------------------------------------------------

#include "libreallive/filemap.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
// kk4s: Windows has no mmap and no POSIX open(); Mapping reads the file into
// memory and writes it back on close. (The old WIN32 branch here never
// compiled: mingw already defines WIN32 and O_BINARY/O_RDONLY as macros.)
const HANDLE INVALID_HANDLE_VALUE = nullptr;
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef O_BINARY
const int O_BINARY = 0;
#endif

namespace libreallive {

void Mapping::mclose() {
#if defined(_WIN32)
  if (fp) {
    FILE* f = (FILE*)fp;
    if (mode_ == Write && mem && len) {
      fseek(f, 0, SEEK_SET);
      fwrite(mem, 1, len, f);
    }
    fclose(f);
    fp = nullptr;
  }
  delete[] (char*)mem;
  mem = nullptr;
  mapped = false;
  len = 0;
#else
  if (mem && mapped) {
    munmap(mem, len);
  } else if (mem) {
    delete[] (char*)mem;
  }
  if (fp != INVALID_HANDLE_VALUE)
    close(fp);
#endif
}

void Mapping::mopen() {
#if defined(_WIN32)
  FILE* f = fopen(fn_.c_str(), mode_ == Read ? "rb" : "r+b");
  if (!f && mode_ != Read)
    f = fopen(fn_.c_str(), "wb");
  if (!f) {
    if (mode_ == Read)
      throw Error("File not found");
    throw Error("Could not open file");
  }
  fseek(f, 0, SEEK_END);
  long long file_size = ftell(f);
  if (file_size < 0)
    file_size = 0;
  fseek(f, 0, SEEK_SET);
  len = (size_t)std::max((long long)msz_, file_size);
  mem = (void*)(new char[len]);
  if (file_size > 0 && fread(mem, 1, (size_t)file_size, f) != (size_t)file_size) {
    fclose(f);
    delete[] (char*)mem;
    mem = nullptr;
    len = 0;
    throw Error("Could not read file");
  }
  for (long long i = file_size; i < (long long)len; ++i)
    ((char*)mem)[i] = 0;
  fp = (HANDLE)f;
  mapped = true;
#else
  struct stat st;
  if (stat(fn_.c_str(), &st) != 0) {
    if (errno != ENOENT)
      throw Error("Could not open file");
    if (mode_ == Read)
      throw Error("File not found");
    if (msz_ == 0)
      throw Error("Cannot create empty file");
    len = msz_;
  } else {
    len = std::max((long long)msz_, (long long)st.st_size);
  }
  fp = open(fn_.c_str(), O_BINARY | (mode_ == Read ? O_RDONLY : O_RDWR), 0644);
  if (fp == INVALID_HANDLE_VALUE)
    throw Error("Could not open file");
  mem = mmap(0, len, mode_ == Read ? PROT_READ : PROT_READ | PROT_WRITE,
             MAP_SHARED, fp, 0);
  if (mem == MAP_FAILED) {
    close(fp);
    fp = INVALID_HANDLE_VALUE;
    if (mode_ != Read) {
      throw Error("Could not map memory");
    } else {
      mapped = false;
      mem = (void*)(new char[len]);
      FILE* fh = fopen(fn_.c_str(), "rb");
      fread(mem, 1, len, fh);
      fclose(fh);
    }
  } else {
    mapped = true;
  }
#endif
}

Mapping::Mapping(string filename, Mode mode, off_t min_size)
    : fp(INVALID_HANDLE_VALUE),
      mem(NULL),
      fn_(filename),
      mode_(mode),
      msz_(min_size) {
  mopen();
}

Mapping::~Mapping() { mclose(); }

}  // namespace libreallive
