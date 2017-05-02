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
#include <ei_data_table.h>
#include <ei_license.h>
#include <ei_verbose.h>
#include <ei_base_bucket.h>
#include <ei_timer.h>
#include <string>

#ifdef _WIN32
	#include <Windows.h>
	#include <shellapi.h>
#endif

#include "esslib.h"

const int GET_RENDER_DATA_PERIOD = 100;

eiBool g_abort_render = EI_FALSE;

/* 是否显示面光源，调试用 */
eiBool g_show_portal_light_area = EI_FALSE;

static void rprocess_pass_started(eiProcess *process, eiInt pass_id);
static void rprocess_pass_finished(eiProcess *process, eiInt pass_id);
static void rprocess_job_started(eiProcess *process, const eiTag job, const eiThreadID threadId);
static void rprocess_job_finished(eiProcess *process, const eiTag job, const eiInt job_state, const eiThreadID threadId);
static void rprocess_info(eiProcess *process, const char *text);

struct EHRenderProcess
{
	eiProcess					base;
	eiThreadHandle				renderThread;
	eiRWLock					*bufferLock;
	std::vector<eiColor>		originalBuffer;
	eiInt						imageWidth;
	eiInt						imageHeight;
	eiRenderParameters			*render_params;
	eiBool						interactive;
	eiBool						progressive;
	eiBool						target_set;
	eiVector					up_vector;
	eiVector					camera_target;
	eiScalar					min_target_dist;
	eiScalar					last_job_percent;
	eiTimer						first_pixel_timer;
	eiBool						is_first_pass;
	EH_LogCallback              log_cb;

	EHRenderProcess(
		eiInt res_x, 
		eiInt res_y, 
		eiRenderParameters *_render_params, 
		eiBool _interactive, 
		eiBool _progressive)
	{
		init_callbacks();
		renderThread = NULL;
		bufferLock = ei_create_rwlock();
		imageWidth = res_x;
		imageHeight = res_y;
		render_params = _render_params;
		interactive = _interactive;
		progressive = _progressive;
		target_set = EI_FALSE;
		up_vector = ei_vector(0.0f, 0.0f, 1.0f);
		camera_target = 0.0f;
		min_target_dist = 0.0f;
		last_job_percent = 0.0f;
		const eiColor blackColor = ei_color(0.0f);
		originalBuffer.resize(imageWidth * imageHeight, blackColor);
		log_cb = NULL;
	}

	~EHRenderProcess()
	{
		ei_delete_rwlock(bufferLock);
	}

	void init_callbacks()
	{
		base.pass_started = rprocess_pass_started;
		base.pass_finished = rprocess_pass_finished;
		base.job_started = rprocess_job_started;
		base.job_finished = rprocess_job_finished;
		base.info = rprocess_info;
	}

	void update_render_view(eiInt frameWidth, eiInt frameHeight)
	{
		eiInt cropWidth = imageWidth;
		eiInt cropHeight = imageHeight;
		eiInt offsetX = 0;
		if (frameWidth > imageWidth)
		{
			offsetX = (frameWidth - imageWidth) / 2;
		}
		eiInt offsetY = 0;
		if (frameHeight > imageHeight)
		{
			offsetY = (frameHeight - imageHeight) / 2;
		}
		offsetY = frameHeight - (offsetY + imageHeight);
		const eiColor *pixels = &(originalBuffer[0]);
		if (offsetY < 0)
		{
			pixels += (-offsetY * imageWidth);
			cropHeight -= (-offsetY);
			offsetY = 0;
		}
		ei_display_clear_viewport(frameWidth, frameHeight);
		ei_write_lock(bufferLock);
		{
			ei_display_draw_pixels(offsetX, offsetY, cropWidth, cropHeight, pixels);
		}
		ei_write_unlock(bufferLock);
	}
};

static void rprocess_pass_started(eiProcess *process, eiInt pass_id)
{
}

