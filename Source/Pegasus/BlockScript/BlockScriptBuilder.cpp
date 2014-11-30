/****************************************************************************************/
/*                                                                                      */
/*                                       Pegasus                                        */
/*                                                                                      */
/****************************************************************************************/

//! \file   BlockScriptBuilder.cpp
//! \author Kleber Garcia
//! \date   28th August 2014
//! \brief  Blockscript abstract syntax tree builder, also type checker. 
//!         returns respective runtime ready structures to run the script.

#include "Pegasus/BlockScript/BlockScriptBuilder.h"
#include "Pegasus/BlockScript/BlockScriptAst.h"
#include "Pegasus/BlockScript/IBlockScriptCompilerListener.h"
#include "Pegasus/BlockScript/BsIntrinsics.h"
#include "Pegasus/BlockScript/FunCallback.h"
#include "Pegasus/BlockScript/TypeTable.h"
#include "Pegasus/BlockScript/bs.parser.hpp"
#include "Pegasus/BlockScript/IddStrPool.h"
#include "Pegasus/Allocator/IAllocator.h"
#include "Pegasus/Allocator/Alloc.h"
#include "Pegasus/Utils/String.h"
#include "Pegasus/Utils/Memcpy.h"
#include "Pegasus/Utils/Memset.h"
#include "Pegasus/Core/Assertion.h"

#define BS_NEW PG_NEW(&mAllocator, -1, "BlockScript::Ast", Pegasus::Alloc::PG_MEM_TEMP)
#define STRING_PAGE_SIZE 512

using namespace Pegasus::BlockScript;
using namespace Pegasus::BlockScript::Ast;
using namespace Pegasus::Utils;


//declaration of unique type iid for expression optimization

const int Idd::sType     = 0;
const int Binop::sType   = 1;
const int FunCall::sType = 2;
const int Imm::sType     = 3;
const int StrImm::sType  = 4;
const int Unop::sType    = 5;

//function defined in parser generator, bs.y
extern void BS_ErrorDispatcher(BlockScriptBuilder* builder, const char* message);

void BlockScriptBuilder::Initialize(Pegasus::Alloc::IAllocator* allocator)
{
    mGeneralAllocator = allocator;
    mAllocator.Initialize(STRING_PAGE_SIZE, allocator);
    mCanonizer.Initialize(allocator);
    mStrPool.Initialize(allocator);
    mSymbolTable.Initialize(allocator);
    Reset();
}

void BlockScriptBuilder::BeginBuild()
{
    PG_ASSERTSTR(
        mErrorCount == 0 &&
        mCurrentLineNumber == 1 &&
        mActiveResult.mAst == nullptr &&
        mActiveResult.mAsm.mBlocks == nullptr &&
        mInFunBody == false,
        "Reset() must be called prior to compiling on BlockScriptBuilder!"
    );
    if (mEventListener != nullptr)
    {
        mEventListener->OnCompilationBegin();
    }
}

void BlockScriptBuilder::EndBuild(BlockScriptBuilder::CompilationResult& result)
{
    //check that all functions have a body
    if (mErrorCount == 0)
    {
        FunTable* funTable = mSymbolTable.GetRootFunTable();
        int funSize = funTable->GetSize();
        for (int f = 0; f < funSize; ++f)
        {
            FunDesc* funDesc = funTable->GetDesc(f);
            if (funDesc->GetDec()->GetStmtList() == nullptr && !funDesc->IsCallback())
            {
                ++mErrorCount;
                BS_ErrorDispatcher(this, "Missing function body.");
                break;
            }
        }
    }

    if (mErrorCount == 0)
    {

        //build of AST is done, lets canonize now (canonization process should not error out)
        mCanonizer.Canonize(
            mActiveResult.mAst,
            &mSymbolTable
        );

        mActiveResult.mAsm = mCanonizer.GetAssembly();
    }
    else
    {
        mActiveResult.mAsm.mBlocks = nullptr;
    }

    if (mEventListener != nullptr)
    {
        //TODO: would be nice to pass down stats of the compilation here (such as assembly count,
        //      compilation time and so on
        mEventListener->OnCompilationEnd(mErrorCount == 0);
    }
    
    result = mActiveResult;
}

