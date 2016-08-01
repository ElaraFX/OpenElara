/**************************************************************************
 * Copyright (C) 2013 Elvic Liang<len3dev@gmail.com>
 * All rights reserved.
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *************************************************************************/

#ifndef EI_PHOTOMETRIC_H
#define EI_PHOTOMETRIC_H

#define EI_OSL_INTEROP
#include <ei_shaderx.h>
#include <fstream>
#include <OpenImageIO/filesystem.h>

inline int floor2Int(float val)
{
	return static_cast<int>(floorf(val));
}

inline int floor2UInt(float val)
{
	return val > 0.f ? static_cast<unsigned int>(floorf(val)) : 0;
}

inline float sphericalTheta(const eiVector &v)
{
	return acosf(clamp((float)v.z, -1.f, 1.f));
}

inline float sphericalPhi(const eiVector &v)
{
	float phi = (v.z == 0.f && v.y == 0.f) ? 0.0f : atan2f(v.y, v.x);
	if (phi < 0.0f)
		phi += 2.0f * (float)EI_PI;
	return phi;
}

/***************************************************************************
*   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  *
*                                                                         *
*   This file is part of LuxRender.                                       *
*                                                                         *
*   Lux Renderer is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 3 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   Lux Renderer is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
*                                                                         *
*   This project is based on PBRT ; see http://www.pbrt.org               *
*   Lux Renderer website : http://www.luxrender.net                       *
***************************************************************************/

class PhotometricDataIES {
public:
	PhotometricDataIES()
	{
		reset();
	}

	PhotometricDataIES(const char *sFileName)
	{
		reset();
		load(sFileName);
	}

	~PhotometricDataIES()
	{
		if (m_fsIES.is_open())
		{
			m_fsIES.close();
		}
	}

	bool isValid() 
	{ 
		return m_bValid; 
	}

	void reset()
	{
		m_bValid 	= false;
		m_Version 	= "NONE";

		m_Keywords.clear();
		m_VerticalAngles.clear(); 
		m_HorizontalAngles.clear(); 
		m_CandelaValues.clear();

		if (m_fsIES.is_open())
		{
			m_fsIES.close();
		}
		m_fsIES.clear();
	}

	bool load(const char *sFileName)
	{
		bool ok = privateLoad(sFileName);
		if (m_fsIES.is_open())
		{
			m_fsIES.close();
		}
		m_fsIES.clear();
		return ok;
	}

	inline void readLine(std::string & sLine)
	{
		memset(&sLine[0], 0, sLine.size());
		m_fsIES.getline(&sLine[0], sLine.size(), 0x0A);
	}

	//////////////////////////////////////////////
	// Keywords and light descriptions.
	std::string m_Version;
	std::map<std::string, std::string> m_Keywords;

	//////////////////////////////////////////////
	// Light data.
	enum PhotometricType {
		PHOTOMETRIC_TYPE_C = 1,
		PHOTOMETRIC_TYPE_B = 2,
		PHOTOMETRIC_TYPE_A = 3
	};

	unsigned int 	m_NumberOfLamps;
	double			m_LumensPerLamp;
	double			m_CandelaMultiplier;
	unsigned int	m_NumberOfVerticalAngles;
	unsigned int	m_NumberOfHorizontalAngles;
	PhotometricType m_PhotometricType;
	unsigned int 	m_UnitsType;
	double			m_LuminaireWidth;
	double			m_LuminaireLength;
	double			m_LuminaireHeight;

	double			BallastFactor;
	double			BallastLampPhotometricFactor;
	double			InputWatts;

	std::vector<double>	m_VerticalAngles; 
	std::vector<double>	m_HorizontalAngles; 