static void rprocess_pass_finished(eiProcess *process, eiInt pass_id)
{
	EHRenderProcess *rp = (EHRenderProcess *)process;

	if (rp->is_first_pass)
	{
		ei_timer_stop(&(rp->first_pixel_timer));
		printf("Time to first pass: %d ms\n", rp->first_pixel_timer.duration);
		rp->is_first_pass = EI_FALSE;
	}
}

const eiInt TARGET_LEN = 4;
const eiColor whiteColor = {1.0f, 1.0f, 1.0f};

static void rprocess_job_started(
	eiProcess *process, 
	const eiTag job, 
	const eiThreadID threadId)
{
	EHRenderProcess *rp = (EHRenderProcess *)process;

	if (rp->progressive)
	{
		return;
	}

	if (ei_db_type(job) != EI_TYPE_JOB_BUCKET)
	{
		return;
	}

	eiDataAccessor<eiBucketJob> pJob(job);
	if (pJob->pass_id <= EI_PASS_GI_CACHE_PROGRESSIVE)
	{
		return;
	}

	const eiInt left = pJob->rect.left;
	const eiInt right = pJob->rect.right;
	const eiInt top = pJob->rect.top;
	const eiInt bottom = pJob->rect.bottom;
	const eiInt imageWidth = rp->imageWidth;
	const eiInt imageHeight = rp->imageHeight;
	eiColor *originalBuffer = &(rp->originalBuffer[0]);
	ei_read_lock(rp->bufferLock);
	{
		for (eiInt j = left; j < left + TARGET_LEN; ++j)
		{
			originalBuffer[(imageHeight - 1 - top) * imageWidth + j] = whiteColor;
			originalBuffer[(imageHeight - 1 - bottom) * imageWidth + j] = whiteColor;
		}
		for (eiInt j = right; j > right - TARGET_LEN; --j)
		{
			originalBuffer[(imageHeight - 1 - top) * imageWidth + j] = whiteColor;
			originalBuffer[(imageHeight - 1 - bottom) * imageWidth + j] = whiteColor;
		}
		for (eiInt j = top; j < top + TARGET_LEN; ++j)
		{
			originalBuffer[(imageHeight - 1 - j) * imageWidth + left] = whiteColor;
			originalBuffer[(imageHeight - 1 - j) * imageWidth + right] = whiteColor;
		}
		for (eiInt j = bottom; j > bottom - TARGET_LEN; --j)
		{
			originalBuffer[(imageHeight - 1 - j) * imageWidth + left] = whiteColor;
			originalBuffer[(imageHeight - 1 - j) * imageWidth + right] = whiteColor;
		}
	}
	ei_read_unlock(rp->bufferLock);
}

static void rprocess_job_finished(
	eiProcess *process, 
	const eiTag job, 
	const eiInt job_state, 
	const eiThreadID threadId)
{
	EHRenderProcess *rp = (EHRenderProcess *)process;

	if (ei_db_type(job) != EI_TYPE_JOB_BUCKET)
	{
		return;
	}

	eiDataAccessor<eiBucketJob> pJob(job);
	if (job_state == EI_JOB_CANCELLED)
	{
		return;
	}

	if (pJob->pass_id <= EI_PASS_GI_CACHE_PROGRESSIVE)
	{
		eiInt GI_cache_samples = (EI_PASS_GI_CACHE_PROGRESSIVE + 1) - pJob->pass_id;
		if ((GI_cache_samples % 4) != 0)
		{
			return;
		}
	}

	eiFrameBufferCache	infoFrameBufferCache;
	eiFrameBufferCache	colorFrameBufferCache;

	ei_framebuffer_cache_init(
		&infoFrameBufferCache, 
		pJob->infoFrameBuffer, 
		pJob->pos_i, 
		pJob->pos_j, 
		pJob->point_spacing, 
		pJob->pass_id, 
		NULL);
	ei_framebuffer_cache_init(
		&colorFrameBufferCache, 
		pJob->colorFrameBuffer, 
		pJob->pos_i, 
		pJob->pos_j, 
		pJob->point_spacing, 
		pJob->pass_id, 
		&infoFrameBufferCache);

	const eiRect4i & fb_rect = infoFrameBufferCache.m_rect;

	/* write bucket updates into the original buffer */
	const eiInt imageWidth = rp->imageWidth;
	const eiInt imageHeight = rp->imageHeight;
	eiColor *originalBuffer = &(rp->originalBuffer[0]);
	originalBuffer += ((imageHeight - 1 - pJob->rect.top) * imageWidth + pJob->rect.left);
	ei_read_lock(rp->bufferLock);
	{
		for (eiInt j = fb_rect.top; j < fb_rect.bottom; ++j)
		{
			for (eiInt i = fb_rect.left; i < fb_rect.right; ++i)
			{
				ei_framebuffer_cache_get_final(
					&colorFrameBufferCache, 
					i, 
					j, 
					&(originalBuffer[i - fb_rect.left]));
			}
			originalBuffer -= imageWidth;
		}
	}
	ei_read_unlock(rp->bufferLock);

	ei_framebuffer_cache_exit(&colorFrameBufferCache);
	ei_framebuffer_cache_exit(&infoFrameBufferCache);
}

