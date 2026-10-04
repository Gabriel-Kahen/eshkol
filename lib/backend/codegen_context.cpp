/*
 * Copyright (C) tsotchke
 *
 * SPDX-License-Identifier: MIT
 *
 * CodegenContext implementation - Shared state management
 */

#include <eshkol/backend/codegen_context.h>
#include <eshkol/eshkol.h>  // HEAP_SUBTYPE_SYMBOL, etc.

#ifdef ESHKOL_LLVM_BACKEND_ENABLED

#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalAlias.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Operator.h>
#include <cstdint>
#include <llvm/IR/Metadata.h>
#include <unordered_set>

namespace eshkol {

void CodegenContext::setPointerProvenance(
    llvm::Value* value, PointerProvenance provenance) {
    if (value) pointer_provenance_[value] = provenance;
}

llvm::Value* CodegenContext::createReviewedHeaderedCall(
    llvm::Function* function, llvm::ArrayRef<llvm::Value*> arguments,
    const llvm::Twine& name) {
    const auto kind = memory_.reviewedHeaderedReturn(function);
    if (kind == HeaderedReturnKind::None || function->getParent() != &module_ ||
        !function->getReturnType()->isPointerTy() ||
        function->getFunctionType()->isVarArg() ||
        function->arg_size() != arguments.size() ||
        !builder_.GetInsertBlock()) {
        eshkol_error("unreviewed headered constructor call");
        markFatalCodegenError();
        return llvm::ConstantPointerNull::get(ptrType());
    }
    for (unsigned i = 0; i < arguments.size(); ++i) {
        if (!arguments[i] || arguments[i]->getType() !=
                                  function->getFunctionType()->getParamType(i)) {
            eshkol_error("headered constructor call has an unreviewed argument ABI");
            markFatalCodegenError();
            return llvm::ConstantPointerNull::get(ptrType());
        }
    }
    // Only calls emitted through this compiler-owned path gain authority.
    // An Eshkol extern can reuse the very same Function* but cannot inherit
    // provenance merely from the declaration or its spelling.
    auto* call = builder_.CreateCall(function, arguments, name);
    setPointerProvenance(call, kind == HeaderedReturnKind::Callable
                                   ? PointerProvenance::ManagedCallable
                                   : PointerProvenance::ManagedHeap);
    return call;
}

llvm::Value* CodegenContext::createReviewedGenericHeapCall(
    llvm::Function* function, llvm::ArrayRef<llvm::Value*> arguments,
    const llvm::Twine& name) {
    auto invalid = [&]() -> llvm::Value* {
        eshkol_error("generic header allocator lacks reviewed heap subtype and ABI");
        markFatalCodegenError();
        return llvm::ConstantPointerNull::get(ptrType());
    };
    if (function != memory_.getArenaAllocateWithHeader() ||
        function->getParent() != &module_ || !builder_.GetInsertBlock() ||
        function->getFunctionType()->isVarArg() || arguments.size() != 4 ||
        !function->getReturnType()->isPointerTy()) return invalid();
    for (unsigned i = 0; i < arguments.size(); ++i)
        if (!arguments[i] || arguments[i]->getType() !=
                                  function->getFunctionType()->getParamType(i))
            return invalid();
    auto* subtype = llvm::dyn_cast<llvm::ConstantInt>(arguments[2]);
    auto* flags = llvm::dyn_cast<llvm::ConstantInt>(arguments[3]);
    if (!subtype || !flags || !flags->isZero()) return invalid();
    switch (subtype->getZExtValue()) {
        case HEAP_SUBTYPE_BYTEVECTOR:
        case HEAP_SUBTYPE_PROMISE:
        case HEAP_SUBTYPE_STRING:
        case HEAP_SUBTYPE_VECTOR:
            break;
        default: return invalid();
    }
    // The native allocator writes the eight-byte heap header and returns
    // its payload pointer, or null.  Guard before any caller payload store.
    auto* call = builder_.CreateCall(function, arguments, name);
    setPointerProvenance(call, PointerProvenance::ManagedHeap);
    emitConstructorAllocationCheck(call);
    return call;
}

void CodegenContext::markNumericInteger(llvm::Value* value) {
    if (!value || !value->getType()->isIntegerTy()) {
        eshkol_error("numeric integer authority requires an LLVM integer value");
        markFatalCodegenError();
        return;
    }
    auto pointer = pointer_provenance_.find(value);
    if (pointer != pointer_provenance_.end() &&
        pointer->second != PointerProvenance::NumericAddress) {
        eshkol_error("raw integer cannot have both numeric and pointer authority");
        markFatalCodegenError();
        return;
    }
    numeric_integer_values_.insert(value);
}

void CodegenContext::registerCaptureBinding(
    llvm::Value* binding, CaptureStorageKind kind,
    llvm::Function* owner, llvm::StoreInst* seed,
    const llvm::Value* origin, bool escapable) {
    bool valid = binding != nullptr;
    switch (kind) {
        case CaptureStorageKind::ValueStorage: {
            auto* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(binding);
            valid = alloca && owner && alloca->getFunction() == owner &&
                    seed && seed->getPointerOperand() == alloca &&
                    seed->getFunction() == owner;
            break;
        }
        case CaptureStorageKind::StackCell: {
            auto* alloca = llvm::dyn_cast_or_null<llvm::AllocaInst>(binding);
            valid = alloca && owner && alloca->getFunction() == owner &&
                    seed && seed->getPointerOperand() == alloca &&
                    seed->getFunction() == owner;
            break;
        }
        case CaptureStorageKind::ArenaCell:
            valid = binding && binding->getType()->isPointerTy() && owner && !seed;
            break;
        case CaptureStorageKind::GlobalCell:
            valid = llvm::isa_and_nonnull<llvm::GlobalVariable>(binding) && !owner && !seed;
            break;
        case CaptureStorageKind::DirectCellArgument: {
            auto* arg = llvm::dyn_cast_or_null<llvm::Argument>(binding);
            valid = arg && arg->getType()->isPointerTy() &&
                    arg->getParent() == owner && !seed;
            break;
        }
        case CaptureStorageKind::TaggedValueSlot:
        case CaptureStorageKind::TaggedCellSlot: {
            auto* arg = llvm::dyn_cast_or_null<llvm::Argument>(binding);
            valid = arg && arg->getType()->isPointerTy() &&
                    arg->getParent() == owner && !seed;
            break;
        }
        case CaptureStorageKind::Value:
            valid = binding && !seed;
            break;
    }
    if (!valid) {
        eshkol_error("invalid capture binding authority");
        markFatalCodegenError();
        return;
    }
    if (kind == CaptureStorageKind::ArenaCell ||
        kind == CaptureStorageKind::GlobalCell)
        escapable = true;
    if (kind == CaptureStorageKind::StackCell ||
        kind == CaptureStorageKind::ValueStorage ||
        kind == CaptureStorageKind::Value ||
        kind == CaptureStorageKind::TaggedValueSlot)
        escapable = false;
    CaptureBindingAuthority authority{kind, owner, seed,
                                      origin ? origin : binding, escapable};
    auto [it, inserted] = capture_bindings_.emplace(binding, authority);
    if (!inserted && (it->second.kind != kind || it->second.owner != owner ||
                      it->second.seed != seed || it->second.origin != authority.origin ||
                      it->second.escapable != escapable)) {
        eshkol_error("conflicting capture binding authority");
        markFatalCodegenError();
    }
}

std::optional<CaptureBindingAuthority>
CodegenContext::captureBinding(llvm::Value* binding) const {
    auto it = capture_bindings_.find(binding);
    if (it == capture_bindings_.end()) return std::nullopt;
    return it->second;
}

void CodegenContext::registerCaptureSlot(
    llvm::Function* function, const std::string& name,
    llvm::Argument* slot, const llvm::Value* origin) {
    auto authority = captureBinding(slot);
    if (!function || !slot || slot->getParent() != function || !origin ||
        !authority || authority->origin != origin ||
        (authority->kind != CaptureStorageKind::TaggedCellSlot &&
         authority->kind != CaptureStorageKind::TaggedValueSlot)) {
        eshkol_error("capture slot registration lacks binding authority");
        markFatalCodegenError();
        return;
    }
    auto [it, inserted] = capture_slots_[function].emplace(
        name, CaptureSlotEntry{slot, origin});
    if (!inserted && (it->second.slot != slot || it->second.origin != origin)) {
        eshkol_error("conflicting capture slot for lexical binding");
        markFatalCodegenError();
    }
}

llvm::Argument* CodegenContext::authenticatedCaptureSlot(
    llvm::Function* function, const std::string& name,
    llvm::Value* observed_binding, bool require_cell) const {
    if (!function || !observed_binding) return nullptr;
    auto outer = capture_slots_.find(function);
    if (outer == capture_slots_.end()) return nullptr;
    auto entry = outer->second.find(name);
    if (entry == outer->second.end()) return nullptr;
    auto observed = captureBinding(observed_binding);
    auto slot = captureBinding(entry->second.slot);
    if (!observed || !slot || observed->origin != entry->second.origin ||
        slot->origin != entry->second.origin ||
        (require_cell && slot->kind != CaptureStorageKind::TaggedCellSlot))
        return nullptr;
    return entry->second.slot;
}

PointerProvenance CodegenContext::pointerProvenance(llvm::Value* value) const {
    std::unordered_set<const llvm::Value*> visiting;
    auto resolve = [&](auto&& self, const llvm::Value* v) -> PointerProvenance {
        if (!v) return PointerProvenance::Unknown;
        auto known = pointer_provenance_.find(v);
        if (known != pointer_provenance_.end()) return known->second;
        if (!visiting.insert(v).second) return PointerProvenance::Unknown;
        PointerProvenance result = PointerProvenance::Unknown;
        if (llvm::isa<llvm::ConstantPointerNull>(v))
            result = PointerProvenance::CanonicalNull;
        else if (auto* alias = llvm::dyn_cast<llvm::GlobalAlias>(v)) {
            result = self(self, alias->getAliasee());
        } else if (auto* cast = llvm::dyn_cast<llvm::CastInst>(v)) {
            switch (cast->getOpcode()) {
                case llvm::Instruction::BitCast:
                case llvm::Instruction::AddrSpaceCast:
                case llvm::Instruction::PtrToInt:
                    result = self(self, cast->getOperand(0));
                    break;
                default: break;
            }
        } else if (auto* constant = llvm::dyn_cast<llvm::ConstantExpr>(v)) {
            if (constant->getOpcode() == llvm::Instruction::BitCast ||
                constant->getOpcode() == llvm::Instruction::AddrSpaceCast ||
                constant->getOpcode() == llvm::Instruction::PtrToInt)
                result = self(self, constant->getOperand(0));
            else if (constant->getOpcode() == llvm::Instruction::Select) {
                auto left = self(self, constant->getOperand(1));
                auto right = self(self, constant->getOperand(2));
                result = left == right ? left : PointerProvenance::Unknown;
            } else if (constant->getOpcode() == llvm::Instruction::GetElementPtr) {
                auto origin = self(self, constant->getOperand(0));
                result = origin == PointerProvenance::Unknown
                    ? origin : PointerProvenance::Foreign;
            }
        } else if (auto* gep = llvm::dyn_cast<llvm::GEPOperator>(v)) {
            auto origin = self(self, gep->getPointerOperand());
            result = origin == PointerProvenance::Unknown
                ? origin : PointerProvenance::Foreign;
        } else if (auto* select = llvm::dyn_cast<llvm::SelectInst>(v)) {
            auto left = self(self, select->getTrueValue());
            auto right = self(self, select->getFalseValue());
            result = left == right ? left : PointerProvenance::Unknown;
        } else if (auto* phi = llvm::dyn_cast<llvm::PHINode>(v)) {
            if (phi->getNumIncomingValues() != 0) {
                result = self(self, phi->getIncomingValue(0));
                for (unsigned i = 1; i < phi->getNumIncomingValues(); ++i)
                    if (self(self, phi->getIncomingValue(i)) != result) {
                        result = PointerProvenance::Unknown;
                        break;
                    }
            }
        }
        visiting.erase(v);
        return result;
    };
    return resolve(resolve, value);
}

bool CodegenContext::rawIntegerMayContainPointerBits(llvm::Value* value) const {
    if (!value || !value->getType()->isIntegerTy()) return false;
    std::unordered_set<const llvm::Value*> visiting;
    auto inspect = [&](auto&& self, const llvm::Value* v) -> bool {
        if (!v || !visiting.insert(v).second) return false;
        if (v->getType()->isFloatingPointTy()) {
            visiting.erase(v);
            return false;
        }
        auto known = pointer_provenance_.find(v);
        if (known != pointer_provenance_.end()) {
            visiting.erase(v);
            return known->second != PointerProvenance::NumericAddress;
        }
        if (numeric_integer_values_.count(v) != 0 ||
            llvm::isa<llvm::ConstantInt>(v)) {
            visiting.erase(v);
            return false;
        }
        bool result = true; // An unclassified integer load/call/argument is not numeric proof.
        if (llvm::isa<llvm::PtrToIntInst>(v)) {
            result = true;
        } else if (auto* c = llvm::dyn_cast<llvm::ConstantExpr>(v)) {
            result = c->getOpcode() == llvm::Instruction::PtrToInt;
            for (const llvm::Use& operand : c->operands())
                result |= self(self, operand.get());
        } else if (auto* extract = llvm::dyn_cast<llvm::ExtractValueInst>(v)) {
            // A payload split from a dynamic tagged value has no independent
            // integer authority. Other aggregates also need a producer's
            // explicit numeric descriptor before an i64 field is repacked.
            (void)extract;
            result = true;
        } else if (auto* inst = llvm::dyn_cast<llvm::Instruction>(v)) {
            if (llvm::isa<llvm::CmpInst>(inst)) {
                result = false;
            } else if (auto* select = llvm::dyn_cast<llvm::SelectInst>(inst)) {
                result = self(self, select->getTrueValue()) ||
                         self(self, select->getFalseValue());
            } else if (llvm::isa<llvm::PHINode, llvm::CastInst,
                          llvm::BinaryOperator, llvm::FreezeInst>(inst)) {
                result = false;
                for (const llvm::Use& operand : inst->operands())
                    result |= self(self, operand.get());
            }
        }
        visiting.erase(v);
        return result;
    };
    return inspect(inspect, value);
}

// ESH-0214c: emit the region write barrier around a store of a tagged value
// into a longer-lived destination. Spills value + result to entry-block allocas
// (so the pattern is safe inside loop bodies) and calls the runtime helper.
llvm::Value* CodegenContext::emitRegionWriteBarrier(llvm::Value* dst_ptr,
                                                    llvm::Value* tagged_value) {
    if (!tagged_value || tagged_value->getType() != taggedValueType()) {
        // Only tagged values can carry a heap pointer that might dangle.
        return tagged_value;
    }

    llvm::Type* tv_ty = taggedValueType();
    llvm::PointerType* ptr_ty = ptrType();

    // Hoist the spill/result slots to the function entry block: an alloca in a
    // loop body re-adjusts the stack pointer every iteration and is only
    // reclaimed at function return.
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::IRBuilderBase::InsertPoint saved_ip = builder_.saveIP();
    if (fn && !fn->empty()) {
        llvm::BasicBlock& entry = fn->getEntryBlock();
        builder_.SetInsertPoint(&entry, entry.begin());
    }
    llvm::AllocaInst* val_slot = builder_.CreateAlloca(tv_ty, nullptr, "wb_val_slot");
    llvm::AllocaInst* out_slot = builder_.CreateAlloca(tv_ty, nullptr, "wb_out_slot");
    builder_.restoreIP(saved_ip);

    builder_.CreateStore(tagged_value, val_slot);

    llvm::Function* wb = module_.getFunction("eshkol_region_write_barrier_checked_v1");
    if (!wb) {
        llvm::FunctionType* wb_ty = llvm::FunctionType::get(
            int32Type(), {ptr_ty, ptr_ty, ptr_ty}, false);
        wb = llvm::Function::Create(wb_ty, llvm::GlobalValue::ExternalLinkage,
                                    "eshkol_region_write_barrier_checked_v1", &module_);
    }

    llvm::Value* dst_cast = dst_ptr;
    if (dst_ptr && dst_ptr->getType() != ptr_ty) {
        dst_cast = builder_.CreateBitCast(dst_ptr, ptr_ty);
    } else if (!dst_ptr) {
        dst_cast = llvm::ConstantPointerNull::get(ptr_ty);
    }

    llvm::Value* status = builder_.CreateCall(wb, {out_slot, dst_cast, val_slot}, "wb_status");
    llvm::BasicBlock* success = llvm::BasicBlock::Create(context_, "wb_success", fn);
    llvm::BasicBlock* failure = llvm::BasicBlock::Create(context_, "wb_failure", fn);
    builder_.CreateCondBr(builder_.CreateICmpEQ(status,
        llvm::ConstantInt::get(int32Type(), 0)), success, failure);
    builder_.SetInsertPoint(failure);
    llvm::Function* emergency = module_.getFunction("eshkol_runtime_emergency_raise_v1");
    if (!emergency) {
        emergency = llvm::Function::Create(
            llvm::FunctionType::get(voidType(), {int32Type()}, false),
            llvm::GlobalValue::ExternalLinkage, "eshkol_runtime_emergency_raise_v1", &module_);
    }
    emergency->setDoesNotReturn();
    builder_.CreateCall(emergency, {status});
    builder_.CreateUnreachable();
    // Never load uninitialized staging output, or reach the caller's store,
    // after a failed transaction. Runtime transaction temporaries are gone.
    builder_.SetInsertPoint(success);
    return builder_.CreateLoad(tv_ty, out_slot, "wb_result");
}

void CodegenContext::emitConstructorAllocationCheck(llvm::Value* pointer) {
    // The whole-module arena-allocation hardening pass recognizes this marker
    // and does not add a second copy of the same condition-5 guard.  Keep the
    // marker on the allocator call itself: it survives block splitting and is
    // unambiguous even when later code adds another null comparison.
    if (auto* allocation = llvm::dyn_cast<llvm::Instruction>(pointer)) {
        allocation->setMetadata(
            "eshkol.arena_alloc.checked",
            llvm::MDNode::get(context_, llvm::ArrayRef<llvm::Metadata*>{}));
    }
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    auto* success = llvm::BasicBlock::Create(context_, "constructor_allocated", fn);
    auto* failure = llvm::BasicBlock::Create(context_, "constructor_failed", fn);
    builder_.CreateCondBr(builder_.CreateIsNotNull(pointer), success, failure);
    builder_.SetInsertPoint(failure);
    auto emergency = module_.getOrInsertFunction("eshkol_runtime_emergency_raise_v1",
        llvm::FunctionType::get(voidType(), {int32Type()}, false));
    auto* call = builder_.CreateCall(emergency, {llvm::ConstantInt::get(int32Type(), 5)});
    call->setDoesNotReturn();
    builder_.CreateUnreachable();
    builder_.SetInsertPoint(success);
}

// === Runtime Guard Failure ===
//
// See the header for the full history: a guard whose diagnostic was conditional
// on lookupFunction("printf")/("exit") compiled to a bare `unreachable`, which
// let LLVM delete the guarding branch outright. These two helpers are the single
// implementation every codegen module uses to close a guard's failure block.

void CodegenContext::emitRaise(const char* message) {
    llvm::Value* msg = builder_.CreateGlobalString(message ? message : "runtime error");
    emitRaiseWithMessagePtr(msg);
}

void CodegenContext::emitRaiseFmt(const char* format, llvm::ArrayRef<llvm::Value*> args) {
    if (args.empty()) {
        emitRaise(format);
        return;
    }

    // Buffer lives in the entry block: a guard can sit inside a loop body, and
    // an alloca there re-adjusts the stack pointer on every iteration.
    constexpr uint64_t kMsgBufSize = 256;
    llvm::Function* fn = builder_.GetInsertBlock()->getParent();
    llvm::IRBuilderBase::InsertPoint saved_ip = builder_.saveIP();
    if (fn && !fn->empty()) {
        llvm::BasicBlock& entry = fn->getEntryBlock();
        builder_.SetInsertPoint(&entry, entry.begin());
    }
    llvm::AllocaInst* buf = builder_.CreateAlloca(
        int8Type(), llvm::ConstantInt::get(int64Type(), kMsgBufSize), "raise_msg_buf");
    builder_.restoreIP(saved_ip);

    llvm::Function* snprintf_fn = funcs_.getSnprintf();
    if (!snprintf_fn) {
        // No snprintf available: still raise, just without the runtime detail.
        emitRaise(format);
        return;
    }

    std::vector<llvm::Value*> call_args;
    call_args.reserve(args.size() + 3);
    call_args.push_back(buf);
    // snprintf's second parameter is size_t, which is i32 on wasm32 and i64 on
    // native 64-bit — take the width from the type system rather than assuming.
    call_args.push_back(llvm::ConstantInt::get(types_.getSizeType(), kMsgBufSize));
    call_args.push_back(builder_.CreateGlobalString(format));
    for (llvm::Value* a : args) call_args.push_back(a);
    builder_.CreateCall(snprintf_fn, call_args);

    emitRaiseWithMessagePtr(buf);
}

void CodegenContext::emitRaiseWithMessagePtr(llvm::Value* message_ptr) {
    llvm::Function* make_exc = module_.getFunction("eshkol_make_exception_with_header");
    if (!make_exc) {
        llvm::FunctionType* make_ty = llvm::FunctionType::get(
            ptrType(), {int32Type(), ptrType()}, false);
        make_exc = llvm::Function::Create(make_ty, llvm::GlobalValue::ExternalLinkage,
                                          "eshkol_make_exception_with_header", &module_);
    }
    llvm::Function* raise_fn = module_.getFunction("eshkol_raise");
    if (!raise_fn) {
        llvm::FunctionType* raise_ty = llvm::FunctionType::get(
            voidType(), {ptrType()}, false);
        raise_fn = llvm::Function::Create(raise_ty, llvm::GlobalValue::ExternalLinkage,
                                          "eshkol_raise", &module_);
        raise_fn->setDoesNotReturn();
    }

    llvm::Value* exc_type = llvm::ConstantInt::get(int32Type(), ESHKOL_EXCEPTION_ERROR);
    llvm::Value* exc = builder_.CreateCall(make_exc, {exc_type, message_ptr});
    builder_.CreateCall(raise_fn, {exc});
    builder_.CreateUnreachable();
}

CodegenContext::CodegenContext(llvm::LLVMContext& llvm_ctx,
                               llvm::Module& llvm_mod,
                               llvm::IRBuilder<>& ir_builder,
                               TypeSystem& type_sys,
                               FunctionCache& func_cache,
                               MemoryCodegen& mem_codegen)
    : context_(llvm_ctx)
    , module_(llvm_mod)
    , builder_(ir_builder)
    , types_(type_sys)
    , funcs_(func_cache)
    , memory_(mem_codegen) {
    // Start with one local scope
    scope_stack_.emplace_back();
}

// === Symbol Table Management ===

void CodegenContext::pushScope() {
    scope_stack_.emplace_back();
}

void CodegenContext::popScope() {
    if (scope_stack_.size() > 1) {
        scope_stack_.pop_back();
    }
}

llvm::Value* CodegenContext::lookupSymbol(const std::string& name) {
    // Search from innermost to outermost local scope
    for (auto it = scope_stack_.rbegin(); it != scope_stack_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return found->second;
        }
    }
    // Fall back to global symbols
    auto global_it = global_symbols_.find(name);
    if (global_it != global_symbols_.end()) {
        return global_it->second;
    }
    return nullptr;
}

