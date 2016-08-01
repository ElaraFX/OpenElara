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

#define EI_OSL_INTEROP
#include <ei_shaderx.h>

#include <ei_vray_proxy_facade.h>
#include <string>

#include "PathUtil.h"
#include "PolyMesh.h"

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