static void rprocess_info(
	eiProcess *process, 
	const char *text)
{
	EHRenderProcess *rp = (EHRenderProcess *)process;
	if (rp->log_cb)
	{
		rp->log_cb(EH_INFO, text);
	}
}


char * EH_utf16_to_utf8(const wchar_t *str)
{
#ifdef _WIN32
	std::string utf8;
	utf8.resize(WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL));
	WideCharToMultiByte(CP_UTF8, 0, str, -1, &utf8[0], (int)utf8.size(), NULL, NULL);
	const int buffer_size = utf8.size() + 1;
	char *ret_str = new char[buffer_size];
	memset(ret_str, 0, sizeof(ret_str));
	memcpy(ret_str, utf8.c_str(), buffer_size);
	return ret_str;
#endif
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
	delete reinterpret_cast<EssExporter*>(ctx);
}

void EH_begin_export(EH_Context *ctx, const char *filename, const EH_ExportOptions *opt)
{
	reinterpret_cast<EssExporter*>(ctx)->BeginExport(std::string(filename), *opt, false);
}

void EH_end_export(EH_Context *ctx)
{
	reinterpret_cast<EssExporter*>(ctx)->EndExport();
}

void EH_set_log_callback(EH_Context *ctx, EH_LogCallback cb)
{
	reinterpret_cast<EssExporter*>(ctx)->log_callback = cb;
}

void EH_set_progress_callback(EH_Context *ctx, EH_ProgressCallback cb)
{
	reinterpret_cast<EssExporter*>(ctx)->progress_callback = cb;
}

void EH_set_render_options(EH_Context *ctx, const EH_RenderOptions *opt)
{
	switch(opt->quality)
	{
	case EH_MEDIUM:
		reinterpret_cast<EssExporter*>(ctx)->AddMediumOption();
		break;
	case EH_FAST:
		reinterpret_cast<EssExporter*>(ctx)->AddLowOption();
		break;
	case EH_HIGH:
		reinterpret_cast<EssExporter*>(ctx)->AddHighOption();
		break;
	default:
		printf("Not support other options now!\n");
		break;
	}
}

void EH_set_custom_render_options(EH_Context *ctx, const EH_CustomRenderOptions *opt)
{
	reinterpret_cast<EssExporter*>(ctx)->AddCustomOption(*opt);
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
	reinterpret_cast<EssExporter*>(ctx)->AddLight(*lgt, std::string(name), g_show_portal_light_area);
}

void EH_set_sky(EH_Context *ctx, const EH_Sky *sky)
{
	reinterpret_cast<EssExporter*>(ctx)->AddBackground(std::string(sky->hdri_name), sky->hdri_rotation, sky->intensity);
}

void EH_set_sun(EH_Context *ctx, const EH_Sun *sun)
{
	reinterpret_cast<EssExporter*>(ctx)->AddSun(*sun);
}

EI_THREAD_FUNC render_callback(void *param)
{
	eiRenderParameters *render_params = (eiRenderParameters *)param;

	ei_job_register_thread();

	ei_render_run(render_params->root_instgroup, render_params->camera_inst, render_params->options);

	ei_job_unregister_thread();

	return (EI_THREAD_FUNC_RESULT)EI_TRUE;
}

