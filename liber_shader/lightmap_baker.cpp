/**************************************************************************
 * Copyright (C) 2016 Rendease Co., Ltd.
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

#include <ei_shaderx.h>
#include <ei_fixed_pool.h>
#include <ei_pool.h>
#include <vector>

#define NODE_POOL_BANK_SIZE		4096

inline eiVector calc_tri_bary(
	eiScalar x, eiScalar y, 
	eiVector a, eiVector b, eiVector c)
{
	eiVector bary;
	eiVector p = ei_vector(x, y, 1.0f);
	a.z = b.z = c.z = 1.0f;
	const eiVector n = cross(b - a, c - a);
	const eiVector na = cross(c - b, p - b);
	const eiVector nb = cross(a - c, p - c);
	const eiVector nc = cross(b - a, p - a);
	const eiScalar nn = dot(n, n);
	bary.x = dot(n, na) / nn;
	bary.y = dot(n, nb) / nn;
	bary.z = dot(n, nc) / nn;
	return bary;
}

/** Utility for planar bounding box
 */
struct BBox2
{
	eiVector2	min;
	eiVector2	max;

	inline void init()
	{
		min = EI_BIG_SCALAR;
		max = -EI_BIG_SCALAR;
	}

	inline void setv(const eiVector & v)
	{
		min.x = v.x;
		min.y = v.y;
		max = min;
	}

	inline void setv(const eiVector2 & v)
	{
		min = v;
		max = v;
	}

	inline void addv(const eiVector & v)
	{
		if (min.x > v.x) { min.x = v.x; }
		if (max.x < v.x) { max.x = v.x; }
		if (min.y > v.y) { min.y = v.y; }
		if (max.y < v.y) { max.y = v.y; }
	}

	inline void addv(const eiVector2 & v)
	{
		if (min.x > v.x) { min.x = v.x; }
		if (max.x < v.x) { max.x = v.x; }
		if (min.y > v.y) { min.y = v.y; }
		if (max.y < v.y) { max.y = v.y; }
	}

	inline void addb(const BBox2 & b)
	{
		if (min.x > b.min.x) { min.x = b.min.x; }
		if (max.x < b.max.x) { max.x = b.max.x; }
		if (min.y > b.min.y) { min.y = b.min.y; }
		if (max.y < b.max.y) { max.y = b.max.y; }
	}

	inline eiBool contains(eiScalar x, eiScalar y) const
	{
		return (
			x >= min.x && x <= max.x && 
			y >= min.y && y <= max.y);
	}

	inline void extends(eiScalar rhs)
	{
		min.x -= rhs;
		min.y -= rhs;
		max.x += rhs;
		max.y += rhs;
	}
};

/** Reference to the source triangle
 */
struct Triangle
{
	eiVector	v1, v2, v3;
	eiIndex		poly_inst_index;
	eiIndex		tri_index;
};

/** Information of a planar intersection
 */
struct HitInfo
{
	eiIndex		poly_inst_index;
	eiIndex		tri_index;
	eiVector	bary, bary_dx, bary_dy;
};

/** Options for building 2D quad BVH
 */
struct BuildOptions
{
	ei_fixed_pool	*node_pool;
	ei_pool			*triangle_pool;
	eiUint			max_size;
	eiUint			max_depth;

	inline BuildOptions(
		ei_fixed_pool *_node_pool, 
		ei_pool *_triangle_pool, 
		eiUint _max_size, 
		eiUint _max_depth) : 
		node_pool(_node_pool), 
		triangle_pool(_triangle_pool), 
		max_size(_max_size), 
		max_depth(_max_depth)
	{
	}
};

/** A cached instance of polygon mesh
 */
struct PolyInstance
{
	eiTag		poly_inst_tag;
	eiTag		poly_obj_tag;
	eiTag		pos_list_tag;
	eiTag		tri_list_tag;
	eiTag		uv_list_tag;
	eiTag		uv_idxs_tag;
	eiMatrix	transform;
	eiVector2	uvScale;
	eiVector2	uvOffset;
	eiBool		flipBakeNormal;
	eiScalar	bakeRayBias;
};