void CodegenContext::defineSymbol(const std::string& name, llvm::Value* value) {
    if (!scope_stack_.empty()) {
        scope_stack_.back()[name] = value;
    }
}

void CodegenContext::defineGlobalSymbol(const std::string& name, llvm::Value* value) {
    global_symbols_[name] = value;
}

llvm::Value* CodegenContext::lookupGlobalSymbol(const std::string& name) {
    auto it = global_symbols_.find(name);
    return (it != global_symbols_.end()) ? it->second : nullptr;
}

// === Function Table ===

llvm::Function* CodegenContext::lookupFunction(const std::string& name) {
    auto it = function_table_.find(name);
    return (it != function_table_.end()) ? it->second : nullptr;
}

void CodegenContext::defineFunction(const std::string& name, llvm::Function* func) {
    function_table_[name] = func;
}

bool CodegenContext::hasFunction(const std::string& name) const {
    return function_table_.find(name) != function_table_.end();
}

// === Variadic Function Info ===

void CodegenContext::registerVariadicFunction(const std::string& name,
                                              uint64_t fixedParams,
                                              bool isVariadic) {
    variadic_info_[name] = {fixedParams, isVariadic};
}

std::pair<uint64_t, bool> CodegenContext::getVariadicInfo(const std::string& name) const {
    auto it = variadic_info_.find(name);
    return (it != variadic_info_.end()) ? it->second : std::make_pair(static_cast<uint64_t>(0), false);
}

