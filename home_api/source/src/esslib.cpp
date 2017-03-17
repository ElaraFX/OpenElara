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

// esslib.cpp : Defines the exported functions for the DLL application.
//

#include "esslib.h"

//const MAT y2z = MAT(1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1);
//const MAT l2r = MAT(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);
const char* instanceExt = "_instance";

std::string AddCameraData(EssWriter& writer, EH_Camera &cam, std::string& envName, bool panorama, int panorama_size)
{
	//Declare camera
	std::string itemID = "Camera_";
	writer.BeginNode("camera", itemID);

	if (!envName.empty())
	{
		writer.AddRef("env_shader", envName);
	}

 	writer.AddScaler("focal", 1.0f);
	writer.AddScaler("aperture", tan(cam.fov / 2.0f) * 2.0f); //how to deal with aperture?
	writer.AddScaler("clip_hither", cam.near_clip);
	writer.AddScaler("clip_yon", cam.far_clip);

	if (panorama)
	{
		writer.AddInt("res_x", panorama_size * 6);
		writer.AddInt("res_y", panorama_size);
	} 
	else
	{
		writer.AddScaler("aspect", (((float)cam.image_width)/cam.image_height));
		writer.AddInt("res_x", cam.image_width);
		writer.AddInt("res_y", cam.image_height);
	}	
 	
	writer.EndNode();

	//Add camera instance
	std::string instanceName = itemID + instanceExt;
	writer.BeginNode("instance", instanceName);
	writer.AddRef("element",itemID);
	//writer.AddMatrix("transform", l2r * pCam->m_matViewInverse * l2r);
	//writer.AddMatrix("motion_transform", l2r * pCam->m_matViewInverse * l2r);
	writer.AddMatrix("transform", *(eiMatrix*)(cam.view_to_world));
	writer.AddMatrix("motion_transform", *(eiMatrix*)(cam.view_to_world));
	writer.EndNode();

	return instanceName;
}

#define EI_BIG_FLOAT 500000.0f

bool CheckMatrix(const eiMatrix& mat){
	for (int i = 0 ; i < 16; i++)
	{
		if(!_finite(mat.m[i])){
			return false;
		}
	}
	return true;
}

bool CheckMatrixBig(const eiMatrix& mat){
	for (int i = 0 ; i < 16; i++)
	{
		if (fabsf(mat.m[i]) > EI_BIG_FLOAT)return true;
	}
	return false;
}

inline bool CheckVec2Nan(eiVector2 &val)
{
	if (!_finite(val.x))return true;
	if (!_finite(val.y))return true;
	return false;
}

inline bool CheckVec3Nan(eiVector &val)
{
	if (!_finite(val.x))return true;
	if (!_finite(val.y))return true;
	if (!_finite(val.z))return true;
	return false;
}

inline bool CheckVec3Big(eiVector &val)
{
	if (fabsf(val.x) > EI_BIG_FLOAT)return true;
	if (fabsf(val.y) > EI_BIG_FLOAT)return true;
	if (fabsf(val.z) > EI_BIG_FLOAT)return true;
	return false;
}

#define ER_GEO_TRANS_EPS		0.00001f