void BlockScriptBuilder::Reset()
{
    //initialize compilation state variables
    mErrorCount = 0;
    mCurrentLineNumber = 1;
    mActiveResult.mAst = nullptr;
    mActiveResult.mAsm.mBlocks = nullptr;
    mInFunBody = false;

    //Reset and initialize symbol containers
    mAllocator.Reset();


    mSymbolTable.Reset();
    mCurrentFrame = mSymbolTable.CreateFrame();
    mCurrentFrame->SetCreatorCategory(StackFrameInfo::GLOBAL);

    mCanonizer.Reset();

    mStrPool.Clear();
}

bool BlockScriptBuilder::StartNewFunction()
{
    if (mInFunBody)
    {
        return false;
    } 
    else
    {
        mInFunBody = true;
        StartNewFrame();
    }
    return true;
}

StackFrameInfo* BlockScriptBuilder::StartNewFrame()
{
    StackFrameInfo* newFrame = mSymbolTable.CreateFrame();
    newFrame->SetParentStackFrame(mCurrentFrame);
    mCurrentFrame = newFrame;
    return newFrame;
}

void BlockScriptBuilder::PopFrame()
{
    //pop to the previous frame
    mCurrentFrame = mCurrentFrame->GetParentStackFrame();
}

void BlockScriptBuilder::BindIntrinsic(Ast::StmtFunDec* funDec, FunCallback callback)
{
    PG_ASSERT(mInFunBody);
    FunDesc* desc = funDec->GetDesc();
    desc->SetCallback(callback);

    PopFrame();
    mInFunBody = false;
}

Program* BlockScriptBuilder::CreateProgram()
{
    PG_ASSERTSTR(mActiveResult.mAst == nullptr, "ensure that BeginBuild() and EndBuild() are called properly");
    mActiveResult.mAst = BS_NEW Program();
	return mActiveResult.mAst;
}

ExpList* BlockScriptBuilder::CreateExpList()
{
    return BS_NEW ExpList;
}

ArgList* BlockScriptBuilder::CreateArgList()
{
    return BS_NEW ArgList;
}

StmtList* BlockScriptBuilder::CreateStmtList()
{
    return BS_NEW StmtList;
}

const TypeDesc* BlockScriptBuilder::GetTypeByName(const char* name) const
{
    return mSymbolTable.GetTypeByName(name);
}

int BlockScriptBuilder::RegisterStackMember(const char* name, const TypeDesc* type)
{
    PG_ASSERT(type != nullptr);
    int offset = mCurrentFrame->Allocate(name, type, true /*is an Arg*/);
    return offset;
}


bool BlockScriptBuilder::IsBinopValid(const TypeDesc* type, int op)
{
    if (type->GetAluEngine() == TypeDesc::E_INT)
    {
        switch(op)
        {
        case O_PLUS:
        case O_MINUS:
        case O_MUL:
        case O_DIV:
        case O_MOD:
        case O_EQ:
        case O_LT:
        case O_GT:
        case O_LTE:
        case O_GTE: 
        case O_LAND:
        case O_LOR:
            return true;
        }
    }
    else if (type->GetAluEngine() == TypeDesc::E_FLOAT)
    {
        switch(op)
        {
        case O_PLUS:
        case O_MINUS:
        case O_MUL:
        case O_DIV:
        case O_EQ:
        case O_LT:
        case O_GT:
        case O_LTE:
        case O_GTE: 
        case O_LAND:
        case O_LOR:
            return true;
        }
    }
    else
    {
        PG_ASSERTSTR(type->GetAluEngine() >= TypeDesc::E_FLOAT && type->GetAluEngine() <= TypeDesc::E_FLOAT4, "unrecognized alu engine. Please add correct one!");
        switch(op)
        {
        case O_PLUS:
        case O_MINUS:
        case O_MUL:
        case O_DIV:
            return true;
        }
    }
    return false;
}