/** Options for 2D ray-tracing (rasterization)
 */
struct TraceOptions
{
	PolyInstance	*poly_insts;
	eiVector2		pixel_size;

	inline TraceOptions(
		PolyInstance *_poly_insts, 
		const eiVector2 & _pixel_size) : 
		poly_insts(_poly_insts), 
		pixel_size(_pixel_size)
	{
	}
};

/** Quad BVH node in 2D
 */
struct BVHNode
{
	BVHNode		*child[4];
	BBox2		bbox;
	eiUint		is_leaf;
	eiUint		num_triangles;
	Triangle	*triangles;

	inline void init()
	{
		for (eiUint i = 0; i < 4; ++i)
		{
			child[i] = NULL;
		}
		bbox.init();
		is_leaf = EI_TRUE;
		num_triangles = 0;
		triangles = NULL;
	}

	inline void subdiv(
		const BuildOptions & opt, 
		eiUint depth)
	{
		if (depth == opt.max_depth || 
			num_triangles <= opt.max_size)
		{
			is_leaf = EI_TRUE;
			return;
		}

		is_leaf = EI_FALSE;
		eiVector2 split = (bbox.min + bbox.max) * 0.5f;

		eiUint triangle_count[4];
		for (eiUint i = 0; i < 4; ++i)
		{
			triangle_count[i] = 0;
			child[i] = NULL;
		}
		for (eiUint i = 0; i < num_triangles; ++i)
		{
			const Triangle & tri = triangles[i];

			BBox2 tri_bbox;
			tri_bbox.setv(tri.v1);
			tri_bbox.addv(tri.v2);
			tri_bbox.addv(tri.v3);
			
			eiVector2 centroid = (tri_bbox.min + tri_bbox.max) * 0.5f;
			if (centroid.x < split.x)
			{
				if (centroid.y < split.y)
				{
					++ triangle_count[2];
				}
				else
				{
					++ triangle_count[1];
				}
			}
			else
			{
				if (centroid.y < split.y)
				{
					++ triangle_count[3];
				}
				else
				{
					++ triangle_count[0];
				}
			}
		}

		for (eiUint i = 0; i < 4; ++i)
		{
			if (triangle_count[i] > 0)
			{
				child[i] = (BVHNode *)ei_fixed_pool_allocate(opt.node_pool);
				child[i]->init();
				child[i]->num_triangles = triangle_count[i];
				child[i]->triangles = (Triangle *)ei_pool_allocate(opt.triangle_pool, sizeof(Triangle) * triangle_count[i]);
				triangle_count[i] = 0;
			}
		}

		for (eiUint i = 0; i < num_triangles; ++i)
		{
			const Triangle & tri = triangles[i];

			BBox2 tri_bbox;
			tri_bbox.setv(tri.v1);
			tri_bbox.addv(tri.v2);
			tri_bbox.addv(tri.v3);
			
			eiVector2 centroid = (tri_bbox.min + tri_bbox.max) * 0.5f;
			if (centroid.x < split.x)
			{
				if (centroid.y < split.y)
				{
					child[2]->bbox.addb(tri_bbox);
					child[2]->triangles[triangle_count[2]] = tri;
					++ triangle_count[2];
				}
				else
				{
					child[1]->bbox.addb(tri_bbox);
					child[1]->triangles[triangle_count[1]] = tri;
					++ triangle_count[1];
				}
			}
			else
			{
				if (centroid.y < split.y)
				{
					child[3]->bbox.addb(tri_bbox);
					child[3]->triangles[triangle_count[3]] = tri;
					++ triangle_count[3];
				}
				else
				{
					child[0]->bbox.addb(tri_bbox);
					child[0]->triangles[triangle_count[0]] = tri;
					++ triangle_count[0];
				}
			}
		}

		if (triangles != NULL)
		{
			ei_pool_free(opt.triangle_pool, triangles);
			num_triangles = 0;
			triangles = NULL;
		}

		if (child[0] != NULL)
		{
			child[0]->subdiv(
				opt, 
				depth + 1);
		}
		if (child[1] != NULL)
		{
			child[1]->subdiv(
				opt, 
				depth + 1);
		}
		if (child[2] != NULL)
		{
			child[2]->subdiv(
				opt, 
				depth + 1);
		}
		if (child[3] != NULL)
		{
			child[3]->subdiv(
				opt, 
				depth + 1);
		}
	}