extern std::string utf16_to_utf8(const char* str);
std::string AddMeshData(EssWriter& writer, CElaraModel& model, const std::string &modelName, std::vector<std::string> &matName)
{
	IMesh* pMesh = model->m_Mesh;
	if (NULL == pMesh) return "";

	g_xhome_mesh.LoadMesh(model, utf16_to_utf8(modelName.c_str()));
	if(g_xhome_mesh.error)return "";

	//CDeviceMesh &mesh = pMesh->m_device_mesh;
	const std::string &itemID = modelName;
	writer.BeginNode("poly", itemID);
	writer.AddPointArray("pos_list", &g_xhome_mesh.positons[0], g_xhome_mesh.positons.size());
	if (model->m_type != 1)
	{
		writer.AddDeclare("vector[]", "N", "varying");
		writer.AddPointArray("N", &g_xhome_mesh.normals[0], g_xhome_mesh.normals.size());
	}
	writer.AddDeclare("vector[]", "uv0", "varying");
	writer.AddPointArray("uv0", &g_xhome_mesh.uvs[0], g_xhome_mesh.uvs.size());
	writer.AddIndexArray("triangle_list", &g_xhome_mesh.indexs[0], g_xhome_mesh.indexs.size(), false);

	//bool isError;
	//writer.AddPointArray("pos_list", mesh->m_pvb, mesh->m_num_vers, get_layout_offset(mesh->m_pvb->m_layout_type, x3d_lt_position) / sizeof(FLOAT), isError);
	//if (!isError)
	//{
	//	writer.AddIndexArray("triangle_list", mesh->m_pib, mesh->m_num_faces, false);
	//	if (model->m_type != 1)
	//	{
	//		writer.AddDeclare("vector[]", "N", "varying");
	//		writer.AddVectorArray("N", mesh->m_pvb, mesh->m_num_vers, get_layout_offset(mesh->m_pvb->m_layout_type, x3d_lt_normal) / sizeof(FLOAT));
	//	}

	//	writer.AddDeclare("vector[]", "uv0", "varying");
	//	writer.AddVector2Array("uv0", mesh->m_pvb, mesh->m_num_vers, get_layout_offset(mesh->m_pvb->m_layout_type, x3d_lt_tex1) / sizeof(FLOAT));
	//}	

	writer.EndNode();

	//Add Mesh instance
	std::string instanceName = itemID + instanceExt;
	writer.BeginNode("instance", instanceName);
	writer.AddRefGroup("mtl_list", matName);
	writer.AddRef("element",itemID);
	writer.AddMatrix("transform", model->m_wts * l2r);
	writer.AddMatrix("motion_transform", model->m_wts * l2r);
	writer.EndNode();
	return instanceName;
}

const char* AddDefaultOptions(EssWriter& writer)
{
	static const char* optName = "GlobalOption";
	writer.BeginNode("options", optName);
	//writer.AddScaler("accel_mode", 0);
	writer.AddBool("motion", false);
	writer.AddBool("use_clamp", false);
	writer.AddScaler("clamp_value", 20.0f);
	writer.AddBool("displace", false);
	writer.AddBool("caustic", false);
	writer.AddEnum("engine", "GI cache");
	writer.AddBool("GI_cache_no_leak", true);
	writer.AddScaler("display_gamma", 2.2f);
	writer.AddScaler("texture_gamma", 2.2f);
	writer.AddScaler("shader_gamma", 2.2f);
	writer.AddScaler("light_gamma", 2.2f);
	//writer.AddBool("exposure", true);
	writer.EndNode();
	return optName;
}

std::string AddDefaultWallMaterial(EssWriter& writer, const bool check_normal)
{
	std::string matName = "GlobalWallMaterial";
	//writer.BeginNode("max_default", "DefaultMaterialShaderName0x32f24105_0x74e20f38");
	//writer.AddInt("version", 2);
	//writer.EndNode();

	writer.BeginNode("max_ei_standard", "DefaultWallMaterialShaderName");
	writer.EndNode();

	writer.BeginNode("backface_cull", "DefaultWallFacingCullName");
	writer.LinkParam("material", "DefaultWallMaterialShaderName", "result");
	writer.EndNode();

	if(check_normal){
		writer.BeginNode("normal_check", "Result_DefaultWallMaterialShaderName");
	}else{
		writer.BeginNode("max_result", "Result_DefaultWallMaterialShaderName");
		writer.LinkParam("input", "DefaultWallFacingCullName", "result");
	}
	
	writer.EndNode();

	std::vector<std::string> shaderNames;
	shaderNames.push_back("Result_DefaultWallMaterialShaderName");

	writer.BeginNode("osl_shadergroup", "DefaultWallOSLShaderName");
	writer.AddRefGroup("nodes", shaderNames);
	writer.EndNode();

	writer.BeginNode("material", matName);
	writer.AddRef("surface_shader", "DefaultWallOSLShaderName");
	writer.EndNode();

	return matName;
}

