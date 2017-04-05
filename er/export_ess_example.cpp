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
#include <ei.h>
#include <ei_data_table.h>
#include <ei_verbose.h>
#include <ei_base_bucket.h>
#include <ei_timer.h>
#include <vector>
#include <deque>
#include <signal.h>
#include "ElaraHomeAPI.h"

int main()
{
	/* 坐标系规则跟矩阵
	Elara uses a right-handed coordinate system, with row-major matrices.
	For world space and object space, Z axis points up.
	For camera space, camera direction points towards negative direction of Z axis,
	X axis points to the right, and Y axis points up.
	https://github.com/ElaraFX/elaradoc/wiki/Nodes

	方向光和面光射灯的初始朝向也跟摄像机一样，都是面向负Z轴
	*/

	/* 输入的文件名都不支持中文 */
	EH_Context *pContext = EH_create();
	EH_ExportOptions option;
	option.base85_encoding = false;
	option.left_handed = false;
	EH_begin_export(pContext, "test.ess", &option); /* ess是本地生成路径 */

	/**< 设置渲染参数 */
	EH_RenderOptions render_op;
	render_op.quality = EH_MEDIUM; /**< 中等渲染品质，ElaraHomeAPI头文件里面有定义品质 */
	EH_set_render_options(pContext, &render_op);

	/**< 创建摄像机
		API暴露的接口中只支持投影摄像机
	*/
	EH_Camera cam;
	cam.fov = 45;
	cam.near_clip = 0.01f;
	cam.far_clip = 1000.0f;
	cam.image_width = 640;
	cam.image_height = 480;
	/* 摄像机变换矩阵 行优先的方式 */
	eiMatrix cam_tran = ei_matrix(0.731353, -0.681999, -0.0, 0.0, 0.255481, 0.27397, 0.927184, 0.0, -0.632338, -0.678099, 0.374607, 0.0, -38.681263, -49.142731, 21.895681, 1.0);
	memcpy(cam.view_to_world, &cam_tran.m[0][0], sizeof(EH_Mat));
	EH_set_camera(pContext, &cam);

	/**< 创建网格信息 */
	const char *mesh_name = "simple_mesh";
	EH_Mesh simple_mesh;
	float vertices[48] = {-8.5f, 0.0f, 8.5f,   8.5f, 0.0f, 8.5f,   8.5f, 8.0f, 8.5f,  -8.5f, 8.0f, 8.5f,
		-8.5f, 8.0f, -8.5f,  8.5f, 8.0f, -8.5f,  8.5f, 0.0f, -8.5f, -8.5f, 0.0f, -8.5f,
		8.5f, 0.0f, 8.5f,   8.5f, 0.0f, -8.5f,  8.5f, 8.0f, -8.5f,  8.5f, 8.0f, 8.5f,
		-8.5f, 0.0f, -8.5f,  -8.5f, 0.0f, 8.5f,  -8.5f, 8.0f, 8.5f, -8.5f, 8.0f, -8.5f
	};
	float uvs[32] = {
		0.0,0.0, 1.0,0.0, 1.0,1.0, 0.0,1.0,
		0.0,0.0, 1.0,0.0, 1.0,1.0, 0.0,1.0,
		0.0,0.0, 1.0,0.0, 1.0,1.0, 0.0,1.0,
		0.0,0.0, 1.0,0.0, 1.0,1.0, 0.0,1.0
	};
	uint_t indice[36] = { /* 顶点索引 */
		0,2,1,
		0,3,2,

		1,2,6,
		6,5,1,

		4,5,6,
		6,7,4,

		2,3,6,
		6,3,7,

		0,7,3,
		0,4,7,

		0,1,5,
		0,5,4
	};
	simple_mesh.num_verts = 16;
	simple_mesh.num_faces = 12;
	simple_mesh.verts = (EH_Vec*)vertices;
	simple_mesh.face_indices = (uint_t*)indice;
	simple_mesh.uvs = (EH_Vec2*)uvs;
	EH_add_mesh(pContext, mesh_name, &simple_mesh);

	/**< 创建材质 */
	const char *simple_mtl = "simple_mtl";
	EH_Material simple_mat;
	float diffuse[3] = {1, 0, 0};
	memcpy(simple_mat.diffuse_color, diffuse, sizeof(EH_RGB));
	EH_add_material(pContext, simple_mtl, &simple_mat);

	/**< 设置方向光 */
	EH_Sun sun;
	sun.dir[0] = 0; /* theta */
	sun.dir[1] = 0; /* phi  头文件有具体描述*/
	float color[3] = {1, 1, 1};
	memcpy(sun.color, color, sizeof(color));
	sun.intensity = 3.1; /* 强度0 - (float)最大值 */
	EH_set_sun(pContext, &sun);

	/**< 关联 网格信息 和 材质 */
	const char *simple_inst_name = "simple_inst_name";
	EH_MeshInstance inst;
	inst.mesh_name = mesh_name;
	inst.mtl_names.push_back(simple_mtl);
	eiMatrix inst_tran = ei_matrix( /* 关联材质网络的变化矩阵 */
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1
		);
	memcpy(inst.mesh_to_world, inst_tran.m, sizeof(inst.mesh_to_world));
	EH_add_mesh_instance(pContext, simple_inst_name, &inst);

	/**< 加载外部ESS */
	eiMatrix include_ess_mat = ei_matrix( /* 引用外部ESS模型的变化矩阵 */
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 200, -10, 1
		);
	EH_AssemblyInstance include_inst;
	include_inst.filename = "default.ess"; /* 需要包含的ESS */
	memcpy(include_inst.mesh_to_world, include_ess_mat.m, sizeof(include_inst.mesh_to_world));
	EH_add_assembly_instance(pContext, "include_test_ess", &include_inst); /* include_test_ess 是ESS中节点的名字 不能重名 */

	//添加portal light
	EH_Sky sky;
	sky.enabled = true;
	sky.hdri_name = "test.hdr";
	sky.hdri_rotation = false;
	sky.intensity = 20.0f;
	EH_set_sky(pContext, &sky);

	EH_Light portal_light;
	portal_light.type = EH_LIGHT_PORTAL;
	portal_light.intensity = 20.0f;
	portal_light.size[0] = 200.0f; /* width */
	portal_light.size[1] = 200.0f; /* height */
	memcpy(portal_light.light_to_world, inst_tran.m, sizeof(portal_light.light_to_world));
	EH_add_light(pContext, "test_portal_light", &portal_light);

	EH_end_export(pContext); /* 保存ESS文件 */

	/**< 设置渲染时候的回调 */
	//EH_set_progress_callback(pContext, (EH_ProgressCallback)test_get_progress_cb);
	//EH_set_display_callback(pContext, (EH_display_callback)test_display_cb);
	//EH_start_render(pContext, "default.ess", false);

	EH_delete(pContext); /* 释放内存，非必须 */

	return 0;
}