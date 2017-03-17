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

using namespace std;

inline bool Checkei_vectorNan(ei_vector &val)
{
	if (!_finite(val.x))return true;
	if (!_finite(val.y))return true;
	if (!_finite(val.z))return true;
	return false;
}

inline bool Checkei_vector2Nan(ei_vector2 &val)
{
	if (!_finite(val.x))return true;
	if (!_finite(val.y))return true;
	return false;
}

std::string utf16_to_utf8(const char* str)
{
	std::string utf8;
	utf8.resize(WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL));
	WideCharToMultiByte(CP_UTF8, 0, str, -1, &utf8[0], (int)utf8.size(), NULL, NULL);
	return utf8;
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
	UINT32 remainder = input_length % 4;
	UINT32 padding_size = 0;
	if (remainder > 0)
	{
		padding_size = 4 - remainder;
	}
	UINT32 padded_length = input_length + padding_size;
	UINT32 output_length = (padded_length / 4) * 5 - padding_size;

	UINT32 char_nbr = 0;
	for (UINT32 byte_nbr = 0; byte_nbr < padded_length; byte_nbr += 4)
	{
		UINT32 value = 0;
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
#define STR_WRAP(x) "\"" << (utf16_to_utf8(x).data()) << "\""
#define ADD_PROPERTY_ITEM(decl, func) \
	decl value;\
	if (propMap.get_property(it->first, value)) { func(it->first, value); }

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

void EssWriter::BeginNode(const char* type, const tstring& name)
{
	BeginNode(type, &name[0]);
}

void EssWriter::BeginNode(const char* type, const char* name)
{
	if (mInNode) EndNode();
	CHECK_STREAM();
	mStream << "node " << STR_WRAP(type) << " " << STR_WRAP(name) << endl;
	mInNode = true;
}

void EssWriter::LinkParam(const char* input, const tstring& shader, const char* output)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tparam_link " << STR_WRAP(input) << " " << STR_WRAP(shader) << " " << STR_WRAP(output) << endl;
}


void EssWriter::AddScaler(const char* name, const float value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tscalar " << STR_WRAP(name) << " " << value << endl;
}

void EssWriter::AddInt(const char * name, const int value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tint " << STR_WRAP(name) << " " << value << endl;
}

void EssWriter::AddVector4(const char* name, const VEC4& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tvector4 " << STR_WRAP(name) << " " << value.x << " " << value.y << " " << value.z << " " << value.w << endl;
}

void EssWriter::AddVector3(const char* name, const ei_vector& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tvector " << STR_WRAP(name) << " " << value.x << " " << value.y << " " << value.z << endl;
}

void EssWriter::AddToken(const char* name, const tstring& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\ttoken " << STR_WRAP(name) << " " << STR_WRAP(value) << endl;
}

void EssWriter::AddColor(const char * name, const VEC4& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tcolor " << STR_WRAP(name) << " " << value.x * value.w << " " << value.y * value.w << " " << value.z * value.w << endl;
}

void EssWriter::AddColor(const char * name, const ei_vector& value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tcolor " << STR_WRAP(name) << " " << value.x << " " << value.y<< " " << value.z<< endl;
}


void EssWriter::AddBool(const char* name, const bool value)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tbool " << STR_WRAP(name) << " " << (value ? "on" : "off") << endl;
}

void EssWriter::AddRef(const tstring& name, const tstring& ref)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tref " <<  STR_WRAP(name) <<" " << STR_WRAP(ref) << endl;
}

void EssWriter::AddRefGroup(const char* grouptype, std::vector<tstring>& refelements)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tref[] " << STR_WRAP(grouptype) << " 1" << endl;
	for (std::vector<tstring>::iterator it = refelements.begin();
	it != refelements.end();
	++it)
	{
		mStream << "\t\t" << STR_WRAP(*it) << endl;
	}
}

void EssWriter::Addei_matrixrix(const char* name, const ei_matrix& ei_matrixrix)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	mStream << "\tei_matrixrix " << STR_WRAP(name) << " ";
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
	mStream << "\tenum " <<  STR_WRAP(name) <<" " << STR_WRAP(value) << endl;
}

void EssWriter::AddRenderCommand(const char* inst_group_name, const char* cam_name, const char* option_name)
{
	CHECK_STREAM();
	mStream << "render " << STR_WRAP(inst_group_name) << " " << STR_WRAP(cam_name) << " " << STR_WRAP(option_name) << endl;
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
	mStream << "\tdeclare " << utf16_to_utf8(type).data() << " " << STR_WRAP(name) << " " << utf16_to_utf8(storage_class).data() << endl;
}

void EssWriter::AddIndexArray(const char* name, const int* pIndexArray, size_t arraySize, bool faceVarying)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (mBinartyEncoding)
	{
		if (faceVarying)
		{
			AddDeclare();
			mStream << "\tb85_index[] " << STR_WRAP(name) << " facevarying";
		}else{
			mStream << "\tb85_index[] " << STR_WRAP(name) << " 1 ";
		}

		size_t memSize = base85_calc_encode_bound(arraySize * sizeof(UINT));
		BYTE* pOutBuffer = new BYTE[memSize];
		size_t realSize = base85_encode((BYTE*)pIndexArray, arraySize * sizeof(UINT), pOutBuffer);
		mStream.write((char*)pOutBuffer, realSize);
		mStream << endl;
		assert(memSize == realSize);
		delete[] pOutBuffer;
		pOutBuffer = NULL;
	}
	else
	{
		if (faceVarying)
		{
			AddDeclare();
			mStream << "\tindex[] " << STR_WRAP(name) << " facevarying";
		}else{
			mStream << "\tindex[] " << STR_WRAP(name) << " 1 ";
		}

		for (UINT i=0;i<arraySize;i++)
		{
			INT _index(pIndexArray[i]);

			if (i%16 == 0)
			{
				mStream << endl << "\t\t";
			}
			mStream << _index << " ";

		}
		mStream << endl;
	}
}

void EssWriter::AddVectorArray(const char* name, const ei_vector* pVectorArray, size_t arraySize, bool faceVarying)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (faceVarying)
	{
		AddDeclare();
		mStream << "vector[] " << STR_WRAP(name) << " facevarying" << endl;
	}
	mStream << "\tb85_vector[] " << STR_WRAP(name) << " 1 ";
	//Todo: add binary encoded data from pVectorArray
}

void EssWriter::AddPointArray(const char* name, const ei_vector* pPointArray, size_t arraySize)
{
	CHECK_STREAM();
	CHECK_EDIT_MODE();
	if (mBinartyEncoding)
	{
		mStream << "\tb85_point[] " << STR_WRAP(name) << " 1 ";

		size_t memSize = base85_calc_encode_bound(arraySize * sizeof(ei_vector));
		BYTE* pOutBuffer = new BYTE[memSize];

		size_t realSize = base85_encode((BYTE*)pPointArray, arraySize * sizeof(ei_vector), pOutBuffer);

		mStream.write((char*)pOutBuffer, realSize);
		mStream << endl;
		assert(memSize == realSize);
		delete[] pOutBuffer;
		pOutBuffer = NULL;
	}
	else
	{
		mStream << "\tpoint[] " << STR_WRAP(name) << " 1 " << endl;

		for (UINT i=0;i<arraySize;i++)
		{
			const ei_vector& _pos = pPointArray[i];
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
	mStream << "link " << STR_WRAP(L"liber_shader") << endl;
	mBinartyEncoding = encoding;
	return true;
}
