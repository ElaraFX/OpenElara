/**************************************************************************
 * Copyright (C) 2017 Rendease Co., Ltd.
 * All rights reserved.
 *
 * This program is commercial software: you must not redistribute it 
 * and/or modify it without written permission from Rendease Co., Ltd.
 *
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * End User License Agreement for more details.
 *
 * You should have received a copy of the End User License Agreement along 
 * with this program.  If not, see <http://www.rendease.com/licensing/>
 *************************************************************************/

#pragma once

#include <fstream>
#include <vector>
#include <string>
#include <ei.h>

class EssWriter
{
private:
	std::locale mPreviousLocale;
	std::ofstream mStream;
	bool mInNode;
	bool mBinartyEncoding;

public:
	EssWriter();
	~EssWriter();
	bool Initialize(const char* filename, const bool encoding);
	void Close();

	void BeginNode(const char* type, const char* name);
	void BeginNode(const char* type, const std::string& name);
	void BeginNameSpace(const char *name);
	void AddParseEss(const char *ess_name);
	void EndNameSpace();
	void LinkParam(const char* input, const std::string& shader, const char* output);
	void AddScaler(const char* name, const float value);
	void AddInt(const char* name, const int value);
	void AddVector4(const char* name, const eiVector4& value);
	void AddVector3(const char* name, const eiVector& value);
	void AddVector2(const char* name, const eiVector2& value);
	void AddToken(const char* name, const std::string& value);
	void AddColor(const char* name, const eiVector4& value);
	void AddColor(const char* name, const eiVector& value);
	void AddBool(const char* name, const bool value);
	void AddRef(const std::string& name, const std::string& ref);
	void AddRefGroup(const char* grouptype, std::vector<std::string>& refelements);
	void AddMatrix(const char* name, const eiMatrix& matrix);
	void AddEnum(const char* name, const char* value);
	void AddRenderCommand(const char* inst_group_name, const char* cam_name, const char* optio_name);
	void AddDeclare();
	void AddDeclare(const char* type, const char* name, const char *storage_class);
	void AddIndexArray(const char* name, const unsigned int* pIndexArray, size_t arraySize, bool faceVarying);
	void AddVectorArray(const char* name, const eiVector* pVectorArray, size_t arraySize, bool faceVarying);
	void AddVector2Array(const char* name, const eiVector2* pVectorArray, size_t arraySize);
	void AddPointArray(const char* name, const eiVector* pVectorArray, size_t arraySize);
	void AddCustomString(const char* string);
	void EndNode();
};