Exp* BlockScriptBuilder::AttemptTypePromotion(Exp* exp, const TypeDesc* targetType)
{
    const TypeDesc* currType = exp->GetTypeDesc();

    if (currType->GetAluEngine() == TypeDesc::E_INT)
    {
        if (targetType->GetAluEngine() >= TypeDesc::E_FLOAT &&
            targetType->GetAluEngine() <= TypeDesc::E_FLOAT4 
           )
        {
            Unop* unop = BS_NEW Unop(O_IMPLICIT_CAST, exp);
            unop->SetTypeDesc(targetType);
            return unop;
        }
    }
    else if (currType->GetAluEngine() == TypeDesc::E_FLOAT)
    {
        if (targetType->GetAluEngine() <= TypeDesc::E_FLOAT4 && targetType->GetAluEngine() > currType->GetAluEngine())
        {
            Unop* unop = BS_NEW Unop(O_IMPLICIT_CAST, exp);
            unop->SetTypeDesc(targetType);
            return unop;
        }
    }

    return exp;
}


Exp* BlockScriptBuilder::BuildBinopArrayAccess (Ast::Exp* lhs, int op, Ast::Exp* rhs)
{
    PG_ASSERT(op == O_ACCESS);
    //determine if lhs is a typed element
    if (lhs->GetExpType() == Idd::sType)
    {
        Idd* lhsIdd = static_cast<Idd*>(lhs);
        const TypeDesc* arrayType = GetTypeByName(lhsIdd->GetName());
        if (arrayType != nullptr)
        {
            int arrayCount = 0;
            if (rhs->GetTypeDesc() == nullptr || rhs->GetExpType() != Imm::sType || rhs->GetTypeDesc()->GetAluEngine() != TypeDesc::E_INT)
            {
                BS_ErrorDispatcher(this, "Array constructor size type must be a constant int");
                return nullptr;
            }
            else
            {
                arrayCount = static_cast<Imm*>(rhs)->GetVariant().i[0];
            }

            arrayType = mSymbolTable.CreateType(
                TypeDesc::M_ARRAY,
                lhsIdd->GetName(),
                arrayType,
                arrayCount
            );

            lhsIdd->SetOffset(-1);
            lhsIdd->SetFrameOffset(-1);

            if (rhs->GetExpType() != Imm::sType)
            {
                BS_ErrorDispatcher(this, "Array rhs must be a numeric constant value.");
                return nullptr;
            }
    
            //promote this node to an array constructor
            Binop* newBinop = BS_NEW Binop(lhs, O_ARRAY_CONSTRUCTOR, rhs);
            newBinop->SetTypeDesc(arrayType);
            return newBinop;
        }
    }
    else if ( lhs->GetExpType() == Binop::sType && static_cast<Binop*>(lhs)->GetOp() == O_ARRAY_CONSTRUCTOR)
    {
        int arrayCount = 0;
        if (rhs->GetTypeDesc() == nullptr || rhs->GetExpType() != Imm::sType || rhs->GetTypeDesc()->GetAluEngine() != TypeDesc::E_INT)
        {
            BS_ErrorDispatcher(this, "Array constructor size type must be a constant int");
            return nullptr;
        }
        else
        {
            arrayCount = static_cast<Imm*>(rhs)->GetVariant().i[0];
        }

        //recursively create the array constructor
        const TypeDesc* childType = lhs->GetTypeDesc();
        TypeDesc* newArrayType = mSymbolTable.CreateType(
            TypeDesc::M_ARRAY,
            childType->GetName(),
            childType,
            arrayCount
        );

        //promote this node to an array constructor
        Binop* newBinop = BS_NEW Binop(lhs, O_ARRAY_CONSTRUCTOR, rhs);
        newBinop->SetTypeDesc(newArrayType);
        return newBinop;
    }

    //if we made it here, means this is not a constructor, is just an array accessor
    const TypeDesc* tid1 = lhs->GetTypeDesc();
    if (tid1 == nullptr)
    {
        BS_ErrorDispatcher(this, "Undefined array.");
        return nullptr;
    }

    if (tid1->GetModifier() != TypeDesc::M_ARRAY)
    {
        BS_ErrorDispatcher(this, "Access [] operator only allowed on array type.");
        return nullptr;
    }

    const TypeDesc* tid2 = rhs->GetTypeDesc();
    if (tid2 == nullptr)
    {
        BS_ErrorDispatcher(this, "Undefined index symbol for array lookup.");
        return nullptr;
    }

    if (tid2->GetAluEngine() != TypeDesc::E_INT)
    {
        BS_ErrorDispatcher(this, "Array index must be an integer.");
        return nullptr;
    }

    Binop* newBinop = BS_NEW Binop(lhs, op, rhs);
    newBinop->SetTypeDesc(tid1->GetChild());
    return newBinop;
}