	std::vector< std::vector<double> > m_CandelaValues;

private:
	bool privateLoad(const char *sFileName)
	{
		reset();
		char resolved_filename[ EI_MAX_FILE_NAME_LEN ];
		ei_resolve_texture_name(resolved_filename, sFileName);
		OIIO::Filesystem::open (m_fsIES, resolved_filename);
		if (!m_fsIES.good())
		{
			return false;
		}

		std::string templine(256, 0);
		readLine(templine);

		size_t vpos = templine.find_first_of("IESNA");

		if (vpos != std::string::npos)
		{
			m_Version = templine.substr(templine.find_first_of(":") + 1);
			m_oldVersion = false;
		}
		else // this is the old version without title
		{
			m_oldVersion = true;
		}

		/////////////////////////////////////
		if (!buildKeywordList())
		{
			return false;
		}

		if (!buildLightData())
		{
			return false;
		}
		
		m_bValid = true;

		return true;
	}

	bool buildKeywordList()
	{
		if (!m_fsIES.good())
		{
			return false;
		}

		m_Keywords.clear();

		std::string templine(256, 0);

		m_fsIES.seekg(0);
		readLine(templine);

		if (templine.find("IESNA") == std::string::npos)
		{
			m_oldVersion = true;

			return true; // dont treat the old version 
		}

		std::string sKey, sVal;

		while (m_fsIES.good())
		{
			readLine(templine);

			if (templine.find("TILT") != std::string::npos)
			{
				break;
			}

			size_t kwStartPos = templine.find_first_of("[");
			size_t kwEndPos   = templine.find_first_of("]");
			if (kwStartPos != std::string::npos && 
				kwEndPos != std::string::npos && 
				kwEndPos > kwStartPos)
			{
				std::string sTemp = templine.substr(kwStartPos + 1, (kwEndPos - kwStartPos) - 1);

				if (templine.find("MORE") == std::string::npos && !sTemp.empty())
				{
					if (!sVal.empty())
					{
						m_Keywords.insert(std::pair<std::string,std::string>(sKey, sVal));
					}

					sKey = sTemp;
					sVal = templine.substr(kwEndPos + 1, templine.size() - (kwEndPos + 1));
				}
				else
				{
					sVal += " " + templine.substr(kwEndPos + 1, templine.size() - (kwEndPos + 1));
				}
			}
		}
		
		if (!m_fsIES.good())
		{
			return false;
		}

		m_Keywords.insert(std::pair<std::string, std::string>(sKey, sVal));

		return true;
	}

	void buildDataLine(std::stringstream & ssLine, unsigned int nDoubles, std::vector<double> & vLine)
	{
		double dTemp = 0.0;

		unsigned int count = 0;

		while (count < nDoubles && ssLine.good())
		{
			ssLine >> dTemp;

			vLine.push_back(dTemp);

			count++;
		}
	}

