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

#include "esswriter.h"
#include <assert.h>

using namespace std;

inline bool Checkei_vectorNan(eiVector &val)
{
	if (!_finite(val.x))return true;
	if (!_finite(val.y))return true;
	if (!_finite(val.z))return true;
	return false;
}

inline bool Checkei_vector2Nan(eiVector2 &val)
{
	if (!_finite(val.x))return true;
	if (!_finite(val.y))return true;
	return false;
}

/** Standard conforming implementation of Base85 encoding
 * reference:
 * https://raw.githubusercontent.com/zeromq/rfc/master/src/spec_32.c
 */
// Maps base 256 to base 85
static const char encoder[85 + 1] = {
    "0123456789" 
    "abcdefghij" 
    "klmnopqrst" 
    "uvwxyzABCD"
    "EFGHIJKLMN" 
    "OPQRSTUVWX" 
    "YZ.-:+=^!/" 
    "*?&<>()[]{" 
    "}@%$#"
};

size_t base85_calc_encode_bound(size_t input_length)
{
	size_t padding_size = 0;
	size_t remainder = input_length % 4;
	if (remainder > 0)
	{
		padding_size = 4 - remainder;
	}
	size_t padded_length = input_length + padding_size;
	size_t output_length = (padded_length / 4) * 5 - padding_size;
	return output_length + 1;
}

size_t base85_encode(const BYTE *data, size_t input_length, BYTE *encoded_data)
{
	unsigned int remainder = input_length % 4;
	unsigned int padding_size = 0;
	if (remainder > 0)
	{
		padding_size = 4 - remainder;
	}
	unsigned int padded_length = input_length + padding_size;
	unsigned int output_length = (padded_length / 4) * 5 - padding_size;

	unsigned int char_nbr = 0;
	for (unsigned int byte_nbr = 0; byte_nbr < padded_length; byte_nbr += 4)
	{
		unsigned int value = 0;
		if (byte_nbr + 0 < input_length)
		{
			value += (data[byte_nbr + 0] << 3 * 8);
		}
		if (byte_nbr + 1 < input_length)
		{
			value += (data[byte_nbr + 1] << 2 * 8);
		}
		if (byte_nbr + 2 < input_length)
		{
			value += (data[byte_nbr + 2] << 1 * 8);
		}
		if (byte_nbr + 3 < input_length)
		{
			value += data[byte_nbr + 3];
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / (85 * 85 * 85 * 85) % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / (85 * 85 * 85) % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / (85 * 85) % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value / 85 % 85];
			++ char_nbr;
		}
		if (char_nbr < output_length)
		{
			encoded_data[char_nbr] = encoder[value % 85];
			++ char_nbr;
		}
	}

	encoded_data[char_nbr] = '\0';

	return char_nbr + 1;
}


#define CHECK_STREAM() if(!mStream.is_open()) return;
#define CHECK_EDIT_MODE() if(!mInNode) return;

EssWriter::EssWriter()
	:mInNode(false)
{

}

EssWriter::~EssWriter()
{
	Close();
}

void EssWriter::Close()
{
	if (mStream.is_open())
	{
		mStream.close();
	}
	std::locale::global(mPreviousLocale);
}

void EssWriter::BeginNode(const char* type, const string& name)
{
	BeginNode(type, &name[0]);
}

void EssWriter::BeginNode(const char* type, const char* name)
{
	if (mInNode) EndNode();
	CHECK_STREAM();
	mStream << "node " << "\"" << (type) << "\"" << " " << "\"" << (name) << "\"" << endl;
	mInNode = true;
}

void EssWriter::BeginNameSpace(const char *name)
{
	mStream << "namespace " << "\"" << name << "\"" << endl;
}

void EssWriter::AddParseEss(const char *ess_name)
{
	mStream << "\tparse2 " << "\"" << ess_name << "\"" << " on" << endl;
}

void EssWriter::EndNameSpace()
{
	mStream << "end namespace" << endl;
}

void EssWriter::LinkParam(const char* input, const string& shader, const char* output)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tparam_link " << "\"" << (input) << "\"" << " " << "\"" << (shader) << "\"" << " " << "\"" << (output) << "\"" << endl;
}


void EssWriter::AddScaler(const char* name, const float value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tscalar " << "\"" << (name) << "\"" << " " << value << endl;
}

void EssWriter::AddInt(const char * name, const int value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tint " << "\"" << (name) << "\"" << " " << value << endl;
}

