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

#pragma once

/** This file includes a set of APIs to simplify integrating Elara 
 * renderer for interior design software.
 * It should be straightforward to port the implementation of this 
 * API to any programming language.
 */

/* Extern "C" */
#ifdef __cplusplus
#	define EH_EXTERN extern "C"
#else
#	define EH_EXTERN extern
#endif

/* Exporting and importing */
/* EI_HOME_EXPORTS must NOT be defined by users */
#ifdef _MSC_VER
#	if defined ELARA_HOME_EXPORTS
#		define EH_XAPI	__declspec(dllexport)
#	else
#		define EH_XAPI	__declspec(dllimport)
#	endif
#else
#	define EH_XAPI		__attribute__((visibility("default")))
#endif

#define EH_API EH_EXTERN EH_XAPI

#include <vector>

/* Data types */
//#ifndef uint_t
	typedef unsigned int uint_t;
//#endif

/** The max number of materials mesh contains
*/
const uint_t MAX_NUM_MTLS = 16;

typedef float EH_RGB[3];
typedef float EH_RGBA[4];
typedef float EH_Vec[3];
typedef float EH_Vec2[2];
typedef float EH_Mat[16];



/** Log message Severity
 */
enum EH_Severity
{
	EH_INFO = 0, 
	EH_WARNING, 
	EH_ERROR, 
	EH_FATAL, 
	EH_DEBUG
};

/** Render quality presets
 */
enum EH_RenderQuality
{
	EH_MEDIUM = 0,		/**< medium quality good for most situations */
	EH_FAST,			/**< very fast preview rendering */
	EH_HIGH,			/**< super high quality rendering but slow */
};

/** Light types.
 */
enum EH_LightType
{
	EH_LIGHT_PORTAL = 0,		/**< Sky portal to be added outside window */
	EH_LIGHT_QUAD,				/**< rectangular light */
	EH_LIGHT_SPHERE,			/**< spherical light */
	EH_LIGHT_IES,				/**< Light with IES distribution */
};



/** A utility for converting UTF-16 string into UTF-8, because Elara 
 * only accepts UTF-8 strings.
 * The caller is responsible for free'ing the returned string.
 */
EH_API char *EH_utf16_to_utf8(const wchar_t *str);

/** A utility function to convert the arguments of command line into 
 * UTF-8, such that Elara can correctly handle file path.
 */
EH_API void EH_convert_native_arguments(int argc, const char *argv[]);


/** Elara license type
*/
enum EH_LicenseKeyType
{
	EH_NONE_LICENSE,        /**< None license */
	EH_DONGLE_LOCK			/**< Using Dongle id for verifying */
};

/** Elara license data
*/
struct EH_LicenseData
{
	char *code1;			/**< Vendor id */
	char *code2;			/**< Vendor id */
	char *activate_code;	/**< License key of applying */

	EH_LicenseKeyType lic_type;

	EH_LicenseData() :
		code1(NULL),
		code2(NULL),
		activate_code(NULL),
		lic_type(EH_NONE_LICENSE)
	{
	}
};

/** Set Elara license to remove watermark
*/
EH_API void EH_set_license_data(EH_LicenseData lic_data);


/** The exporting context.
 */
typedef void * EH_Context;

/** Create exporting context, initialize its parameters.
 */
EH_API EH_Context *EH_create();

/** Delete exporting context, free all of its resources.
 */
EH_API void EH_delete(EH_Context *ctx);



/** The exporting options for user to fill
 */
struct EH_ExportOptions
{
	bool base85_encoding;	/**< Use Base85 encoding? */
	bool left_handed;		/**< Is source data in left-handed space? */
};

/** Begin exporting, set options which are effective 
 * during the exporting process.
 * \param filename The ESS filename to create.
 * \param opt The export options
 */
EH_API void EH_begin_export(EH_Context *ctx, const char *filename, const EH_ExportOptions *opt);
/** End exporting, clean up resources only valid during 
 * the exporting process.
 */
EH_API void EH_end_export(EH_Context *ctx);


/** The callback to log messages during exporting.
 */
typedef void (*EH_LogCallback)(EH_Severity severity, const char *msg);
/** The callback to update exporting progress. To cancel 
 * the exporting process, return false for this function.
 */
typedef bool (*EH_ProgressCallback)(float progress);

/** Set log callback.
 */
void EH_set_log_callback(EH_Context *ctx, EH_LogCallback cb);
/** Set progress callback.
 */