Exp* BlockScriptBuilder::BuildBinop (Ast::Exp* lhs, int op, Ast::Exp* rhs)
{
    PG_ASSERT(lhs != nullptr);
    PG_ASSERT(rhs != nullptr);

    const TypeDesc* tid1 = lhs->GetTypeDesc();
    const TypeDesc* tid2 = rhs != nullptr ? rhs->GetTypeDesc() : nullptr;

    Binop* output = nullptr;

    if(op == O_SET)
    {
        if (tid2 == nullptr)
        {
            BS_ErrorDispatcher(this, "Undefined type on = operator.");
            return nullptr;
        }
        else
        {
            if (lhs->GetExpType() == Idd::sType)
            {
                Idd* idd = static_cast<Idd*>(lhs);
                if (idd->GetOffset() == -1)
                {
                    PG_ASSERT(tid1 == nullptr);
                    //register into stack
                    idd->SetTypeDesc(tid2);
					tid1 = tid2;
                    //TODO: simplify this
                    int offset = mCurrentFrame->Allocate(idd->GetName(), tid2);
                    idd->SetOffset(offset);
                    idd->SetFrameOffset(0);
                }
                else
                {
                    rhs = AttemptTypePromotion(rhs, tid1);
                    tid2 = rhs->GetTypeDesc();
                    PG_ASSERT(tid2 != nullptr);
                }

                output = BS_NEW Binop(lhs, op, rhs);

                if (tid1 != tid2)
                {
                    BS_ErrorDispatcher(this, "Incompatible types not allowed on operation.");
                    return nullptr;
                }
                else
                {
                    output->SetTypeDesc(tid1);
                }
            }
            else if (lhs->GetExpType() == Binop::sType && (static_cast<Binop*>(lhs)->GetOp() == O_DOT || static_cast<Binop*>(lhs)->GetOp() == O_ACCESS))
            {
                if (tid1 == nullptr || tid2 == nullptr)
                {
                    BS_ErrorDispatcher(this, "undefined member on = operator.");
                    return nullptr;
                }
    
                rhs = AttemptTypePromotion(rhs, tid1);
                tid2 = rhs->GetTypeDesc();

                if (tid1 != tid2)
                {
                    BS_ErrorDispatcher(this, "incompatible types on = operator.");
                    return nullptr;
                }

                output = BS_NEW Binop(lhs, op, rhs);
                output->SetTypeDesc(tid1);

                return output;

            }
            else
            {
                BS_ErrorDispatcher(this, "lhs can only be an accessor operation.");
                return nullptr;
            }
        }
    }
    else if (op == O_DOT)
    {
        PG_ASSERT(rhs->GetExpType() == Idd::sType);
        Idd* accessOffset = static_cast<Idd*>(rhs);
        accessOffset->SetFrameOffset(0);//no weird frames
        accessOffset->SetIsGlobal(false);//not a global
        if (tid1 == nullptr)
        {
            BS_ErrorDispatcher(this, "undefined lhs of dot operator");
            return nullptr;
        }
        
        if (tid1->GetModifier() == TypeDesc::M_STRUCT)
        {
            //find the offset of this type.
            const StmtStructDef* structDef = tid1->GetStructDef();
            PG_ASSERT(structDef != nullptr);
            
            ArgList* argList = structDef->GetArgList();
            
            tid2 = nullptr;
            int offset = 0;
            while (argList != nullptr)
            {
                if (!Pegasus::Utils::Strcmp(argList->GetArgDec()->GetVar(), accessOffset->GetName()))
                {
                    tid2 = argList->GetArgDec()->GetType();
                    PG_ASSERT(tid2 != nullptr);
                    break;
                }
                offset += argList->GetArgDec()->GetType()->GetByteSize();
                argList = argList->GetTail();
            }

            accessOffset->SetOffset(offset);

            if (tid2 == nullptr)
            {
                BS_ErrorDispatcher(this, "Member of struct not found.");
            }

            output = BS_NEW Binop(lhs, op, rhs);
            output->SetTypeDesc(tid2);
            return output;
        }
        else if (tid1->GetModifier() == TypeDesc::M_VECTOR)
        {
            if (lhs->GetExpType() == Binop::sType &&
                static_cast<Binop*>(lhs)->GetOp() == O_DOT)
            {
                BS_ErrorDispatcher(this, "Nested swizzle access not allowed.");
                return nullptr;
            }

            const char* swizzleName = accessOffset->GetName();
            int swizzleLen = Utils::Strlen(swizzleName);
            if (swizzleLen > 4)
            {
                BS_ErrorDispatcher(this, "Vector swizzle too long");
                return nullptr;
            }

            int dimensionality = tid1->GetModifier() == TypeDesc::M_SCALAR ? 1 : tid1->GetModifierProperty();

            int maxChar = -1;
            for (int i = 0; i < swizzleLen; ++i)
            {
                if (swizzleName[i] < 'w' || swizzleName[i] > 'z')
                {
                    BS_ErrorDispatcher(this, "vector access can only be 'x', 'y', 'z', 'w'");
                    return nullptr;
                }

                int currElement = (swizzleName[i] - 'w' + 3) % 4;
                maxChar = currElement > maxChar ? currElement : maxChar;
            }

            if (maxChar >= dimensionality)
            {
                BS_ErrorDispatcher(this, "Invalid subscript, make sure the vector's dimensions are accessed correctly.");
                return nullptr;
            }

            char* newName = mStrPool.AllocateString();
            newName[0] = '\0';
            Utils::Strcat(newName, tid1->GetChild()->GetName());
            if (swizzleLen >= 2)
            {
                char str[2] = {(swizzleLen + '0'), '\0'};
                Utils::Strcat(newName, str);
            }

            const TypeDesc* newType = mSymbolTable.GetTypeByName(newName);

            Binop* newBinop = BS_NEW Binop(
                lhs, op, rhs);
            newBinop->SetTypeDesc(newType);
            return newBinop;

        }
        else
        {
            BS_ErrorDispatcher(this, "dot operator can only be executed in structs types");
            return nullptr;
        }
    }
    else if (op == O_ACCESS)
    {
        return BuildBinopArrayAccess(lhs, op, rhs);
    }
    else
    {
        if (tid1 == nullptr || tid2  == nullptr)
        {
            BS_ErrorDispatcher(this, "undefined symbol.");
            return nullptr;
        }

        lhs = AttemptTypePromotion(lhs, tid2); 
        tid1 = lhs->GetTypeDesc();

        rhs = AttemptTypePromotion(rhs, tid1); 
        tid2 = rhs->GetTypeDesc();

        if (tid1 != tid2)
        {
            BS_ErrorDispatcher(this, "Incompatible types not allowed on operation.");
            return nullptr;
        }

        if (!IsBinopValid(tid1, op))
        {
            BS_ErrorDispatcher(this, "Arithmetic operation is invalid for type.");
            return nullptr;
        }

        output = BS_NEW Binop(lhs, op, rhs);
        output->SetTypeDesc(tid1);
        
    }
	return output;
}

