/**************************************************************************
 * Copyright (C) 2013 Rendease Co., Ltd.
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

#define EI_OSL_INTEROP
#include <ei_shaderx.h>
#include <ei_vray_proxy_facade.h>
#include <string>

geometry (ess_loader)

	enum
	{
		e_filename = 0, 
		e_root_name, 
	};

	static void parameters()
	{
		declare_token(filename, NULL);
		declare_token(root_name, NULL);
	}

	static void init()
	{
	}

	static void exit()
	{
	}

	void init_node()
	{
	}

	void exit_node()
	{
	}

	void main(void *arg)
	{
		ei_sub_context();

		eiToken filename = eval_token(filename);
		eiToken root_name = eval_token(root_name);

		ei_parse2(filename.str, EI_TRUE);

		// set the root node for current procedural object
		geometry_root(root_name.str);

		ei_end_sub_context();
	}

end_shader (ess_loader)

geometry (vrmesh_loader)

	enum
	{
		e_fileName = 0,
		e_proxy_scale = 1,
	};

	static void parameters()
	{
		declare_token(fileName, NULL);
		declare_scalar(proxy_scale, 1.0f);
	}

	static void init()
	{
	}

	static void exit()
	{
	}

	void init_node()
	{
	}

	void exit_node()
	{
	}

	void main(void *arg)
	{
		ei_sub_context();

		eiToken filename = eval_token(fileName);
		eiScalar proxy_scale = eval_scalar(proxy_scale);

		char resolved_filename[ EI_MAX_FILE_NAME_LEN ];
		ei_resolve_scene_name(resolved_filename, filename.str);
		ei_parse_vrmesh(resolved_filename, proxy_scale, "instance_group");

		// set the root node for current procedural object
		geometry_root("instance_group");

		ei_end_sub_context();
	}

end_shader (vrmesh_loader)