void EH_set_progress_callback(EH_Context *ctx, EH_ProgressCallback cb);



/** The render options for user to fill
 */
struct EH_RenderOptions
{
	EH_RenderQuality quality;
};

/** The custom render options for user
*/
struct EH_CustomRenderOptions
{
	uint_t sampler_AA;
	uint_t diffuse_sample_num;	
	uint_t trace_diffuse_depth;
	uint_t trace_total_depth;

	EH_CustomRenderOptions() :
		sampler_AA(16),
		diffuse_sample_num(8),
		trace_diffuse_depth(3),
		trace_total_depth(10)
	{

	}
};

/** Set current render options.
 */
EH_API void EH_set_render_options(EH_Context *ctx, const EH_RenderOptions *opt);

/** Set current custom render options.
 */
EH_API void EH_set_custom_render_options(EH_Context *ctx, const EH_CustomRenderOptions *opt);

/** The final render image exposure setting.
*/
struct EH_Exposure
{
	float exposure_value;
	float exposure_highlight;
	float exposure_shadow;
	float exposure_saturation;
	float exposure_whitepoint;	

	EH_Exposure() :
		exposure_value(-1.0f),
		exposure_highlight(0.05f),
		exposure_shadow(0.1f),
		exposure_saturation(1.4f),
		exposure_whitepoint(6500.0f)
	{

	}
};

/** Set render image exposure, you MUST call this function after set EH_RenderOptions
*/
EH_API void EH_set_exposure(EH_Context *ctx, const EH_Exposure *exposure);

/** The final render image gamma setting.
*/
struct EH_Gamma
{
	float texture_gamma;
	float display_gamma;

	EH_Gamma() :
		texture_gamma(2.2f),
		display_gamma(2.2f)
	{

	}
};
/** Set render image gamma, you MUST call this function after set EH_RenderOptions
*/
EH_API void EH_set_gamma(EH_Context *ctx, const EH_Gamma *gamma);

/** The camera data for user to fill
 */
struct EH_Camera
{
	float fov;              /**< Field of view in the horizontal direction, in radians. */
	float near_clip;
	float far_clip;
	uint_t image_width;
	uint_t image_height;
	EH_Mat view_to_world;	/**< View to world transform matrix,it's the inverse of view matrix. */
	bool cubemap_render;	/**< Render a 6x1 cubemap? */
	bool spherical_render;  /**< Render spherical map */
	bool stereo;

	EH_Camera() :
		fov(0.0f),
		near_clip(0.0f),
		far_clip(0.0f),
		image_width(0),
		image_height(0),
		cubemap_render(false),
		spherical_render(false),
		stereo(false)
	{
		memset(view_to_world, 0, sizeof(view_to_world));
	}
};

/** Set current camera to render.
 */
EH_API void EH_set_camera(EH_Context *ctx, const EH_Camera *cam);



/** The triangle mesh data for user to fill
 */
struct EH_Mesh
{
	uint_t num_verts;
	uint_t num_faces;
	EH_Vec *verts;
	EH_Vec *normals;
	EH_Vec2 *uvs;
	uint_t *face_indices;		/** Should have (num_faces * 3) indices */
	uint_t *mtl_indices;        /** For multiple material model. Material index on every triangle */

	EH_Mesh() :
		num_verts(0),
		num_faces(0),
		verts(NULL),
		normals(NULL),
		uvs(NULL),
		face_indices(NULL),
		mtl_indices(NULL)
	{

	}
};

/** Add a triangle mesh to the scene.
 * \param name The name of the mesh
 * \param The mesh data
 */
EH_API void EH_add_mesh(EH_Context *ctx, const char *name, const EH_Mesh *mesh);



/** The texture to be connected to material.
 */
struct EH_Texture
{
	const char *filename;	/**< The image filename */
	float repeat_u;			/**< The u repeat scale */
	float repeat_v;			/**< The v repeat scale */

	EH_Texture() :
		filename(NULL),
		repeat_u(1.0f),
		repeat_v(1.0f)
	{

	}
};

/** The material data for user to fill
 */
struct EH_Material
{
	bool backface_cull;			/**< Use backface cull? */

	/* Diffuse layer */
	float diffuse_weight;		/**< Diffuse color weight, Range: [0, 1], default: 0.7 */
	EH_RGB diffuse_color;		/**< Diffuse color, Range: [0, inf), default: (1 1 1) */  
	EH_Texture diffuse_tex;		/**< Diffuse texture */
	float roughness;			/**< Oren-Nayar based Roughness, Range: [0, 1], default: 0 */
	float backlight;			/**< Lights can illuminate surface from the back, providing a translucent look, Range: [0, 1], default: 0 */