Exp* BlockScriptBuilder::BuildUnop(int op, Exp* exp)
{
    Exp* unop = BS_NEW Unop(op, exp);
    unop->SetTypeDesc(exp->GetTypeDesc());
    return unop;
}

Exp* BlockScriptBuilder::BuildExplicitCast(Exp* exp, const char* type)
{
    const TypeDesc* targetType = GetTypeByName(type);
    
    if (targetType != nullptr)
    {
        Unop* unop = BS_NEW Unop(O_EXPLICIT_CAST, exp);        
        unop->SetTypeDesc(targetType);
        return unop;
    }
    
    BS_ErrorDispatcher(this, "Unknown type cast requested.");
    return nullptr;
}

Exp*   BlockScriptBuilder::BuildImmInt   (int i)
{
    Ast::Variant v;
    v.i[0] = i;
    Imm* imm = BS_NEW Imm(v);
    const TypeDesc* typeDesc = GetTypeByName("int");
    PG_ASSERT(typeDesc != nullptr);
    imm->SetTypeDesc(typeDesc);
    return imm;
}

Exp*  BlockScriptBuilder::BuildStrImm(const char* strToCopy)
{

    int len = Utils::Strlen(strToCopy);
    if (len >= mAllocator.GetPageSize())
    {
        BS_ErrorDispatcher(this, "String immediate value too big.");
        return nullptr;
    } 

    char* destStr = static_cast<char*>(mAllocator.Alloc(len + 1, Pegasus::Alloc::PG_MEM_PERM));
    
    if (destStr == nullptr)
    {
        BS_ErrorDispatcher(this, "internal compiler error while allocting imm string");
        return nullptr;
    }

    destStr[0] = '\0';
    Utils::Strcat(destStr, strToCopy);

    const TypeDesc* strTypeDesc = GetTypeByName("string");
    PG_ASSERT(strTypeDesc != nullptr);

    StrImm* strImm = BS_NEW StrImm(destStr);
    strImm->SetTypeDesc(strTypeDesc);

    return strImm;
}