	inline eiBool intersect(
		TraceOptions & opt, 
		eiScalar x, eiScalar y, 
		HitInfo & hit)
	{
		if (is_leaf)
		{
			for (eiUint i = 0; i < num_triangles; ++i)
			{
				const Triangle & tri = triangles[i];
				const eiVector & v1 = tri.v1;
				const eiVector & v2 = tri.v2;
				const eiVector & v3 = tri.v3;

				const eiScalar det = (v2.y - v3.y) * (v1.x - v3.x) + (v3.x - v2.x) * (v1.y - v3.y);
				if (det == 0.0f) { continue; }

				const eiScalar b1 = ((v2.y - v3.y) * (x - v3.x) + (v3.x - v2.x) * (y - v3.y)) / det;
				if (b1 < 0.0f || b1 > 1.0f) { continue; }

				const eiScalar b2 = ((v3.y - v1.y) * (x - v3.x) + (v1.x - v3.x) * (y - v3.y)) / det;
				if (b2 < 0.0f || b2 > 1.0f) { continue; }

				const eiScalar b3 = 1.0f - b1 - b2;
				if (b3 < 0.0f || b3 > 1.0f) { continue; }

				hit.poly_inst_index = tri.poly_inst_index;
				hit.tri_index = tri.tri_index;
				hit.bary = ei_vector(b1, b2, b3);
				hit.bary_dx = calc_tri_bary(x + opt.pixel_size.x, y, v1, v2, v3);
				hit.bary_dy = calc_tri_bary(x, y + opt.pixel_size.y, v1, v2, v3);

				return EI_TRUE;
			}
		}
		else
		{
			if (child[0] != NULL && child[0]->bbox.contains(x, y))
			{
				if (child[0]->intersect(opt, x, y, hit))
				{
					return EI_TRUE;
				}
			}

			if (child[1] != NULL && child[1]->bbox.contains(x, y))
			{
				if (child[1]->intersect(opt, x, y, hit))
				{
					return EI_TRUE;
				}
			}

			if (child[2] != NULL && child[2]->bbox.contains(x, y))
			{
				if (child[2]->intersect(opt, x, y, hit))
				{
					return EI_TRUE;
				}
			}

			if (child[3] != NULL && child[3]->bbox.contains(x, y))
			{
				if (child[3]->intersect(opt, x, y, hit))
				{
					return EI_TRUE;
				}
			}
		}

		return EI_FALSE;
	}
};

/** Serves as the intersector for UV space geometry
 */
struct LightmapGlobals
{
	eiDataTableAccessor<eiTag>		poly_insts_accessor;
	std::vector<PolyInstance>		poly_insts;
	const char						*uv_name;
	eiVector2						bbox_min, bbox_max;
	eiScalar						ray_bias;
	BVHNode							*root_node;
	ei_fixed_pool					node_pool;
	ei_pool							triangle_pool;
	eiUint							max_size, max_depth;
	eiInt							res_x, res_y;

