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
#include <ei.h>

const char* instanceExt = "_instance";
const char* MAX_EXPORT_ESS_DEFAULT_INST_NAME = "mtoer_instgroup_00";
const char* g_inst_group_name = "GlobalInstGroupName";

//left hand to right hand matrix
const eiMatrix l2r = ei_matrix(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);

bool g_check_normal = false;
bool g_gi_cache_show_samples = false;

std::string AddTexture(EssWriter& writer, const std::string texPath, const float repeat_u, float repeat_v, const std::string texName, const std::string rootPath);
std::string AddNormalBump(EssWriter& writer, const std::string &normalMap);

std::string AddCameraData(EssWriter& writer, const EH_Camera &cam, std::string& NodeName, std::string& envName, bool panorama, int panorama_size, bool is_lefthand)
{	
	std::string cubemap_len_str;
	if (cam.cubemap_render)
	{
		cubemap_len_str = "cubemap_cam_len";
		writer.BeginNode("cubemap_camera", cubemap_len_str);
			writer.AddBool("stereo", cam.stereo);
		writer.EndNode();
	}
	else if(cam.spherical_render)
	{
		cubemap_len_str = "spherical_camera_len";
		writer.BeginNode("spherical_camera", cubemap_len_str);
		writer.AddBool("stereo", cam.stereo);
		writer.EndNode();
	}

	//Declare camera
	std::string itemID = NodeName;
	writer.BeginNode("camera", itemID);

	if (!cubemap_len_str.empty())
	{
		writer.AddRef("lens_shader", cubemap_len_str);
	}

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
	std::string instanceName = "inst_";
	instanceName += NodeName;
	if (instanceName.size() == 0)
	{
		instanceName = "GlobalCameraInstanceName";
	}
	writer.BeginNode("instance", instanceName);
	writer.AddRef("element",itemID);
	eiMatrix cam_tranmat = *(eiMatrix*)(cam.view_to_world);
	if (is_lefthand)
	{
		cam_tranmat = cam_tranmat * l2r;
	}
	writer.AddMatrix("transform", cam_tranmat);
	writer.AddMatrix("motion_transform", cam_tranmat);
	writer.EndNode();

	return instanceName;
}

#define EI_BIG_FLOAT 500000.0f

bool CheckMatrix(const eiMatrix& mat){
	for (int i = 0 ; i < 4; ++i)
	{
		for(int j = 0; j < 4; ++j)
		{
			if(!_finite(mat.m[i][j])){
				return false;
			}
		}
	}
	return true;
}