Exp*   BlockScriptBuilder::BuildImmFloat   (float f)
{
    Ast::Variant v;
    v.f[0] = f;
    Imm* imm = BS_NEW Imm(v);
    const TypeDesc* typeDesc = GetTypeByName("float");
    PG_ASSERT(typeDesc != nullptr);
    imm->SetTypeDesc(typeDesc);
    return imm;
}

Idd*   BlockScriptBuilder::BuildIdd   (const char * name)
{
    //create the idd
    Idd* idd = BS_NEW Idd(name);

    //find type
    StackFrameInfo* currentFrame = mCurrentFrame; 
    int frameOffset = 0;
    while (currentFrame != nullptr)
    {
        StackFrameInfo* info = currentFrame;
        StackFrameInfo::Entry* found = info->FindDeclaration(name);
        if (found != nullptr) {
            idd->SetOffset(found->mOffset);
            idd->SetFrameOffset(frameOffset);
            idd->SetTypeDesc(found->mType);
            idd->SetIsGlobal(currentFrame == 0);
            break;
        }
        currentFrame = info->GetParentStackFrame();
        frameOffset++;
    }


    return idd;
}

FunDesc* BlockScriptBuilder::FindFunctionDescription(FunCall* fcSignature)
{
    return mSymbolTable.FindFunctionDescription(fcSignature);
}

Exp* BlockScriptBuilder::BuildFunCall(ExpList* args, const char* name)
{
    PG_ASSERT(args != nullptr);
    PG_ASSERT(name != nullptr);

    //check that all args have a type. otherwise bail.
    ExpList* tail = args;
    while(tail != nullptr && tail->GetExp() != nullptr)
    {
        if (tail->GetExp()->GetTypeDesc() == nullptr)
        {
            BS_ErrorDispatcher(this, "undefined parameter passed to function call.");
            return nullptr;
        }
        tail = tail->GetTail();
    }
    
    FunCall* fc = BS_NEW FunCall(args, name);
    FunDesc* desc = FindFunctionDescription(fc);
    if (desc != nullptr)
    {
        fc->SetDesc(desc);
		const TypeDesc* type = desc->GetDec()->GetReturnType();
        fc->SetTypeDesc(type);
        return fc;
    }
    else
    {
        BS_ErrorDispatcher(this, "No function declaration found.");
        return nullptr;
    }
}