bool WindowProcOnRendering(void *param, bool is_abort_render, EH_display_callback display_cb, EH_ProgressCallback progress_cb)
{
	ei_sleep(GET_RENDER_DATA_PERIOD);

	EHRenderProcess *rp = (EHRenderProcess *)param;

	if (is_abort_render)
	{
		//ei_job_abort(EI_TRUE);
		return false;
	}

	const eiScalar job_percent = (eiScalar)ei_job_get_percent();	

	if (display_cb)
	{
		int color_num = rp->originalBuffer.size();
		EH_RGBA *color_data = new EH_RGBA[color_num];
		memset(color_data, 0, sizeof(color_data));
		for(int i = 0; i < color_num; ++i)
		{
			color_data[i][0] = rp->originalBuffer[i].r;
			color_data[i][1] = rp->originalBuffer[i].g;
			color_data[i][2] = rp->originalBuffer[i].b;
			//color_data[i][3] = rp->opacityBuffer[i];
			color_data[i][3] = 1.0f;
		}

		display_cb(rp->imageWidth, rp->imageHeight, color_data);
		delete []color_data;
	}

	if (progress_cb)
	{
		progress_cb(job_percent);
	}

	if (!ei_job_aborted())
	{
		/* display render progress */
		if (absf(rp->last_job_percent - job_percent) >= 0.5f)
		{
			printf("Render progress: %.1f %% ...\n", job_percent);
			fflush(stdout);
			rp->last_job_percent = job_percent;
		}
	}
	//return job_percent < 100.0f;
	return job_percent < 99.5f; //到不了100%，后面看看
}

void EH_set_display_callback(EH_Context *ctx, EH_display_callback cb)
{
	reinterpret_cast<EssExporter*>(ctx)->display_callback = cb;
}

