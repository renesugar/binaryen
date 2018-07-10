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
// This defines an IR for wasm in stack machine format, and implements
// building that IR from Binaryen IR.
//
// This IR is closer to wasm's binary format than Binaryen IR is. Binaryen
// IR focuses on making it easy to write useful optimization passes, while
// the Stack IR focuses on modeling the binary format more accurately. In
// general, most optimizations work best on Binaryen IR, but the Stack IR
// allows some specific "final" optimizations to be done before emitting
// the binary.
//
// The actual IR here is extremely simple: just a single flat vector of
// Expression* nodes, where each item is either:
//
//  * A pointer to a Binaryen expression.
//  * A pointer to a Custom, representing something not in Binaryen IR:
//    * A Block or If "end" marker.
//    * An If "else" marker.
//  * A nullptr, which means "nothing" - we support that to make it
//    easy and efficient to remove nodes, which is the most common
//    optimization.
//
// For example, consider this Stack IR:
//  * => block with name $b and result i32
//  * => i32.const 10
//  * => get_local $x
//  * nullptr
//  * => i32.add
//  * => custom (block "end")
// It represents something like this:
//  (block $b (result i32)
//   (i32.add
//    (i32.const 10)
//    (get_local $x)
//   )
//  )
// Note how the nullptr is ignored. Note also that if the nullptr
// were replaced with
//  * => call to a void(void) function $foo
// then there would be no Binaryen IR that is directly equivalent, and
// we'd need something like
//  (block $b (result i32)
//   (i32.add
//    (i32.const 10)
//    (block
//     (set_local $temp (get_local $x))
//     (call $foo)
//     (get_local $temp)
//    )
//   )
//  )
//

#ifndef wasm_stack_builder_h
#define wasm_stack_builder_h

#include "wasm.h"

namespace wasm {

namespace stack {

// Builds Stack IR for a given expression
struct Builder : public Visitor<Builder> {
  bool debug;

  std::vector<Expression*> nodes;

  Builder(Expression* expr, bool debug=false, bool possibleBlockContents=false) {
    assert(depth == 0);
    if (!possibleBlockContent) {
      recurse(curr);
    } else {
      recursePossibleBlockContents(curr);
    }
    assert(depth == 0);
  }

  void recurse(Expression* curr);

  // emits a node, but if it is a block with no name, emit a list of its contents
  void recursePossibleBlockContents(Expression* curr);

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

// A builder for function bodies, where the toplevel element is a possible
// list of items (so we don't need to emit an explicit block if we have more
// than one item).
class FunctionBodyBuilder : public Builder {
  FunctionBodyBuilder(Expression* expr, bool debug=false) {
    Builder(expr, debug, true) {}
};

} // namespace stack

} // namespace wasm

#endif // wasm_stack_builder_h
