/*
**  LLVM code generated drawers
**  Copyright (c) 2016 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "precomp.h"
#include "timestamp.h"
#include "fixedfunction/drawspancodegen.h"
#include "fixedfunction/drawwallcodegen.h"
#include "fixedfunction/drawcolumncodegen.h"
#include "fixedfunction/drawskycodegen.h"
#include "fixedfunction/drawtrianglecodegen.h"
#include "ssa/ssa_function.h"
#include "ssa/ssa_scope.h"
#include "ssa/ssa_for_block.h"
#include "ssa/ssa_if_block.h"
#include "ssa/ssa_stack.h"
#include "ssa/ssa_function.h"
#include "ssa/ssa_struct_type.h"
#include "ssa/ssa_value.h"
#include "ssa/ssa_barycentric_weight.h"
#include <iostream>

class Exception : public std::exception
{
public:
	Exception(const std::string &message) : message(message) { }
	const char *what() const noexcept override { return message.c_str(); }

private:
	std::string message;
};

class LLVMProgram
{
public:
	LLVMProgram();

	void CreateModule();
	std::vector<uint8_t> GenerateObjectFile(const std::string &triple, const std::string &cpuName, const std::string &features);
	std::string DumpModule();

	llvm::LLVMContext &context() { return *mContext; }
	llvm::Module *module() { return mModule.get(); }

private:
	llvm::TargetMachine *machine = nullptr;
	std::unique_ptr<llvm::LLVMContext> mContext;
	std::unique_ptr<llvm::Module> mModule;
};

class LLVMDrawers
{
public:
	LLVMDrawers(const std::string &triple, const std::string &cpuName, const std::string &features, const std::string namePostfix);

	std::vector<uint8_t> ObjectFile;

private:
	void CodegenDrawColumn(const char *name, DrawColumnVariant variant, DrawColumnMethod method);
	void CodegenDrawSpan(const char *name, DrawSpanVariant variant);
	void CodegenDrawWall(const char *name, DrawWallVariant variant, int columns);
	void CodegenDrawSky(const char *name, DrawSkyVariant variant, int columns);
	void CodegenDrawTriangle(const std::string &name, TriDrawVariant variant, TriBlendMode blendmode, bool truecolor);

	static llvm::Type *GetDrawColumnArgsStruct(llvm::LLVMContext &context);
	static llvm::Type *GetDrawSpanArgsStruct(llvm::LLVMContext &context);
	static llvm::Type *GetDrawWallArgsStruct(llvm::LLVMContext &context);
	static llvm::Type *GetDrawSkyArgsStruct(llvm::LLVMContext &context);
	static llvm::Type *GetWorkerThreadDataStruct(llvm::LLVMContext &context);
	static llvm::Type *GetTriVertexStruct(llvm::LLVMContext &context);
	static llvm::Type *GetTriMatrixStruct(llvm::LLVMContext &context);
	static llvm::Type *GetTriUniformsStruct(llvm::LLVMContext &context);
	static llvm::Type *GetTriDrawTriangleArgs(llvm::LLVMContext &context);

	LLVMProgram mProgram;
	std::string mNamePostfix;
};

/////////////////////////////////////////////////////////////////////////////

LLVMDrawers::LLVMDrawers(const std::string &triple, const std::string &cpuName, const std::string &features, const std::string namePostfix) : mNamePostfix(namePostfix)
{
	mProgram.CreateModule();

	CodegenDrawColumn("FillColumn", DrawColumnVariant::Fill, DrawColumnMethod::Normal);
	CodegenDrawColumn("FillColumnAdd", DrawColumnVariant::FillAdd, DrawColumnMethod::Normal);
	CodegenDrawColumn("FillColumnAddClamp", DrawColumnVariant::FillAddClamp, DrawColumnMethod::Normal);
	CodegenDrawColumn("FillColumnSubClamp", DrawColumnVariant::FillSubClamp, DrawColumnMethod::Normal);
	CodegenDrawColumn("FillColumnRevSubClamp", DrawColumnVariant::FillRevSubClamp, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumn", DrawColumnVariant::Draw, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnAdd", DrawColumnVariant::DrawAdd, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnShaded", DrawColumnVariant::DrawShaded, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnAddClamp", DrawColumnVariant::DrawAddClamp, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnSubClamp", DrawColumnVariant::DrawSubClamp, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnRevSubClamp", DrawColumnVariant::DrawRevSubClamp, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnTranslated", DrawColumnVariant::DrawTranslated, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnTlatedAdd", DrawColumnVariant::DrawTlatedAdd, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnAddClampTranslated", DrawColumnVariant::DrawAddClampTranslated, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnSubClampTranslated", DrawColumnVariant::DrawSubClampTranslated, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnRevSubClampTranslated", DrawColumnVariant::DrawRevSubClampTranslated, DrawColumnMethod::Normal);
	CodegenDrawColumn("DrawColumnRt1", DrawColumnVariant::Draw, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1Copy", DrawColumnVariant::DrawCopy, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1Add", DrawColumnVariant::DrawAdd, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1Shaded", DrawColumnVariant::DrawShaded, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1AddClamp", DrawColumnVariant::DrawAddClamp, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1SubClamp", DrawColumnVariant::DrawSubClamp, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1RevSubClamp", DrawColumnVariant::DrawRevSubClamp, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1Translated", DrawColumnVariant::DrawTranslated, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1TlatedAdd", DrawColumnVariant::DrawTlatedAdd, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1AddClampTranslated", DrawColumnVariant::DrawAddClampTranslated, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1SubClampTranslated", DrawColumnVariant::DrawSubClampTranslated, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt1RevSubClampTranslated", DrawColumnVariant::DrawRevSubClampTranslated, DrawColumnMethod::Rt1);
	CodegenDrawColumn("DrawColumnRt4", DrawColumnVariant::Draw, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4Copy", DrawColumnVariant::DrawCopy, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4Add", DrawColumnVariant::DrawAdd, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4Shaded", DrawColumnVariant::DrawShaded, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4AddClamp", DrawColumnVariant::DrawAddClamp, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4SubClamp", DrawColumnVariant::DrawSubClamp, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4RevSubClamp", DrawColumnVariant::DrawRevSubClamp, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4Translated", DrawColumnVariant::DrawTranslated, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4TlatedAdd", DrawColumnVariant::DrawTlatedAdd, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4AddClampTranslated", DrawColumnVariant::DrawAddClampTranslated, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4SubClampTranslated", DrawColumnVariant::DrawSubClampTranslated, DrawColumnMethod::Rt4);
	CodegenDrawColumn("DrawColumnRt4RevSubClampTranslated", DrawColumnVariant::DrawRevSubClampTranslated, DrawColumnMethod::Rt4);
	CodegenDrawSpan("DrawSpan", DrawSpanVariant::Opaque);
	CodegenDrawSpan("DrawSpanMasked", DrawSpanVariant::Masked);
	CodegenDrawSpan("DrawSpanTranslucent", DrawSpanVariant::Translucent);
	CodegenDrawSpan("DrawSpanMaskedTranslucent", DrawSpanVariant::MaskedTranslucent);
	CodegenDrawSpan("DrawSpanAddClamp", DrawSpanVariant::AddClamp);
	CodegenDrawSpan("DrawSpanMaskedAddClamp", DrawSpanVariant::MaskedAddClamp);
	CodegenDrawWall("vlinec1", DrawWallVariant::Opaque, 1);
	CodegenDrawWall("vlinec4", DrawWallVariant::Opaque, 4);
	CodegenDrawWall("mvlinec1", DrawWallVariant::Masked, 1);
	CodegenDrawWall("mvlinec4", DrawWallVariant::Masked, 4);
	CodegenDrawWall("tmvline1_add", DrawWallVariant::Add, 1);
	CodegenDrawWall("tmvline4_add", DrawWallVariant::Add, 4);
	CodegenDrawWall("tmvline1_addclamp", DrawWallVariant::AddClamp, 1);
	CodegenDrawWall("tmvline4_addclamp", DrawWallVariant::AddClamp, 4);
	CodegenDrawWall("tmvline1_subclamp", DrawWallVariant::SubClamp, 1);
	CodegenDrawWall("tmvline4_subclamp", DrawWallVariant::SubClamp, 4);
	CodegenDrawWall("tmvline1_revsubclamp", DrawWallVariant::RevSubClamp, 1);
	CodegenDrawWall("tmvline4_revsubclamp", DrawWallVariant::RevSubClamp, 4);
	CodegenDrawSky("DrawSky1", DrawSkyVariant::Single, 1);
	CodegenDrawSky("DrawSky4", DrawSkyVariant::Single, 4);
	CodegenDrawSky("DrawDoubleSky1", DrawSkyVariant::Double, 1);
	CodegenDrawSky("DrawDoubleSky4", DrawSkyVariant::Double, 4);
	for (int i = 0; i < NumTriBlendModes(); i++)
	{
		CodegenDrawTriangle("TriDrawNormal8_" + std::to_string(i), TriDrawVariant::DrawNormal, (TriBlendMode)i, false);
		CodegenDrawTriangle("TriDrawNormal32_" + std::to_string(i), TriDrawVariant::DrawNormal, (TriBlendMode)i, true);
		CodegenDrawTriangle("TriFillNormal8_" + std::to_string(i), TriDrawVariant::FillNormal, (TriBlendMode)i, false);
		CodegenDrawTriangle("TriFillNormal32_" + std::to_string(i), TriDrawVariant::FillNormal, (TriBlendMode)i, true);
		CodegenDrawTriangle("TriDrawSubsector8_" + std::to_string(i), TriDrawVariant::DrawSubsector, (TriBlendMode)i, false);
		CodegenDrawTriangle("TriDrawSubsector32_" + std::to_string(i), TriDrawVariant::DrawSubsector, (TriBlendMode)i, true);
		CodegenDrawTriangle("TriFillSubsector8_" + std::to_string(i), TriDrawVariant::FillSubsector, (TriBlendMode)i, false);
		CodegenDrawTriangle("TriFillSubsector32_" + std::to_string(i), TriDrawVariant::FillSubsector, (TriBlendMode)i, true);
	}
	CodegenDrawTriangle("TriStencil", TriDrawVariant::Stencil, TriBlendMode::Copy, false);
	CodegenDrawTriangle("TriStencilClose", TriDrawVariant::StencilClose, TriBlendMode::Copy, false);

	ObjectFile = mProgram.GenerateObjectFile(triple, cpuName, features);
}

void LLVMDrawers::CodegenDrawColumn(const char *name, DrawColumnVariant variant, DrawColumnMethod method)
{
	llvm::IRBuilder<> builder(mProgram.context());
	SSAScope ssa_scope(&mProgram.context(), mProgram.module(), &builder);

	SSAFunction function(name + mNamePostfix);
	function.add_parameter(GetDrawColumnArgsStruct(mProgram.context()));
	function.add_parameter(GetWorkerThreadDataStruct(mProgram.context()));
	function.create_public();

	DrawColumnCodegen codegen;
	codegen.Generate(variant, method, function.parameter(0), function.parameter(1));

	builder.CreateRetVoid();

	if (llvm::verifyFunction(*function.func))
		throw Exception("verifyFunction failed for CodegenDrawColumn()");
}

void LLVMDrawers::CodegenDrawSpan(const char *name, DrawSpanVariant variant)
{
	llvm::IRBuilder<> builder(mProgram.context());
	SSAScope ssa_scope(&mProgram.context(), mProgram.module(), &builder);

	SSAFunction function(name + mNamePostfix);
	function.add_parameter(GetDrawSpanArgsStruct(mProgram.context()));
	function.create_public();

	DrawSpanCodegen codegen;
	codegen.Generate(variant, function.parameter(0));

	builder.CreateRetVoid();

	if (llvm::verifyFunction(*function.func))
		throw Exception("verifyFunction failed for CodegenDrawSpan()");
}

void LLVMDrawers::CodegenDrawWall(const char *name, DrawWallVariant variant, int columns)
{
	llvm::IRBuilder<> builder(mProgram.context());
	SSAScope ssa_scope(&mProgram.context(), mProgram.module(), &builder);

	SSAFunction function(name + mNamePostfix);
	function.add_parameter(GetDrawWallArgsStruct(mProgram.context()));
	function.add_parameter(GetWorkerThreadDataStruct(mProgram.context()));
	function.create_public();

	DrawWallCodegen codegen;
	codegen.Generate(variant, columns == 4, function.parameter(0), function.parameter(1));

	builder.CreateRetVoid();

	if (llvm::verifyFunction(*function.func))
		throw Exception("verifyFunction failed for CodegenDrawWall()");
}

void LLVMDrawers::CodegenDrawSky(const char *name, DrawSkyVariant variant, int columns)
{
	llvm::IRBuilder<> builder(mProgram.context());
	SSAScope ssa_scope(&mProgram.context(), mProgram.module(), &builder);

	SSAFunction function(name + mNamePostfix);
	function.add_parameter(GetDrawSkyArgsStruct(mProgram.context()));
	function.add_parameter(GetWorkerThreadDataStruct(mProgram.context()));
	function.create_public();

	DrawSkyCodegen codegen;
	codegen.Generate(variant, columns == 4, function.parameter(0), function.parameter(1));

	builder.CreateRetVoid();

	if (llvm::verifyFunction(*function.func))
		throw Exception("verifyFunction failed for CodegenDrawSky()");
}

void LLVMDrawers::CodegenDrawTriangle(const std::string &name, TriDrawVariant variant, TriBlendMode blendmode, bool truecolor)
{
	llvm::IRBuilder<> builder(mProgram.context());
	SSAScope ssa_scope(&mProgram.context(), mProgram.module(), &builder);

	SSAFunction function(name + mNamePostfix);
	function.add_parameter(GetTriDrawTriangleArgs(mProgram.context()));
	function.add_parameter(GetWorkerThreadDataStruct(mProgram.context()));
	function.create_public();

	DrawTriangleCodegen codegen;
	codegen.Generate(variant, blendmode, truecolor, function.parameter(0), function.parameter(1));

	builder.CreateRetVoid();

	if (llvm::verifyFunction(*function.func))
		throw Exception(std::string("verifyFunction failed for CodegenDrawTriangle(") + std::to_string((int)variant) + ", " + std::to_string((int)blendmode) + ", " + std::to_string((int)truecolor) + ")");
}

llvm::Type *LLVMDrawers::GetDrawColumnArgsStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // uint32_t *dest;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // const uint32_t *source;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // const uint32_t *source2;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // uint8_t *colormap;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // uint8_t *translation;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // const uint32_t *basecolors;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t pitch;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t count;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t dest_y;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t iscale;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t texturefracx;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t textureheight;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t texturefrac;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t light;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t color;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t srccolor;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t srcalpha;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t destalpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t desaturate;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t flags;
	return llvm::StructType::create(context, elements, "DrawColumnArgs", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetDrawSpanArgsStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // uint8_t *destorg;
	elements.push_back(llvm::Type::getInt8PtrTy(context)); // const uint32_t *source;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t destpitch;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t xfrac;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t yfrac;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t xstep;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t ystep;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t x1;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t x2;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t y;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t xbits;
	elements.push_back(llvm::Type::getInt32Ty(context)); // int32_t ybits;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t light;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t srcalpha;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t destalpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t desaturate;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t flags;
	return llvm::StructType::create(context, elements, "DrawSpanArgs", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetDrawWallArgsStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	elements.push_back(llvm::Type::getInt8PtrTy(context));
	for (int i = 0; i < 8; i++)
		elements.push_back(llvm::Type::getInt8PtrTy(context));
	for (int i = 0; i < 25; i++)
		elements.push_back(llvm::Type::getInt32Ty(context));
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t desaturate;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t flags;
	return llvm::StructType::create(context, elements, "DrawWallArgs", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetDrawSkyArgsStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	elements.push_back(llvm::Type::getInt8PtrTy(context));
	for (int i = 0; i < 8; i++)
		elements.push_back(llvm::Type::getInt8PtrTy(context));
	for (int i = 0; i < 15; i++)
		elements.push_back(llvm::Type::getInt32Ty(context));
	return llvm::StructType::create(context, elements, "DrawSkyArgs", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetWorkerThreadDataStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	for (int i = 0; i < 4; i++)
		elements.push_back(llvm::Type::getInt32Ty(context));
	elements.push_back(llvm::Type::getInt8PtrTy(context));
	return llvm::StructType::create(context, elements, "ThreadData", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetTriVertexStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	for (int i = 0; i < 4 + TriVertex::NumVarying; i++)
		elements.push_back(llvm::Type::getFloatTy(context));
	return llvm::StructType::create(context, elements, "TriVertex", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetTriMatrixStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	for (int i = 0; i < 4 * 4; i++)
		elements.push_back(llvm::Type::getFloatTy(context));
	return llvm::StructType::create(context, elements, "TriMatrix", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetTriUniformsStruct(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t light;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t subsectorDepth;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t color;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t srcalpha;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t destalpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t light_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_alpha;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_red;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_green;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t fade_blue;
	elements.push_back(llvm::Type::getInt16Ty(context)); // uint16_t desaturate;
	elements.push_back(llvm::Type::getInt32Ty(context)); // uint32_t flags;
	elements.push_back(GetTriMatrixStruct(context));     // TriMatrix objectToClip
	return llvm::StructType::create(context, elements, "TriUniforms", false)->getPointerTo();
}

llvm::Type *LLVMDrawers::GetTriDrawTriangleArgs(llvm::LLVMContext &context)
{
	std::vector<llvm::Type *> elements;
	elements.push_back(llvm::Type::getInt8PtrTy(context));  // uint8_t *dest;
	elements.push_back(llvm::Type::getInt32Ty(context));    // int32_t pitch;
	elements.push_back(GetTriVertexStruct(context)); // TriVertex *v1;
	elements.push_back(GetTriVertexStruct(context)); // TriVertex *v2;
	elements.push_back(GetTriVertexStruct(context)); // TriVertex *v3;
	elements.push_back(llvm::Type::getInt32Ty(context));    // int32_t clipleft;
	elements.push_back(llvm::Type::getInt32Ty(context));    // int32_t clipright;
	elements.push_back(llvm::Type::getInt32Ty(context));    // int32_t cliptop;
	elements.push_back(llvm::Type::getInt32Ty(context));    // int32_t clipbottom;
	elements.push_back(llvm::Type::getInt8PtrTy(context));  // const uint8_t *texturePixels;
	elements.push_back(llvm::Type::getInt32Ty(context));    // uint32_t textureWidth;
	elements.push_back(llvm::Type::getInt32Ty(context));    // uint32_t textureHeight;
	elements.push_back(llvm::Type::getInt8PtrTy(context));  // const uint8_t *translation;
	elements.push_back(GetTriUniformsStruct(context)); // const TriUniforms *uniforms;
	elements.push_back(llvm::Type::getInt8PtrTy(context));  // uint8_t *stencilValues;
	elements.push_back(llvm::Type::getInt32PtrTy(context)); // uint32_t *stencilMasks;
	elements.push_back(llvm::Type::getInt32Ty(context));    // int32_t stencilPitch;
	elements.push_back(llvm::Type::getInt8Ty(context));     // uint8_t stencilTestValue;
	elements.push_back(llvm::Type::getInt8Ty(context));     // uint8_t stencilWriteValue;
	elements.push_back(llvm::Type::getInt32PtrTy(context)); // uint32_t *subsectorGBuffer;
	elements.push_back(llvm::Type::getInt8PtrTy(context));  // const uint8_t *colormaps;
	elements.push_back(llvm::Type::getInt8PtrTy(context));  // const uint8_t *RGB32k;
	elements.push_back(llvm::Type::getInt8PtrTy(context));  // const uint8_t *BaseColors;
	return llvm::StructType::create(context, elements, "TriDrawTriangle", false)->getPointerTo();
}

/////////////////////////////////////////////////////////////////////////////

LLVMProgram::LLVMProgram()
{
	mContext = std::make_unique<llvm::LLVMContext>();
}

void LLVMProgram::CreateModule()
{
	mModule = std::make_unique<llvm::Module>("render", context());
}

std::vector<uint8_t> LLVMProgram::GenerateObjectFile(const std::string &triple, const std::string &cpuName, const std::string &features)
{
	using namespace llvm;

	std::string errorstring;

	llvm::Module *module = mModule.get();

	const Target *target = TargetRegistry::lookupTarget(triple, errorstring);
	
#if LLVM_VERSION_MAJOR < 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 9)
	Reloc::Model relocationModel = Reloc::PIC_;
#else
	Optional<Reloc::Model> relocationModel = Reloc::PIC_;
#endif
	
	CodeModel::Model codeModel = CodeModel::Model::Default;

	TargetOptions options;
	options.LessPreciseFPMADOption = true;
	options.AllowFPOpFusion = FPOpFusion::Fast;
	options.UnsafeFPMath = true;
	options.NoInfsFPMath = true;
	options.NoNaNsFPMath = true;
	options.HonorSignDependentRoundingFPMathOption = false;
	options.NoZerosInBSS = false;
	options.GuaranteedTailCallOpt = false;
	options.StackAlignmentOverride = 0;
	options.UseInitArray = true;
	options.DataSections = false;
	options.FunctionSections = false;
	options.JTType = JumpTable::Single; // Create a single table for all jumptable functions
	options.ThreadModel = ThreadModel::POSIX;
	options.DisableIntegratedAS = false;
	options.MCOptions.SanitizeAddress = false;
	options.MCOptions.MCRelaxAll = false; // relax all fixups in the emitted object file
	options.MCOptions.DwarfVersion = 0;
	options.MCOptions.ShowMCInst = false;
	options.MCOptions.ABIName = "";
	options.MCOptions.MCFatalWarnings = false;
	options.MCOptions.ShowMCEncoding = false; // Show encoding in .s output
	options.MCOptions.MCUseDwarfDirectory = false;
	options.MCOptions.AsmVerbose = true;

#if LLVM_VERSION_MAJOR > 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR >= 9)
	options.Reciprocals = TargetRecip({ "all" });
	options.StackSymbolOrdering = true;
	options.UniqueSectionNames = true;
	options.EmulatedTLS = false;
	options.ExceptionModel = ExceptionHandling::None;
	options.EABIVersion = EABI::Default;
	options.DebuggerTuning = DebuggerKind::Default;
	options.MCOptions.MCIncrementalLinkerCompatible = false;
	options.MCOptions.MCNoWarn = false;
	options.MCOptions.PreserveAsmComments = true;
#endif

	CodeGenOpt::Level optimizationLevel = CodeGenOpt::Aggressive;
	machine = target->createTargetMachine(triple, cpuName, features, options, relocationModel, codeModel, optimizationLevel);


#if LLVM_VERSION_MAJOR < 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 8)
	std::string targetTriple = machine->getTargetTriple();
#else
	std::string targetTriple = machine->getTargetTriple().getTriple();
#endif

	module->setTargetTriple(targetTriple);
#if LLVM_VERSION_MAJOR < 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 8)
	module->setDataLayout(new DataLayout(*machine->getSubtargetImpl()->getDataLayout()));
#else
	module->setDataLayout(machine->createDataLayout());
#endif

	legacy::FunctionPassManager PerFunctionPasses(module);
	legacy::PassManager PerModulePasses;

#if LLVM_VERSION_MAJOR > 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR >= 8)
	PerFunctionPasses.add(createTargetTransformInfoWrapperPass(machine->getTargetIRAnalysis()));
	PerModulePasses.add(createTargetTransformInfoWrapperPass(machine->getTargetIRAnalysis()));
#endif

	SmallString<16 * 1024> str;
#if LLVM_VERSION_MAJOR < 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 8)
	raw_svector_ostream vecstream(str);
	formatted_raw_ostream stream(vecstream);
#else
	raw_svector_ostream stream(str);
#endif
	machine->addPassesToEmitFile(PerModulePasses, stream, TargetMachine::CGFT_ObjectFile);

	PassManagerBuilder passManagerBuilder;
	passManagerBuilder.OptLevel = 3;
	passManagerBuilder.SizeLevel = 0;
	passManagerBuilder.Inliner = createFunctionInliningPass();
	passManagerBuilder.SLPVectorize = true;
	passManagerBuilder.LoopVectorize = true;
	passManagerBuilder.LoadCombine = true;
	passManagerBuilder.populateModulePassManager(PerModulePasses);
	passManagerBuilder.populateFunctionPassManager(PerFunctionPasses);

	// Run function passes:
	PerFunctionPasses.doInitialization();
	for (llvm::Function &func : *module)
	{
		if (!func.isDeclaration())
			PerFunctionPasses.run(func);
	}
	PerFunctionPasses.doFinalization();

	// Run module passes:
	PerModulePasses.run(*module);

	// Return the resulting object file
#if LLVM_VERSION_MAJOR < 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 8)
	stream.flush();
	vecstream.flush();
#endif
	std::vector<uint8_t> data;
	data.resize(str.size());
	memcpy(data.data(), str.data(), data.size());
	return data;
}

std::string LLVMProgram::DumpModule()
{
	std::string str;
	llvm::raw_string_ostream stream(str);
#if LLVM_VERSION_MAJOR < 3 || (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR < 8)
	mModule->print(stream, nullptr);
#else
	mModule->print(stream, nullptr, false, true);
#endif
	return stream.str();
}

/////////////////////////////////////////////////////////////////////////////

std::string &AllTimestamps()
{
	static std::string timestamps;
	return timestamps;
}

void AddSourceFileTimestamp(const char *timestamp)
{
	if (!AllTimestamps().empty()) AllTimestamps().push_back(' ');
	AllTimestamps() += timestamp;
}

int main(int argc, char **argv)
{
	try
	{
		if (argc != 2)
		{
			std::cerr << "Usage: " << argv[0] << "<output filename>" << std::endl;
			return 1;
		}

		std::string timestamp_filename = argv[1] + std::string(".timestamp");

		FILE *file = fopen(timestamp_filename.c_str(), "rb");
		if (file != nullptr)
		{
			char buffer[4096];
			int bytes_read = fread(buffer, 1, 4096, file);
			fclose(file);
			std::string last_timestamp;
			if (bytes_read > 0)
				last_timestamp = std::string(buffer, bytes_read);

			if (AllTimestamps() == last_timestamp)
			{
				std::cout << "Not recompiling drawers because the object file is already up to date." << std::endl;
				exit(0);
			}
		}

		llvm::install_fatal_error_handler([](void *user_data, const std::string& reason, bool gen_crash_diag)
		{
			std::cerr << "LLVM fatal error: " << reason;
			exit(1);
		});

		llvm::InitializeNativeTarget();
		llvm::InitializeNativeTargetAsmPrinter();

		std::string triple = llvm::sys::getDefaultTargetTriple();
		
#ifdef __APPLE__
		// Target triple is x86_64-apple-darwin15.6.0
		auto pos = triple.find("-apple-darwin");
		if (pos != std::string::npos)
		{
			triple = triple.substr(0, pos) + "-apple-darwin10.11.0";
		}
#endif
		
		std::cout << "Target triple is " << triple << std::endl;

		std::string cpuName = "pentium4";
		std::string features;
		std::cout << "Compiling drawer code for " << cpuName << ".." << std::endl;

		LLVMDrawers drawersSSE2(triple, cpuName, features, "_SSE2");

		file = fopen(argv[1], "wb");
		if (file == nullptr)
		{
			std::cerr << "Unable to open " << argv[1] << " for writing." << std::endl;
			return 1;
		}

		int result = fwrite(drawersSSE2.ObjectFile.data(), drawersSSE2.ObjectFile.size(), 1, file);
		fclose(file);

		if (result != 1)
		{
			std::cerr << "Could not write data to " << argv[1] << std::endl;
			return 1;
		}

		file = fopen(timestamp_filename.c_str(), "wb");
		if (file == nullptr)
		{
			std::cerr << "Could not create timestamp file" << std::endl;
			return 1;
		}
		result = fwrite(AllTimestamps().data(), AllTimestamps().length(), 1, file);
		fclose(file);
		if (result != 1)
		{
			std::cerr << "Could not write timestamp file" << std::endl;
			return 1;
		}

		//LLVMDrawers drawersSSE4("core2");
		//LLVMDrawers drawersAVX("sandybridge");
		//LLVMDrawers drawersAVX2("haswell");

		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
		return 1;
	}
}