std::string AddDefaultMaterial(EssWriter& writer, const bool check_normal)
{
	std::string matName = "GlobalMaterial";

	writer.BeginNode("max_ei_standard", "DefaultMaterialShaderName");
	writer.EndNode();

	if(check_normal){
		writer.BeginNode("normal_check", "Result_DefaultMaterialShaderName");
	}else{
		writer.BeginNode("max_result", "Result_DefaultMaterialShaderName");
		writer.LinkParam("input", "DefaultMaterialShaderName", "result");
	}
	writer.EndNode();

	std::vector<std::string> shaderNames;
	shaderNames.push_back("Result_DefaultMaterialShaderName");

	writer.BeginNode("osl_shadergroup", "DefaultOSLShaderName");
	writer.AddRefGroup("nodes", shaderNames);
	writer.EndNode();

	writer.BeginNode("material", matName);
	writer.AddRef("surface_shader", "DefaultOSLShaderName");
	writer.EndNode();

	return matName;
}

std::string AddHDRI(EssWriter& writer, const std::string hdri_name, float roation, float intensity)
{
	if(hdri_name.empty())return "";
	std::string texName = "hdri_env";
	std::string uvgenName = texName + "_uvgen";
	writer.BeginNode("max_stduv", uvgenName);
	writer.AddToken("mapChannel", "uv0");
	writer.AddScaler("uOffset", Math::Radian(roation));
	writer.AddScaler("uScale", 1.0f);
	writer.AddBool("uWrap", true);
	writer.AddScaler("vScale", 1.0f);
	writer.AddBool("vWrap", true);
	writer.AddInt("slotType", 1);
	writer.AddInt("coordMapping", 1);
	writer.EndNode();

	std::string bitmapName = texName + "_bitmap";
	writer.BeginNode("max_bitmap", bitmapName);
	writer.LinkParam("tex_coords", uvgenName, "result");
	writer.AddToken("tex_fileName", hdri_name);
	writer.AddInt("tex_alphaSource", 0);
	writer.EndNode();

	std::string stdoutName = texName + "_stdout";
	writer.BeginNode("max_stdout", stdoutName);
	writer.AddInt("useColorMap", 0);
	writer.AddScaler("outputAmount", intensity);
	writer.LinkParam("stdout_color", bitmapName, "result");
	writer.EndNode();

	return stdoutName;
}


std::string AddBackground(EssWriter& writer, float intensity, const float haze, const VEC3 &sun_dir, const bool use_sky, const std::string &hdri_name, const float rotation, const float hdri_intensity)
{
	std::string sky_shader;
	
	if (use_sky)
	{
		if (intensity != 0.0f)
		{
			sky_shader = "sky_shader";
			writer.BeginNode("max_vray_sky", sky_shader);
			writer.AddScaler("intensity", intensity);
			writer.AddScaler("sun_disk_intensity", 0.0f);
			writer.AddScaler("tex_sun_turbidity", haze);
			writer.AddVector3("sun_dir", sun_dir);
			writer.EndNode();
		}		
	} 
	else if(hdri_intensity != 0.0f)
	{
		sky_shader = AddHDRI(writer, hdri_name, rotation, hdri_intensity);
	}

	if (sky_shader.empty())return "";

	writer.BeginNode("output_result", "global_environment");
	writer.LinkParam("input", sky_shader, "result");	
	writer.AddBool("env_emits_GI", false);
	writer.EndNode();

	std::vector<std::string> names;
	names.push_back("global_environment");

	std::string environmentShaderName = "environment_shader";
	writer.BeginNode("osl_shadergroup", environmentShaderName);
	writer.AddRefGroup("nodes", names);
	writer.EndNode();
	
	return environmentShaderName;
}

std::string AddSun(EssWriter& writer, const VEC3 &dir, const float intensity, const VEC3 sun_color, const float hardness, const int samples)
{
	std::string sunName = "elara_sun";
	writer.BeginNode("directlight", sunName);
	writer.AddScaler("intensity", intensity);
	writer.AddEnum("face", "both");
	//VEC3 sun_color;
	//get_haze_driven_sky_color(sun_color, haze);
	writer.AddColor("color", sun_color);
	writer.AddScaler("hardness", hardness);
	writer.AddInt("samples", samples);
	writer.EndNode();

	MAT mat;
	MAT::LookAtLH(mat, dir, VEC3::ZERO, VEC3::UNIT_Y);
	mat = mat.Inverse();
	std::string instanceName = sunName + "_instance";
	writer.BeginNode("instance", instanceName);
	writer.AddRef("element",sunName);
	writer.AddBool("cast_shadow", false);
	writer.AddBool("shadow", true);
	writer.AddMatrix("transform", l2r * mat * l2r);
	writer.AddMatrix("motion_transform", l2r * mat * l2r);
	writer.EndNode();

	return instanceName;
}

