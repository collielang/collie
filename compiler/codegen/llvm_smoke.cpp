/**
 * @file llvm_smoke.cpp
 * @brief LLVM 集成冒烟验证（t48b）：用 C++ API 构造 hello world 模块并打印 IR。
 *
 * 验证点：
 *   1. LLVM 头文件可被 include（LLVM_INCLUDE_DIRS 生效）；
 *   2. LLVMCore/LLVMSupport 静态库可链接（IRBuilder/verifyModule 符号解析）；
 *   3. 生成的模块通过 verifyModule 校验，IR 输出到 stdout。
 *
 * 生成的 IR 等价于：
 *   declare i32 @puts(ptr)
 *   define i32 @main() {
 *     %1 = call i32 @puts(ptr @.str)
 *     ret i32 0
 *   }
 */
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/raw_ostream.h>

int main() {
    llvm::LLVMContext context;
    auto module = std::make_unique<llvm::Module>("collie_smoke", context);
    llvm::IRBuilder<> builder(context);

    // declare i32 @puts(ptr)
    auto* puts_type = llvm::FunctionType::get(
        builder.getInt32Ty(), {llvm::PointerType::getUnqual(context)},
        /*isVarArg=*/false);
    llvm::FunctionCallee puts_fn = module->getOrInsertFunction("puts", puts_type);

    // define i32 @main()
    auto* main_type = llvm::FunctionType::get(builder.getInt32Ty(), /*isVarArg=*/false);
    auto* main_fn = llvm::Function::Create(
        main_type, llvm::Function::ExternalLinkage, "main", module.get());
    builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", main_fn));

    // call puts("Hello, Collie x LLVM!"); ret 0
    llvm::Value* message = builder.CreateGlobalString("Hello, Collie x LLVM!");
    builder.CreateCall(puts_fn, {message});
    builder.CreateRet(builder.getInt32(0));

    // 校验模块合法性（true 表示有错误）
    if (llvm::verifyModule(*module, &llvm::errs())) {
        llvm::errs() << "[llvm_smoke] module verification FAILED\n";
        return 1;
    }

    llvm::outs() << "[llvm_smoke] LLVM " << LLVM_VERSION_STRING
                 << " OK, verified module IR:\n\n";
    module->print(llvm::outs(), nullptr);
    return 0;
}