	inline LightmapGlobals(
		eiTag poly_insts_tag, 
		const char *_uv_name, 
		eiScalar _ray_bias, 
		eiUint _max_size, 
		eiUint _max_depth) : 
		poly_insts_accessor(poly_insts_tag), 
		uv_name(_uv_name), 
		ray_bias(_ray_bias), 
		max_size(_max_size), 
		max_depth(_max_depth), 
		res_x(1), res_y(1)
	{
		ei_fixed_pool_init(&node_pool, sizeof(BVHNode), NODE_POOL_BANK_SIZE);
		ei_pool_init(&triangle_pool);

		std::string uv_name_idx(uv_name);
		uv_name_idx += "_idx";

		poly_insts.resize(poly_insts_accessor.size());
		for (eiInt i = 0; i < poly_insts_accessor.size(); ++i)
		{
			eiTag poly_inst_tag = poly_insts_accessor.get(i);
			eiDataAccessor<eiNode> poly_inst(poly_inst_tag);
			eiTag poly_obj_tag = ei_node_get_node(poly_inst.get(), ei_node_find_param(poly_inst.get(), "element"));
			eiDataAccessor<eiNode> poly_obj(poly_obj_tag);
			eiTag pos_list_tag = ei_node_get_array(poly_obj.get(), ei_node_find_param(poly_obj.get(), "pos_list"));
			eiTag tri_list_tag = ei_node_get_array(poly_obj.get(), ei_node_find_param(poly_obj.get(), "triangle_list"));
			eiTag uv_list_tag = ei_node_get_array(poly_obj.get(), ei_node_find_param(poly_obj.get(), uv_name));
			eiTag uv_idxs_tag = ei_node_get_array(poly_obj.get(), ei_node_find_param(poly_obj.get(), uv_name_idx.c_str()));
			const eiMatrix & transform = (*ei_node_get_matrix(poly_inst.get(), ei_node_find_param(poly_inst.get(), "transform")));

			poly_insts[i].poly_inst_tag = poly_inst_tag;
			poly_insts[i].poly_obj_tag = poly_obj_tag;
			poly_insts[i].pos_list_tag = pos_list_tag;
			poly_insts[i].tri_list_tag = tri_list_tag;
			poly_insts[i].uv_list_tag = uv_list_tag;
			poly_insts[i].uv_idxs_tag = uv_idxs_tag;
			poly_insts[i].uvScale = 1.0f;
			poly_insts[i].uvOffset = 0.0f;
			poly_insts[i].flipBakeNormal = EI_FALSE;
			poly_insts[i].bakeRayBias = 0.0f;
			eiIndex uvScale_pid = ei_node_find_param(poly_inst.get(), "uvScale");
			if (uvScale_pid != EI_NULL_INDEX)
			{
				poly_insts[i].uvScale = *ei_node_get_vector2(poly_inst.get(), uvScale_pid);
			}
			eiIndex uvOffset_pid = ei_node_find_param(poly_inst.get(), "uvOffset");
			if (uvOffset_pid != EI_NULL_INDEX)
			{
				poly_insts[i].uvOffset = *ei_node_get_vector2(poly_inst.get(), uvOffset_pid);
			}
			eiIndex flipBakeNormal_pid = ei_node_find_param(poly_inst.get(), "flipBakeNormal");
			if (flipBakeNormal_pid != EI_NULL_INDEX)
			{
				poly_insts[i].flipBakeNormal = ei_node_get_bool(poly_inst.get(), flipBakeNormal_pid);
			}
			eiIndex bakeRayBias_pid = ei_node_find_param(poly_inst.get(), "bakeRayBias");
			if (bakeRayBias_pid != EI_NULL_INDEX)
			{
				poly_insts[i].bakeRayBias = ei_node_get_scalar(poly_inst.get(), bakeRayBias_pid);
			}
			poly_insts[i].transform = transform;
		}
	}

	inline ~LightmapGlobals()
	{
		ei_fixed_pool_clear(&node_pool);
		ei_pool_clear(&triangle_pool);
	}

	inline void reset()
	{
		ei_fixed_pool_clear(&node_pool);
		ei_pool_clear(&triangle_pool);
		
		ei_fixed_pool_init(&node_pool, sizeof(BVHNode), NODE_POOL_BANK_SIZE);
		ei_pool_init(&triangle_pool);
	}