void EssWriter::AddVector4(const char* name, const eiVector4& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tvector4 " << "\"" << (name) << "\"" << " " << value.x << " " << value.y << " " << value.z << " " << value.w << endl;
}

void EssWriter::AddVector3(const char* name, const eiVector& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tvector " << "\"" << (name) << "\"" << " " << value.x << " " << value.y << " " << value.z << endl;
}

void EssWriter::AddVector2(const char* name, const eiVector2& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tvector2 " << "\"" << (name) << "\"" << " " << value.x << " " << value.y << endl;
}

void EssWriter::AddToken(const char* name, const string& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\ttoken " << "\"" << (name) << "\"" << " " << "\"" << (value) << "\"" << endl;
}

void EssWriter::AddColor(const char * name, const eiVector4& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tcolor " << "\"" << (name) << "\"" << " " << value.x * value.w << " " << value.y * value.w << " " << value.z * value.w << endl;
}

void EssWriter::AddColor(const char * name, const eiVector& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tcolor " << "\"" << (name) << "\"" << " " << value.x << " " << value.y<< " " << value.z<< endl;
}


void EssWriter::AddBool(const char* name, const bool value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tbool " << "\"" << (name) << "\"" << " " << (value ? "on" : "off") << endl;
}

void EssWriter::AddRef(const string& name, const string& ref)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tref " <<  "\"" << (name) << "\"" <<" " << "\"" << (ref) << "\"" << endl;
}

void EssWriter::AddRefGroup(const char* grouptype, const std::vector<std::string>& refelements)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tref[] " << "\"" << (grouptype) << "\"" << " 1" << endl;
	for (std::vector<std::string>::const_iterator it = refelements.begin();
	it != refelements.end();
	++it)
	{
		mStream << "\t\t" << "\"" << (*it) << "\"" << endl;
	}
}

void EssWriter::AddMatrix(const char* name, const eiMatrix& ei_matrixrix)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tmatrix " << "\"" << (name) << "\"" << " ";
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			mStream << ei_matrixrix.m[row][col] << " ";
		}		
	}
	mStream << endl;
}
void EssWriter::AddEnum(const char* name, const char* value)
{	
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tenum " <<  "\"" << (name) << "\"" <<" " << "\"" << (value) << "\"" << endl;
}

void EssWriter::AddRenderCommand(const char* inst_group_name, const char* cam_name, const char* option_name)
{
	CHECK_STREAM();
	mStream << "render " << "\"" << (inst_group_name) << "\"" << " " << "\"" << (cam_name) << "\"" << " " << "\"" << (option_name) << "\"" << endl;
}

void EssWriter::AddDeclare()
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tdeclare ";
}

void EssWriter::AddDeclare(const char* type, const char* name, const char *storage_class)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tdeclare " << type << " " << "\"" << (name) << "\"" << " " << storage_class << endl;
}

void EssWriter::AddIndexArray(const char* name, const unsigned int* pIndexArray, size_t arraySize, bool faceVarying)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (mBinartyEncoding)
	{
		if (faceVarying)
		{
			AddDeclare();
			mStream << "\tb85_index[] " << "\"" << (name) << "\"" << " facevarying";
		}else{
			mStream << "\tb85_index[] " << "\"" << (name) << "\"" << " 1 ";
		}

		size_t memSize = base85_calc_encode_bound(arraySize * sizeof(unsigned int));
		BYTE* pOutBuffer = new BYTE[memSize];
		size_t realSize = base85_encode((BYTE*)pIndexArray, arraySize * sizeof(unsigned int), pOutBuffer);
		mStream.write((char*)pOutBuffer, realSize);
		mStream << endl;
		if (memSize != realSize)
		{
			printf("Error! MemSize != RealSize in AddIndexArray\n");
		}
		delete[] pOutBuffer;
		pOutBuffer = NULL;
	}
	else
	{
		if (faceVarying)
		{
			AddDeclare();
			mStream << "\tindex[] " << "\"" << (name) << "\"" << " facevarying";
		}else{
			mStream << "\tindex[] " << "\"" << (name) << "\"" << " 1 ";
		}

		for (UINT i=0;i<arraySize;i++)
		{
			const unsigned int _index = pIndexArray[i];

			if (i%16 == 0)
			{
				mStream << endl << "\t\t";
			}
			mStream << _index << " ";

		}
		mStream << endl;
	}
}