EssExporter::EssExporter(void)
{
	mLightSamples = 16;
}

EssExporter::~EssExporter()
{

}

bool EssExporter::BeginExport(std::string &filename, const bool encoding, const bool check_normal)
{
	printf("BeginExport");
	mCheckNormal = check_normal;
	return mWriter.Initialize(filename, encoding);
}

void EssExporter::SetTexPath(std::string &path)
{
	mRootPath = path;
}

//bool EssExporter::AddItem(IBaseItem* pItem)
//{
//	mContainer.push_back(pItem);
//	std::string instanceName;
//	switch (pItem->get_type())
//	{
//	case ict_model:
//		break;
//	case ict_helper:
//		break;
//	case ict_camera:
//		//instanceName = AddCameraData(mWriter, pItem);
//		//mCamName = instanceName;
//		break;
//	case ict_group:
//		break;
//	case ict_light:
//		instanceName = AddLightData(mWriter, pItem);
//		break;
//	case ict_light_data:
//		break;
//	case ict_helper_data:
//		break;
//	case ict_actor:
//	case ict_ui:
//	case ict_script:
//	case ict_envsetting:
//	case ict_texture:
//	case ict_image:
//	case ict_shader:
//	case ict_mesh:
//		//instanceName = AddMeshData(mWriter, pItem);
//		//break;
//	case ict_material:
//	case ict_emtpy:
//	case ict_unknown:
//	default:
//		return false;
//	}
//	if (instanceName != "")
//	{
//		mElInstances.push_back(instanceName);
//		return true;
//	}
//	else
//	{
//		return false;
//	}
//}

void TranslateLight(EssWriter& writer, const char *pTypeName, const EH_Light &light, const std::string &lightName, const std::string &envName, const int samples){
	writer.BeginNode(pTypeName, lightName);

	if (light.type == EH_LIGHT_PORTAL && !envName.empty())
	{
		writer.AddRef("map", envName);
	}
	writer.AddPropertyMap(*pLight->m_property, true);
	writer.AddInt("samples", samples);
	writer.EndNode();
}

void TranlateIESLight(EssWriter& writer, const EH_Light &light, std::string &lightName, std::string &rootPath, const int samples){
	std::string filterName = lightName + "_filter";
	std::string web_filename;
	float intensity;
	VEC3 color;

	pLight->m_property->get_property("web_filename", web_filename);
	pLight->m_property->get_property("intensity", intensity);
	pLight->m_property->get_property("color", color);

	writer.BeginNode("std_light_filter", filterName);
	writer.AddBool("use_near_atten", false);
	writer.AddScaler("near_start", 140.0f);
	writer.AddScaler("near_stop", 140.0f);
	writer.AddBool("use_far_atten", false);
	writer.AddScaler("far_start", 80.0f);
	writer.AddScaler("far_stop", 200.0f);
	writer.AddBool("use_web_dist", true);
	writer.AddToken("web_filename", rootPath + web_filename);
	writer.AddScaler("web_scale", 1.0f);
	writer.AddBool("web_normalize", true);
	writer.EndNode();

	writer.BeginNode("pointlight", lightName);
	writer.AddScaler("intensity", intensity);
	writer.AddColor("color", color);
	writer.AddRef("shader", filterName);
	writer.AddInt("samples", samples);
	writer.EndNode();
}