StmtExp* BlockScriptBuilder::BuildStmtExp(Exp* exp)
{
    PG_ASSERT(exp != nullptr);
    if (exp->GetExpType() == Binop::sType)
    {
        Binop* binop = static_cast<Binop*>(exp);
        if (binop->GetOp() != O_SET)
        {
            BS_ErrorDispatcher(this, "Empty expressions not allowed! value must be stored in a variable");
            return nullptr;
        }
    }
    else if (exp->GetExpType() != FunCall::sType)
    {
        BS_ErrorDispatcher(this, "Empty expressions not allowed! expression must be a function call, did you forget passing parameters ?");
        return nullptr;
    }
    return BS_NEW StmtExp(exp);
}

StmtReturn* BlockScriptBuilder::BuildStmtReturn(Exp* exp)
{
    PG_ASSERT(exp != nullptr);
    if (mInFunBody == false)
    {
        BS_ErrorDispatcher(this, "return statements not allowed on global scope.");
        return nullptr;
    }
    return BS_NEW StmtReturn(exp);
}

StmtTreeModifier* BlockScriptBuilder::BuildStmtTreeModifier(ExpList* expList, Idd* var)
{
    PG_ASSERT(expList != nullptr);
    PG_ASSERT(var     != nullptr);
    return BS_NEW StmtTreeModifier(expList, var);
}

FunDesc* BlockScriptBuilder::RegisterFunctionDeclaration(Ast::StmtFunDec* funDec)
{
    return mSymbolTable.CreateFunctionDescription(funDec);
}

StmtFunDec* BlockScriptBuilder::BuildStmtFunDec(Ast::ArgList* argList, const char * returnIdd, const char * nameIdd)
{
    PG_ASSERT(returnIdd != nullptr);
    PG_ASSERT(nameIdd != nullptr);


    //first, find the return type
    const TypeDesc* retType = GetTypeByName(returnIdd);
    if (retType == nullptr)
    {
        BS_ErrorDispatcher(this, "Unknown return type.");
        return nullptr;
    }

    StmtFunDec* funDec = BS_NEW StmtFunDec(argList, returnIdd, nameIdd);

    funDec->SetReturnType(retType);

    // record the frame for this function
    funDec->SetFrame(mCurrentFrame);

    mCurrentFrame->SetCreatorCategory(StackFrameInfo::FUN_BODY);

    
    PG_ASSERT(mCurrentFrame != nullptr);

    //register in the function table
    FunDesc* description = RegisterFunctionDeclaration(funDec);

    if (description == nullptr)
    {
        BS_ErrorDispatcher(this, "Duplicate function declaration found.");
        return nullptr;
    }

    funDec->SetDesc(description);

    
    return funDec;
}

StmtFunDec* BlockScriptBuilder::BindFunImplementation(StmtFunDec* funDec, StmtList* stmts)
{
    PG_ASSERT (funDec != nullptr);

    funDec->SetStmtList(stmts);

    //pop to the previous frame
    mInFunBody = false;


    PopFrame();

    return funDec;
}

StmtWhile* BlockScriptBuilder::BuildStmtWhile(Exp* exp, StmtList* stmtList)
{
    PG_ASSERT(exp != nullptr);

    StmtWhile* stmtWhile = BS_NEW StmtWhile(exp, stmtList);

    stmtWhile->SetFrame(mCurrentFrame);

    mCurrentFrame->SetCreatorCategory(StackFrameInfo::LOOP);

    //pop to the previous frame
    PopFrame();

    return stmtWhile; 
}

StmtIfElse* BlockScriptBuilder::BuildStmtIfElse(Exp* exp, StmtList* ifBlock, StmtIfElse* tail, StackFrameInfo* frame)
{
    StmtIfElse* stmtIfElse = BS_NEW StmtIfElse(exp, ifBlock, tail, frame);

    mCurrentFrame->SetCreatorCategory(StackFrameInfo::IF_STMT);

    return stmtIfElse;
}

