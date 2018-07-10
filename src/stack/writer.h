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

struct Writer : public Visitor<Writer> {
  bool debug;

  BufferWithRandomAccess& o;

  Writer(Builder& builder, BufferWithRandomAccess& o, bool debug=false) : o(o) {
    for (auto* node : builder.nodes) {
      process(node);
    }
  }

  void process(Expression* curr) {
    if (!curr) return; // nullptr - just skip it
    visit(curr);
  }

  // AST writing via visitors
  int depth = 0; // only for debugging

  std::vector<Name> breakStack;
  Function::DebugLocation lastDebugLocation;
  size_t lastBytecodeOffset;

  void visit(Expression* curr);
  void visitBlock(Block *curr);
  void visitIf(If *curr);
  void visitLoop(Loop *curr);
  int32_t getBreakIndex(Name name);
  void visitBreak(Break *curr);
  void visitSwitch(Switch *curr);
  void visitCall(Call *curr);
  void visitCallImport(CallImport *curr);
  void visitCallIndirect(CallIndirect *curr);
  void visitGetLocal(GetLocal *curr);
  void visitSetLocal(SetLocal *curr);
  void visitGetGlobal(GetGlobal *curr);
  void visitSetGlobal(SetGlobal *curr);
  void emitMemoryAccess(size_t alignment, size_t bytes, uint32_t offset);
  void visitLoad(Load *curr);
  void visitStore(Store *curr);
  void visitAtomicRMW(AtomicRMW *curr);
  void visitAtomicCmpxchg(AtomicCmpxchg *curr);
  void visitAtomicWait(AtomicWait *curr);
  void visitAtomicWake(AtomicWake *curr);
  void visitConst(Const *curr);
  void visitUnary(Unary *curr);
  void visitBinary(Binary *curr);
  void visitSelect(Select *curr);
  void visitReturn(Return *curr);
  void visitHost(Host *curr);
  void visitNop(Nop *curr);
  void visitUnreachable(Unreachable *curr);
  void visitDrop(Drop *curr);
};

} // namespace stack

} // namespace wasm

#endif // wasm_stack_writer_h