bool EH_start_render(EH_Context *ctx, const char *ess_name, bool is_interactive)
{	
	bool ret = true;
	ei_context();

	if (ess_name != NULL)
	{		
		ei_info("Start parsing file: %s\n", ess_name);

		if (!ei_parse2(ess_name, is_interactive))
		{
			ei_error("Failed to parse file: %s\n", ess_name);

			ret = false;
		}

		ei_info("Finished parsing file: %s\n", ess_name);
		
		ei_info("Start display and rendering...\n");
		eiRenderParameters render_params;
		memset(&render_params, 0, sizeof(render_params));
		eiBool get_render_params = EI_FALSE;			
		get_render_params = ei_get_last_render_params(&render_params);

		if (!get_render_params)
		{
			ei_error("Cannot get last render parameters.\n");
		}
		else
		{
			eiTag cam_inst_tag = ei_find_node(render_params.camera_inst);
			if (cam_inst_tag != EI_NULL_TAG)
			{
				eiDataAccessor<eiNode> cam_inst(cam_inst_tag);
				eiTag cam_item_tag = ei_node_get_node(cam_inst.get(), ei_node_find_param(cam_inst.get(), "element"));
				if (cam_item_tag != EI_NULL_TAG)
				{
					eiDataAccessor<eiNode> cam_item(cam_item_tag);
					eiInt res_x = ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_x"));
					eiInt res_y = ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_y"));
					eiBool progressive = EI_FALSE;
					eiTag opt_item_tag = ei_find_node(render_params.options);
					if (opt_item_tag != EI_NULL_TAG)
					{
						eiDataAccessor<eiNode> opt_item(opt_item_tag);
						progressive = ei_node_get_bool(opt_item.get(), ei_node_find_param(opt_item.get(), "progressive"));
					}

					progressive = EI_TRUE;
					EHRenderProcess rp(res_x, res_y, &render_params, is_interactive, progressive);
					rp.log_cb = reinterpret_cast<EssExporter*>(ctx)->log_callback;

					if (is_interactive)
					{
						ei_verbose("warning");

						if (ei_find_node(render_params.options) != EI_NULL_TAG)
						{
							eiBool need_init;
							eiNode *opt_node = ei_edit_node(render_params.options, &need_init);

							ei_node_enum(opt_node, "accel_mode", "large");

							eiInt max_samples = 1;
							eiIndex max_samples_pid = ei_node_find_param(opt_node, "max_samples");
							if (max_samples_pid != EI_NULL_TAG)
							{
								max_samples = ei_node_get_int(opt_node, max_samples_pid);
							}
							printf("AA samples: %d\n", max_samples);

							eiInt diffuse_samples = 1;
							eiIndex diffuse_samples_pid = ei_node_find_param(opt_node, "diffuse_samples");
							if (diffuse_samples_pid != EI_NULL_TAG)
							{
								diffuse_samples = ei_node_get_int(opt_node, diffuse_samples_pid);
							}
							printf("Diffuse samples: %d\n", diffuse_samples);

							eiInt sss_samples = 1;
							eiIndex sss_samples_pid = ei_node_find_param(opt_node, "sss_samples");
							if (sss_samples_pid != EI_NULL_TAG)
							{
								sss_samples = ei_node_get_int(opt_node, sss_samples_pid);
							}
							printf("SSS samples: %d\n", sss_samples);

							eiInt volume_indirect_samples = 1;
							eiIndex volume_indirect_samples_pid = ei_node_find_param(opt_node, "volume_indirect_samples");
							if (volume_indirect_samples_pid != EI_NULL_TAG)
							{
								volume_indirect_samples = ei_node_get_int(opt_node, volume_indirect_samples_pid);
							}
							printf("Volume indirect samples: %d\n", volume_indirect_samples);

							eiInt random_lights = 1;
							eiIndex random_lights_pid = ei_node_find_param(opt_node, "random_lights");
							if (random_lights_pid != EI_NULL_TAG)
							{
								random_lights = ei_node_get_int(opt_node, random_lights_pid);
							}
							printf("Random lights: %d\n", random_lights);

							eiInt max_dist_samples = max(diffuse_samples, max(sss_samples, volume_indirect_samples));
							if (max_samples > 16)
							{
								max_samples *= max(1, max_dist_samples / (max_samples / 16));
							}
							else
							{
								max_samples *= max_dist_samples;
							}

							printf("Interactive samples: %d\n", max_samples);
							ei_node_set_int(opt_node, max_samples_pid, max_samples);

							ei_node_int(opt_node, "diffuse_samples", 1);
							ei_node_int(opt_node, "sss_samples", 1);
							ei_node_int(opt_node, "volume_indirect_samples", 1);
							if (random_lights <= 0 || random_lights > 16)
							{
								ei_node_int(opt_node, "random_lights", 16);
							}
							ei_node_bool(opt_node, "progressive", EI_TRUE);

							ei_end_edit_node(opt_node);
						}
					}

					ei_job_set_process(&(rp.base));
					ei_timer_reset(&(rp.first_pixel_timer));
					ei_timer_start(&(rp.first_pixel_timer));
					rp.is_first_pass = EI_TRUE;
					ei_render_prepare();
					{
						rp.renderThread = ei_create_thread(render_callback, &render_params, NULL);
						ei_set_low_thread_priority(rp.renderThread);							

						EH_display_callback display_cb = reinterpret_cast<EssExporter*>(ctx)->display_callback;
						EH_ProgressCallback progress_cb = reinterpret_cast<EssExporter*>(ctx)->progress_callback;

						while(WindowProcOnRendering(&rp, g_abort_render, 
							display_cb, 
							progress_cb))
						{

						}

						ei_wait_thread(rp.renderThread);
						ei_delete_thread(rp.renderThread);
						rp.renderThread = NULL;
					}
					ei_render_cleanup();
					ei_job_set_process(NULL);
				}
			}
		}

		ei_info("Finished display and rendering.\n");
	}
	else
	{
		ei_error("Scene file is not specified.\n");

		ret = false;
	}

	ei_end_context();

	return ret;
}

void EH_stop_render(EH_Context *ctx)
{
	g_abort_render = EI_TRUE;
	ei_job_abort(EI_TRUE);
}
