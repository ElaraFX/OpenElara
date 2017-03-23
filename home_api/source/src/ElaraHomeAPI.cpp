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

#include "ElaraHomeAPI.h"
#include <ei.h>
#include <string>

#ifdef _WIN32
	#include <Windows.h>
	#include <shellapi.h>
#endif

#include "esslib.h"

char * EH_utf16_to_utf8(const wchar_t *str)
{
	/*std::string utf8;
	utf8.resize(WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL));
	WideCharToMultiByte(CP_UTF8, 0, str, -1, &utf8[0], (int)utf8.size(), NULL, NULL);
	return utf8.c_str();*/
	return NULL;
}

void EH_convert_native_arguments(int argc, const char *argv[])
{
#ifdef _WIN32
	// Windows only, standard main() entry point does not accept unicode file
	// paths, here we retrieve wide char arguments and convert them to utf8
	if (argc == 0)
		return;

	int native_argc;
	std::vector<std::string> argvList;
	wchar_t **native_argv = CommandLineToArgvW(GetCommandLineW(), &native_argc);

	if (!native_argv || native_argc != argc)
		return;

	for (int i = 0; i < argc; i++) {
		std::string utf8_arg = EH_utf16_to_utf8(native_argv[i]);
		argvList.push_back(utf8_arg);
	}
	for (int i = 0; i < argc; i++) {
		argv[i] = argvList[i].c_str();
	}
#endif
}

EH_Context * EH_create()
{
	return (EH_Context*)new EssExporter();
}

void EH_delete(EH_Context *ctx)
{
	delete ctx;
}

void EH_begin_export(EH_Context *ctx, const char *filename, const EH_ExportOptions *opt)
{
	reinterpret_cast<EssExporter*>(ctx)->BeginExport(std::string(filename), opt->base85_encoding, false);
}

void EH_end_export(EH_Context *ctx)
{
	reinterpret_cast<EssExporter*>(ctx)->EndExport();
}

void EH_set_log_callback(EH_Context *ctx, EH_LogCallback cb)
{

}

void EH_set_progress_callback(EH_Context *ctx, EH_ProgressCallback cb)
{

}

void EH_set_render_options(EH_Context *ctx, const EH_RenderOptions *opt)
{
	switch(opt->quality)
	{
	case EH_MEDIUM:
		reinterpret_cast<EssExporter*>(ctx)->AddDefaultOption();
		break;
	default:
		printf("Not support other options now!\n");
		break;
	}
}

void EH_set_camera(EH_Context *ctx, const EH_Camera *cam)
{
	reinterpret_cast<EssExporter*>(ctx)->AddCamera(*cam, false, 0);
}

void EH_add_mesh(EH_Context *ctx, const char *name, const EH_Mesh *mesh)
{
	reinterpret_cast<EssExporter*>(ctx)->AddMesh(*mesh, std::string(name));
}

void EH_add_material(EH_Context *ctx, const char *name, const EH_Material *mtl)
{
	reinterpret_cast<EssExporter*>(ctx)->AddMaterial(*mtl, std::string(name));
}

void EH_add_mesh_instance(EH_Context *ctx, const char *name, const EH_MeshInstance *inst)
{
	reinterpret_cast<EssExporter*>(ctx)->AddMeshInstance(name, *inst);
}

void EH_add_assembly_instance(EH_Context *ctx, const char *name, const EH_AssemblyInstance *inst)
{
	reinterpret_cast<EssExporter*>(ctx)->AddAssemblyInstance(name, *inst);
}

void EH_add_light(EH_Context *ctx, const char *name, const EH_Light *lgt)
{
	reinterpret_cast<EssExporter*>(ctx)->AddLight(*lgt, std::string(name));
}

void EH_set_sky(EH_Context *ctx, const EH_Sky *sky)
{
	reinterpret_cast<EssExporter*>(ctx)->AddSky(sky->hdri_name, sky->hdri_rotation, sky->intensity);
}

void EH_set_sun(EH_Context *ctx, const EH_Sun *sun)
{
	reinterpret_cast<EssExporter*>(ctx)->AddSun(*sun);
}
