#include <fstream>
#include <memory>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <set>
#include <vector>
#include <utility>

#include "llvm-c/Core.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

extern FunctionCallee AssertFT;
extern FunctionCallee AssertCFG;

void BuildExit(Module *M) {
    // make exit declaration
    std::vector<Type*> exit_arg_types;
    exit_arg_types.push_back(Type::getInt32Ty(M->getContext()));
    FunctionType *exit_type = FunctionType::get(Type::getVoidTy(M->getContext()), exit_arg_types, true);
    Function::Create(exit_type, Function::ExternalLinkage, "exit", M);
}

void BuildPrintf(Module *M) {
    // declare i32 @printf(ptr noundef, ...)
    std::vector<Type*> printf_arg_types;
    printf_arg_types.push_back(Type::getInt8PtrTy(M->getContext()));
    printf_arg_types.push_back(Type::getInt32Ty(M->getContext()));
    FunctionType *printf_type = FunctionType::get(Type::getInt32Ty(M->getContext()), printf_arg_types, true);
    Function::Create(printf_type, Function::ExternalLinkage, "printf", M); 
}

FunctionCallee BuildAssertFT(Module *M) {
    LLVMContext &Context = M->getContext();
    
    std::vector<Type*> v;
    v.push_back(IntegerType::get(M->getContext(),32));
    v.push_back(IntegerType::get(M->getContext(),32));

    ArrayRef<Type*> Params(v);
    FunctionType* FunType = FunctionType::get(Type::getVoidTy(Context),Params,false);
    
    FunctionCallee fun = M->getOrInsertFunction("assert_ft",FunType);

    Function *F = cast<Function>(fun.getCallee());
    // add code to function that does the following:
    // two basic blocks
    // 1. if (arg0 == arg1) return;
    // 2. assert(0);
    
    BasicBlock *BB1 = BasicBlock::Create(Context,"entry",F);
    BasicBlock *BB2 = BasicBlock::Create(Context,"fail",F);
    BasicBlock *BB3 = BasicBlock::Create(Context,"exit",F);
    IRBuilder<> Builder(BB1);
    Value *cmp = Builder.CreateICmpNE(F->getArg(0),Builder.getInt32(1));
    Builder.CreateCondBr(cmp,BB2,BB3);
    Builder.SetInsertPoint(BB2);
    // call fprintf to print the error message
    std::vector<Value*> args;
    Value* s = Builder.CreateGlobalStringPtr("**Possible soft-error detected due to data corruption (%d).\n");
    args.push_back(s);
    args.push_back(F->getArg(1));
    Builder.CreateCall(F->getParent()->getFunction("printf"),args,"assertcheck");
    Builder.CreateCall(F->getParent()->getFunction("exit"),Builder.getInt32(1099));
    Builder.CreateBr(BB3);
    Builder.SetInsertPoint(BB3);
    Builder.CreateRetVoid();  
    
    return fun;
}

FunctionCallee BuildAssertCFG(Module *M) {
    LLVMContext &Context = M->getContext();
    
    std::vector<Type*> v;
    v.push_back(IntegerType::get(M->getContext(),32));
    v.push_back(IntegerType::get(M->getContext(),32));  
    v.push_back(IntegerType::get(M->getContext(),32));
    
    ArrayRef<Type*> Params2(v);
    FunctionType* FunType2 = FunctionType::get(Type::getVoidTy(Context),Params2,false);
    FunctionCallee fun = M->getOrInsertFunction("assert_cfg_ft",FunType2);
    Function *F = cast<Function>(fun.getCallee());
    
    BasicBlock *BB1 = BasicBlock::Create(Context,"entry",F);
    BasicBlock *BB2 = BasicBlock::Create(Context,"fail",F);
    BasicBlock *BB3 = BasicBlock::Create(Context,"exit",F);
    IRBuilder<> Builder(BB1);
    Value *cmp = Builder.CreateICmpNE(F->getArg(0),Builder.getInt32(1));
    Builder.CreateCondBr(cmp,BB2,BB3);
    Builder.SetInsertPoint(BB2);
    // call fprintf to print the error message
    std::vector<Value*> args;
    Value* s = Builder.CreateGlobalStringPtr("**Possible soft-error detected due to control flow into block %d (%d). Exiting program.\n");
    args.push_back(s);
    args.push_back(F->getArg(1));
    args.push_back(F->getArg(2));
    Builder.CreateCall(F->getParent()->getFunction("printf"),args,"assertcheck");
    Builder.CreateCall(F->getParent()->getFunction("exit"),Builder.getInt32(1099));
    Builder.CreateBr(BB3);
    Builder.SetInsertPoint(BB3);
    Builder.CreateRetVoid();
    
    return fun;
}

void  BuildHelperFunctions(Module *M) {
    BuildExit(M);
    BuildPrintf(M);

    AssertFT = BuildAssertFT(M);
    AssertCFG = BuildAssertCFG(M);  
}