	/* Specular layer */
	float specular_weight;		/**< Specular color weight, Range: [0, 1], default: 0.2 */
	EH_RGB specular_color;		/**< Specular color, Range: [0, inf), default: (1 1 1) */
	EH_Texture specular_tex;	/**< Specular texture */
	float glossiness;			/**< Glossiness, Range: [0, 100], default: 90 */
	float specular_fresnel;		/**< IOR for adjusting Fresnel effect using Schlick's approximation, Range: [0, inf), default: 1.5 */
	float anisotropy;			/**< Lower value gives a more anisotropic look. No anisotropic effect when the value is 1, Range: [0, 1], default: 1 */
	float rotation;				/**< Rotate the anisotropic BRDF around shading normal with given angle in radians, Range: [0, 2*PI], default: 0 */

	/* Transparency layer */
	float transp_weight;		/**< Transparency weight, Range: [0, 1], default: 0 */
	bool transp_invert_weight;	/**< A Boolean, when the value is true, invert transparency weight, making it (1 - transparency_weight) */
	EH_Texture transp_tex;		/**< Transparency texture */

	/* Bump mapping */
	float bump_weight;			/**< bump weight, Range: (-inf, inf), default: 0 */
	EH_Texture bump_tex;		/**< The texture for bump mapping */
	bool normal_bump;			/**< The bump texture is actually a normal map? */

	/* Mirror layer */			/**< Mirror not used */
	float mirror_weight;		/**< Weight of mirror reflection, Range: [0, 1], default: 0 */
	EH_RGB mirror_color;		/**< Tint of mirror reflection, Range: [0, inf), default: (1 1 1) */ 
	float mirror_fresnel;		/**< IOR for adjusting Fresnel effect using Schlick's approximation, Range: [0, inf), default: 1.5 */

	/* Refraction layer */
	float refract_weight;		/**< Weight of refraction, Range: [0, 1], default: 0 */
	bool refract_invert_weight; /**< A Boolean, when the value is true, invert refraction weight, making it (1 - refraction_weight) */
	EH_RGB refract_color;		/**< Tint of refraction, Range: [0, inf), default: (1 1 1) */ 
	float ior;					/**< Index of refraction, which affects both the direction of refracted rays and the Fresnel effect, Range: [0, inf), default: 1.5 */
	float refract_glossiness;	/**< Higher value gives a sharper refraction, while lower value gives a more blurred refraction, Range: [0, 100], default: 100 */

	/* Emission layer */
	float emission_weight;		/**< Intensity of emission, Range: [0, inf), default: 0 */
	EH_RGB emission_color;		/**< Tint of emission, Range: [0, inf), default: (1 1 1) */ 
	EH_Texture emission_tex;	/**< Emission texture */

	/* Displace layer */
	float displace_weight;		/**< displacement mapping weight, Range: [0, inf), default: 0 */
	EH_Texture displace_tex;

	EH_Material() :
		backface_cull(false),
		diffuse_weight(0.8f),
		roughness(0.0f),
		backlight(0.0f),
		specular_weight(0.0f),
		glossiness(0.0f),
		specular_fresnel(1.5f),
		anisotropy(1.0f),
		rotation(0.0f),
		transp_weight(0.0f),
		transp_invert_weight(false),
		bump_weight(0.0f),
		normal_bump(true),
		mirror_weight(0.0f),
		mirror_fresnel(1.5f),
		refract_weight(0.0f),
		refract_invert_weight(false),
		ior(1.5f),
		refract_glossiness(0.0f),
		emission_weight(0.0f),
		displace_weight(0.0f)
	{		
		diffuse_color[0] = 1.0f; /* default diffuse color is red */
		diffuse_color[1] = 0.0f;
		diffuse_color[2] = 0.0f;

		specular_color[0] = 1.0f;
		specular_color[1] = 1.0f;
		specular_color[2] = 1.0f;

		mirror_color[0] = 1.0f;
		mirror_color[1] = 1.0f;
		mirror_color[2] = 1.0f;

		refract_color[0] = 1.0f;
		refract_color[1] = 1.0f;
		refract_color[2] = 1.0f;

		emission_color[0] = 1.0f;
		emission_color[1] = 1.0f;
		emission_color[2] = 1.0f;
	}
};

