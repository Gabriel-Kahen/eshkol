/* Compiler-owned call origin is separate from a native Function declaration. */
#include <eshkol/backend/codegen_context.h>
#include <eshkol/backend/tagged_value_codegen.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <cstdio>

struct Fixture {
    llvm::LLVMContext llvm;
    llvm::Module module{"headered-call-provenance", llvm};
    llvm::IRBuilder<> builder{llvm};
    eshkol::TypeSystem types{llvm};
    eshkol::FunctionCache functions{module, types};
    eshkol::MemoryCodegen memory{module, types};
    eshkol::CodegenContext context{llvm, module, builder, types, functions, memory};
    eshkol::TaggedValueCodegen tagged{context};
    llvm::Function* function;

    Fixture() {
        function = llvm::Function::Create(
            llvm::FunctionType::get(context.voidType(),
                {context.int1Type(), context.int8Type()}, false),
            llvm::GlobalValue::ExternalLinkage, "test", module);
        builder.SetInsertPoint(llvm::BasicBlock::Create(llvm, "entry", function));
    }
    llvm::Value* arena() { return llvm::ConstantPointerNull::get(context.ptrType()); }
    llvm::Value* cons() {
        return context.createReviewedHeaderedCall(
            memory.getArenaAllocateConsWithHeader(), {arena()}, "owned_cons");
    }
};

static int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