std::string AddLight(EssWriter& writer, EH_Light& light, std::string &lightName, std::string &envName, std::string &rootPath, const int samples)
{
	if (!light) return "";

	std::string itemID = lightName;
	switch (light.type)
	{
	//case EH_LIGHT_SPHERE:
		//TranslateLight(writer, "pointlight", light, lightName, envName, 1);
		//break;
	//case LT_Target:
		//TranslateLight(writer, "spotlight", light, lightName, envName, samples);
		//break;
	//case LT_Distant:
		//TranslateLight(writer, "directlight", light, lightName, envName, samples);
		//break;
	case EH_LIGHT_PORTAL:
		TranslateLight(writer, "portallight", light, lightName, envName, samples);		
		break;
	case EH_LIGHT_QUAD:
		TranslateLight(writer, "quadlight", light, lightName, envName, samples);
		break;
	//case LT_Standard:
		//TranlateIESLight(writer, light, lightName, rootPath, 1);
		//break;
	case EH_LIGHT_SPHERE:
		TranslateLight(writer, "spherelight", light, lightName, envName, samples);
		break;
	default: // default select the point light
		TranslateLight(writer, "pointlight", light, lightName, envName, 1);
		break;
	}

	//Add light instance
	std::string instanceName = lightName + instanceExt;
	writer.BeginNode("instance", lightName + instanceExt);
	writer.AddRef("element",lightName);
	writer.AddBool("visible_primary", false);
	writer.AddBool("cast_shadow", false); // block other lights? no!
	writer.AddMatrix("transform", y2z * light->m_wts * l2r);
	writer.AddMatrix("motion_transform", y2z * light->m_wts * l2r);

	//How to get light matrix?
	writer.EndNode();
	return instanceName;
}

std::string AddTexture(EssWriter& writer, const std::string &texPath, const float repeat, const std::string &texName, const std::string &rootPath){
	if(texPath.empty())return "";

	std::string uvgenName = texName + "_uvgen";
	writer.BeginNode("max_stduv", uvgenName);
	writer.AddToken("mapChannel", "uv0");
	writer.AddScaler("uScale", repeat);
	writer.AddBool("uWrap", true);
	writer.AddScaler("vScale", repeat);
	writer.AddBool("vWrap", true);
	writer.EndNode();

	std::string bitmapName = texName + "_bitmap";
	writer.BeginNode("max_bitmap", bitmapName);
	writer.LinkParam("tex_coords", uvgenName, "result");
	writer.AddToken("tex_fileName", rootPath + texPath);
	writer.EndNode();

	std::string stdoutName = texName + "_stdout";
	writer.BeginNode("max_stdout", stdoutName);
	writer.AddInt("useColorMap", 0);
	writer.LinkParam("stdout_color", bitmapName, "result");
	writer.EndNode();

	return stdoutName;
}

std::string AddAlphaTexture(EssWriter& writer, const std::string &texPath, const float repeat, const std::string &texName, const std::string &rootPath){
	if(texPath.empty())return "";

	std::string uvgenName = texName + "_uvgen";
	writer.BeginNode("max_stduv", uvgenName);
	writer.AddToken("mapChannel", "uv0");
	writer.AddScaler("uScale", repeat);
	writer.AddBool("uWrap", true);
	writer.AddScaler("vScale", repeat);
	writer.AddBool("vWrap", true);
	writer.EndNode();

	std::string bitmapName = texName + "_bitmap";
	writer.BeginNode("max_bitmap", bitmapName);
	writer.LinkParam("tex_coords", uvgenName, "result");
	writer.AddToken("tex_fileName", rootPath + texPath);
	writer.AddInt("tex_rgbOutput", 1);
	writer.AddInt("tex_alphaSource", 0);
	writer.EndNode();

	std::string stdoutName = texName + "_stdout";
	writer.BeginNode("max_stdout", stdoutName);
	writer.AddInt("useColorMap", 0);
	writer.AddInt("invert", 1);
	writer.LinkParam("stdout_color", bitmapName, "result");
	writer.EndNode();

	return stdoutName;
}

std::string AddNormalBump(EssWriter& writer, const std::string &normalMap){
	std::string normalName = normalMap + "_normal";
	writer.BeginNode("max_normal_bump", normalName);
	writer.LinkParam("tex_normal_map", normalMap, "result");
	writer.EndNode();
	return normalName;
}