/** Add a material to the scene.
 * \param name The name of the material.
 * \param mtl The material data.
 */
EH_API void EH_add_material(EH_Context *ctx, const char *name, const EH_Material *mtl);



/** The mesh instance data for user to fill
 */
struct EH_MeshInstance
{
	const char *mesh_name;					/**< The name of the mesh which we reference to */
	EH_Mat mesh_to_world;					/**< Mesh local space to world space transform */
	const char *mtl_names[MAX_NUM_MTLS];	/**< The name array of the materials which we reference to */	

	EH_MeshInstance() :
		mesh_name(NULL)
	{
		memset(mesh_to_world, 0, sizeof(mesh_to_world));
		memset(mtl_names, 0, sizeof(mtl_names));
	}

};

/** Instance a mesh into the scene.
 * \param name The name of the mesh instance
 * \param The mesh instance data
 */
EH_API void EH_add_mesh_instance(EH_Context *ctx, const char *name, const EH_MeshInstance *inst);



/** The instance of an assembly.
 * An assembly is an ESS file exported before hand. You can import its 
 * content into current scene later using assembly instance.
 */
struct EH_AssemblyInstance
{
	const char *filename;	/**< The name of the ESS file to import */
	EH_Mat mesh_to_world;	/**< Assembly local space to world space transform */
};

/** Instance an assembly into the scene.
 * \param name The name of the assembly instance
 * \param The assembly instance data
 */
EH_API void EH_add_assembly_instance(EH_Context *ctx, const char *name, const EH_AssemblyInstance *inst);



/** The light data for user to fill
 */
struct EH_Light
{
	EH_LightType type;
	const char *ies_filename;	/**< Only used for IES light type */
	float intensity;
	EH_Vec2 size;				/**< Use size[0] as radius for sphere light, 
									 Use size[0] as width and use size[1] as 
									 height for quad light */
	EH_Mat light_to_world;		/**< Light local space to world space transform */
	int sample_num_coefficient;    /**< Light sample num coefficient, default value is 1*/
	EH_Vec light_color;

	EH_Light() :
		ies_filename(NULL),
		type(EH_LIGHT_SPHERE),
		sample_num_coefficient(1)
	{
		memset(size, 0, sizeof(EH_Vec2));
		memset(light_to_world, 0, sizeof(light_to_world));

		light_color[0] = 1.0f;
		light_color[1] = 1.0f;
		light_color[2] = 1.0f;
	}
};

/** Add a light to the scene.
 * \param name The name of the light
 * \param lgt The light data.
 */
EH_API void EH_add_light(EH_Context *ctx, const char *name, const EH_Light *lgt);



/** The sky data for user to fill
 */
struct EH_Sky
{
	bool enabled;
	float intensity;
	const char *hdri_name;
	float hdri_rotation; /* in degrees */
	bool enable_emit_GI;

	EH_Sky() :
		enabled(true),
		intensity(1.0f),
		hdri_name(NULL),
		hdri_rotation(0.0f)
	{

	}
};

/** Set the current sky environment.
 */
EH_API void EH_set_sky(EH_Context *ctx, const EH_Sky *sky);



/** The sun data for user to fill
 */
struct EH_Sun
{
	bool enabled;
	float intensity;
	EH_RGB color;
	float soft_shadow;		/**< range is [1.0, 1000.0] */
	EH_Vec2 dir;			/**< dir[0] is theta angle, 
							     dir[1] is phi angle, both in radians */
	EH_Sun() :
		enabled(true),
		intensity(1.0f),
		soft_shadow(1.0f)
	{

	}
};

/** Set the current sun parameters.
 */
EH_API void EH_set_sun(EH_Context *ctx, const EH_Sun *sun);

/** The callback to color buffer during rendering.
	For users to better to deal with color such as exposure, tone mapping and so on,
	color_data in callback WON'T clamp to range of [0,1].
	MUST CLAMP the color_data to range of [0,1] WHEN SHOW the pixels.
 */
typedef void (*EH_display_callback)(uint_t width, uint_t height, const EH_RGBA *color_data);

/** Set display callback
 */
EH_API void EH_set_display_callback(EH_Context *ctx, EH_display_callback cb);

/** Start rendering
*/
EH_API bool EH_start_render(EH_Context *ctx, const char *ess_name, bool is_interactive);

/** Stop rendering
*/
EH_API void EH_stop_render(EH_Context *ctx);