// === String Interning ===

llvm::GlobalVariable* CodegenContext::internString(const std::string& str) {
    // Check if already interned
    auto it = interned_strings_.find(str);
    if (it != interned_strings_.end()) {
        return it->second;
    }

    // Create a new global string
    llvm::Constant* str_constant = llvm::ConstantDataArray::getString(context_, str, true);
    llvm::GlobalVariable* global_str = new llvm::GlobalVariable(
        module_,
        str_constant->getType(),
        true,  // isConstant
        llvm::GlobalValue::PrivateLinkage,
        str_constant,
        ".str"
    );
    global_str->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    interned_strings_[str] = global_str;
    return global_str;
}

llvm::Value* CodegenContext::internCString(const std::string& str) {
    llvm::GlobalVariable* global_str = internString(str);
    return builder_.CreatePointerCast(global_str, llvm::PointerType::get(context_, 0));
}

llvm::GlobalVariable* CodegenContext::lookupInternedString(const std::string& str) {
    auto it = interned_strings_.find(str);
    return (it != interned_strings_.end()) ? it->second : nullptr;
}

llvm::Value* CodegenContext::internStringWithHeader(const std::string& str, uint8_t subtype) {
    // SYMBOL subtype is special: R7RS §6.5 requires every literal (quote sym)
    // to denote the same object across the whole program. A per-module
    // private constant (what the STRING path below produces) gives us a
    // fresh pointer per module, and since each top-level form in the JIT
    // compiles to its own module, `(define x 'foo) (define y 'foo)` ends
    // up with `(eq? x y)` = #f — broken. Route symbol literals through a
    // runtime helper that hashes the name into a process-global intern
    // table, so every module observes the same canonical pointer.
    //
    // The helper eshkol_intern_symbol_lookup lives in introspection.cpp
    // and shares g_interned_symbols with string->symbol / procedure-name /
    // type-of, so all four interning paths converge.
    if (subtype == HEAP_SUBTYPE_SYMBOL) {
        // Name string constant (module-local; LLVM dedups these across the
        // module naturally, so repeated references to the same symbol share
        // a single underlying `.str` global).
        llvm::Value* name_ptr = builder_.CreateGlobalStringPtr(str, ".sym_name");

        // Declare the extern runtime helper.
        llvm::Type* ptr_ty = llvm::PointerType::get(context_, 0);
        llvm::FunctionType* fn_ty = llvm::FunctionType::get(ptr_ty, {ptr_ty}, false);
        llvm::FunctionCallee fn =
            module_.getOrInsertFunction("eshkol_intern_symbol_lookup", fn_ty);

        // Emit the call. Result is the canonical symbol pointer — same
        // representation as the module-local constant the STRING path
        // produces (pointer to char data, with a HEAP_SUBTYPE_SYMBOL header
        // at ptr-8), so all downstream packPtrToTaggedValue / eq? / etc.
        // callers work unchanged.
        return builder_.CreateCall(fn, {name_ptr}, "sym");
    }

    // Check if already interned with header
    std::string key = str + "_hdr_" + std::to_string(subtype);
    auto it = headered_strings_.find(key);
    if (str.find('\0') == std::string::npos && it != headered_strings_.end()) {
        return it->second;
    }

    // A global constant whose payload begins with NUL can be shortened by the
    // object/linker pipeline even when its LLVM type and header retain the
    // full length.  Materialize such literals in the arena instead, writing
    // the complete payload with explicit byte offsets.  This keeps the
    // length-carrying string contract intact for literals as well as runtime
    // strings and avoids any C-string interpretation at this boundary.
    if (subtype == HEAP_SUBTYPE_STRING && str.find('\0') != std::string::npos) {
        llvm::Value* arena = builder_.CreateLoad(ptrType(), globalArena());
        llvm::Value* length = llvm::ConstantInt::get(sizeType(), str.size());
        llvm::Value* result = createReviewedHeaderedCall(
            memory().getArenaAllocateStringWithHeader(), {arena, length},
            "nul_string_literal");
        if (!result) return nullptr;
        // Make the local null transfer explicit before payload writes. The
        // whole-module arena hardening pass also covers this constructor;
        // the checked marker keeps the two guards from being duplicated.
        emitConstructorAllocationCheck(result);
        builder_.CreateMemSet(result, llvm::ConstantInt::get(int8Type(), 0),
                              length, llvm::MaybeAlign(1));
        for (size_t i = 0; i < str.size(); i++) {
            if (str[i] == '\0') continue;
            llvm::Value* byte_ptr = builder_.CreateGEP(
                int8Type(), result,
                llvm::ConstantInt::get(int64Type(), i));
            builder_.CreateStore(
                llvm::ConstantInt::get(int8Type(),
                                       static_cast<unsigned char>(str[i])),
                byte_ptr);
        }
        return result;
    }

    // Create struct type: {i8 subtype, i8 flags, i16 ref_count, i32 size, [N x i8] data}
    // This matches eshkol_object_header_t followed by string data
    size_t str_len = str.length() + 1;  // Include null terminator

    llvm::ArrayType* str_array_type = llvm::ArrayType::get(int8Type(), str_len);
    llvm::StructType* headered_str_type = llvm::StructType::get(
        context_,
        {int8Type(), int8Type(), int16Type(), int32Type(), str_array_type},
        true  // packed for exact layout
    );

    // Create header constants
    llvm::Constant* subtype_const = llvm::ConstantInt::get(int8Type(), subtype);
    llvm::Constant* flags_const = llvm::ConstantInt::get(int8Type(), 0);
    llvm::Constant* ref_count_const = llvm::ConstantInt::get(int16Type(), 0);
    llvm::Constant* size_const = llvm::ConstantInt::get(int32Type(), (uint32_t)str_len);

    // Create the payload from explicit bytes.  ConstantDataArray::getString
    // treats an embedded NUL as the end of C-string text on this LLVM path,
    // leaving the rest of a length-recorded literal undefined.  That made a
    // long NUL-bearing literal have the right header but nondeterministic
    // contents.  The byte-array overload preserves the complete payload.
    std::vector<llvm::Constant*> str_bytes;
    str_bytes.reserve(str_len);
    for (unsigned char byte : str) {
        str_bytes.push_back(llvm::ConstantInt::get(int8Type(), byte));
    }
    str_bytes.push_back(llvm::ConstantInt::get(int8Type(), 0));
    llvm::Constant* str_data = llvm::ConstantArray::get(
        str_array_type, str_bytes);

    // Create the complete struct initializer
    llvm::Constant* struct_init = llvm::ConstantStruct::get(
        headered_str_type,
        {subtype_const, flags_const, ref_count_const, size_const, str_data}
    );

    // Create global variable
    llvm::GlobalVariable* global_struct = new llvm::GlobalVariable(
        module_,
        headered_str_type,
        true,  // isConstant
        llvm::GlobalValue::PrivateLinkage,
        struct_init,
        ".str_hdr"
    );
    global_struct->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    // GEP to get pointer to string data (field index 4, the array)
    llvm::Constant* zero = llvm::ConstantInt::get(int32Type(), 0);
    llvm::Constant* idx_data = llvm::ConstantInt::get(int32Type(), 4);
    llvm::Constant* indices[] = {zero, idx_data, zero};
    llvm::Constant* str_ptr = llvm::ConstantExpr::getInBoundsGetElementPtr(
        headered_str_type, global_struct, indices
    );

    headered_strings_[key] = str_ptr;
    return str_ptr;
}

// === Nested Function Captures ===

void CodegenContext::setFunctionCaptures(const std::string& funcName,
                                         const std::vector<std::string>& captures) {
    function_captures_[funcName] = captures;
}

const std::vector<std::string>* CodegenContext::getFunctionCaptures(const std::string& funcName) const {
    auto it = function_captures_.find(funcName);
    return (it != function_captures_.end()) ? &it->second : nullptr;
}

// === Lambda Return Tracking ===

void CodegenContext::setFunctionReturnsLambda(const std::string& funcName,
                                              const std::string& lambdaName) {
    functions_returning_lambda_[funcName] = lambdaName;
}

std::string CodegenContext::getFunctionReturnsLambda(const std::string& funcName) const {
    auto it = functions_returning_lambda_.find(funcName);
    return (it != functions_returning_lambda_.end()) ? it->second : "";
}

} // namespace eshkol

#endif // ESHKOL_LLVM_BACKEND_ENABLED
