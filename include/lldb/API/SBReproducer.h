//===-- SBReproducer.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBREPRODUCER_H
#define LLDB_API_SBREPRODUCER_H

#include "lldb/lldb-defines.h"

namespace lldb {

class LLDB_API SBReproducer {
public:
  bool Replay() const;
};

} // namespace lldb

#endif
