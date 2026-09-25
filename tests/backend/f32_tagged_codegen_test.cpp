/*
 * Copyright (C) tsotchke
 * SPDX-License-Identifier: MIT
 */

#include <eshkol/backend/arithmetic_codegen.h>
#include <eshkol/backend/autodiff_codegen.h>
#include <eshkol/backend/codegen_context.h>
#include <eshkol/backend/complex_codegen.h>
#include <eshkol/backend/function_cache.h>
#include <eshkol/backend/hash_codegen.h>
#include <eshkol/backend/memory_codegen.h>
#include <eshkol/backend/tagged_value_codegen.h>
#include <eshkol/backend/tensor_codegen.h>
#include <eshkol/backend/type_system.h>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

llvm::ConstantInt* as_int(llvm::Value* value) {
    return llvm::dyn_cast_or_null<llvm::ConstantInt>(value);
}

llvm::Constant* tagged_constant(eshkol::CodegenContext& context,
                                uint8_t type, uint8_t flags,
                                uint16_t reserved, uint32_t padding,
                                uint64_t payload) {
    return llvm::ConstantStruct::get(
        context.taggedValueType(),
        {llvm::ConstantInt::get(context.int8Type(), type),
         llvm::ConstantInt::get(context.int8Type(), flags),
         llvm::ConstantInt::get(context.int16Type(), reserved),
         llvm::ConstantInt::get(context.int32Type(), padding),
         llvm::ConstantInt::get(context.int64Type(), payload)});
}

bool is_fixed_positive_qnan(const llvm::Value* value) {
    auto* fp = llvm::dyn_cast_or_null<llvm::ConstantFP>(value);
    return fp && fp->getType()->isDoubleTy() &&
           fp->getValueAPF().bitcastToAPInt().getZExtValue() ==
               UINT64_C(0x7ff8000000000000);
}

bool has_checked_canonical_nan_promotion(const llvm::Function& function) {
    bool has_unordered_nan_check = false;
    bool has_widen = false;
    bool has_fixed_nan_select = false;
    for (const llvm::BasicBlock& block : function) {
        for (const llvm::Instruction& instruction : block) {
            if (const auto* compare = llvm::dyn_cast<llvm::FCmpInst>(&instruction)) {
                has_unordered_nan_check |=
                    compare->getPredicate() == llvm::FCmpInst::FCMP_UNO;
            }
            has_widen |= llvm::isa<llvm::FPExtInst>(instruction);
            if (const auto* select = llvm::dyn_cast<llvm::SelectInst>(&instruction)) {
                has_fixed_nan_select |=
                    is_fixed_positive_qnan(select->getTrueValue()) ||
                    is_fixed_positive_qnan(select->getFalseValue());
            }
        }
    }
    return has_unordered_nan_check && has_widen && has_fixed_nan_select;
}

}  // namespace

