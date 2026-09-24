/*
 * Copyright (C) tsotchke
 * SPDX-License-Identifier: MIT
 */

#include <eshkol/backend/arithmetic_codegen.h>
#include <eshkol/backend/autodiff_codegen.h>
#include <eshkol/backend/codegen_context.h>
#include <eshkol/backend/complex_codegen.h>
#include <eshkol/backend/function_cache.h>
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
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <string>

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

    constexpr std::array<uint32_t, 12> patterns = {
        UINT32_C(0x00000000), UINT32_C(0x80000000),
        UINT32_C(0x00000001), UINT32_C(0x007fffff),
        UINT32_C(0x00800000), UINT32_C(0x3f800000),
        UINT32_C(0x7f7fffff), UINT32_C(0x7f800000),
        UINT32_C(0xff800000), UINT32_C(0x7fc12345),
        UINT32_C(0xffc12345), UINT32_C(0x7f812345),
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

    eshkol::TensorCodegen tensor(context, tagged, memory);
    eshkol::AutodiffCodegen autodiff(context, tagged, memory);
    eshkol::ComplexCodegen complex(context, tagged, memory);
    tensor.setAutodiffCodegen(&autodiff);
    eshkol::ArithmeticCodegen arithmetic(
        context, tagged, tensor, autodiff, complex);

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
        modulo_ir.find("f32_to_f64") == std::string::npos ||
        modulo_ir.find("f32_scalar_peer_reject") == std::string::npos) {
        return fail("f32 modulo omitted promotion or peer guard");
    }

    std::cout << "PASS: canonical LLVM f32 classification and checked f64 promotion\n";
    return 0;
}