	bool buildLightData()
	{
		if (!m_fsIES.good())
		{
			return false;
		}

		std::string templine(1024, 0);

		//////////////////////////////////////////////////////////////////
		// Find the start of the light data...
		m_fsIES.seekg(0);

		while (m_fsIES.good())
		{
			readLine(templine);

			if (templine.find("TILT") != std::string::npos)
			{
				break;
			}
		}

		////////////////////////////////////////
		// Only supporting TILT=NONE right now
		if (templine.find("TILT=NONE") == std::string::npos)
		{
			return false;
		}

		//////////////////////////////////////////////////////////////////
		// clean the data fields, some IES files use comma's in data 
		// fields which breaks ifstreams >> op.
		int spos = (int)m_fsIES.tellg();

		m_fsIES.seekg(0, std::ios_base::end);

		int epos = (int)m_fsIES.tellg() - spos;

		m_fsIES.seekg(spos);

		std::string strIES(epos, 0);

		int nChar;
		int n1 = 0;

		for (int n = 0; n < epos; n++)
		{
			if (m_fsIES.eof())
			{
				break;
			}

			nChar = m_fsIES.get();

			if (nChar != ',')
			{
				strIES[n1] = (char)nChar;
				n1++;
			}
		}

		m_fsIES.close(); // done with the file.

		strIES.resize(n1);

		std::stringstream ssIES(strIES, std::stringstream::in);

		ssIES.seekg(0, std::ios_base::beg);

		//////////////////////////////////////////////////////////////////	
		// Read first two lines containing light vars.
		ssIES >> m_NumberOfLamps;
		ssIES >> m_LumensPerLamp;
		ssIES >> m_CandelaMultiplier;
		ssIES >> m_NumberOfVerticalAngles;
		ssIES >> m_NumberOfHorizontalAngles;
		unsigned int photometricTypeInt;
		ssIES >> photometricTypeInt;
		m_PhotometricType = PhotometricType(photometricTypeInt);
		ssIES >> m_UnitsType;
		ssIES >> m_LuminaireWidth;
		ssIES >> m_LuminaireLength;
		ssIES >> m_LuminaireHeight;

		ssIES >> BallastFactor;
		ssIES >> BallastLampPhotometricFactor;
		ssIES >> InputWatts;

		//////////////////////////////////////////////////////////////////	
		// Read angle data
		m_VerticalAngles.clear();
		buildDataLine(ssIES, m_NumberOfVerticalAngles, m_VerticalAngles);

		m_HorizontalAngles.clear();
		buildDataLine(ssIES, m_NumberOfHorizontalAngles, m_HorizontalAngles);

		m_CandelaValues.clear();
			
		std::vector<double> vTemp;

		for (unsigned int n1 = 0; n1 < m_NumberOfHorizontalAngles; n1++)
		{
			vTemp.clear();

			buildDataLine(ssIES, m_NumberOfVerticalAngles, vTemp);

			m_CandelaValues.push_back(vTemp);
		}

		return true;
	}

	bool 			m_bValid;
	std::ifstream	m_fsIES;
	bool            m_oldVersion;
};

/**
* A utility class for evaluating an irregularly sampled 1D function.
*/
class IrregularFunction1D {
public:
	/**
	* Creates a 1D function from the given data.
	* It is assumed that the given x values are ordered, starting with the
	* smallest value. The function value is clamped at the edges. It is
	* assumed there are no duplicate sample locations.
	*
	* @param aX   The sample locations of the function.
	* @param aFx  The values of the function.
	* @param aN   The number of samples.
	*/
	IrregularFunction1D(float *aX, float *aFx, int aN)
	{
		count = aN;
		xFunc = new float[count];
		yFunc = new float[count];
		memcpy(xFunc, aX, aN * sizeof(float));
		memcpy(yFunc, aFx, aN * sizeof(float));
	}

	~IrregularFunction1D()
	{
		delete [] xFunc;
		delete [] yFunc;
	}

	/**
	* Evaluates the function at the given position.
	* 
	* @param x The x value to evaluate the function at.
	*
	* @return The function value at the given position.
	*/
	inline float eval(float x) const
	{
		if (x <= xFunc[0])
		{
			return yFunc[0];
		}
		if (x >= xFunc[count - 1])
		{
			return yFunc[count - 1];
		}

		float *ptr = std::upper_bound(xFunc, xFunc + count, x);
		const unsigned int offset = static_cast<unsigned int>(ptr - xFunc - 1);

		float d = (x - xFunc[offset]) / (xFunc[offset + 1] - xFunc[offset]);

		return lerp(yFunc[offset], yFunc[offset + 1], d);
	}

private:
	// IrregularFunction1D Data
	/*
	* The sample locations and the function values.
	*/
	float *xFunc, *yFunc;
	/*
	* The number of function values. The number of cdf values is count+1.
	*/
	int count;
};

class PhotometricSampler : public light_filter_interface
{
public:
	PhotometricSampler()
	{
		m_img = NULL;
		m_xres = 0;
		m_yres = 0;
	}

	~PhotometricSampler()
	{
		clear();
	}

