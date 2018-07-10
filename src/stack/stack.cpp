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

void Writer::recurse(Expression* curr) {
  // TODO: add optional debugging stuff?
  visit(curr);
}

// emits a node, but if it is a block with no name, emit a list of its contents
void Writer::recursePossibleBlockContents(Expression* curr) {
  auto* block = curr->dynCast<Block>();
  if (!block || brokenTo(block)) {
    recurse(curr);
    return;
  }
  for (auto* child : block->list) {
    recurse(child);
  }
  if (block->type == unreachable && block->list.back()->type != unreachable) {
    // similar to in visitBlock, here we could skip emitting the block itself,
    // but must still end the 'block' (the contents, really) with an unreachable
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visit(Expression* curr) {
  if (sourceMap && currFunction) {
    // Dump the sourceMap debug info
    auto& debugLocations = currFunction->debugLocations;
    auto iter = debugLocations.find(curr);
    if (iter != debugLocations.end() && iter->second != lastDebugLocation) {
      writeDebugLocation(o.size(), iter->second);
    }
  }
  Visitor<WasmBinaryWriter>::visit(curr);
}

void Writer::visitBlock(Block *curr) {
  if (debug) std::cerr << "zz node: Block" << std::endl;
  o << int8_t(BinaryConsts::Block);
  o << binaryType(curr->type != unreachable ? curr->type : none);
  breakStack.push_back(curr->name);
  Index i = 0;
  for (auto* child : curr->list) {
    if (debug) std::cerr << "  " << size_t(curr) << "\n zz Block element " << i++ << std::endl;
    recurse(child);
  }
  breakStack.pop_back();
  if (curr->type == unreachable) {
    // an unreachable block is one that cannot be exited. We cannot encode this directly
    // in wasm, where blocks must be none,i32,i64,f32,f64. Since the block cannot be
    // exited, we can emit an unreachable at the end, and that will always be valid,
    // and then the block is ok as a none
    o << int8_t(BinaryConsts::Unreachable);
  }
  o << int8_t(BinaryConsts::End);
  if (curr->type == unreachable) {
    // and emit an unreachable *outside* the block too, so later things can pop anything
    o << int8_t(BinaryConsts::Unreachable);
  }
}

static bool brokenTo(Block* block) {
  return block->name.is() && BranchUtils::BranchSeeker::hasNamed(block, block->name);
}

void Writer::visitIf(If *curr) {
  if (debug) std::cerr << "zz node: If" << std::endl;
  if (curr->condition->type == unreachable) {
    // this if-else is unreachable because of the condition, i.e., the condition
    // does not exit. So don't emit the if, but do consume the condition
    recurse(curr->condition);
    o << int8_t(BinaryConsts::Unreachable);
    return;
  }
  recurse(curr->condition);
  o << int8_t(BinaryConsts::If);
  o << binaryType(curr->type != unreachable ? curr->type : none);
  breakStack.push_back(IMPOSSIBLE_CONTINUE); // the binary format requires this; we have a block if we need one; TODO: optimize
  recursePossibleBlockContents(curr->ifTrue); // TODO: emit block contents directly, if possible
  breakStack.pop_back();
  if (curr->ifFalse) {
    o << int8_t(BinaryConsts::Else);
    breakStack.push_back(IMPOSSIBLE_CONTINUE); // TODO ditto
    recursePossibleBlockContents(curr->ifFalse);
    breakStack.pop_back();
  }
  o << int8_t(BinaryConsts::End);
  if (curr->type == unreachable) {
    // we already handled the case of the condition being unreachable. otherwise,
    // we may still be unreachable, if we are an if-else with both sides unreachable.
    // wasm does not allow this to be emitted directly, so we must do something more. we could do
    // better, but for now we emit an extra unreachable instruction after the if, so it is not consumed itself,
    assert(curr->ifFalse);
    o << int8_t(BinaryConsts::Unreachable);
  }
}
void Writer::visitLoop(Loop *curr) {
  if (debug) std::cerr << "zz node: Loop" << std::endl;
  o << int8_t(BinaryConsts::Loop);
  o << binaryType(curr->type != unreachable ? curr->type : none);
  breakStack.push_back(curr->name);
  recursePossibleBlockContents(curr->body);
  breakStack.pop_back();
  o << int8_t(BinaryConsts::End);
  if (curr->type == unreachable) {
    // we emitted a loop without a return type, so it must not be consumed
    o << int8_t(BinaryConsts::Unreachable);
  }
}

int32_t Writer::getBreakIndex(Name name) { // -1 if not found
  for (int i = breakStack.size() - 1; i >= 0; i--) {
    if (breakStack[i] == name) {
      return breakStack.size() - 1 - i;
    }
  }
  std::cerr << "bad break: " << name << " in " << currFunction->name << std::endl;
  abort();
}

void Writer::visitBreak(Break *curr) {
  if (debug) std::cerr << "zz node: Break" << std::endl;
  if (curr->value) {
    recurse(curr->value);
  }
  if (curr->condition) recurse(curr->condition);
  o << int8_t(curr->condition ? BinaryConsts::BrIf : BinaryConsts::Br)
    << U32LEB(getBreakIndex(curr->name));
  if (curr->condition && curr->type == unreachable) {
    // a br_if is normally none or emits a value. if it is unreachable,
    // then either the condition or the value is unreachable, which is
    // extremely rare, and may require us to make the stack polymorphic
    // (if the block we branch to has a value, we may lack one as we
    // are not a reachable branch; the wasm spec on the other hand does
    // presume the br_if emits a value of the right type, even if it
    // popped unreachable)
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visitSwitch(Switch *curr) {
  if (debug) std::cerr << "zz node: Switch" << std::endl;
  if (curr->value) {
    recurse(curr->value);
  }
  recurse(curr->condition);
  if (!BranchUtils::isBranchReachable(curr)) {
    // if the branch is not reachable, then it's dangerous to emit it, as
    // wasm type checking rules are different, especially in unreachable
    // code. so just don't emit that unreachable code.
    o << int8_t(BinaryConsts::Unreachable);
    return;
  }
  o << int8_t(BinaryConsts::TableSwitch) << U32LEB(curr->targets.size());
  for (auto target : curr->targets) {
    o << U32LEB(getBreakIndex(target));
  }
  o << U32LEB(getBreakIndex(curr->default_));
}

void Writer::visitCall(Call *curr) {
  if (debug) std::cerr << "zz node: Call" << std::endl;
  for (auto* operand : curr->operands) {
    recurse(operand);
  }
  o << int8_t(BinaryConsts::CallFunction) << U32LEB(getFunctionIndex(curr->target));
  if (curr->type == unreachable) {
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visitCallImport(CallImport *curr) {
  if (debug) std::cerr << "zz node: CallImport" << std::endl;
  for (auto* operand : curr->operands) {
    recurse(operand);
  }
  o << int8_t(BinaryConsts::CallFunction) << U32LEB(getFunctionIndex(curr->target));
}

void Writer::visitCallIndirect(CallIndirect *curr) {
  if (debug) std::cerr << "zz node: CallIndirect" << std::endl;

  for (auto* operand : curr->operands) {
    recurse(operand);
  }
  recurse(curr->target);
  o << int8_t(BinaryConsts::CallIndirect)
    << U32LEB(getFunctionTypeIndex(curr->fullType))
    << U32LEB(0); // Reserved flags field
  if (curr->type == unreachable) {
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visitGetLocal(GetLocal *curr) {
  if (debug) std::cerr << "zz node: GetLocal " << (o.size() + 1) << std::endl;
  o << int8_t(BinaryConsts::GetLocal) << U32LEB(mappedLocals[curr->index]);
}

void Writer::visitSetLocal(SetLocal *curr) {
  if (debug) std::cerr << "zz node: Set|TeeLocal" << std::endl;
  recurse(curr->value);
  o << int8_t(curr->isTee() ? BinaryConsts::TeeLocal : BinaryConsts::SetLocal) << U32LEB(mappedLocals[curr->index]);
  if (curr->type == unreachable) {
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visitGetGlobal(GetGlobal *curr) {
  if (debug) std::cerr << "zz node: GetGlobal " << (o.size() + 1) << std::endl;
  o << int8_t(BinaryConsts::GetGlobal) << U32LEB(getGlobalIndex(curr->name));
}

void Writer::visitSetGlobal(SetGlobal *curr) {
  if (debug) std::cerr << "zz node: SetGlobal" << std::endl;
  recurse(curr->value);
  o << int8_t(BinaryConsts::SetGlobal) << U32LEB(getGlobalIndex(curr->name));
}

void Writer::emitMemoryAccess(size_t alignment, size_t bytes, uint32_t offset) {
  o << U32LEB(Log2(alignment ? alignment : bytes));
  o << U32LEB(offset);
}

void Writer::visitLoad(Load *curr) {
  if (debug) std::cerr << "zz node: Load" << std::endl;
  recurse(curr->ptr);
  if (!curr->isAtomic) {
    switch (curr->type) {
      case i32: {
        switch (curr->bytes) {
          case 1: o << int8_t(curr->signed_ ? BinaryConsts::I32LoadMem8S : BinaryConsts::I32LoadMem8U); break;
          case 2: o << int8_t(curr->signed_ ? BinaryConsts::I32LoadMem16S : BinaryConsts::I32LoadMem16U); break;
          case 4: o << int8_t(BinaryConsts::I32LoadMem); break;
          default: abort();
        }
        break;
      }
      case i64: {
        switch (curr->bytes) {
          case 1: o << int8_t(curr->signed_ ? BinaryConsts::I64LoadMem8S : BinaryConsts::I64LoadMem8U); break;
          case 2: o << int8_t(curr->signed_ ? BinaryConsts::I64LoadMem16S : BinaryConsts::I64LoadMem16U); break;
          case 4: o << int8_t(curr->signed_ ? BinaryConsts::I64LoadMem32S : BinaryConsts::I64LoadMem32U); break;
          case 8: o << int8_t(BinaryConsts::I64LoadMem); break;
          default: abort();
        }
        break;
      }
      case f32: o << int8_t(BinaryConsts::F32LoadMem); break;
      case f64: o << int8_t(BinaryConsts::F64LoadMem); break;
      case unreachable: return; // the pointer is unreachable, so we are never reached; just don't emit a load
      default: WASM_UNREACHABLE();
    }
  } else {
    if (curr->type == unreachable) {
      // don't even emit it; we don't know the right type
      o << int8_t(BinaryConsts::Unreachable);
      return;
    }
    o << int8_t(BinaryConsts::AtomicPrefix);
    switch (curr->type) {
      case i32: {
        switch (curr->bytes) {
          case 1: o << int8_t(BinaryConsts::I32AtomicLoad8U); break;
          case 2: o << int8_t(BinaryConsts::I32AtomicLoad16U); break;
          case 4: o << int8_t(BinaryConsts::I32AtomicLoad); break;
          default: WASM_UNREACHABLE();
        }
        break;
      }
      case i64: {
        switch (curr->bytes) {
          case 1: o << int8_t(BinaryConsts::I64AtomicLoad8U); break;
          case 2: o << int8_t(BinaryConsts::I64AtomicLoad16U); break;
          case 4: o << int8_t(BinaryConsts::I64AtomicLoad32U); break;
          case 8: o << int8_t(BinaryConsts::I64AtomicLoad); break;
          default: WASM_UNREACHABLE();
        }
        break;
      }
      case unreachable: return;
      default: WASM_UNREACHABLE();
    }
  }
  emitMemoryAccess(curr->align, curr->bytes, curr->offset);
}

void Writer::visitStore(Store *curr) {
  if (debug) std::cerr << "zz node: Store" << std::endl;
  recurse(curr->ptr);
  recurse(curr->value);
  if (!curr->isAtomic) {
    switch (curr->valueType) {
      case i32: {
        switch (curr->bytes) {
          case 1: o << int8_t(BinaryConsts::I32StoreMem8); break;
          case 2: o << int8_t(BinaryConsts::I32StoreMem16); break;
          case 4: o << int8_t(BinaryConsts::I32StoreMem); break;
          default: abort();
        }
        break;
      }
      case i64: {
        switch (curr->bytes) {
          case 1: o << int8_t(BinaryConsts::I64StoreMem8); break;
          case 2: o << int8_t(BinaryConsts::I64StoreMem16); break;
          case 4: o << int8_t(BinaryConsts::I64StoreMem32); break;
          case 8: o << int8_t(BinaryConsts::I64StoreMem); break;
          default: abort();
        }
        break;
      }
      case f32: o << int8_t(BinaryConsts::F32StoreMem); break;
      case f64: o << int8_t(BinaryConsts::F64StoreMem); break;
      default: abort();
    }
  } else {
    if (curr->type == unreachable) {
      // don't even emit it; we don't know the right type
      o << int8_t(BinaryConsts::Unreachable);
      return;
    }
    o << int8_t(BinaryConsts::AtomicPrefix);
    switch (curr->valueType) {
      case i32: {
        switch (curr->bytes) {
          case 1: o << int8_t(BinaryConsts::I32AtomicStore8); break;
          case 2: o << int8_t(BinaryConsts::I32AtomicStore16); break;
          case 4: o << int8_t(BinaryConsts::I32AtomicStore); break;
          default: WASM_UNREACHABLE();
        }
        break;
      }
      case i64: {
        switch (curr->bytes) {
          case 1: o << int8_t(BinaryConsts::I64AtomicStore8); break;
          case 2: o << int8_t(BinaryConsts::I64AtomicStore16); break;
          case 4: o << int8_t(BinaryConsts::I64AtomicStore32); break;
          case 8: o << int8_t(BinaryConsts::I64AtomicStore); break;
          default: WASM_UNREACHABLE();
        }
        break;
      }
      default: WASM_UNREACHABLE();
    }
  }
  emitMemoryAccess(curr->align, curr->bytes, curr->offset);
}

void Writer::visitAtomicRMW(AtomicRMW *curr) {
  if (debug) std::cerr << "zz node: AtomicRMW" << std::endl;
  recurse(curr->ptr);
  // stop if the rest isn't reachable anyhow
  if (curr->ptr->type == unreachable) return;
  recurse(curr->value);
  if (curr->value->type == unreachable) return;

  if (curr->type == unreachable) {
    // don't even emit it; we don't know the right type
    o << int8_t(BinaryConsts::Unreachable);
    return;
  }

  o << int8_t(BinaryConsts::AtomicPrefix);

#define CASE_FOR_OP(Op) \
  case Op: \
    switch (curr->type) {                                               \
      case i32:                                                         \
        switch (curr->bytes) {                                          \
          case 1: o << int8_t(BinaryConsts::I32AtomicRMW##Op##8U); break; \
          case 2: o << int8_t(BinaryConsts::I32AtomicRMW##Op##16U); break; \
          case 4: o << int8_t(BinaryConsts::I32AtomicRMW##Op); break;   \
          default: WASM_UNREACHABLE();                                  \
        }                                                               \
        break;                                                          \
      case i64:                                                         \
        switch (curr->bytes) {                                          \
          case 1: o << int8_t(BinaryConsts::I64AtomicRMW##Op##8U); break; \
          case 2: o << int8_t(BinaryConsts::I64AtomicRMW##Op##16U); break; \
          case 4: o << int8_t(BinaryConsts::I64AtomicRMW##Op##32U); break; \
          case 8: o << int8_t(BinaryConsts::I64AtomicRMW##Op); break;   \
          default: WASM_UNREACHABLE();                                  \
        }                                                               \
        break;                                                          \
      default: WASM_UNREACHABLE();                                      \
    }                                                                   \
    break

  switch(curr->op) {
    CASE_FOR_OP(Add);
    CASE_FOR_OP(Sub);
    CASE_FOR_OP(And);
    CASE_FOR_OP(Or);
    CASE_FOR_OP(Xor);
    CASE_FOR_OP(Xchg);
    default: WASM_UNREACHABLE();
  }
#undef CASE_FOR_OP

  emitMemoryAccess(curr->bytes, curr->bytes, curr->offset);
}

void Writer::visitAtomicCmpxchg(AtomicCmpxchg *curr) {
  if (debug) std::cerr << "zz node: AtomicCmpxchg" << std::endl;
  recurse(curr->ptr);
  // stop if the rest isn't reachable anyhow
  if (curr->ptr->type == unreachable) return;
  recurse(curr->expected);
  if (curr->expected->type == unreachable) return;
  recurse(curr->replacement);
  if (curr->replacement->type == unreachable) return;

  if (curr->type == unreachable) {
    // don't even emit it; we don't know the right type
    o << int8_t(BinaryConsts::Unreachable);
    return;
  }

  o << int8_t(BinaryConsts::AtomicPrefix);
  switch (curr->type) {
    case i32:
      switch (curr->bytes) {
        case 1: o << int8_t(BinaryConsts::I32AtomicCmpxchg8U); break;
        case 2: o << int8_t(BinaryConsts::I32AtomicCmpxchg16U); break;
        case 4: o << int8_t(BinaryConsts::I32AtomicCmpxchg); break;
        default: WASM_UNREACHABLE();
      }
      break;
    case i64:
      switch (curr->bytes) {
        case 1: o << int8_t(BinaryConsts::I64AtomicCmpxchg8U); break;
        case 2: o << int8_t(BinaryConsts::I64AtomicCmpxchg16U); break;
        case 4: o << int8_t(BinaryConsts::I64AtomicCmpxchg32U); break;
        case 8: o << int8_t(BinaryConsts::I64AtomicCmpxchg); break;
        default: WASM_UNREACHABLE();
      }
      break;
    default: WASM_UNREACHABLE();
  }
  emitMemoryAccess(curr->bytes, curr->bytes, curr->offset);
}

void Writer::visitAtomicWait(AtomicWait *curr) {
  if (debug) std::cerr << "zz node: AtomicWait" << std::endl;
  recurse(curr->ptr);
  // stop if the rest isn't reachable anyhow
  if (curr->ptr->type == unreachable) return;
  recurse(curr->expected);
  if (curr->expected->type == unreachable) return;
  recurse(curr->timeout);
  if (curr->timeout->type == unreachable) return;

  o << int8_t(BinaryConsts::AtomicPrefix);
  switch (curr->expectedType) {
    case i32: {
      o << int8_t(BinaryConsts::I32AtomicWait);
      emitMemoryAccess(4, 4, 0);
      break;
    }
    case i64: {
      o << int8_t(BinaryConsts::I64AtomicWait);
      emitMemoryAccess(8, 8, 0);
      break;
    }
    default: WASM_UNREACHABLE();
  }
}

void Writer::visitAtomicWake(AtomicWake *curr) {
  if (debug) std::cerr << "zz node: AtomicWake" << std::endl;
  recurse(curr->ptr);
  // stop if the rest isn't reachable anyhow
  if (curr->ptr->type == unreachable) return;
  recurse(curr->wakeCount);
  if (curr->wakeCount->type == unreachable) return;

  o << int8_t(BinaryConsts::AtomicPrefix) << int8_t(BinaryConsts::AtomicWake);
  emitMemoryAccess(4, 4, 0);
}

void Writer::visitConst(Const *curr) {
  if (debug) std::cerr << "zz node: Const" << curr << " : " << curr->type << std::endl;
  switch (curr->type) {
    case i32: {
      o << int8_t(BinaryConsts::I32Const) << S32LEB(curr->value.geti32());
      break;
    }
    case i64: {
      o << int8_t(BinaryConsts::I64Const) << S64LEB(curr->value.geti64());
      break;
    }
    case f32: {
      o << int8_t(BinaryConsts::F32Const) << curr->value.reinterpreti32();
      break;
    }
    case f64: {
      o << int8_t(BinaryConsts::F64Const) << curr->value.reinterpreti64();
      break;
    }
    default: abort();
  }
  if (debug) std::cerr << "zz const node done.\n";
}

void Writer::visitUnary(Unary *curr) {
  if (debug) std::cerr << "zz node: Unary" << std::endl;
  recurse(curr->value);
  switch (curr->op) {
    case ClzInt32:               o << int8_t(BinaryConsts::I32Clz); break;
    case CtzInt32:               o << int8_t(BinaryConsts::I32Ctz); break;
    case PopcntInt32:            o << int8_t(BinaryConsts::I32Popcnt); break;
    case EqZInt32:               o << int8_t(BinaryConsts::I32EqZ); break;
    case ClzInt64:               o << int8_t(BinaryConsts::I64Clz); break;
    case CtzInt64:               o << int8_t(BinaryConsts::I64Ctz); break;
    case PopcntInt64:            o << int8_t(BinaryConsts::I64Popcnt); break;
    case EqZInt64:               o << int8_t(BinaryConsts::I64EqZ); break;
    case NegFloat32:             o << int8_t(BinaryConsts::F32Neg); break;
    case AbsFloat32:             o << int8_t(BinaryConsts::F32Abs); break;
    case CeilFloat32:            o << int8_t(BinaryConsts::F32Ceil); break;
    case FloorFloat32:           o << int8_t(BinaryConsts::F32Floor); break;
    case TruncFloat32:           o << int8_t(BinaryConsts::F32Trunc); break;
    case NearestFloat32:         o << int8_t(BinaryConsts::F32NearestInt); break;
    case SqrtFloat32:            o << int8_t(BinaryConsts::F32Sqrt); break;
    case NegFloat64:             o << int8_t(BinaryConsts::F64Neg); break;
    case AbsFloat64:             o << int8_t(BinaryConsts::F64Abs); break;
    case CeilFloat64:            o << int8_t(BinaryConsts::F64Ceil); break;
    case FloorFloat64:           o << int8_t(BinaryConsts::F64Floor); break;
    case TruncFloat64:           o << int8_t(BinaryConsts::F64Trunc); break;
    case NearestFloat64:         o << int8_t(BinaryConsts::F64NearestInt); break;
    case SqrtFloat64:            o << int8_t(BinaryConsts::F64Sqrt); break;
    case ExtendSInt32:           o << int8_t(BinaryConsts::I64STruncI32); break;
    case ExtendUInt32:           o << int8_t(BinaryConsts::I64UTruncI32); break;
    case WrapInt64:              o << int8_t(BinaryConsts::I32ConvertI64); break;
    case TruncUFloat32ToInt32:   o << int8_t(BinaryConsts::I32UTruncF32); break;
    case TruncUFloat32ToInt64:   o << int8_t(BinaryConsts::I64UTruncF32); break;
    case TruncSFloat32ToInt32:   o << int8_t(BinaryConsts::I32STruncF32); break;
    case TruncSFloat32ToInt64:   o << int8_t(BinaryConsts::I64STruncF32); break;
    case TruncUFloat64ToInt32:   o << int8_t(BinaryConsts::I32UTruncF64); break;
    case TruncUFloat64ToInt64:   o << int8_t(BinaryConsts::I64UTruncF64); break;
    case TruncSFloat64ToInt32:   o << int8_t(BinaryConsts::I32STruncF64); break;
    case TruncSFloat64ToInt64:   o << int8_t(BinaryConsts::I64STruncF64); break;
    case ConvertUInt32ToFloat32: o << int8_t(BinaryConsts::F32UConvertI32); break;
    case ConvertUInt32ToFloat64: o << int8_t(BinaryConsts::F64UConvertI32); break;
    case ConvertSInt32ToFloat32: o << int8_t(BinaryConsts::F32SConvertI32); break;
    case ConvertSInt32ToFloat64: o << int8_t(BinaryConsts::F64SConvertI32); break;
    case ConvertUInt64ToFloat32: o << int8_t(BinaryConsts::F32UConvertI64); break;
    case ConvertUInt64ToFloat64: o << int8_t(BinaryConsts::F64UConvertI64); break;
    case ConvertSInt64ToFloat32: o << int8_t(BinaryConsts::F32SConvertI64); break;
    case ConvertSInt64ToFloat64: o << int8_t(BinaryConsts::F64SConvertI64); break;
    case DemoteFloat64:          o << int8_t(BinaryConsts::F32ConvertF64); break;
    case PromoteFloat32:         o << int8_t(BinaryConsts::F64ConvertF32); break;
    case ReinterpretFloat32:     o << int8_t(BinaryConsts::I32ReinterpretF32); break;
    case ReinterpretFloat64:     o << int8_t(BinaryConsts::I64ReinterpretF64); break;
    case ReinterpretInt32:       o << int8_t(BinaryConsts::F32ReinterpretI32); break;
    case ReinterpretInt64:       o << int8_t(BinaryConsts::F64ReinterpretI64); break;
    case ExtendS8Int32:          o << int8_t(BinaryConsts::I32ExtendS8); break;
    case ExtendS16Int32:         o << int8_t(BinaryConsts::I32ExtendS16); break;
    case ExtendS8Int64:          o << int8_t(BinaryConsts::I64ExtendS8); break;
    case ExtendS16Int64:         o << int8_t(BinaryConsts::I64ExtendS16); break;
    case ExtendS32Int64:         o << int8_t(BinaryConsts::I64ExtendS32); break;
    default: abort();
  }
  if (curr->type == unreachable) {
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visitBinary(Binary *curr) {
  if (debug) std::cerr << "zz node: Binary" << std::endl;
  recurse(curr->left);
  recurse(curr->right);

  switch (curr->op) {
    case AddInt32:      o << int8_t(BinaryConsts::I32Add); break;
    case SubInt32:      o << int8_t(BinaryConsts::I32Sub); break;
    case MulInt32:      o << int8_t(BinaryConsts::I32Mul); break;
    case DivSInt32:     o << int8_t(BinaryConsts::I32DivS); break;
    case DivUInt32:     o << int8_t(BinaryConsts::I32DivU); break;
    case RemSInt32:     o << int8_t(BinaryConsts::I32RemS); break;
    case RemUInt32:     o << int8_t(BinaryConsts::I32RemU); break;
    case AndInt32:      o << int8_t(BinaryConsts::I32And); break;
    case OrInt32:       o << int8_t(BinaryConsts::I32Or); break;
    case XorInt32:      o << int8_t(BinaryConsts::I32Xor); break;
    case ShlInt32:      o << int8_t(BinaryConsts::I32Shl); break;
    case ShrUInt32:     o << int8_t(BinaryConsts::I32ShrU); break;
    case ShrSInt32:     o << int8_t(BinaryConsts::I32ShrS); break;
    case RotLInt32:     o << int8_t(BinaryConsts::I32RotL); break;
    case RotRInt32:     o << int8_t(BinaryConsts::I32RotR); break;
    case EqInt32:       o << int8_t(BinaryConsts::I32Eq); break;
    case NeInt32:       o << int8_t(BinaryConsts::I32Ne); break;
    case LtSInt32:      o << int8_t(BinaryConsts::I32LtS); break;
    case LtUInt32:      o << int8_t(BinaryConsts::I32LtU); break;
    case LeSInt32:      o << int8_t(BinaryConsts::I32LeS); break;
    case LeUInt32:      o << int8_t(BinaryConsts::I32LeU); break;
    case GtSInt32:      o << int8_t(BinaryConsts::I32GtS); break;
    case GtUInt32:      o << int8_t(BinaryConsts::I32GtU); break;
    case GeSInt32:      o << int8_t(BinaryConsts::I32GeS); break;
    case GeUInt32:      o << int8_t(BinaryConsts::I32GeU); break;

    case AddInt64:      o << int8_t(BinaryConsts::I64Add); break;
    case SubInt64:      o << int8_t(BinaryConsts::I64Sub); break;
    case MulInt64:      o << int8_t(BinaryConsts::I64Mul); break;
    case DivSInt64:     o << int8_t(BinaryConsts::I64DivS); break;
    case DivUInt64:     o << int8_t(BinaryConsts::I64DivU); break;
    case RemSInt64:     o << int8_t(BinaryConsts::I64RemS); break;
    case RemUInt64:     o << int8_t(BinaryConsts::I64RemU); break;
    case AndInt64:      o << int8_t(BinaryConsts::I64And); break;
    case OrInt64:       o << int8_t(BinaryConsts::I64Or); break;
    case XorInt64:      o << int8_t(BinaryConsts::I64Xor); break;
    case ShlInt64:      o << int8_t(BinaryConsts::I64Shl); break;
    case ShrUInt64:     o << int8_t(BinaryConsts::I64ShrU); break;
    case ShrSInt64:     o << int8_t(BinaryConsts::I64ShrS); break;
    case RotLInt64:     o << int8_t(BinaryConsts::I64RotL); break;
    case RotRInt64:     o << int8_t(BinaryConsts::I64RotR); break;
    case EqInt64:       o << int8_t(BinaryConsts::I64Eq); break;
    case NeInt64:       o << int8_t(BinaryConsts::I64Ne); break;
    case LtSInt64:      o << int8_t(BinaryConsts::I64LtS); break;
    case LtUInt64:      o << int8_t(BinaryConsts::I64LtU); break;
    case LeSInt64:      o << int8_t(BinaryConsts::I64LeS); break;
    case LeUInt64:      o << int8_t(BinaryConsts::I64LeU); break;
    case GtSInt64:      o << int8_t(BinaryConsts::I64GtS); break;
    case GtUInt64:      o << int8_t(BinaryConsts::I64GtU); break;
    case GeSInt64:      o << int8_t(BinaryConsts::I64GeS); break;
    case GeUInt64:      o << int8_t(BinaryConsts::I64GeU); break;

    case AddFloat32:      o << int8_t(BinaryConsts::F32Add); break;
    case SubFloat32:      o << int8_t(BinaryConsts::F32Sub); break;
    case MulFloat32:      o << int8_t(BinaryConsts::F32Mul); break;
    case DivFloat32:      o << int8_t(BinaryConsts::F32Div); break;
    case CopySignFloat32: o << int8_t(BinaryConsts::F32CopySign);break;
    case MinFloat32:      o << int8_t(BinaryConsts::F32Min); break;
    case MaxFloat32:      o << int8_t(BinaryConsts::F32Max); break;
    case EqFloat32:       o << int8_t(BinaryConsts::F32Eq); break;
    case NeFloat32:       o << int8_t(BinaryConsts::F32Ne); break;
    case LtFloat32:       o << int8_t(BinaryConsts::F32Lt); break;
    case LeFloat32:       o << int8_t(BinaryConsts::F32Le); break;
    case GtFloat32:       o << int8_t(BinaryConsts::F32Gt); break;
    case GeFloat32:       o << int8_t(BinaryConsts::F32Ge); break;

    case AddFloat64:      o << int8_t(BinaryConsts::F64Add); break;
    case SubFloat64:      o << int8_t(BinaryConsts::F64Sub); break;
    case MulFloat64:      o << int8_t(BinaryConsts::F64Mul); break;
    case DivFloat64:      o << int8_t(BinaryConsts::F64Div); break;
    case CopySignFloat64: o << int8_t(BinaryConsts::F64CopySign);break;
    case MinFloat64:      o << int8_t(BinaryConsts::F64Min); break;
    case MaxFloat64:      o << int8_t(BinaryConsts::F64Max); break;
    case EqFloat64:       o << int8_t(BinaryConsts::F64Eq); break;
    case NeFloat64:       o << int8_t(BinaryConsts::F64Ne); break;
    case LtFloat64:       o << int8_t(BinaryConsts::F64Lt); break;
    case LeFloat64:       o << int8_t(BinaryConsts::F64Le); break;
    case GtFloat64:       o << int8_t(BinaryConsts::F64Gt); break;
    case GeFloat64:       o << int8_t(BinaryConsts::F64Ge); break;
    default: abort();
  }
  if (curr->type == unreachable) {
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visitSelect(Select *curr) {
  if (debug) std::cerr << "zz node: Select" << std::endl;
  recurse(curr->ifTrue);
  recurse(curr->ifFalse);
  recurse(curr->condition);
  o << int8_t(BinaryConsts::Select);
  if (curr->type == unreachable) {
    o << int8_t(BinaryConsts::Unreachable);
  }
}

void Writer::visitReturn(Return *curr) {
  if (debug) std::cerr << "zz node: Return" << std::endl;
  if (curr->value) {
    recurse(curr->value);
  }
  o << int8_t(BinaryConsts::Return);
}

void Writer::visitHost(Host *curr) {
  if (debug) std::cerr << "zz node: Host" << std::endl;
  switch (curr->op) {
    case CurrentMemory: {
      o << int8_t(BinaryConsts::CurrentMemory);
      break;
    }
    case GrowMemory: {
      recurse(curr->operands[0]);
      o << int8_t(BinaryConsts::GrowMemory);
      break;
    }
    default: abort();
  }
  o << U32LEB(0); // Reserved flags field
}

void Writer::visitNop(Nop *curr) {
  if (debug) std::cerr << "zz node: Nop" << std::endl;
  o << int8_t(BinaryConsts::Nop);
}

void Writer::visitUnreachable(Unreachable *curr) {
  if (debug) std::cerr << "zz node: Unreachable" << std::endl;
  o << int8_t(BinaryConsts::Unreachable);
}

void Writer::visitDrop(Drop *curr) {
  if (debug) std::cerr << "zz node: Drop" << std::endl;
  recurse(curr->value);
  o << int8_t(BinaryConsts::Drop);
}

} // namespace stack

} // namespace wasm

#endif // wasm_stack_writer_h