std::string AddMaterial(EssWriter& writer, const EH_Material& mat, std::string &matName, const std::string &rootPath)
{
	//std::string diffuseTex = AddTexture(writer, pMaterial->tex[0], pMaterial->tex_repeat[0], matName + "_d", texPath);
	//std::string normalTex = AddTexture(writer, pMaterial->tex[1], pMaterial->tex_repeat[1], matName + "_n", texPath);
	//std::string specularTex = AddTexture(writer, pMaterial->tex[2], pMaterial->tex_repeat[2], matName + "_s", texPath);
	float eps = 0.0000001
	std::string transparencyTex = mat.transp_weight > eps ? AddAlphaTexture(writer, mat.transp_tex, mat.transp_tex_repeat, matName + "_a", rootPath): "";

	writer.BeginNode("max_ei_standard", matName);
	//writer.AddPropertyMap(*pMaterial, true);

	if(mat.diffuse_tex != 0){
		writer.AddScaler("diffuse_color_alpha", 0);
		writer.LinkParam("diffuse_color", mat.diffuse_tex, "result");
	}
	if(mat.bump_tex != 0)writer.LinkParam("bump_map_bump", mat.bump_tex, "result_bump");
	if(mat.specular_tex != 0)
	{
		writer.AddScaler("specular_color_alpha", 0);
		writer.LinkParam("specular_color", mat.specular_tex, "result");
	}
	if(mat.transp_tex != 0)writer.LinkParam("transparency_weight", mat.transp_tex, "result");

	writer.EndNode();
	return matName;
}

inline int ClampToRange(int value, int max)
{
	if (max == 0) return 0;
	return (int)(100 * (float)value / (float)max);
}

bool EssExporter::AddCamera(EH_Camera &cam, bool panorama, int panorama_size) 
{
	std::string instanceName = AddCameraData(mWriter, pCamera, mEnvName, panorama, panorama_size);
	mCamName = instanceName;
	if (instanceName != "")
	{
		mElInstances.push_back(instanceName);
		return true;
	}else{
		return false;
	}
}

bool EssExporter::AddDefaultMaterial()
{
	std::string wname = ::AddDefaultWallMaterial(mWriter, mCheckNormal);
	mDefaultWallMatName.push_back(wname);
	std::string name = ::AddDefaultMaterial(mWriter,mCheckNormal);
	mDefaultMatName.push_back(name);
	return true;
}

bool EssExporter::AddBackground(float intensity, const float haze, const VEC3 &sun_dir, const bool use_sky, const std::string &hdri_name, const float rotation, const float hdri_intensity)
{
	if (intensity == 0)return true;
	mEnvName = ::AddBackground(mWriter, intensity, haze, sun_dir, use_sky, hdri_name, rotation, hdri_intensity);
	return true;
}

bool EssExporter::AddSun(const VEC3 &dir, const float intensity, const VEC3 sun_color, const float hardness)
{
	if (intensity == 0)return true;
	std::string sunName = ::AddSun(mWriter, dir, intensity, sun_color, hardness, mLightSamples);
	mElInstances.push_back(sunName);
	return true;
}

bool EssExporter::AddMaterial(EH_Material& mat, std::string &matName)
{
	std::string materialName = ::AddMaterial(mWriter, mat, matName, mRootPath);
	if (materialName != "")
	{
		mElMaterials.push_back(materialName);
		return true;
	}else{
		return false;
	}
}

bool EssExporter::AddDefaultOption()
{
	mOptionName = AddDefaultOptions(mWriter);	
}

bool EssExporter::AddLight(EH_Light& light, std::string &lightName)
{
	std::string instanceName = ::AddLight(mWriter, light, lightName, mEnvName, mRootPath, mLightSamples);
	if (instanceName != "")
	{
		mElInstances.push_back(instanceName);
		return true;
	}else{
		return false;
	}
}

bool EssExporter::AddMesh(EH_Mesh& model, const std::string &modelName) 
{
	//std::vector<std::string> &matName = model->m_type == 1 ? mDefaultWallMatName : mDefaultMatName;
	//std::vector<std::string> matName;
	//matName.push_back(GetMaterialName(mWriter, model, mElMaterials, mCheckNormal));
	std::string instanceName = AddMeshData(mWriter, model, modelName, matName);
	if (instanceName != "")
	{
		mElInstances.push_back(instanceName);
		return true;
	}else{
		return false;
	}
}

void EssExporter::EndExport()
{
	printf("EndExport");
	mWriter.BeginNode("instgroup", "er_instgroup");
	mWriter.AddRefGroup("instance_list", mElInstances);
	mWriter.EndNode();

	const char* optName = mOptionName.empty() ? AddDefaultOptions(mWriter) : mOptionName.c_str();
	mWriter.AddRenderCommand("er_instgroup", mCamName, optName);
	mWriter.Close();
}

void EssExporter::SetLightSamples( const int samples )
{
	mLightSamples = samples;
}