	bool load(const char *filename, bool flip_z, bool web_normalize)
	{
		clear();

		PhotometricDataIES data(filename);

		if (!data.isValid())
		{
			return false;
		}

		if (data.m_PhotometricType != PhotometricDataIES::PHOTOMETRIC_TYPE_C)
		{
			std::cerr << "Unsupported file type: " << filename << std::endl;
			return false;
		}

		std::vector<double> vertAngles = data.m_VerticalAngles;
		std::vector<double> horizAngles = data.m_HorizontalAngles;
		std::vector< std::vector<double> > values = data.m_CandelaValues;

		// Add a begin/end vertical angle with 0 emission if necessary
		if (vertAngles[0] < 0.)
		{
			for (int i = 0; i < vertAngles.size(); ++i)
			{
				vertAngles[i] = vertAngles[i] + 90.;
			}
		}
		if (vertAngles[0] > 0.)
		{
			vertAngles.insert(vertAngles.begin(), std::max(0., vertAngles[0] - 1e-3));
			for (int i = 0; i < horizAngles.size(); ++i)
			{
				values[i].insert(values[i].begin(), 0.);
			}
			if (vertAngles[0] > 0.)
			{
				vertAngles.insert(vertAngles.begin(), 0.);
				for (int i = 0; i < horizAngles.size(); ++i)
				{
					values[i].insert(values[i].begin(), 0.);
				}
			}
		}
		if (vertAngles.back() < 180.)
		{
			vertAngles.push_back(std::min(180., vertAngles.back() + 1e-3));
			for (int i = 0; i < horizAngles.size(); ++i)
			{
				values[i].push_back(0.);
			}
			if (vertAngles.back() < 180.)
			{
				vertAngles.push_back(180.);
				for (int i = 0; i < horizAngles.size(); ++i)
				{
					values[i].push_back(0.);
				}
			}
		}
		// Generate missing horizontal angles
		if (horizAngles[0] == 0.)
		{
			if (horizAngles.size() == 1)
			{
				horizAngles.push_back(90.);
				std::vector<double> tmpVals = values[0];
				values.push_back(tmpVals);
			}
			if (horizAngles.back() == 90.)
			{
				for (int i = horizAngles.size() - 2; i >= 0; --i)
				{
					horizAngles.push_back(180. - horizAngles[i]);
					std::vector<double> tmpVals = values[i]; // copy before adding!
					values.push_back(tmpVals);
				}
			}
			if (horizAngles.back() == 180.)
			{
				for (int i = horizAngles.size() - 2; i >= 0; --i)
				{
					horizAngles.push_back(360. - horizAngles[i]);
					std::vector<double> tmpVals = values[i]; // copy before adding!
					values.push_back(tmpVals);
				}
			}
			if (horizAngles.back() != 360.)
			{
				if ((360. - horizAngles.back()) !=
					(horizAngles.back() - horizAngles[horizAngles.size() - 2]))
				{
					std::cerr << "Invalid horizontal angles in IES file: " << filename << std::endl;
					// Invalid horizontal angles in IES file
					return false;
				}
				horizAngles.push_back(360.);
				std::vector<double> tmpVals = values[0];
				values.push_back(tmpVals);
			}
		}
		else
		{
			std::cerr <<"Invalid horizontal angles in IES file: " << filename << std::endl;
			return false;
		}

		// Initialize irregular functions
		float valueScale = data.m_CandelaMultiplier * 
			data.BallastFactor * 
			data.BallastLampPhotometricFactor;
		if (web_normalize)
		{
			valueScale /= data.m_LumensPerLamp;
		}
		unsigned int nVFuncs = horizAngles.size();
		IrregularFunction1D** vFuncs = new IrregularFunction1D* [nVFuncs];
		unsigned int vFuncLength = vertAngles.size();
		float* vFuncX = new float[vFuncLength];
		float* vFuncY = new float[vFuncLength];
		float* uFuncX = new float[nVFuncs];
		float* uFuncY = new float[nVFuncs];
		for (unsigned int i = 0; i < nVFuncs; ++i)
		{
			for (unsigned int j = 0; j < vFuncLength; ++j)
			{
				vFuncX[j] = clamp((float)radians(vertAngles[j]) * (float)EI_1_PI, 0.f, 1.f);
				vFuncY[j] = values[i][j] * valueScale;
			}

			vFuncs[i] = new IrregularFunction1D(vFuncX, vFuncY, vFuncLength);

			const float inv_2PI = 1.0f / ((float)EI_PI * 2.0f);
			uFuncX[i] = clamp((float)radians(horizAngles[i]) * inv_2PI, 0.f, 1.f);
			uFuncY[i] = i;
		}
		delete[] vFuncX;
		delete[] vFuncY;

		IrregularFunction1D* uFunc = new IrregularFunction1D(uFuncX, uFuncY, nVFuncs);
		delete[] uFuncX;
		delete[] uFuncY;

		// Resample the irregular functions
		unsigned int xRes = 512;
		unsigned int yRes = 256;
		unsigned int img_size = xRes * yRes;
		float *img = new float[img_size];
		for (unsigned int y = 0; y < yRes; ++y)
		{
			const float t = (y + .5f) / yRes;
			for (unsigned int x = 0; x < xRes; ++x)
			{
				const float s = (x + .5f) / xRes;
				const float u = uFunc->eval(s);
				const unsigned int u1 = floor2UInt(u);
				const unsigned int u2 = min(nVFuncs - 1, u1 + 1);
				const float du = u - u1;
				const unsigned int tgtY = flip_z ? (yRes - 1) - y : y;
				img[x + tgtY * xRes] = lerp(vFuncs[u1]->eval(t),
					vFuncs[u2]->eval(t), du);
			}
		}
		delete uFunc;
		for (unsigned int i = 0; i < nVFuncs; ++i)
		{
			delete vFuncs[i];
		}
		delete[] vFuncs;

		m_img = img;
		m_xres = xRes;
		m_yres = yRes;

		m_max_intensity = 0.0f;
		for (unsigned int i = 0; i < img_size; ++i)
		{
			m_max_intensity = max(m_max_intensity, img[i]);
		}

		return true;
	}

