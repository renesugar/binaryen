/*
 * Copyright 2018 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// This implements writing Stack IR to the wasm binary format.
//

#ifndef wasm_stack_writer_h
#define wasm_stack_writer_h

#include "wasm.h"
#include "wasm-binary.h"

namespace wasm {

namespace stack {

class Writer {
  Writer(Builder& builder, BufferWithRandomAccess& o, bool possibleBlockContent=false) {
  }
};

class PossibleBlockContentWriter : public Writer {
  PossibleBlockContentWriter(Builder& builder, BufferWithRandomAccess& o) :
    Writer(builder, o, true) {}
};

} // namespace stack

} // namespace wasm

#endif // wasm_stack_writer_h