	inline void subdiv(eiScalar pixel_padding)
	{
		eiUint num_vertices = 0;
		eiUint num_triangles = 0;
		for (size_t i = 0; i < poly_insts.size(); ++i)
		{
			const PolyInstance & poly_inst = poly_insts[i];
			eiDataTableAccessor<eiVector> uv_list(poly_inst.uv_list_tag);
			eiDataTableAccessor<eiIndex> uv_idxs(poly_inst.uv_idxs_tag);

			eiUint cur_vertices = (eiUint)uv_list.size();
			eiUint cur_triangles = (eiUint)uv_idxs.size() / 3;
			num_vertices += cur_vertices;
			num_triangles += cur_triangles;
		}

		root_node = (BVHNode *)ei_fixed_pool_allocate(&node_pool);
		root_node->init();

		if (num_vertices > 0 && num_triangles > 0)
		{
			root_node->num_triangles = num_triangles;
			root_node->triangles = (Triangle *)ei_pool_allocate(&triangle_pool, sizeof(Triangle) * num_triangles);

			eiUint triangle_offset = 0;
			for (eiInt i = 0; i < poly_insts.size(); ++i)
			{
				const PolyInstance & poly_inst = poly_insts[i];
				eiVector2 uvScale = poly_inst.uvScale;
				eiVector2 uvOffset = poly_inst.uvOffset;
				eiDataTableAccessor<eiVector> uv_list(poly_inst.uv_list_tag);
				eiDataTableAccessor<eiIndex> uv_idxs(poly_inst.uv_idxs_tag);

				eiUint cur_vertices = (eiUint)uv_list.size();
				eiUint cur_triangles = (eiUint)uv_idxs.size() / 3;

				std::vector<eiVector> conservative_uv_list(cur_vertices);
				for (eiUint j = 0; j < cur_vertices; ++j)
				{
					eiVector & v = uv_list.get(j);
					v.x *= uvScale[0];
					v.y *= uvScale[1];
					v.x += uvOffset[0];
					v.y += uvOffset[1];
					conservative_uv_list[j] = v;
				}

				for (eiUint j = 0; j < cur_triangles; ++j)
				{
					eiIndex i1 = uv_idxs.get(j * 3 + 0);
					eiIndex i2 = uv_idxs.get(j * 3 + 1);
					eiIndex i3 = uv_idxs.get(j * 3 + 2);
					eiVector v1 = uv_list.get(i1);
					eiVector v2 = uv_list.get(i2);
					eiVector v3 = uv_list.get(i3);
					eiVector normal = cross(v2 - v1, v3 - v1);
					eiVector d1 = normalize(cross(v2 - v1, normal));
					eiVector d2 = normalize(cross(v3 - v2, normal));
					eiVector d3 = normalize(cross(v1 - v3, normal));
					conservative_uv_list[i1] += ((d3 + d1) * pixel_padding);
					conservative_uv_list[i2] += ((d1 + d2) * pixel_padding);
					conservative_uv_list[i3] += ((d2 + d3) * pixel_padding);
				}

				for (eiUint j = 0; j < cur_triangles; ++j)
				{
					Triangle & tri = root_node->triangles[triangle_offset + j];

					eiIndex i1 = uv_idxs.get(j * 3 + 0);
					eiIndex i2 = uv_idxs.get(j * 3 + 1);
					eiIndex i3 = uv_idxs.get(j * 3 + 2);
					tri.v1 = conservative_uv_list[i1];
					tri.v2 = conservative_uv_list[i2];
					tri.v3 = conservative_uv_list[i3];
					tri.poly_inst_index = i;
					tri.tri_index = j;
				}

				for (eiUint j = 0; j < cur_vertices; ++j)
				{
					eiVector & v = conservative_uv_list[j];
					root_node->bbox.addv(v);
				}

				triangle_offset += cur_triangles;
			}

			BuildOptions opt(
				&node_pool, 
				&triangle_pool, 
				max_size, 
				max_depth);
			root_node->subdiv(opt, 1);
		}
	}
};

/** Built-in lens shader for baking lightmaps
 */
lens (lightmap_baker)

enum
{
	e_poly_instances = 0, 
	e_uv_name, 
	e_ray_bias, 
	e_pixel_padding, 
	e_max_size, 
	e_max_depth, 
};

static void parameters()
{
	declare_array(poly_instances, EI_TYPE_TAG_NODE, EI_NULL_TAG);
	declare_token(uv_name, "uv1");
	declare_scalar(ray_bias, 0.0001f);
	declare_scalar(pixel_padding, 0.5f);
	declare_int(max_size, 10);
	declare_int(max_depth, 30);
}

static void init()
{
}

static void exit()
{
}