bool CheckMatrixBig(const eiMatrix& mat){
	for (int i = 0 ; i < 4; ++i)
	{
		for(int j = 0; j < 4; ++j)
		{
			if (fabsf(mat.m[i][j]) > EI_BIG_FLOAT)return true;
		}		
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
#define ER_TRIANGLE_AREA_EPS	0.000001f

void AddDefaultOptions(EssWriter& writer, std::string &opt_name)
{
	if (opt_name.size() == 0)
	{
		opt_name = "GlobalOptionsName";
	}
	writer.BeginNode("options", opt_name.c_str());
	writer.AddInt("min_samples", -3);
	writer.AddInt("max_samples", 16);
	writer.AddInt("diffuse_samples", 8);
	writer.AddInt("sss_samples", 16);
	writer.AddInt("volume_indirect_samples", 8);
	writer.AddScaler("light_cutoff", 0.01);
	writer.AddScaler("GI_cache_density", 1.0);
	writer.AddInt("GI_cache_passes", 100);	
	writer.AddInt("GI_cache_points", 5);
	writer.AddEnum("GI_cache_preview", "accurate");
	writer.AddInt("diffuse_depth", 5);
	writer.AddInt("sum_depth", 10);
	writer.AddBool("caustic", false);
	writer.AddBool("motion", false);
	writer.AddBool("use_clamp", false);
	writer.AddScaler("clamp_value", 20.0f);
	writer.AddBool("displace", false);	
	writer.AddEnum("engine", "GI cache");
	writer.AddBool("GI_cache_no_leak", true);
	writer.AddScaler("display_gamma", 2.2f);
	writer.AddScaler("texture_gamma", 2.2f);
	writer.AddScaler("shader_gamma", 2.2f);
	writer.AddScaler("light_gamma", 2.2f);
	writer.AddBool("exposure", false);
	writer.AddScaler("GI_cache_screen_scale", 1.0f);
	writer.AddScaler("GI_cache_radius", 0.0f);
	writer.AddBool("GI_cache_show_samples", g_gi_cache_show_samples);
	writer.EndNode();
}

void AddMediumOptions(EssWriter &writer, std::string &opt_name)
{
	if (opt_name.size() == 0)
	{
		opt_name = "GlobalMediumOption";
	}
	writer.BeginNode("options", opt_name.c_str());
	writer.AddInt("min_samples", -3);
	writer.AddInt("max_samples", 16);
	writer.AddInt("diffuse_samples", 4);
	writer.AddInt("sss_samples", 16);
	writer.AddInt("volume_indirect_samples", 8);
	writer.AddScaler("light_cutoff", 0.01);
	writer.AddScaler("GI_cache_density", 1.0);
	writer.AddInt("GI_cache_passes", 100);	
	writer.AddInt("GI_cache_points", 5);
	writer.AddEnum("GI_cache_preview", "accurate");
	writer.AddInt("diffuse_depth", 5);
	writer.AddInt("sum_depth", 10);
	writer.AddBool("caustic", false);
	writer.AddBool("motion", false);
	writer.AddBool("use_clamp", false);
	writer.AddScaler("clamp_value", 20.0f);
	writer.AddBool("displace", false);	
	writer.AddEnum("engine", "GI cache");
	writer.AddBool("GI_cache_no_leak", true);
	writer.AddScaler("display_gamma", 2.2f);
	writer.AddScaler("texture_gamma", 2.2f);
	writer.AddScaler("shader_gamma", 2.2f);
	writer.AddScaler("light_gamma", 2.2f);
	writer.AddBool("exposure", false);
	writer.AddScaler("GI_cache_screen_scale", 1.0f);
	writer.AddScaler("GI_cache_radius", 0.0f);
	writer.AddBool("GI_cache_show_samples", g_gi_cache_show_samples);
	writer.EndNode();
}

void AddLowOptions(EssWriter &writer, std::string &opt_name)
{
	if (opt_name.size() == 0)
	{
		opt_name = "GlobalLowOption";
	}
	writer.BeginNode("options", opt_name.c_str());
	writer.AddInt("min_samples", -3);
	writer.AddInt("max_samples", 4);
	writer.AddInt("diffuse_samples", 4);
	writer.AddInt("sss_samples", 16);
	writer.AddInt("volume_indirect_samples", 8);
	writer.AddScaler("light_cutoff", 0.01);
	writer.AddScaler("GI_cache_density", 0.5);
	writer.AddInt("GI_cache_passes", 50);
	writer.AddInt("GI_cache_points", 5);
	writer.AddEnum("GI_cache_preview", "accurate");
	writer.AddInt("diffuse_depth", 3);
	writer.AddInt("sum_depth", 10);
	writer.AddBool("caustic", false);
	writer.AddBool("motion", false);
	writer.AddBool("use_clamp", false);
	writer.AddScaler("clamp_value", 20.0f);
	writer.AddBool("displace", false);
	writer.AddEnum("engine", "GI cache");
	writer.AddBool("GI_cache_no_leak", true);
	writer.AddScaler("display_gamma", 2.2f);
	writer.AddScaler("texture_gamma", 2.2f);
	writer.AddScaler("shader_gamma", 2.2f);
	writer.AddScaler("light_gamma", 2.2f);
	writer.AddBool("exposure", false);
	writer.AddScaler("GI_cache_screen_scale", 1.0f);
	writer.AddScaler("GI_cache_radius", 0.0f);
	writer.AddBool("GI_cache_show_samples", g_gi_cache_show_samples);
	writer.EndNode();
}

void AddHighOptions(EssWriter &writer, std::string &opt_name)
{
	if (opt_name.size() == 0)
	{
		opt_name = "GlobalHighOption";
	}
	writer.BeginNode("options", opt_name.c_str());
	writer.AddInt("min_samples", -3);
	writer.AddInt("max_samples", 36);
	writer.AddInt("diffuse_samples", 8);
	writer.AddInt("sss_samples", 16);
	writer.AddInt("volume_indirect_samples", 8);
	writer.AddScaler("light_cutoff", 0.01);
	writer.AddScaler("GI_cache_density", 1.0);
	writer.AddInt("GI_cache_passes", 150);	
	writer.AddInt("GI_cache_points", 5);
	writer.AddEnum("GI_cache_preview", "accurate");
	writer.AddInt("diffuse_depth", 5);
	writer.AddInt("sum_depth", 10);
	writer.AddBool("caustic", false);
	writer.AddBool("motion", false);
	writer.AddBool("use_clamp", false);
	writer.AddScaler("clamp_value", 20.0f);
	writer.AddBool("displace", false);	
	writer.AddEnum("engine", "GI cache");
	writer.AddBool("GI_cache_no_leak", true);
	writer.AddScaler("display_gamma", 2.2f);
	writer.AddScaler("texture_gamma", 2.2f);
	writer.AddScaler("shader_gamma", 2.2f);
	writer.AddScaler("light_gamma", 2.2f);
	writer.AddBool("exposure", false);
	writer.AddScaler("GI_cache_screen_scale", 1.0f);
	writer.AddScaler("GI_cache_radius", 0.0f);
	writer.AddBool("GI_cache_show_samples", g_gi_cache_show_samples);
	writer.EndNode();
}

void AddCustomOptions(EssWriter &writer, const EH_CustomRenderOptions &option, std::string &opt_name)
{
	if (opt_name.size() == 0)
	{
		opt_name = "GlobalHighOption";
	}
	writer.BeginNode("options", opt_name.c_str());
	writer.AddInt("min_samples", -3);
	writer.AddInt("max_samples", option.sampler_AA);
	writer.AddInt("diffuse_samples", option.diffuse_sample_num);
	writer.AddInt("sss_samples", 16);
	writer.AddInt("volume_indirect_samples", 8);
	writer.AddScaler("light_cutoff", 0.01);
	writer.AddScaler("GI_cache_density", 1.0);
	writer.AddInt("GI_cache_passes", 150);	
	writer.AddInt("GI_cache_points", 5);
	writer.AddEnum("GI_cache_preview", "accurate");
	writer.AddInt("diffuse_depth", option.trace_diffuse_depth);
	writer.AddInt("sum_depth", option.trace_total_depth);
	writer.AddBool("caustic", false);
	writer.AddBool("motion", false);
	writer.AddBool("use_clamp", false);
	writer.AddScaler("clamp_value", 20.0f);
	writer.AddBool("displace", false);	
	writer.AddEnum("engine", "GI cache");
	writer.AddBool("GI_cache_no_leak", true);
	writer.AddScaler("display_gamma", 2.2f);
	writer.AddScaler("texture_gamma", 2.2f);
	writer.AddScaler("shader_gamma", 2.2f);
	writer.AddScaler("light_gamma", 2.2f);
	writer.AddBool("exposure", false);
	writer.AddScaler("GI_cache_screen_scale", 1.0f);
	writer.AddScaler("GI_cache_radius", 0.0f);
	writer.AddBool("GI_cache_show_samples", g_gi_cache_show_samples);
	writer.EndNode();
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

std::string AddHDRI(EssWriter& writer, const std::string hdri_name, float rotation, float intensity)
{
	if(hdri_name.empty())return "";
	std::string texName = "hdri_env";
	std::string uvgenName = texName + "_uvgen";
	writer.BeginNode("max_stduv", uvgenName);
	writer.AddToken("mapChannel", "uv0");
	writer.AddScaler("uOffset", rotation/360.0f);
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


std::string AddBackground(EssWriter& writer, const std::string &hdri_name, const float rotation, const float hdri_intensity, bool enable_emit_GI)
{
	std::string sky_shader;
	sky_shader = AddHDRI(writer, hdri_name, rotation, hdri_intensity);

	if (sky_shader.empty())return "";

	writer.BeginNode("output_result", "global_environment");
	writer.LinkParam("input", sky_shader, "result");	
	writer.AddBool("env_emits_GI", enable_emit_GI);
	writer.EndNode();

	std::vector<std::string> names;
	names.push_back("global_environment");

	std::string environmentShaderName = "environment_shader";
	writer.BeginNode("osl_shadergroup", environmentShaderName);
	writer.AddRefGroup("nodes", names);
	writer.EndNode();
	
	return environmentShaderName;
}

void CalDirectionFromSphereCoordinate(const eiVector2 &sphere_dir, eiVector &out_vector)
{
	//eiScalar neg_z_theta_rad = EI_PI - sphere_dir.x;
	out_vector.x = std::sin(sphere_dir.x) * std::cos(sphere_dir.y);
	out_vector.y = std::sin(sphere_dir.x) * std::sin(sphere_dir.y);
	out_vector.z = std::cos(sphere_dir.x);
}

void SunMatrixLookToRH(const eiVector &dir, eiMatrix &in_matrix)
{	
	eiVector cam_up_vec = ei_vector(0, 1, 0);
	eiVector zaxis = dir;
	eiVector xaxis = normalize(cross(cam_up_vec, zaxis));
	eiVector yaxis = cross(zaxis, xaxis);

	in_matrix = ei_matrix(
		xaxis.x, xaxis.y, xaxis.z, 0,
		yaxis.x, yaxis.y, yaxis.z, 0,
		zaxis.x, zaxis.y, zaxis.z, 0,
		0, 0, 0, 1
		);
}

std::string AddSun(EssWriter& writer, const eiMatrix &mat, const float intensity, const eiVector sun_color, const float hardness, const int samples)
{
	std::string sunName = "elara_sun";
	writer.BeginNode("directlight", sunName);
	writer.AddScaler("intensity", intensity);
	writer.AddEnum("face", "both");
	writer.AddColor("color", sun_color);
	writer.AddScaler("hardness", hardness);
	writer.AddInt("samples", samples);
	writer.EndNode();

	std::string instanceName = sunName + "_instance";
	writer.BeginNode("instance", instanceName);
	writer.AddRef("element",sunName);
	writer.AddBool("cast_shadow", false);
	writer.AddBool("shadow", true);
	writer.AddMatrix("transform", mat);
	writer.AddMatrix("motion_transform", mat);
	writer.EndNode();

	return instanceName;
}

double PointDistance(eiVector &p1, eiVector &p2)
{
	return std::sqrt((p1.x - p2.x)*(p1.x - p2.x) + (p1.y - p2.y)*(p1.y - p2.y) + (p1.z - p2.z) * (p1.z - p2.z));
}

double TriangleArea(eiVector &p1, eiVector &p2, eiVector &p3)
{
	double area = 0;  
	double a = 0, b = 0, c = 0, s = 0;  
	a = PointDistance(p1, p2);  
	b = PointDistance(p2, p3);  
	c = PointDistance(p1, p3);  
	s = 0.5 * (a + b + c);  
	area = std::sqrt(s * (s - a) * (s - b) * (s - c));  
	return area;
}

EssExporter::EssExporter(void) :
	display_callback(NULL),
	progress_callback(NULL),
	log_callback(NULL),
	mIsLeftHand(false),
	mUseDisplacement(false),
	mIsNeedEmitGI(true),
	mOptionName(std::string(""))
{
	mLightSamples = 16;
}

EssExporter::~EssExporter()
{
	mElInstances.clear();
	mElMaterials.clear();
	mDefaultWallMatName.clear();
	mDefaultMatName.clear();	

	display_callback = NULL;
	progress_callback = NULL;
	log_callback = NULL;
}

bool EssExporter::BeginExport(std::string &filename, const EH_ExportOptions &option, const bool check_normal)
{
	printf("BeginExport\n");
	mCheckNormal = check_normal;
	mIsLeftHand = option.left_handed;
	return mWriter.Initialize(filename.c_str(), option.base85_encoding);
}

void EssExporter::SetTexPath(std::string &path)
{
	mRootPath = path;
}

void EssExporter::AddAssemblyInstance(const char *name, const EH_AssemblyInstance &assembly_inst)
{
	std::string name_str = name;
	std::string space_name = name_str + "_space";
	mWriter.BeginNameSpace(space_name.c_str());
	mWriter.AddParseEss(assembly_inst.filename);
	mWriter.EndNameSpace();

	mWriter.BeginNode("instance", name);
	std::string instGroup = space_name + "::" + MAX_EXPORT_ESS_DEFAULT_INST_NAME;
	mWriter.AddRef("element", instGroup);
	mWriter.AddMatrix("transform", *((eiMatrix*)(assembly_inst.mesh_to_world)));
	mWriter.AddMatrix("motion_transform", *((eiMatrix*)(assembly_inst.mesh_to_world)));
	mWriter.EndNode();

	mElInstances.push_back(name);
}

void EssExporter::AddMaterialFromEss(const EH_Material &mat, std::string matName, const char *essName)
{
	float eps = 0.0000001;
	std::string transparent_tex_node, diffuse_tex_node, normal_map_tex_node, specular_tex_node, emission_tex_node;
	if(mat.diffuse_tex.filename && strlen(mat.diffuse_tex.filename) > 0)
	{
		diffuse_tex_node = AddTexture(mWriter, mat.diffuse_tex.filename, mat.diffuse_tex.repeat_u, mat.diffuse_tex.repeat_v, matName + "_d", mRootPath);
	}	
	if(mat.bump_tex.filename && strlen(mat.bump_tex.filename) > 0)
	{
		normal_map_tex_node = AddTexture(mWriter, mat.bump_tex.filename, mat.bump_tex.repeat_u, mat.bump_tex.repeat_v, matName + "_n", mRootPath);
		if(mat.normal_bump)
		{
			normal_map_tex_node = AddNormalBump(mWriter, normal_map_tex_node);
		}
	}
	if(mat.specular_tex.filename && strlen(mat.specular_tex.filename) > 0)
	{
		specular_tex_node = AddTexture(mWriter, mat.specular_tex.filename, mat.specular_tex.repeat_u, mat.specular_tex.repeat_v, matName + "_s", mRootPath);
	}
	if(mat.transp_tex.filename && strlen(mat.transp_tex.filename) > 0)
	{
		transparent_tex_node = AddTexture(mWriter, mat.transp_tex.filename, mat.transp_tex.repeat_u, mat.transp_tex.repeat_v, matName + "_t", mRootPath);
	}
	if(mat.emission_tex.filename && strlen(mat.emission_tex.filename) > 0)
	{
		emission_tex_node = AddTexture(mWriter, mat.emission_tex.filename, mat.emission_tex.repeat_u, mat.emission_tex.repeat_v, matName + "_e", mRootPath);
	}

	std::string ei_standard_node = matName + "_ei_stn";
	mWriter.BeginNode("max_ei_standard", ei_standard_node);

	if(mat.diffuse_tex.filename != 0 && strlen(mat.diffuse_tex.filename) > 0){
		mWriter.LinkParam("diffuse_color", diffuse_tex_node, "result");
	}
	else
	{
		eiVector color = ei_vector(mat.diffuse_color[0], mat.diffuse_color[1], mat.diffuse_color[2]);
		mWriter.AddColor("diffuse_color", color);
	}
	if(mat.bump_tex.filename != 0 && strlen(mat.bump_tex.filename) > 0)
	{		
		mWriter.LinkParam("bump_map_bump", normal_map_tex_node, "result_bump");
	}
	if(mat.specular_tex.filename != 0 && strlen(mat.specular_tex.filename) > 0)
	{
		mWriter.LinkParam("specular_color", specular_tex_node, "result");
	}
	else
	{
		eiVector color = ei_vector(mat.specular_color[0], mat.specular_color[1], mat.specular_color[2]);
		mWriter.AddColor("specular_color", color);
	}
	if(mat.transp_tex.filename != 0 && strlen(mat.transp_tex.filename) > 0)
	{
		mWriter.LinkParam("transparency_weight", transparent_tex_node, "result");
	}
	if(mat.emission_tex.filename != 0 && strlen(mat.emission_tex.filename) > 0)
	{
		mWriter.LinkParam("emission_weight", emission_tex_node, "result");
	}
	else
	{
		eiVector color = ei_vector(mat.emission_color[0], mat.emission_color[1], mat.emission_color[2]);
		mWriter.AddColor("emission_color", color);
	}

	const int MTL_BUF_SIZE = 2048;
	char *mtl_buf = NULL;
	std::streampos size;
	std::ifstream file((wchar_t*)essName, std::ios::in|std::ios::binary|std::ios::ate);
	if (file.is_open())
	{
		size = file.tellg();
		int int_size = size;
		mtl_buf = new char [int_size + 2];
		file.seekg (0, std::ios::beg);
		file.read (mtl_buf, size);
		file.close();

		mtl_buf[int_size] = '\n';
		mtl_buf[int_size + 1] = '\0';
	}

	mWriter.AddCustomString(mtl_buf);

	std::string max_input_mtl_name;
	if (mat.backface_cull)
	{
		max_input_mtl_name = matName + "backface_shader";
		mWriter.BeginNode( "backface_cull", max_input_mtl_name );
		mWriter.LinkParam( "material", ei_standard_node, "result" );
		mWriter.EndNode();
	}
	else
	{
		max_input_mtl_name = ei_standard_node;
	}

	std::string result_node = matName + "_result";
	if (g_check_normal)
	{
		mWriter.BeginNode("normal_check", result_node);
	}
	else
	{
		mWriter.BeginNode("max_result", result_node);
		mWriter.LinkParam("input", max_input_mtl_name, "result");
	}	
	mWriter.EndNode();

	std::string mat_link_name = matName + "_osl";
	std::vector<std::string> shaderNames;
	shaderNames.push_back(result_node);
	mWriter.BeginNode("osl_shadergroup", mat_link_name);
	mWriter.AddRefGroup("nodes", shaderNames);
	mWriter.EndNode();

	mWriter.BeginNode("material", matName);
	mWriter.AddRef("surface_shader", mat_link_name);
	mWriter.EndNode();

	mElMaterials.push_back(matName);
}

void TranslateLight(EssWriter& writer, const char *pTypeName, const EH_Light &light, const std::string &lightName, const std::string &envName, const int samples){
	writer.BeginNode(pTypeName, lightName);

	const eiVector light_default_color = ei_vector(light.light_color[0], light.light_color[1], light.light_color[2]);
	if (light.type == EH_LIGHT_PORTAL)
	{
		if (!envName.empty())
		{
			writer.AddRef("map", envName);
		}		
		writer.AddColor("color", light_default_color);
		writer.AddScaler("width", light.size[0] );
		writer.AddScaler("height", light.size[1]);
		writer.AddScaler("intensity", light.intensity);
	}
	else if (light.type == EH_LIGHT_SPHERE)
	{		
		writer.AddScaler("radius", light.size[0]);
		writer.AddColor("color", light_default_color);
		writer.AddScaler("intensity", light.intensity);
	}
	else if (light.type == EH_LIGHT_QUAD)
	{
		writer.AddScaler("width", light.size[0] );
		writer.AddScaler("height", light.size[1]);
		writer.AddColor("color", light_default_color);
		writer.AddScaler("intensity", light.intensity);
	}
	else if (light.type == EH_LIGHT_SPOT)
	{
		writer.AddScaler("spread", light.size[0] );
		writer.AddScaler("deltaangle", light.size[1]);
		writer.AddColor("color", light_default_color);
		writer.AddScaler("intensity", light.intensity);
	}
	else if (light.type == EH_LIGHT_POINT)
	{
		writer.AddColor("color", light_default_color);
		writer.AddScaler("intensity", light.intensity);
	}

	writer.AddInt("samples", samples);
	writer.EndNode();
}

void TranslateIES(EssWriter& writer, const EH_Light &light, const std::string &lightName, const std::string &envName, const int samples)
{
	std::string filterName = lightName + "_filter";
	std::string web_filename = light.ies_filename;
	const eiVector color = ei_vector(light.light_color[0], light.light_color[1], light.light_color[2]);

	writer.BeginNode("std_light_filter", filterName);
		writer.AddBool("use_near_atten", false);
		writer.AddScaler("near_start", 140.0f);
		writer.AddScaler("near_stop", 140.0f);
		writer.AddBool("use_far_atten", false);
		writer.AddScaler("far_start", 80.0f);
		writer.AddScaler("far_stop", 200.0f);
		writer.AddBool("use_web_dist", true);
		writer.AddToken("web_filename", web_filename);
		writer.AddScaler("web_scale", 0.000029f);
	writer.EndNode();

	writer.BeginNode("pointlight", lightName);
		writer.AddScaler("intensity", light.intensity);
		writer.AddColor("color", color);
		writer.AddRef("shader", filterName);
		writer.AddInt("samples", samples);
	writer.EndNode();
}

std::string AddLight(EssWriter& writer, const EH_Light& light, std::string &lightName, std::string &envName, std::string &rootPath, const int samples, bool is_show_area)
{
	std::string itemID = lightName;
	switch (light.type)
	{		
	case EH_LIGHT_PORTAL:
		TranslateLight(writer, "portallight", light, lightName, envName, samples);		
		break;
	case EH_LIGHT_QUAD:
		TranslateLight(writer, "quadlight", light, lightName, envName, samples);
		break;
	case EH_LIGHT_IES:
		TranslateIES(writer, light, lightName, envName, samples);
		break;
	case EH_LIGHT_SPHERE:
		TranslateLight(writer, "spherelight", light, lightName, envName, samples);
		break;
	case EH_LIGHT_SPOT:
		TranslateLight(writer, "spotlight", light, lightName, envName, samples);
		break;
	case EH_LIGHT_POINT:
		TranslateLight(writer, "pointlight", light, lightName, envName, samples);
		break;
	default: // default select the point light
		TranslateLight(writer, "pointlight", light, lightName, envName, samples);
		break;
	}

	//Add light instance
	std::string instanceName = lightName + instanceExt;
	writer.BeginNode("instance", lightName + instanceExt);
	writer.AddRef("element",lightName);
	writer.AddBool("visible_primary", is_show_area);
	writer.AddBool("cast_shadow", false); // block other lights? no!
	writer.AddMatrix("transform", *((eiMatrix*)(light.light_to_world)));
	writer.AddMatrix("motion_transform", *((eiMatrix*)(light.light_to_world)));

	//How to get light matrix?
	writer.EndNode();
	return instanceName;
}

std::string AddTexture(EssWriter& writer, const std::string texPath, const float repeat_u, float repeat_v, const std::string texName, const std::string rootPath){
	if(texPath.empty())return "";

	std::string uvgenName = texName + "_uvgen";
	writer.BeginNode("max_stduv", uvgenName);
	writer.AddToken("mapChannel", "uv0");
	writer.AddScaler("uScale", repeat_u);
	writer.AddBool("uWrap", true);
	writer.AddScaler("vScale", repeat_v);
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

std::string AddAlphaTexture(EssWriter& writer, const std::string &texPath, const float repeat_u, float repeat_v, const std::string &texName, const std::string &rootPath){
	if(texPath.empty())return "";

	std::string uvgenName = texName + "_uvgen";
	writer.BeginNode("max_stduv", uvgenName);
	writer.AddToken("mapChannel", "uv0");
	writer.AddScaler("uScale", repeat_u);
	writer.AddBool("uWrap", true);
	writer.AddScaler("vScale", repeat_v);
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

std::string AddMaterial(EssWriter& writer, const EH_Material& mat, std::string &matName, const std::string &rootPath, bool &use_displace)
{
	float eps = 0.0000001;
	std::string transparent_tex_node, diffuse_tex_node, normal_map_tex_node, specular_tex_node, emission_tex_node, displace_tex_node, refract_tex_node;
	if(mat.diffuse_tex.filename && strlen(mat.diffuse_tex.filename) > 0)
	{
		diffuse_tex_node = AddTexture(writer, mat.diffuse_tex.filename, mat.diffuse_tex.repeat_u, mat.diffuse_tex.repeat_v, matName + "_d", rootPath);
	}	
	if(mat.bump_tex.filename && strlen(mat.bump_tex.filename) > 0)
	{
		normal_map_tex_node = AddTexture(writer, mat.bump_tex.filename, mat.bump_tex.repeat_u, mat.bump_tex.repeat_v, matName + "_n", rootPath);
		if(mat.normal_bump)
		{
			normal_map_tex_node = AddNormalBump(writer, normal_map_tex_node);
		}
	}
	if(mat.specular_tex.filename && strlen(mat.specular_tex.filename) > 0)
	{
		specular_tex_node = AddTexture(writer, mat.specular_tex.filename, mat.specular_tex.repeat_u, mat.specular_tex.repeat_v, matName + "_s", rootPath);
	}
	if(mat.transp_tex.filename && strlen(mat.transp_tex.filename) > 0)
	{
		transparent_tex_node = AddTexture(writer, mat.transp_tex.filename, mat.transp_tex.repeat_u, mat.transp_tex.repeat_v, matName + "_t", rootPath);
	}
	if(mat.emission_tex.filename && strlen(mat.emission_tex.filename) > 0)
	{
		emission_tex_node = AddTexture(writer, mat.emission_tex.filename, mat.emission_tex.repeat_u, mat.emission_tex.repeat_v, matName + "_e", rootPath);
	}
	if(mat.displace_tex.filename && strlen(mat.displace_tex.filename) > 0)
	{
		displace_tex_node = AddTexture(writer, mat.displace_tex.filename, mat.displace_tex.repeat_u, mat.displace_tex.repeat_v, matName + "_disp", rootPath);
	}
	if(mat.refract_tex.filename && strlen(mat.refract_tex.filename) > 0)
	{
		refract_tex_node = AddTexture(writer, mat.refract_tex.filename, mat.refract_tex.repeat_u, mat.refract_tex.repeat_v, matName + "_refract", rootPath);
	}

	std::string ei_standard_node = matName + "_ei_stn";
	writer.BeginNode("max_ei_standard", ei_standard_node);

	if(mat.diffuse_tex.filename != 0 && strlen(mat.diffuse_tex.filename) > 0){
		writer.LinkParam("diffuse_color", diffuse_tex_node, "result");
	}
	else
	{
		eiVector color = ei_vector(mat.diffuse_color[0], mat.diffuse_color[1], mat.diffuse_color[2]);
		writer.AddColor("diffuse_color", color);
	}
	if(mat.bump_tex.filename != 0 && strlen(mat.bump_tex.filename) > 0)
	{		
		writer.LinkParam("bump_map_bump", normal_map_tex_node, "result_bump");
	}
	if(mat.specular_tex.filename != 0 && strlen(mat.specular_tex.filename) > 0)
	{
		writer.LinkParam("specular_color", specular_tex_node, "result");
	}
	else
	{
		eiVector color = ei_vector(mat.specular_color[0], mat.specular_color[1], mat.specular_color[2]);
		writer.AddColor("specular_color", color);
	}
	if(mat.transp_tex.filename != 0 && strlen(mat.transp_tex.filename) > 0)
	{
		writer.LinkParam("transparency_weight", transparent_tex_node, "result");
	}
	if(mat.emission_tex.filename != 0 && strlen(mat.emission_tex.filename) > 0)
	{
		writer.LinkParam("emission_weight", emission_tex_node, "result");
	}
	else
	{
		eiVector color = ei_vector(mat.emission_color[0], mat.emission_color[1], mat.emission_color[2]);
		writer.AddColor("emission_color", color);
	}
	if(mat.displace_tex.filename && strlen(mat.displace_tex.filename) > 0)
	{
		writer.LinkParam("displace_map", displace_tex_node, "result");
		use_displace = true;
	}
	if(mat.refract_tex.filename && strlen(mat.refract_tex.filename) > 0)
	{
		writer.LinkParam("refraction_weight", refract_tex_node, "result");
	}
	else
	{
		eiVector refraction_color = ei_vector(mat.refract_color[0], mat.refract_color[1], mat.refract_color[2]);
		writer.AddColor("refraction_color", refraction_color);
	}

	writer.AddScaler("diffuse_weight", mat.diffuse_weight);
	writer.AddScaler("roughness", mat.roughness);
	writer.AddScaler("backlighting_weight", mat.backlight);

	writer.AddScaler("bump_weight", mat.bump_weight);

	writer.AddScaler("specular_weight", mat.specular_weight);
	writer.AddScaler("glossiness", mat.glossiness);
	writer.AddScaler("fresnel_ior_glossy", mat.specular_fresnel);
	writer.AddScaler("anisotropy", mat.anisotropy);

	eiVector reflection_color = ei_vector(mat.mirror_color[0], mat.mirror_color[1], mat.mirror_color[2]);
	writer.AddScaler("reflection_weight", mat.mirror_weight);
	writer.AddColor("reflection_color", reflection_color);
	writer.AddScaler("fresnel_ior", mat.mirror_fresnel);

	writer.AddScaler("refraction_weight", mat.refract_weight);
	writer.AddScaler("refraction_glossiness", mat.refract_glossiness);
	writer.AddScaler("ior", mat.ior);
	writer.AddInt("refraction_invert_weight", mat.refract_invert_weight);

	writer.AddScaler("transparency_weight", mat.transp_weight);
	writer.AddInt("transparency_invert_weight", mat.transp_invert_weight);

	writer.AddScaler("emission_weight", mat.emission_weight);
	writer.AddScaler("displace_weight", mat.displace_weight);

	writer.EndNode();

	std::string max_input_mtl_name;
	if (mat.backface_cull)
	{
		max_input_mtl_name = matName + "backface_shader";
		writer.BeginNode( "backface_cull", max_input_mtl_name );
		writer.LinkParam( "material", ei_standard_node, "result" );
		writer.EndNode();
	}
	else
	{
		max_input_mtl_name = ei_standard_node;
	}

	std::string result_node = matName + "_result";

	if (g_check_normal)
	{
		writer.BeginNode("normal_check", result_node);
	}
	else
	{
		writer.BeginNode("max_result", result_node);
		writer.LinkParam("input", max_input_mtl_name, "result");
	}	
	writer.EndNode();

	std::string mat_link_name = matName + "_osl";
	std::vector<std::string> shaderNames;
	shaderNames.push_back(result_node);
	writer.BeginNode("osl_shadergroup", mat_link_name);
	writer.AddRefGroup("nodes", shaderNames);
	writer.EndNode();
	
	writer.BeginNode("material", matName);
	writer.AddRef("surface_shader", mat_link_name);

	if(use_displace)
	{
		writer.AddRef("displace_shader", mat_link_name);
	}

	writer.EndNode();

	return matName;
}

inline int ClampToRange(int value, int max)
{
	if (max == 0) return 0;
	return (int)(100 * (float)value / (float)max);
}

bool EssExporter::AddCamera(const EH_Camera &cam, bool panorama, int panorama_size, std::string &NodeName) 
{
	std::string instanceName = AddCameraData(mWriter, cam, NodeName, mEnvName, panorama, panorama_size, mIsLeftHand);
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

void EssExporter::AddCustomOption(const EH_CustomRenderOptions &option)
{
	AddCustomOptions(mWriter, option, mOptionName);
}

bool EssExporter::AddBackground(const std::string &hdri_name, const float rotation, const float hdri_intensity, bool enable_emit_GI)
{
	mEnvName = ::AddBackground(mWriter, hdri_name, rotation, hdri_intensity, enable_emit_GI);
	return true;
}

bool EssExporter::AddSun(const EH_Sun &sun)
{
	if (sun.intensity == 0 || sun.enabled == false)return true;
	eiMatrix sun_mat;
	eiVector sun_dir;
	eiVector suncolor;
	suncolor.x = sun.color[0];
	suncolor.y = sun.color[1];
	suncolor.z = sun.color[2];
	eiVector2 sun_sphere_coord = ei_vector2(sun.dir[0], sun.dir[1]);	
	CalDirectionFromSphereCoordinate(sun_sphere_coord, sun_dir);
	SunMatrixLookToRH(sun_dir, sun_mat);
	if (mIsLeftHand)
	{
		sun_mat = sun_mat * l2r;
	}

	double sun_size = 695500.0 * sun.soft_shadow;
	double sun_dist = 149597870.0;
	float hardness = cos(asin(sun_size / (sun_dist + sun_size)));
	std::string sunName = ::AddSun(mWriter, sun_mat, sun.intensity, suncolor, hardness, mLightSamples);
	mElInstances.push_back(sunName);
	return true;
}

bool EssExporter::AddMaterial(const EH_Material& mat, std::string &matName)
{
	bool use_displace = false;
	std::string materialName = ::AddMaterial(mWriter, mat, matName, mRootPath, use_displace);

	if (use_displace)
	{
		mUseDisplacement = true;
	}

	if (materialName != "")
	{
		mElMaterials.push_back(materialName);
		return true;
	}else{
		return false;
	}
}

void EssExporter::SetOptionName(std::string &name)
{
	mOptionName = name;
}

void EssExporter::AddMediumOption()
{
	AddMediumOptions(mWriter, mOptionName);
}

void EssExporter::AddDefaultOption()
{
	AddDefaultOptions(mWriter, mOptionName);
}

void EssExporter::AddLowOption()
{
	AddLowOptions(mWriter, mOptionName);
}

void EssExporter::AddHighOption()
{
	AddHighOptions(mWriter, mOptionName);
}

void EssExporter::SetExposure(const EH_Exposure &exposure)
{
	mWriter.BeginNode("options", mOptionName);
		mWriter.AddBool("exposure", true);
		mWriter.AddScaler("exposure_value", exposure.exposure_value);
		mWriter.AddScaler("exposure_highlight", exposure.exposure_highlight);
		mWriter.AddScaler("exposure_shadow", exposure.exposure_shadow);
		mWriter.AddScaler("exposure_saturation", exposure.exposure_saturation);
		mWriter.AddScaler("exposure_whitepoint", exposure.exposure_whitepoint);
	mWriter.EndNode();
}

void EssExporter::SetGamma(const EH_Gamma &gamma)
{
	mWriter.BeginNode("options", mOptionName);	
	mWriter.AddScaler("texture_gamma", gamma.texture_gamma);
	mWriter.AddScaler("display_gamma", gamma.display_gamma);
	mWriter.EndNode();
}

bool EssExporter::AddLight(const EH_Light& light, std::string &lightName, bool is_show_area)
{
	if(light.type == EH_LIGHT_PORTAL)
	{
		mIsNeedEmitGI = false;
	}

	std::string instanceName = ::AddLight(mWriter, light, lightName, mEnvName, mRootPath, light.sample_num_coefficient * mLightSamples, is_show_area);
	if (instanceName != "")
	{
		mElInstances.push_back(instanceName);
		return true;
	}else{
		return false;
	}
}

void EssExporter::AddMesh(const EH_Mesh& model, const std::string &modelName) 
{
	mWriter.BeginNode("poly", modelName.c_str());
	mWriter.AddPointArray("pos_list", (eiVector*)model.verts, model.num_verts);

	std::vector<uint_t> filter_vert_index;	
	std::vector<uint_t> filter_mtl_index;
	filter_vert_index.reserve(model.num_faces * 3);
	if (model.mtl_indices)
	{		
		filter_mtl_index.reserve(model.num_faces);
	}	
	for (int i = 0; i < model.num_faces; ++i)
	{
		uint_t *p_index = (uint_t*)model.face_indices + (i * 3);
		uint_t index0 = (*p_index);
		uint_t index1 = (*(p_index + 1));
		uint_t index2 = (*(p_index + 2));
		eiVector *p0 = (eiVector*)model.verts + index0;
		eiVector *p1 = (eiVector*)model.verts + index1;
		eiVector *p2 = (eiVector*)model.verts + index2;

		if (TriangleArea(*p0, *p1, *p2) > ER_TRIANGLE_AREA_EPS)
		{
			filter_vert_index.push_back(index0);
			filter_vert_index.push_back(index1);
			filter_vert_index.push_back(index2);

			if (model.mtl_indices)
			{
				filter_mtl_index.push_back(*(model.mtl_indices + i));
			}
		}		
	}
	mWriter.AddIndexArray("triangle_list", &filter_vert_index[0], filter_vert_index.size(), false);

	if (model.normals)
	{
		mWriter.AddDeclare("vector[]", "N", "varying");
		mWriter.AddPointArray("N", (eiVector*)model.normals, model.num_verts);
	}

	if(model.uvs)
	{
		mWriter.AddDeclare("vector2[]", "uv0", "varying");
		mWriter.AddVector2Array("uv0", (eiVector2*)model.uvs, model.num_verts);
	}	

	if (model.mtl_indices)
	{
		mWriter.AddDeclare("index[]", "mtl_index", "uniform");
		mWriter.AddIndexArray("mtl_index", &filter_mtl_index[0], filter_mtl_index.size(), false);
	}

	mWriter.EndNode();
}

void EssExporter::AddMeshInstance(const char *instName, const EH_MeshInstance &meshInst)
{
	std::vector<std::string> mtl_list;
	for(int i = 0; i < MAX_NUM_MTLS; ++i)
	{
		if (meshInst.mtl_names[i])
		{
			mtl_list.push_back(meshInst.mtl_names[i]);
		}		
	}
	mWriter.BeginNode("instance", instName);
	mWriter.AddRefGroup("mtl_list", mtl_list);
	mWriter.AddRef("element", meshInst.mesh_name);
	mWriter.AddMatrix("transform", *((eiMatrix*)meshInst.mesh_to_world));
	mWriter.AddMatrix("motion_transform", *((eiMatrix*)meshInst.mesh_to_world));
	mWriter.EndNode();

	mElInstances.push_back(instName);
}

void EssExporter::EndExport()
{
	printf("EndExport\n");
	if (mOptionName.empty())
	{
		AddMediumOptions(mWriter, mOptionName);
	}
	const char* optName = mOptionName.c_str();

	if (mUseDisplacement)
	{
		const char *global_approx = "GlobalApproxDisplace";
		mWriter.BeginNode("approx", global_approx);
		mWriter.AddInt("method", 1);
		mWriter.AddBool("view_dep", true);
		mWriter.AddScaler("edge_length", 1.0f);
		mWriter.AddScaler("motion_factor", 16.0f);
		mWriter.AddInt("max_subdiv", 7);
		mWriter.EndNode();

		mWriter.BeginNode("options", optName);
		mWriter.AddBool("displace", true);
		mWriter.AddScaler("max_displace", 1.0f);
		mWriter.AddRef("approx", global_approx);
		mWriter.EndNode();

		mUseDisplacement = false;
	}

	if(!mEnvName.empty())
	{
		mWriter.BeginNode("output_result", "global_environment");
		mWriter.AddBool("env_emits_GI", mIsNeedEmitGI);
		mWriter.EndNode();
	}

	mWriter.BeginNode("instgroup", g_inst_group_name);
	mWriter.AddRefGroup("instance_list", mElInstances);
	mWriter.EndNode();
	
	mWriter.AddRenderCommand(g_inst_group_name, mCamName.c_str(), optName);
	mWriter.Close();		

	mElInstances.clear();
}

void EssExporter::SetLightSamples( const int samples )
{
	mLightSamples = samples;
}