	inline float sample(const eiVector & dir) const
	{
		if (m_img == NULL)
		{
			return 0.0f;
		}

		// map from sphere to unit-square
		const float u = sphericalPhi(dir) * (1.0f / (2.0f * (float)EI_PI));
		const float v = sphericalTheta(dir) * (1.0f / (float)EI_PI);
		// retrieve nearest neighbor
		const float ures = u * m_xres;
		const float vres = v * m_yres;
		const int x = floor2Int(ures);
		const int y = floor2Int(vres);
		const float dx = ures - x;
		const float dy = vres - y;

		const float r1 = lerp(get(x, y), get(x, y + 1), dy);
		const float r2 = lerp(get(x + 1, y), get(x + 1, y + 1), dy);
		const float r = lerp(r1, r2, dx);

		return r;
	}

	bool is_valid() const
	{
		return (m_img != NULL);
	}

	void clear()
	{
		if (m_img != NULL)
		{
			delete [] m_img;
			m_img = NULL;
		}
		m_xres = 0;
		m_yres = 0;
	}

private:
	inline int mod(int a, int b) const
	{
		// note - radiance - added 0 check to prevent divide by zero error(s)
		if (b == 0)
		{
			b = 1;
		}

		a %= b;

		if (a < 0)
		{
			a += b;
		}

		return a;
	}

	inline float get(int x, int y) const
	{
		const int wx = mod(x, m_xres);
		const int wy = clamp(y, 0, (int)m_yres - 1);

		return m_img[wy * m_xres + wx];
	}

	float *m_img;
	unsigned int m_xres;
	unsigned int m_yres;
};

#endif
