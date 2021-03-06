
#include "jitintern.h"

/////////////////////////////////////////////////////////////////////////////
// Load constants.

void JitCompiler::EmitLI()
{
	cc.mov(regD[A], BCs);
}

void JitCompiler::EmitLK()
{
	cc.mov(regD[A], konstd[BC]);
}

void JitCompiler::EmitLKF()
{
	auto base = newTempIntPtr();
	cc.mov(base, asmjit::imm_ptr(konstf + BC));
	cc.movsd(regF[A], asmjit::x86::qword_ptr(base));
}

void JitCompiler::EmitLKS()
{
	auto call = CreateCall<void, FString*, FString*>(&JitCompiler::CallAssignString);
	call->setArg(0, regS[A]);
	call->setArg(1, asmjit::imm_ptr(konsts + BC));
}

void JitCompiler::EmitLKP()
{
	cc.mov(regA[A], (int64_t)konsta[BC].v);
}

void JitCompiler::EmitLK_R()
{
	auto base = newTempIntPtr();
	cc.mov(base, asmjit::imm_ptr(konstd + C));
	cc.mov(regD[A], asmjit::x86::ptr(base, regD[B], 2));
}

void JitCompiler::EmitLKF_R()
{
	auto base = newTempIntPtr();
	cc.mov(base, asmjit::imm_ptr(konstf + C));
	cc.movsd(regF[A], asmjit::x86::qword_ptr(base, regD[B], 3));
}

void JitCompiler::EmitLKS_R()
{
	auto base = newTempIntPtr();
	cc.mov(base, asmjit::imm_ptr(konsts + C));
	auto ptr = newTempIntPtr();
	if (cc.is64Bit())
		cc.lea(ptr, asmjit::x86::ptr(base, regD[B], 3));
	else
		cc.lea(ptr, asmjit::x86::ptr(base, regD[B], 2));
	auto call = CreateCall<void, FString*, FString*>(&JitCompiler::CallAssignString);
	call->setArg(0, regS[A]);
	call->setArg(1, ptr);
}

void JitCompiler::EmitLKP_R()
{
	auto base = newTempIntPtr();
	cc.mov(base, asmjit::imm_ptr(konsta + C));
	if (cc.is64Bit())
		cc.mov(regA[A], asmjit::x86::ptr(base, regD[B], 3));
	else
		cc.mov(regA[A], asmjit::x86::ptr(base, regD[B], 2));
}

void JitCompiler::EmitLFP()
{
	cc.lea(regA[A], asmjit::x86::ptr(vmframe, offsetExtra));
}

static uint8_t *GetClassMeta(DObject *o)
{
	return o->GetClass()->Meta;
}

void JitCompiler::EmitMETA()
{
	auto label = EmitThrowExceptionLabel(X_READ_NIL);
	cc.test(regA[B], regA[B]);
	cc.je(label);

	auto result = newResultIntPtr();
	auto call = CreateCall<uint8_t*, DObject*>(GetClassMeta);
	call->setRet(0, result);
	call->setArg(0, regA[B]);
	cc.mov(regA[A], result);
}

static PClass *GetClass(DObject *o)
{
	return o->GetClass();
}

void JitCompiler::EmitCLSS()
{
	auto label = EmitThrowExceptionLabel(X_READ_NIL);
	cc.test(regA[B], regA[B]);
	cc.je(label);

	auto result = newResultIntPtr();
	auto call = CreateCall<PClass*, DObject*>(GetClass);
	call->setRet(0, result);
	call->setArg(0, regA[B]);
	cc.mov(regA[A], result);
}

/////////////////////////////////////////////////////////////////////////////
// Load from memory. rA = *(rB + rkC)