static void StructGenericConstructor(FunCallbackContext& ctx)
{
    void* argin = ctx.GetRawInputBuffer();
    void* argout = ctx.GetRawOutputBuffer();
    int arginByteSize = ctx.GetInputBufferSize();
    int argoutByteSize = ctx.GetOutputBufferSize();
    if (arginByteSize != 0)
    {
        PG_ASSERT(arginByteSize == argoutByteSize);
        Pegasus::Utils::Memcpy(argout, argin, argoutByteSize);
    }
    else
    {
        Pegasus::Utils::Memset8(argout, 0, argoutByteSize);
    }
}

StmtStructDef* BlockScriptBuilder::BuildStmtStructDef(const char* name, ArgList* definitions)
{
    PG_ASSERT(definitions != nullptr);

    //first make sure that there is no struct redefinition.
    if (GetTypeByName(name) != nullptr)
    {
        BS_ErrorDispatcher(this, "Struct type is trying to redefine a type.");
        return nullptr;
    }

    if (mInFunBody)
    {
        BS_ErrorDispatcher(this, "Struct definitions not allowed inside function bodies.");
        return nullptr;
    }
    
    StmtStructDef* newDef = BS_NEW StmtStructDef(name, definitions);

    StackFrameInfo* frameInfo = mCurrentFrame;

    newDef->SetFrameInfo(frameInfo);

    frameInfo->SetCreatorCategory(StackFrameInfo::STRUCT_DEF);

    //pop to the previous frame
    PopFrame();

    // unlink this to any parent, preventing searches on parent frames. this is now an orphan stack frame
    frameInfo->UnlinkParentStackFrame();

    TypeDesc* newStructType = mSymbolTable.CreateType(
        TypeDesc::M_STRUCT,
        name,
        nullptr, // no child type
        0, //no modifier property
        TypeDesc::E_NONE, //no alu engine
        newDef //register this types structural definition AST member
    );

    //Create empty constructor
    bool result = CreateIntrinsicFunction(
        this,
        name,
        nullptr, //no argins types
        nullptr, //no argins names
        0, //no argcounts
        name,
        StructGenericConstructor
    );
    PG_ASSERT(result);

    
    static const int MAX_CHILD_MEMBERS = 255;

    static const char* sMassiveCharTypeContainer[MAX_CHILD_MEMBERS];
    static const char* sMassiveCharNameContainer[MAX_CHILD_MEMBERS];

    int count = 0;
    ArgList* argList = definitions;
    while (argList != nullptr)
    {
        if (count >= MAX_CHILD_MEMBERS)
        {
            BS_ErrorDispatcher(this, "Too many members in structure");
            return nullptr;
        }

        sMassiveCharNameContainer[count] = argList->GetArgDec()->GetVar();
        sMassiveCharTypeContainer[count] = argList->GetArgDec()->GetType()->GetName();
        ++count;
        argList = argList->GetTail();
    }

    //Create constructor
    result = CreateIntrinsicFunction(
        this,
        name,
        sMassiveCharTypeContainer, //no argins types
        sMassiveCharNameContainer, //no argins names
        count, //no argcounts
        name,
        StructGenericConstructor
    );
    PG_ASSERT(result);

    //copy all the declaration info
    

    if (newStructType == nullptr)
    {
        BS_ErrorDispatcher(this, "An error occured creating the type for this struct.");
        return nullptr;
    }
    
    return newDef;
    
}

ArgDec*  BlockScriptBuilder::BuildArgDec(const char* var, const TypeDesc* type)
{
    PG_ASSERT(var != nullptr);
    PG_ASSERT(type != nullptr);

    StackFrameInfo::Entry* found = mCurrentFrame->FindDeclaration(var);
    if (found != nullptr)
    {
        BS_ErrorDispatcher(this, "Duplicate name of declaration\n");
        return nullptr;
    }

    int offset = RegisterStackMember(var, type);
    
    ArgDec* argDec = BS_NEW ArgDec(var, type);
    argDec->SetOffset(offset);
    
    return argDec;
}