int main() {
    {
        Fixture f;
        auto* owned = f.cons();
        if (!llvm::isa<llvm::CallInst>(owned) ||
            f.context.pointerProvenance(owned) != eshkol::PointerProvenance::ManagedHeap ||
            f.context.hasFatalCodegenError()) return fail("owned call lacked heap authority");
        f.tagged.packHeapPtr(owned);
        if (f.context.hasFatalCodegenError()) return fail("matching managed tag rejected");
        auto* raw_alias = f.builder.CreateCall(
            f.memory.getArenaAllocateConsWithHeader(), {f.arena()}, "extern_alias");
        if (f.context.pointerProvenance(raw_alias) != eshkol::PointerProvenance::Unknown)
            return fail("shared Function declaration authenticated extern alias");
        f.context.setPointerProvenance(raw_alias, eshkol::PointerProvenance::Foreign);
        f.tagged.packPtr(raw_alias, ESHKOL_VALUE_FOREIGN_PTR);
        if (f.context.hasFatalCodegenError()) return fail("foreign alias rejected");
        auto* null_value = f.tagged.packHeapPtr(
            llvm::ConstantPointerNull::get(f.context.ptrType()));
        auto* null_type = llvm::dyn_cast<llvm::ConstantInt>(
            f.builder.CreateExtractValue(null_value, {0}));
        auto* null_flags = llvm::dyn_cast<llvm::ConstantInt>(
            f.builder.CreateExtractValue(null_value, {1}));
        auto* null_payload = llvm::dyn_cast<llvm::ConstantInt>(
            f.builder.CreateExtractValue(null_value, {4}));
        if (!null_type || !null_type->isZero() || !null_flags ||
            !null_flags->isZero() || !null_payload || !null_payload->isZero())
            return fail("null was not canonical");
        f.builder.CreateRetVoid();
        if (llvm::verifyFunction(*f.function)) return fail("positive IR invalid");
    }
    {
        Fixture f;
        f.tagged.packCallable(f.cons());
        if (!f.context.hasFatalCodegenError()) return fail("wrong managed kind accepted");
    }
    {
        Fixture f;
        auto* owned = f.cons();
        auto* foreign = f.builder.CreateCall(
            f.memory.getArenaAllocateConsWithHeader(), {f.arena()});
        f.context.setPointerProvenance(foreign, eshkol::PointerProvenance::Foreign);
        auto* choice = f.function->getArg(0);
        auto* mixed = f.builder.CreateSelect(choice, owned, foreign);
        if (f.context.pointerProvenance(mixed) != eshkol::PointerProvenance::Unknown)
            return fail("mixed select retained managed authority");
        f.tagged.packHeapPtr(mixed);
        if (!f.context.hasFatalCodegenError()) return fail("mixed provenance accepted");
    }
    {
        Fixture f;
        f.tagged.packPtrWithFlags(f.cons(), f.function->getArg(1),
            llvm::ConstantInt::get(f.context.int8Type(), 0));
        if (!f.context.hasFatalCodegenError()) return fail("dynamic type recreated from raw pointer");
    }
    {
        Fixture f;
        f.tagged.packInt64WithType(f.cons(), ESHKOL_VALUE_HANDLE);
        if (!f.context.hasFatalCodegenError())
            return fail("resource tag fabricated from managed pointer");
    }
    for (auto type : {ESHKOL_VALUE_SYMBOL, ESHKOL_VALUE_DUAL_NUMBER,
                      ESHKOL_VALUE_COMPLEX, ESHKOL_VALUE_HANDLE}) {
        Fixture f;
        f.tagged.packInt64WithType(
            llvm::ConstantInt::get(f.context.int64Type(), 7), type);
        if (!f.context.hasFatalCodegenError())
            return fail("pointer or resource tag fabricated from scalar bits");
    }
    {
        Fixture f;
        auto* pointer_bits = f.builder.CreatePtrToInt(f.cons(), f.context.int64Type());
        f.tagged.packInt64WithType(pointer_bits, ESHKOL_VALUE_LOGIC_VAR);
        if (!f.context.hasFatalCodegenError())
            return fail("pointer bits fabricated as immediate id");
    }
    {
        Fixture f;
        auto* bits = f.builder.CreatePtrToInt(f.cons(), f.context.int64Type());
        auto* selected = f.builder.CreateSelect(f.function->getArg(0), bits,
            llvm::ConstantInt::get(f.context.int64Type(), 7));
        if (!f.context.rawIntegerMayContainPointerBits(selected))
            return fail("ptr-to-i64 select lost pointer taint");
        f.tagged.packInt64(selected);
        if (!f.context.hasFatalCodegenError())
            return fail("ptr-to-i64 select packed as numeric");
    }
    {
        Fixture f;
        auto* bits = f.builder.CreatePtrToInt(f.cons(), f.context.int64Type());
        auto* left = llvm::BasicBlock::Create(f.llvm, "left", f.function);
        auto* right = llvm::BasicBlock::Create(f.llvm, "right", f.function);
        auto* joined = llvm::BasicBlock::Create(f.llvm, "joined", f.function);
        f.builder.CreateCondBr(f.function->getArg(0), left, right);
        f.builder.SetInsertPoint(left);
        f.builder.CreateBr(joined);
        f.builder.SetInsertPoint(right);
        f.builder.CreateBr(joined);
        f.builder.SetInsertPoint(joined);
        auto* phi = f.builder.CreatePHI(f.context.int64Type(), 2);
        phi->addIncoming(bits, left);
        phi->addIncoming(llvm::ConstantInt::get(f.context.int64Type(), 7), right);
        if (!f.context.rawIntegerMayContainPointerBits(phi))
            return fail("ptr-to-i64 PHI lost pointer taint");
        f.tagged.packInt64(phi);
        if (!f.context.hasFatalCodegenError())
            return fail("ptr-to-i64 PHI packed as numeric");
    }
    {
        Fixture f;
        auto* slot = f.builder.CreateAlloca(f.context.int64Type());
        f.builder.CreateStore(llvm::ConstantInt::get(f.context.int64Type(), 7), slot);
        auto* loaded = f.builder.CreateLoad(f.context.int64Type(), slot);
        if (!f.context.rawIntegerMayContainPointerBits(loaded))
            return fail("unclassified i64 load gained numeric authority");
        f.tagged.packInt64(loaded);
        if (!f.context.hasFatalCodegenError())
            return fail("unclassified i64 load packed as numeric");
    }
    {
        Fixture f;
        auto* unknown = llvm::Function::Create(
            llvm::FunctionType::get(f.context.int64Type(), false),
            llvm::GlobalValue::ExternalLinkage, "unknown_integer_or_address", f.module);
        auto* called = f.builder.CreateCall(unknown);
        if (!f.context.rawIntegerMayContainPointerBits(called))
            return fail("unclassified i64 call gained numeric authority");
        f.tagged.packInt64(called);
        if (!f.context.hasFatalCodegenError())
            return fail("unclassified i64 call packed as numeric");
    }
    {
        Fixture f;
        f.tagged.packInt64WithTypeAndFlags(
            llvm::ConstantInt::get(f.context.int64Type(), 7),
            f.function->getArg(1), llvm::ConstantInt::get(f.context.int8Type(), 0));
        if (!f.context.hasFatalCodegenError())
            return fail("dynamic type byte admitted without full tagged carrier");
    }
    {
        Fixture f;
        if (f.memory.reviewedHeaderedReturn(f.memory.getArenaAllocateWithHeader()) !=
                eshkol::HeaderedReturnKind::None ||
            f.memory.reviewedHeaderedReturn(f.memory.getArenaAllocate()) !=
                eshkol::HeaderedReturnKind::None)
            return fail("generic/headerless allocation gained managed authority");
        f.context.createReviewedHeaderedCall(f.memory.getArenaAllocateWithHeader(),
            {f.arena(), llvm::ConstantInt::get(f.context.sizeType(), 16),
             llvm::ConstantInt::get(f.context.int8Type(), HEAP_SUBTYPE_VECTOR),
             llvm::ConstantInt::get(f.context.int8Type(), 0)});
        if (!f.context.hasFatalCodegenError())
            return fail("generic dynamic-subtype constructor admitted without descriptor");
    }
    {
        Fixture f;
        auto* owned = f.context.createReviewedGenericHeapCall(
            f.memory.getArenaAllocateWithHeader(),
            {f.arena(), llvm::ConstantInt::get(f.context.sizeType(), 16),
             llvm::ConstantInt::get(f.context.int8Type(), HEAP_SUBTYPE_VECTOR),
             llvm::ConstantInt::get(f.context.int8Type(), 0)}, "owned_vector");
        if (!llvm::isa<llvm::CallInst>(owned) ||
            f.context.pointerProvenance(owned) != eshkol::PointerProvenance::ManagedHeap ||
            f.context.hasFatalCodegenError())
            return fail("reviewed generic allocation lacked heap authority");
        auto* call = llvm::cast<llvm::CallInst>(owned);
        if (!call->getMetadata("eshkol.arena_alloc.checked") ||
            !llvm::isa<llvm::BranchInst>(call->getParent()->getTerminator()) ||
            f.builder.GetInsertBlock() == call->getParent())
            return fail("generic allocation lacks a pre-payload null transfer");
        f.tagged.packHeapPtr(owned);
        if (f.context.hasFatalCodegenError())
            return fail("reviewed generic allocation rejected by packer");
        f.builder.CreateRetVoid();
        if (llvm::verifyFunction(*f.function))
            return fail("reviewed generic allocation guard IR invalid");
    }
    {
        Fixture f;
        f.context.createReviewedGenericHeapCall(
            f.memory.getArenaAllocateWithHeader(),
            {f.arena(), llvm::ConstantInt::get(f.context.sizeType(), 16),
             f.function->getArg(1),
             llvm::ConstantInt::get(f.context.int8Type(), 0)});
        if (!f.context.hasFatalCodegenError())
            return fail("dynamic generic subtype admitted as managed");
    }
    {
        Fixture f;
        f.context.createReviewedGenericHeapCall(
            f.memory.getArenaAllocateWithHeader(),
            {f.arena(), llvm::ConstantInt::get(f.context.sizeType(), 16),
             llvm::ConstantInt::get(f.context.int8Type(), HEAP_SUBTYPE_VECTOR),
             llvm::ConstantInt::get(f.context.int8Type(), 1)});
        if (!f.context.hasFatalCodegenError())
            return fail("unreviewed generic header flags admitted");
    }
    std::puts("PASS: compiler-owned constructor and foreign alias provenance");
    return 0;
}