void JitCompiler::EmitLB()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movsx(regD[A], asmjit::x86::byte_ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLB_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movsx(regD[A], asmjit::x86::byte_ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLH()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movsx(regD[A], asmjit::x86::word_ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLH_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movsx(regD[A], asmjit::x86::word_ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLW()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.mov(regD[A], asmjit::x86::dword_ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLW_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.mov(regD[A], asmjit::x86::dword_ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLBU()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movzx(regD[A], asmjit::x86::byte_ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLBU_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movzx(regD[A].r8Lo(), asmjit::x86::byte_ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLHU()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movzx(regD[A].r16(), asmjit::x86::word_ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLHU_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movzx(regD[A].r16(), asmjit::x86::word_ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLSP()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.xorpd(regF[A], regF[A]);
	cc.cvtss2sd(regF[A], asmjit::x86::dword_ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLSP_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.xorpd(regF[A], regF[A]);
	cc.cvtss2sd(regF[A], asmjit::x86::dword_ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLDP()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movsd(regF[A], asmjit::x86::qword_ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLDP_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movsd(regF[A], asmjit::x86::qword_ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLS()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto ptr = newTempIntPtr();
	cc.lea(ptr, asmjit::x86::ptr(regA[B], konstd[C]));
	auto call = CreateCall<void, FString*, FString*>(&JitCompiler::CallAssignString);
	call->setArg(0, regS[A]);
	call->setArg(1, ptr);
}

void JitCompiler::EmitLS_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto ptr = newTempIntPtr();
	cc.lea(ptr, asmjit::x86::ptr(regA[B], regD[C]));
	auto call = CreateCall<void, FString*, FString*>(&JitCompiler::CallAssignString);
	call->setArg(0, regS[A]);
	call->setArg(1, ptr);
}

static DObject *ReadBarrier(DObject *p)
{
	return GC::ReadBarrier(p);
}

void JitCompiler::EmitLO()
{
	EmitNullPointerThrow(B, X_READ_NIL);

	auto ptr = newTempIntPtr();
	cc.mov(ptr, asmjit::x86::ptr(regA[B], konstd[C]));

	auto result = newResultIntPtr();
	auto call = CreateCall<DObject*, DObject*>(ReadBarrier);
	call->setRet(0, result);
	call->setArg(0, ptr);
	cc.mov(regA[A], result);
}

void JitCompiler::EmitLO_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);

	auto ptr = newTempIntPtr();
	cc.mov(ptr, asmjit::x86::ptr(regA[B], regD[C]));

	auto result = newResultIntPtr();
	auto call = CreateCall<DObject*, DObject*>(ReadBarrier);
	call->setRet(0, result);
	call->setArg(0, ptr);
	cc.mov(regA[A], result);
}

void JitCompiler::EmitLP()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.mov(regA[A], asmjit::x86::ptr(regA[B], konstd[C]));
}

void JitCompiler::EmitLP_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.mov(regA[A], asmjit::x86::ptr(regA[B], regD[C]));
}

void JitCompiler::EmitLV2()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto tmp = newTempIntPtr();
	cc.lea(tmp, asmjit::x86::qword_ptr(regA[B], konstd[C]));
	cc.movsd(regF[A], asmjit::x86::qword_ptr(tmp));
	cc.movsd(regF[A + 1], asmjit::x86::qword_ptr(tmp, 8));
}

void JitCompiler::EmitLV2_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto tmp = newTempIntPtr();
	cc.lea(tmp, asmjit::x86::qword_ptr(regA[B], regD[C]));
	cc.movsd(regF[A], asmjit::x86::qword_ptr(tmp));
	cc.movsd(regF[A + 1], asmjit::x86::qword_ptr(tmp, 8));
}

void JitCompiler::EmitLV3()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto tmp = newTempIntPtr();
	cc.lea(tmp, asmjit::x86::qword_ptr(regA[B], konstd[C]));
	cc.movsd(regF[A], asmjit::x86::qword_ptr(tmp));
	cc.movsd(regF[A + 1], asmjit::x86::qword_ptr(tmp, 8));
	cc.movsd(regF[A + 2], asmjit::x86::qword_ptr(tmp, 16));
}

void JitCompiler::EmitLV3_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto tmp = newTempIntPtr();
	cc.lea(tmp, asmjit::x86::qword_ptr(regA[B], regD[C]));
	cc.movsd(regF[A], asmjit::x86::qword_ptr(tmp));
	cc.movsd(regF[A + 1], asmjit::x86::qword_ptr(tmp, 8));
	cc.movsd(regF[A + 2], asmjit::x86::qword_ptr(tmp, 16));
}

static void SetString(FString *to, char **from)
{
	*to = *from;
}

void JitCompiler::EmitLCS()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto ptr = newTempIntPtr();
	cc.lea(ptr, asmjit::x86::ptr(regA[B], konstd[C]));
	auto call = CreateCall<void, FString*, char**>(SetString);
	call->setArg(0, regS[A]);
	call->setArg(1, ptr);
}

void JitCompiler::EmitLCS_R()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	auto ptr = newTempIntPtr();
	cc.lea(ptr, asmjit::x86::ptr(regA[B], regD[C]));
	auto call = CreateCall<void, FString*, char**>(SetString);
	call->setArg(0, regS[A]);
	call->setArg(1, ptr);
}

void JitCompiler::EmitLBIT()
{
	EmitNullPointerThrow(B, X_READ_NIL);
	cc.movsx(regD[A], asmjit::x86::byte_ptr(regA[B]));
	cc.and_(regD[A], C);
	cc.cmp(regD[A], 0);
	cc.setne(regD[A]);
}