void EssWriter::AddVectorArray(const char* name, const eiVector* pVectorArray, size_t arraySize, bool faceVarying)
{
	/*CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (faceVarying)
	{
	AddDeclare();
	mStream << "vector[] " << "\"" << (name) << "\"" << " facevarying" << endl;
	}
	mStream << "\tb85_vector[] " << "\"" << (name) << "\"" << " 1 ";*/
	//Todo: add binary encoded data from pVectorArray

	CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (mBinartyEncoding)
	{
		mStream << "\tb85_vector[] " << "\"" << (name) << "\"" << " 1 ";

		size_t memSize = base85_calc_encode_bound(arraySize * sizeof(eiVector));
		BYTE* pOutBuffer = new BYTE[memSize];

		size_t realSize = base85_encode((BYTE*)pVectorArray, arraySize * sizeof(eiVector), pOutBuffer);

		mStream.write((char*)pOutBuffer, realSize);
		mStream << endl;
		if (memSize != realSize)
		{
			printf("Error! MemSize != RealSize in AddVectorArray\n");
		}
		delete[] pOutBuffer;
		pOutBuffer = NULL;
	}
	else
	{
		mStream << "\tvector[] " << "\"" << (name) << "\"" << " 1 " << endl;

		for (UINT i=0;i<arraySize;i++)
		{
			const eiVector& _vec = pVectorArray[i];
			mStream << "\t\t" << _vec.x << " " << _vec.y << " " << _vec.z << endl;
		}
	}
}

void EssWriter::AddVector2Array(const char* name, const eiVector2* pVectorArray, size_t arraySize)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (mBinartyEncoding)
	{
		mStream << "\tb85_vector2[] " << "\"" << (name) << "\"" << " 1 ";

		size_t memSize = base85_calc_encode_bound(arraySize * sizeof(eiVector2));
		BYTE* pOutBuffer = new BYTE[memSize];

		size_t realSize = base85_encode((BYTE*)pVectorArray, arraySize * sizeof(eiVector2), pOutBuffer);

		mStream.write((char*)pOutBuffer, realSize);
		mStream << endl;
		if (memSize != realSize)
		{
			printf("Error! MemSize != RealSize in AddVector2Array\n");
		}
		delete[] pOutBuffer;
		pOutBuffer = NULL;
	}
	else
	{
		mStream << "\tvector2[] " << "\"" << (name) << "\"" << " 1 " << endl;

		for (UINT i=0; i<arraySize; i++)
		{
			const eiVector2& _vec = pVectorArray[i];
			mStream << "\t\t" << _vec.x << " " << _vec.y << endl;
		}
	}
}

void EssWriter::AddPointArray(const char* name, const eiVector* pPointArray, size_t arraySize)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (mBinartyEncoding)
	{
		mStream << "\tb85_point[] " << "\"" << (name) << "\"" << " 1 ";

		size_t memSize = base85_calc_encode_bound(arraySize * sizeof(eiVector));
		BYTE* pOutBuffer = new BYTE[memSize];

		size_t realSize = base85_encode((BYTE*)pPointArray, arraySize * sizeof(eiVector), pOutBuffer);

		mStream.write((char*)pOutBuffer, realSize);
		mStream << endl;
		if (memSize != realSize)
		{
			printf("Error! MemSize != RealSize in AddPointArray\n");
		}
		delete[] pOutBuffer;
		pOutBuffer = NULL;
	}
	else
	{
		mStream << "\tpoint[] " << "\"" << (name) << "\"" << " 1 " << endl;

		for (UINT i=0;i<arraySize;i++)
		{
			const eiVector& _pos = pPointArray[i];
			mStream << "\t\t" << _pos.x << " " << _pos.y << " " << _pos.z << endl;
		}
	}
}

void EssWriter::AddCustomString(const char* string)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << string;
}

void EssWriter::EndNode()
{
	CHECK_STREAM();
	mStream << "end" << endl;
	mInNode = false;
}

bool EssWriter::Initialize(const char* filename, const bool encoding)
{
	std::locale newLocale(std::locale(), "", std::locale::ctype);
	mPreviousLocale = std::locale::global(newLocale);
	try
	{
		mStream.open(filename, ios::out);
		if (!mStream.is_open())
		{
			return false;
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	
	mStream << "# ESS generated by esswriter" << endl <<endl;
	mStream << "link " << "\"" << "liber_shader" << "\"" << endl;
	mBinartyEncoding = encoding;
	return true;
}