void init_node()
{
	glob = NULL;
	
	eiTag poly_insts_tag = eval_array(poly_instances);
	eiToken uv_name = eval_token(uv_name);
	eiScalar ray_bias = eval_scalar(ray_bias);
	eiUint max_size = (eiUint)eval_int(max_size);
	eiUint max_depth = (eiUint)eval_int(max_depth);

	if (poly_insts_tag != EI_NULL_TAG && 
		uv_name.str != NULL)
	{
		glob = new LightmapGlobals(
			poly_insts_tag, 
			uv_name.str, 
			ray_bias, 
			max_size, 
			max_depth);
	}
}

void exit_node()
{
	if (glob != NULL)
	{
		delete ((LightmapGlobals *)glob);
		glob = NULL;
	}
}

eiBool support(
	eiNode *cam, 
	eiInt feature, 
	void *feature_params)
{
	return (feature != EI_FEATURE_VIEWDEP_DISPLACEMENT);
}

eiBool object_to_screen(
	eiNode *cam, 
	eiVector *rpos, 
	const eiVector *opos, 
	const eiMatrix *object_to_view)
{
	return EI_FALSE;
}

void update_world_bbox(
	eiNode *cam, 
	const eiBBox *world_bbox)
{
	LightmapGlobals *g = (LightmapGlobals *)glob;
	if (g == NULL)
	{
		return;
	}

	g->res_x = ei_node_get_int(cam, EI_CAMERA_res_x);
	g->res_y = ei_node_get_int(cam, EI_CAMERA_res_y);
	eiScalar pixel_padding = eval_scalar(pixel_padding);
	pixel_padding /= (eiScalar)min(g->res_x, g->res_y);

	g->reset();
	g->subdiv(pixel_padding);
}

eiBool generate_ray(
	eiNode *cam, 
	eiCameraOutput *out)
{
	LightmapGlobals *g = (LightmapGlobals *)glob;
	if (g == NULL)
	{
		return EI_FALSE;
	}

	eiVector2 raster = raster_pos();
	raster.x = clamp(raster.x / (eiScalar)g->res_x, 0.0f, 1.0f);
	raster.y = clamp(1.0f - raster.y / (eiScalar)g->res_y, 0.0f, 1.0f);
	eiVector2 pixel_size = ei_vector2(1.0f / (eiScalar)g->res_x, 1.0f / (eiScalar)g->res_y);
	
	TraceOptions opt(&(g->poly_insts[0]), pixel_size);
	HitInfo hit;

	if (!g->root_node->intersect(opt, raster.x, raster.y, hit))
	{
		return EI_FALSE;
	}

	const PolyInstance & poly_inst = opt.poly_insts[hit.poly_inst_index];
	eiDataTableAccessor<eiVector> pos_list(poly_inst.pos_list_tag);
	eiDataTableAccessor<eiIndex> tri_list(poly_inst.tri_list_tag);
	eiIndex wi0 = tri_list.get(hit.tri_index * 3 + 0);
	eiIndex wi1 = tri_list.get(hit.tri_index * 3 + 1);
	eiIndex wi2 = tri_list.get(hit.tri_index * 3 + 2);
	const eiMatrix & transform = poly_inst.transform;
	eiVector w0 = point_transform(pos_list.get(wi0), transform);
	eiVector w1 = point_transform(pos_list.get(wi1), transform);
	eiVector w2 = point_transform(pos_list.get(wi2), transform);
	eiVector wPos = hit.bary.x * w0 + hit.bary.y * w1 + hit.bary.z * w2;
	eiVector wNormal = normalize(cross(w1 - w0, w2 - w0));
	if (poly_inst.flipBakeNormal)
	{
		wNormal = -wNormal;
	}
	out->E = wPos + wNormal * max(g->ray_bias, poly_inst.bakeRayBias);
	out->dEdx = (hit.bary_dx.x * w0 + hit.bary_dx.y * w1 + hit.bary_dx.z * w2) - wPos;
	out->dEdy = (hit.bary_dy.x * w0 + hit.bary_dy.y * w1 + hit.bary_dy.z * w2) - wPos;
	out->I = -wNormal;

	return EI_TRUE;
}

end_lens (lightmap_baker)