int main() {
    llvm::LLVMContext llvm_context;
    llvm::Module module("f32-tagged-codegen-gate", llvm_context);
    llvm::IRBuilder<> builder(llvm_context);
    eshkol::TypeSystem types(llvm_context);
    eshkol::FunctionCache functions(module, types);
    eshkol::MemoryCodegen memory(module, types);
    eshkol::CodegenContext context(
        llvm_context, module, builder, types, functions, memory);
    eshkol::TaggedValueCodegen tagged(context);
    new llvm::GlobalVariable(
        module, context.ptrType(), false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantPointerNull::get(context.ptrType()),
        "__global_arena");

    llvm::Function* function = llvm::Function::Create(
        llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_context), false),
        llvm::GlobalValue::ExternalLinkage,
        "f32_tagged_codegen_gate",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", function));

    constexpr std::array<uint32_t, 13> patterns = {
        UINT32_C(0x00000000), UINT32_C(0x80000000),
        UINT32_C(0x00000001), UINT32_C(0x007fffff),
        UINT32_C(0x00800000), UINT32_C(0x3f800000),
        UINT32_C(0x7f7fffff), UINT32_C(0x7f800000),
        UINT32_C(0xff800000), UINT32_C(0x7fc12345),
        UINT32_C(0xffc12345), UINT32_C(0x7f812345),
        UINT32_C(0xff812345),
    };

    for (uint32_t bits : patterns) {
        llvm::Constant* raw = llvm::ConstantFP::get(
            llvm_context,
            llvm::APFloat(llvm::APFloat::IEEEsingle(), llvm::APInt(32, bits)));
        llvm::Value* packed = tagged.packFloat32(raw);
        if (!packed || packed->getType() != context.taggedValueType()) {
            return fail("packFloat32 did not return tagged_value");
        }

        llvm::ConstantInt* type = as_int(builder.CreateExtractValue(
            packed, {eshkol::TAGGED_TYPE_IDX}));
        llvm::ConstantInt* flags = as_int(builder.CreateExtractValue(
            packed, {eshkol::TAGGED_FLAGS_IDX}));
        llvm::ConstantInt* reserved = as_int(builder.CreateExtractValue(
            packed, {eshkol::TAGGED_RESERVED_IDX}));
        llvm::ConstantInt* padding = as_int(builder.CreateExtractValue(
            packed, {eshkol::TAGGED_PADDING_IDX}));
        llvm::ConstantInt* payload = as_int(builder.CreateExtractValue(
            packed, {eshkol::TAGGED_DATA_IDX}));
        if (!type || type->getZExtValue() != ESHKOL_VALUE_FLOAT32 ||
            !flags || flags->getZExtValue() != ESHKOL_VALUE_INEXACT_FLAG ||
            !reserved || !reserved->isZero() ||
            !padding || !padding->isZero() ||
            !payload || payload->getZExtValue() != bits) {
            return fail("packFloat32 violated canonical tag-11 layout");
        }

        llvm::Value* unpacked = tagged.unpackFloat32(packed);
        auto* unpacked_fp = llvm::dyn_cast_or_null<llvm::ConstantFP>(unpacked);
        if (!unpacked_fp ||
            unpacked_fp->getValueAPF().bitcastToAPInt().getZExtValue() != bits) {
            return fail("unpackFloat32 changed the raw binary32 word");
        }

        const bool is_nan =
            (bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
            (bits & UINT32_C(0x007fffff)) != 0;
        if (is_nan &&
            (!is_fixed_positive_qnan(tagged.promoteFloat32ToDouble(raw)) ||
             !is_fixed_positive_qnan(tagged.promoteFloat32ToDouble(packed)))) {
            return fail("raw/tagged signed qNaN/sNaN promotion was not fixed +qNaN");
        }

        llvm::Value* ensured = tagged.ensureTagged(raw);
        llvm::ConstantInt* ensured_payload = as_int(builder.CreateExtractValue(
            ensured, {eshkol::TAGGED_DATA_IDX}));
        if (!ensured_payload || ensured_payload->getZExtValue() != bits) {
            return fail("ensureTagged did not route LLVM f32 through tag 11");
        }
        llvm::ConstantInt* raw_type = as_int(tagged.getType(raw));
        llvm::ConstantInt* is_f32 = as_int(tagged.isFloat32(packed));
        llvm::ConstantInt* is_numeric = as_int(tagged.isNumeric(packed));
        if (!raw_type || raw_type->getZExtValue() != ESHKOL_VALUE_FLOAT32 ||
            !is_f32 || !is_f32->isOne() ||
            !is_numeric || !is_numeric->isOne()) {
            return fail("LLVM f32 numeric classification failed");
        }
    }

    llvm::Constant* f32_pzero = tagged_constant(
        context, ESHKOL_VALUE_FLOAT32, ESHKOL_VALUE_INEXACT_FLAG,
        0, 0, UINT64_C(0x00000000));
    llvm::Constant* f32_nzero = tagged_constant(
        context, ESHKOL_VALUE_FLOAT32, ESHKOL_VALUE_INEXACT_FLAG,
        0, 0, UINT64_C(0x80000000));
    llvm::Constant* f32_normal = tagged_constant(
        context, ESHKOL_VALUE_FLOAT32, ESHKOL_VALUE_INEXACT_FLAG,
        0, 0, UINT64_C(0x3fc00000));
    llvm::Constant* f32_qnan = tagged_constant(
        context, ESHKOL_VALUE_FLOAT32, ESHKOL_VALUE_INEXACT_FLAG,
        0, 0, UINT64_C(0x7fc12345));
    llvm::Value* tagged_double = tagged.packDouble(
        llvm::ConstantFP::get(context.doubleType(), 1.5));
    llvm::ConstantInt* zeros_equal = as_int(
        tagged.float32Equal(f32_pzero, f32_nzero));
    llvm::ConstantInt* normal_equal = as_int(
        tagged.float32Equal(f32_normal, f32_normal));
    llvm::ConstantInt* nan_equal = as_int(
        tagged.float32Equal(f32_qnan, f32_qnan));
    llvm::ConstantInt* cross_tag_equal = as_int(
        tagged.float32Equal(f32_normal, tagged_double));
    if (!zeros_equal || !zeros_equal->isOne() ||
        !normal_equal || !normal_equal->isOne() ||
        !nan_equal || !nan_equal->isZero() ||
        !cross_tag_equal || !cross_tag_equal->isZero()) {
        return fail("FLOAT32 equality policy violated IEEE/tag semantics");
    }

    const std::array<llvm::Constant*, 6> malformed = {
        tagged_constant(context, ESHKOL_VALUE_FLOAT32, 0, 0, 0,
                        UINT64_C(0x3f800000)),
        tagged_constant(context, ESHKOL_VALUE_FLOAT32,
                        ESHKOL_VALUE_INEXACT_FLAG, 1, 0,
                        UINT64_C(0x3f800000)),
        tagged_constant(context, ESHKOL_VALUE_FLOAT32,
                        ESHKOL_VALUE_INEXACT_FLAG, 0, 1,
                        UINT64_C(0x3f800000)),
        tagged_constant(context, ESHKOL_VALUE_FLOAT32,
                        ESHKOL_VALUE_INEXACT_FLAG, 0, 0,
                        UINT64_C(0x000000013f800000)),
        tagged_constant(context, 27, ESHKOL_VALUE_INEXACT_FLAG, 0, 0,
                        UINT64_C(0x3f800000)),
        tagged_constant(context, 43, ESHKOL_VALUE_INEXACT_FLAG, 0, 0,
                        UINT64_C(0x3f800000)),
    };
    for (llvm::Constant* value : malformed) {
        llvm::ConstantInt* is_f32 = as_int(tagged.isFloat32(value));
        if (!is_f32 || !is_f32->isZero()) {
            return fail("isFloat32 accepted a malformed or folded value");
        }
        if (tagged.unpackFloat32(value) != nullptr) {
            return fail("unpackFloat32 accepted a malformed or folded value");
        }
        llvm::ConstantInt* self_equal = as_int(
            tagged.float32Equal(value, value));
        if (!self_equal || !self_equal->isZero()) {
            return fail("FLOAT32 equality accepted malformed or folded data");
        }
    }

    builder.CreateRetVoid();
    if (llvm::verifyFunction(*function, &llvm::errs())) {
        return fail("generated f32 helper IR did not verify");
    }

    llvm::Function* checked_unpack = llvm::Function::Create(
        llvm::FunctionType::get(
            llvm::Type::getFloatTy(llvm_context),
            {context.taggedValueType()}, false),
        llvm::GlobalValue::ExternalLinkage,
        "checked_unpack_f32",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", checked_unpack));
    llvm::Value* dynamic_unpacked = tagged.unpackFloat32(
        checked_unpack->getArg(0));
    if (!dynamic_unpacked || checked_unpack->size() != 3) {
        return fail("dynamic unpackFloat32 did not emit a checked layout branch");
    }
    builder.CreateRet(dynamic_unpacked);
    if (llvm::verifyFunction(*checked_unpack, &llvm::errs())) {
        return fail("dynamic checked f32 unpack IR did not verify");
    }

    llvm::Function* type_of_delegate = llvm::Function::Create(
        llvm::FunctionType::get(
            context.taggedValueType(), {context.taggedValueType()}, false),
        llvm::GlobalValue::ExternalLinkage,
        "checked_type_of_full_carrier",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", type_of_delegate));
    llvm::Value* type_symbol = tagged.typeOf(type_of_delegate->getArg(0));
    if (!type_symbol) {
        return fail("typeOf rejected a tagged carrier");
    }
    builder.CreateRet(type_symbol);
    if (llvm::verifyFunction(*type_of_delegate, &llvm::errs())) {
        return fail("full-carrier typeOf delegation IR did not verify");
    }
    bool stored_original_carrier = false;
    bool called_pointer_mapper = false;
    for (const llvm::BasicBlock& block : *type_of_delegate) {
        for (const llvm::Instruction& instruction : block) {
            if (const auto* store = llvm::dyn_cast<llvm::StoreInst>(&instruction)) {
                stored_original_carrier |=
                    store->getValueOperand() == type_of_delegate->getArg(0);
            }
            if (const auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
                const llvm::Function* callee = call->getCalledFunction();
                called_pointer_mapper |= callee &&
                    callee->getName() == "eshkol_type_of_ref_v1_store";
            }
        }
    }
    if (!stored_original_carrier || !called_pointer_mapper) {
        return fail("typeOf rebuilt the carrier or bypassed the pointer mapper");
    }

    eshkol::TensorCodegen tensor(context, tagged, memory);
    eshkol::AutodiffCodegen autodiff(context, tagged, memory);
    eshkol::ComplexCodegen complex(context, tagged, memory);
    tensor.setAutodiffCodegen(&autodiff);
    eshkol::ArithmeticCodegen arithmetic(
        context, tagged, tensor, autodiff, complex);
    std::unordered_map<std::string, llvm::Function*> hash_functions;
    eshkol::HashCodegen hash(
        context, tagged, memory, hash_functions, arithmetic);

    for (uint32_t bits : patterns) {
        llvm::Constant* raw = llvm::ConstantFP::get(
            llvm_context,
            llvm::APFloat(llvm::APFloat::IEEEsingle(), llvm::APInt(32, bits)));
        llvm::Value* stored = hash.tagForStorage(raw);
        llvm::ConstantInt* type = as_int(builder.CreateExtractValue(
            stored, {eshkol::TAGGED_TYPE_IDX}));
        llvm::ConstantInt* flags = as_int(builder.CreateExtractValue(
            stored, {eshkol::TAGGED_FLAGS_IDX}));
        llvm::ConstantInt* payload = as_int(builder.CreateExtractValue(
            stored, {eshkol::TAGGED_DATA_IDX}));
        if (!type || type->getZExtValue() != ESHKOL_VALUE_FLOAT32 ||
            !flags || flags->getZExtValue() != ESHKOL_VALUE_INEXACT_FLAG ||
            !payload || payload->getZExtValue() != bits) {
            return fail("HashCodegen changed a raw LLVM f32 storage key");
        }
    }

    llvm::Function* promote = llvm::Function::Create(
        llvm::FunctionType::get(
            llvm::Type::getDoubleTy(llvm_context),
            {context.taggedValueType()}, false),
        llvm::GlobalValue::ExternalLinkage,
        "checked_promote_f32",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", promote));
    llvm::Value* promoted = arithmetic.extractAsDouble(promote->getArg(0));
    builder.CreateRet(promoted);
    if (llvm::verifyFunction(*promote, &llvm::errs())) {
        return fail("checked f32-to-f64 promotion IR did not verify");
    }
    if (!has_checked_canonical_nan_promotion(*promote)) {
        return fail("tagged f32 promotion omitted unordered check, widen, or fixed +qNaN select");
    }

    llvm::Function* promote_raw = llvm::Function::Create(
        llvm::FunctionType::get(
            llvm::Type::getDoubleTy(llvm_context),
            {llvm::Type::getFloatTy(llvm_context)}, false),
        llvm::GlobalValue::ExternalLinkage,
        "checked_promote_raw_f32",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", promote_raw));
    builder.CreateRet(tagged.promoteFloat32ToDouble(promote_raw->getArg(0)));
    if (llvm::verifyFunction(*promote_raw, &llvm::errs()) ||
        !has_checked_canonical_nan_promotion(*promote_raw)) {
        return fail("raw f32 promotion omitted unordered check, widen, or fixed +qNaN select");
    }

    // Generic numeric dispatch still emits an f32 arm when an operand's tag is
    // compile-time constant.  A tagged integer makes that arm unreachable and
    // unpackFloat32 returns nullptr by contract; extractAsDouble must terminate
    // the arm without passing that nullptr into LLVM's CreateFPExt.
    llvm::Function* promote_tagged_int = llvm::Function::Create(
        llvm::FunctionType::get(
            llvm::Type::getDoubleTy(llvm_context), false),
        llvm::GlobalValue::ExternalLinkage,
        "checked_promote_tagged_int",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", promote_tagged_int));
    llvm::Value* promoted_tagged_int = arithmetic.extractAsDouble(
        tagged_constant(context, ESHKOL_VALUE_INT64, 0, 0, 0, 0));
    builder.CreateRet(promoted_tagged_int);
    if (llvm::verifyFunction(*promote_tagged_int, &llvm::errs())) {
        return fail("constant tagged integer promotion IR did not verify");
    }

    llvm::Function* scalar_pair = llvm::Function::Create(
        llvm::FunctionType::get(
            llvm::Type::getDoubleTy(llvm_context),
            {context.taggedValueType(), context.taggedValueType()}, false),
        llvm::GlobalValue::ExternalLinkage,
        "checked_f32_scalar_pair",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", scalar_pair));
    arithmetic.guardFloat32ScalarBinaryOperands(
        scalar_pair->getArg(0), scalar_pair->getArg(1));
    llvm::Value* pair_left = arithmetic.extractAsDouble(scalar_pair->getArg(0));
    llvm::Value* pair_right = arithmetic.extractAsDouble(scalar_pair->getArg(1));
    builder.CreateRet(builder.CreateFAdd(pair_left, pair_right));
    if (llvm::verifyFunction(*scalar_pair, &llvm::errs())) {
        return fail("guarded f32 scalar-pair IR did not verify");
    }
    std::string scalar_pair_ir;
    llvm::raw_string_ostream scalar_pair_stream(scalar_pair_ir);
    scalar_pair->print(scalar_pair_stream);
    scalar_pair_stream.flush();
    if (scalar_pair_ir.find("f32_folded_tag_reject") == std::string::npos)
        return fail("f32 scalar guard omitted folded-tag rejection block");
    if (scalar_pair_ir.find("f32_scalar_peer_reject") == std::string::npos)
        return fail("f32 scalar guard omitted unsupported-peer rejection block");
    if (scalar_pair_ir.find(", 27") == std::string::npos)
        return fail("f32 scalar guard omitted folded tag 27");
    if (scalar_pair_ir.find(", 43") == std::string::npos)
        return fail("f32 scalar guard omitted folded tag 43");

    llvm::Function* modulo = llvm::Function::Create(
        llvm::FunctionType::get(
            context.taggedValueType(),
            {context.taggedValueType(), context.taggedValueType()}, false),
        llvm::GlobalValue::ExternalLinkage,
        "checked_f32_modulo",
        module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        llvm_context, "entry", modulo));
    llvm::Value* modulo_result = arithmetic.mod(
        modulo->getArg(0), modulo->getArg(1));
    builder.CreateRet(modulo_result);
    if (llvm::verifyFunction(*modulo, &llvm::errs())) {
        return fail("guarded f32 modulo IR did not verify");
    }
    std::string modulo_ir;
    llvm::raw_string_ostream modulo_stream(modulo_ir);
    modulo->print(modulo_stream);
    modulo_stream.flush();
    if (modulo_ir.find("mod_double") == std::string::npos ||
        !has_checked_canonical_nan_promotion(*modulo) ||
        modulo_ir.find("f32_scalar_peer_reject") == std::string::npos) {
        return fail("f32 modulo omitted promotion or peer guard");
    }

    std::cout << "PASS: canonical LLVM f32 classification and checked f64 promotion\n";
    return 0;
}
