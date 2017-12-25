/**************************************************************************
 * Copyright (C) 2015 Rendease Co., Ltd.
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
#include <ei_license.h>
#include <ei_verbose.h>
#include <ei_base_bucket.h>
#include <ei_timer.h>
#include <vector>
#include <deque>
#include <csignal>
#include <ctime>

static const char *g_str_on = "on";

#define MSG_QUEUE_WAIT_TIME		50

EI_API void ei_dongle_license_set(eiUint code1, eiUint code2, const char *license_code);

struct RenderProcess
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
	eiBool						debug_adaptive;
	eiVector					obj_center;
	eiVector					up_vector;
	eiVector					camera_target;
	eiScalar					min_target_dist;
	eiScalar					last_job_percent;
	eiTimer						first_pixel_timer;
	eiBool						is_first_pass;
	std::string					aov_name;
	eiBool						is_idle;
	eiInt						start_idle_time;
	eiInt						idle_threshold;
	eiBool						last_render_is_interactive;
	eiInt						org_engine;
	eiInt						org_min_samples;
	eiInt						org_max_samples;
	eiInt						org_diffuse_samples;
	eiInt						org_sss_samples;
	eiInt						org_volume_indirect_samples;
	eiInt						org_random_lights;
	eiScalar					org_light_sample_quality;
	eiInt						org_progressive;

	RenderProcess(
		eiInt res_x, 
		eiInt res_y, 
		eiRenderParameters *_render_params, 
		eiBool _interactive, 
		eiBool _progressive, 
		eiBool _debug_adaptive,
		const std::string & _aov_name)
	{
		init_callbacks();
		renderThread = NULL;
		bufferLock = ei_create_rwlock();
		imageWidth = res_x;
		imageHeight = res_y;
		render_params = _render_params;
		interactive = _interactive;
		progressive = _progressive;
		debug_adaptive = _debug_adaptive;
		target_set = EI_FALSE;
		obj_center = 0.0f;
		up_vector = ei_vector(0.0f, 0.0f, 1.0f);
		camera_target = 0.0f;
		min_target_dist = 0.0f;
		last_job_percent = 0.0f;
		const eiColor blackColor = ei_color(0.0f);
		originalBuffer.resize(imageWidth * imageHeight, blackColor);
		aov_name = _aov_name;
		is_idle = EI_TRUE;
		start_idle_time = ei_get_time();
		idle_threshold = 3;
		last_render_is_interactive = FALSE;
		org_engine = EI_ENGINE_HYBRID_PATH_TRACER;
		org_min_samples = -3;
		org_max_samples = 1;
		org_diffuse_samples = 1;
		org_sss_samples = 1;
		org_volume_indirect_samples = 1;
		org_random_lights = 16;
		org_light_sample_quality = 0.05f;
		org_progressive = EI_FALSE;
	}

	~RenderProcess()
	{
		ei_delete_rwlock(bufferLock);
	}

	void init_callbacks();
	
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

static eiUint custom_trace(
	eiProcess *process, 
	eiTLS *tls, 
	eiBaseBucket *bucket, 
	eiTag scene_tag)
{
	RenderProcess *rp = (RenderProcess *)process;

	/* currently we are simply using the center of the entire scene 
	   to facilitate the computation of camera target */
	/* TODO: for more advanced navigation, you can call ei_rt_trace 
	   in this function to perform hit-testing with the scene, and 
	   use the center of the hit object instance */
	eiBBox bbox = ei_bbox();
	ei_rt_scene_box(scene_tag, &bbox);
	bbox.center(rp->obj_center);

	/* since we only need to re-compute one time, clear ourselves 
	   once we are done */
	ei_set_custom_trace(NULL);
	/* issue target re-computation */
	rp->target_set = EI_FALSE;

	/* TODO: remember to return the number of rays shot in this 
	   function if you called ei_rt_trace */
	return 0;
}

static void rprocess_pass_started(eiProcess *process, eiInt pass_id)
{
}

static void rprocess_pass_finished(eiProcess *process, eiInt pass_id)
{
	RenderProcess *rp = (RenderProcess *)process;

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
	RenderProcess *rp = (RenderProcess *)process;

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
	RenderProcess *rp = (RenderProcess *)process;
	
	if (ei_db_type(job) != EI_TYPE_JOB_BUCKET)
	{
		return;
	}

	eiDataAccessor<eiBucketJob> pJob(job);
	if (job_state == EI_JOB_CANCELLED)
	{
		return;
	}

	eiFrameBufferCache	infoBuffer;
	eiFrameBufferCache	sourceBuffer;

	ei_framebuffer_cache_init(
		&infoBuffer, 
		pJob->infoFrameBuffer, 
		pJob->pos_i, 
		pJob->pos_j, 
		pJob->point_spacing, 
		pJob->pass_id, 
		NULL);

	if (rp->aov_name.empty() || rp->aov_name == "color")
	{
		ei_framebuffer_cache_init(
			&sourceBuffer, 
			pJob->colorFrameBuffer, 
			pJob->pos_i, 
			pJob->pos_j, 
			pJob->point_spacing, 
			pJob->pass_id, 
			&infoBuffer);
	}
	else if (rp->aov_name == "opacity")
	{
		ei_framebuffer_cache_init(
			&sourceBuffer, 
			pJob->opacityFrameBuffer, 
			pJob->pos_i, 
			pJob->pos_j, 
			pJob->point_spacing, 
			pJob->pass_id, 
			&infoBuffer);
	}
	else
	{
		eiDataTableAccessor<eiTag> frameBuffers_iter(pJob->frameBuffers);
		bool foundFrameBuffer = false;

		for (eiInt i = 0; i < frameBuffers_iter.size(); ++i)
		{
			eiTag frameBufferTag = frameBuffers_iter.get(i);

			eiDataAccessor<eiFrameBuffer> frameBuffer(frameBufferTag);

			if (rp->aov_name == ei_framebuffer_get_name(frameBuffer.get()))
			{
				ei_framebuffer_cache_init(
					&sourceBuffer, 
					frameBufferTag, 
					pJob->pos_i, 
					pJob->pos_j, 
					pJob->point_spacing, 
					pJob->pass_id, 
					&infoBuffer);
				foundFrameBuffer = true;
				break;
			}
		}

		if (!foundFrameBuffer)
		{
			ei_framebuffer_cache_exit(&infoBuffer);
			return;
		}
	}

	const eiRect4i & fb_rect = infoBuffer.m_rect;

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
				if (rp->debug_adaptive)
				{
					const eiInt cur_samples_num = pJob->pass_id + 2;
					eiScalar h = 0;
					if (cur_samples_num > 1)
					{
						eiPixelInfo info_dest;
						ei_framebuffer_cache_get(&infoBuffer, i, j, &info_dest);
						h = 2.0f * (1.0f - eiScalar(info_dest.num_samples) / eiScalar(cur_samples_num)) / 3.0f;
						if (h > 1) h -= 1;
					}
					originalBuffer[i - fb_rect.left] = ei_hsv_to_rgb(h, 1, 1);
				}
				else
				{
					ei_framebuffer_cache_get_final(
						&sourceBuffer, 
						i, 
						j, 
						&(originalBuffer[i - fb_rect.left]));
				}
			}
			originalBuffer -= imageWidth;
		}
	}
	ei_read_unlock(rp->bufferLock);

	ei_framebuffer_cache_exit(&sourceBuffer);
	ei_framebuffer_cache_exit(&infoBuffer);
}

static void rprocess_info(
	eiProcess *process, 
	const char *text)
{
}

void RenderProcess::init_callbacks()
{
	base.pass_started = rprocess_pass_started;
	base.pass_finished = rprocess_pass_finished;
	base.job_started = rprocess_job_started;
	base.job_finished = rprocess_job_finished;
	base.info = rprocess_info;
}

static EI_THREAD_FUNC render_callback(void *param)
{
	eiRenderParameters *render_params = (eiRenderParameters *)param;

	ei_job_register_thread();

	ei_render_run(render_params->root_instgroup, render_params->camera_inst, render_params->options);

	ei_job_unregister_thread();

	return (EI_THREAD_FUNC_RESULT)EI_TRUE;
}

static void display_callback(eiInt frameWidth, eiInt frameHeight, void *param)
{
	RenderProcess *rp = (RenderProcess *)param;

	ei_sleep(MSG_QUEUE_WAIT_TIME);

	if (rp->interactive)
	{
		eiScalar offset[2];
		eiInt drag_button;
		eiInt drag_mode = ei_display_get_mouse_move(offset, &drag_button);

		if (drag_mode == EI_DRAG_MODE_NONE)
		{
			ei_display_get_scroll_offset(offset);
			offset[0] *= -20.0f;
			offset[1] *= -20.0f;
			drag_mode = EI_DRAG_MODE_SHIFT;
			drag_button = EI_DRAG_BUTTON_LEFT;
		}

		eiBool has_user_action = (
			drag_mode != EI_DRAG_MODE_NONE && 
			drag_button != EI_DRAG_BUTTON_RIGHT && 
			(offset[0] != 0.0f || offset[1] != 0.0f));

		eiBool idle_timeout = EI_FALSE;
		if (has_user_action)
		{
			/* clear idle state if user has actions */
			rp->is_idle = EI_FALSE;
		}
		else
		{
			/* enter idle time if we don't have user actions */
			eiInt current_time = ei_get_time();
			if (!rp->is_idle)
			{
				rp->start_idle_time = current_time;
				rp->is_idle = EI_TRUE;
			}
			else if ((current_time - rp->start_idle_time) > rp->idle_threshold * 1000)
			{
				rp->start_idle_time = current_time;
				idle_timeout = EI_TRUE;
			}
		}

		eiBool switch_to_final_render = (
			rp->last_render_is_interactive && 
			idle_timeout && 
			!rp->is_first_pass);

		if (has_user_action || switch_to_final_render)
		{
			if (rp->renderThread != NULL) /* is rendering */
			{
				/* cancel current rendering */
				ei_job_abort(EI_TRUE);
				ei_wait_thread(rp->renderThread);
				ei_delete_thread(rp->renderThread);
				rp->renderThread = NULL;

				ei_render_cleanup();
			}

			/* modify options parameters */
			const char *opt_name = rp->render_params->options;
			if (opt_name != NULL)
			{
				eiBool need_init;
				eiNode *opt_node = ei_edit_node(opt_name, &need_init);

				if (switch_to_final_render)
				{
					ei_node_int(opt_node, "engine", rp->org_engine);
					ei_node_int(opt_node, "min_samples", rp->org_min_samples);
					ei_node_int(opt_node, "max_samples", rp->org_max_samples);
					ei_node_int(opt_node, "diffuse_samples", rp->org_diffuse_samples);
					ei_node_int(opt_node, "sss_samples", rp->org_sss_samples);
					ei_node_int(opt_node, "volume_indirect_samples", rp->org_volume_indirect_samples);
					ei_node_int(opt_node, "random_lights", rp->org_random_lights);
					ei_node_scalar(opt_node, "light_sample_quality", rp->org_light_sample_quality);
					ei_node_bool(opt_node, "progressive", rp->org_progressive);
					rp->last_render_is_interactive = EI_FALSE;
				}
				else
				{
					ei_node_int(opt_node, "engine", EI_ENGINE_HYBRID_PATH_TRACER);
					ei_node_int(opt_node, "min_samples", -3);
					ei_node_int(opt_node, "max_samples", 1);
					ei_node_int(opt_node, "diffuse_samples", 1);
					ei_node_int(opt_node, "sss_samples", 1);
					ei_node_int(opt_node, "volume_indirect_samples", 1);
					ei_node_int(opt_node, "random_lights", 16);
					ei_node_scalar(opt_node, "light_sample_quality", 0.05f);
					ei_node_bool(opt_node, "progressive", EI_TRUE);
					rp->last_render_is_interactive = EI_TRUE;
				}

				ei_end_edit_node(opt_node);
			}

			/* modify viewport parameters */
			const char *cam_inst_name = rp->render_params->camera_inst;
			if (cam_inst_name != NULL)
			{
				eiBool need_init;
				eiNode *cam_inst = ei_edit_node(cam_inst_name, &need_init);

				eiIndex xform_pid = ei_node_find_param(cam_inst, "transform");
				eiIndex motion_xform_pid = ei_node_find_param(cam_inst, "motion_transform");
				eiMatrix xform_val = *ei_node_get_matrix(cam_inst, xform_pid);

				eiVector camera_pos = point_transform(ei_vector(0.0f, 0.0f, 0.0f), xform_val);
				eiVector cam_up = normalize(vector_transform(ei_vector(0.0f, 1.0f, 0.0f), xform_val));
				eiVector cam_right = normalize(vector_transform(ei_vector(1.0f, 0.0f, 0.0f), xform_val));
				eiVector cam_dir = normalize(vector_transform(ei_vector(0.0f, 0.0f, -1.0f), xform_val));
				if (!rp->target_set) /* initialize camera to align with original view */
				{
					eiVector abs_cam_up = ei_vector(absf(cam_up.x), absf(cam_up.y), absf(cam_up.z));
					eiInt up_axis = abs_cam_up.max_axis();
					rp->up_vector = 0.0f;
					rp->up_vector[up_axis] = sign(cam_up[up_axis]);
					printf("Up vector set to [%f %f %f]\n", rp->up_vector.x, rp->up_vector.y, rp->up_vector.z);
					/* use absolute value to ensure camera target is always in front of camera direction */
					eiScalar focal_dist = absf(point_plane_dist(camera_pos, -cam_dir, rp->obj_center));
					printf("Initial focal distance: %f\n", focal_dist);
					rp->camera_target = camera_pos + cam_dir * focal_dist;
					rp->min_target_dist = 0.01f * dist(camera_pos, rp->camera_target);
					rp->target_set = EI_TRUE;
				}
				eiVector target_vec = camera_pos - rp->camera_target;
				eiScalar target_dist = normalize_len(target_vec, target_vec);

				if ((drag_button == EI_DRAG_BUTTON_LEFT && drag_mode == EI_DRAG_MODE_CTRL) || 
					(drag_button == EI_DRAG_BUTTON_MIDDLE && drag_mode == EI_DRAG_MODE_NORMAL)) /* pan */
				{
					rp->camera_target += cam_right * (-0.001f * offset[0] * max(rp->min_target_dist, target_dist));
					rp->camera_target += cam_up * (0.001f * offset[1] * max(rp->min_target_dist, target_dist));
				}
				else if (drag_button == EI_DRAG_BUTTON_LEFT && drag_mode == EI_DRAG_MODE_SHIFT) /* zoom */
				{
					target_dist += 0.002f * offset[1] * max(rp->min_target_dist, target_dist);
				}
				else
				{
					/* make horizontal speed slower when approaching up vector */
					eiScalar horiz_speed = 1.0f - 0.7f * absf(dot(rp->up_vector, target_vec));
					target_vec = vector_transform(target_vec, rotate(radians(offset[0] * -0.2f * horiz_speed), cam_up));
					target_vec = vector_transform(target_vec, rotate(radians(offset[1] * -0.2f), cam_right));
				}

				camera_pos = rp->camera_target + target_vec * target_dist;
				eiVector camera_dir_z = target_vec;
				eiVector camera_dir_x = cross(rp->up_vector, camera_dir_z);
				if (absf(dot(rp->up_vector, camera_dir_z)) > 0.99f) /* fix up vector precision issue */
				{
					eiVector fixed_up = cross(camera_dir_z, cam_right);
					camera_dir_x = cross(fixed_up, camera_dir_z);
				}
				if (dot(cam_right, camera_dir_x) < 0.0f) /* fix sudden flip when approaching up vector */
				{
					camera_dir_x = - camera_dir_x;
				}
				eiVector camera_dir_y = cross(camera_dir_z, camera_dir_x);

				xform_val = ei_matrix(
					camera_dir_x.x, camera_dir_x.y, camera_dir_x.z, 0.0f, 
					camera_dir_y.x, camera_dir_y.y, camera_dir_y.z, 0.0f, 
					camera_dir_z.x, camera_dir_z.y, camera_dir_z.z, 0.0f, 
					camera_pos.x, camera_pos.y, camera_pos.z, 1.0f);

				ei_node_set_matrix(cam_inst, xform_pid, &xform_val);
				ei_node_set_matrix(cam_inst, motion_xform_pid, &xform_val);

				ei_end_edit_node(cam_inst);
			}

			/* start a new rendering */
			ei_timer_reset(&(rp->first_pixel_timer));
			ei_timer_start(&(rp->first_pixel_timer));
			rp->is_first_pass = EI_TRUE;
			ei_render_prepare();

			rp->renderThread = ei_create_thread(render_callback, rp->render_params, NULL);
			ei_set_low_thread_priority(rp->renderThread);
		}
	}
	
	if (!ei_job_aborted())
	{
		/* display render progress */
		const eiScalar job_percent = (eiScalar)ei_job_get_percent();
		if (absf(rp->last_job_percent - job_percent) >= 0.5f)
		{
			printf("Render progress: %.1f %% ...\n", job_percent);
			rp->last_job_percent = job_percent;
		}
	}
	
	rp->update_render_view(frameWidth, frameHeight);
}

// convert_native_arguments is used to convert native argument 
// strings into UTF-8 strings which is required by the core.
// The implementation is modified from OpenImageIO project:
// https://github.com/OpenImageIO/oiio
//
#ifdef _WIN32
#include <shellapi.h>
std::vector<std::string> argvList;
std::string utf16_to_utf8(const WCHAR* str)
{
	std::string utf8;
	utf8.resize(WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL));
	WideCharToMultiByte(CP_UTF8, 0, str, -1, &utf8[0], (int)utf8.size(), NULL, NULL);
	return utf8;
}
#endif

void convert_native_arguments(int argc, const char *argv[])
{
#ifdef _WIN32
    // Windows only, standard main() entry point does not accept unicode file
    // paths, here we retrieve wide char arguments and convert them to utf8
    if (argc == 0)
        return;

    int native_argc;
    wchar_t **native_argv = CommandLineToArgvW(GetCommandLineW(), &native_argc);

    if (!native_argv || native_argc != argc)
        return;

    for (int i = 0; i < argc; i++) {
        std::string utf8_arg = utf16_to_utf8(native_argv[i]);
        argvList.push_back(utf8_arg);
    }
    for (int i = 0; i < argc; i++) {
        argv[i] = argvList[i].c_str();
    }
#endif
}

static void print_ref_callback(const char *ref_filename)
{
	if (ref_filename != NULL && strlen(ref_filename) > 0)
	{
		printf("%s\n", ref_filename);
	}
	else
	{
		ei_error("Invalid file reference.\n");
	}
}

int main_body(int argc, char *argv[])
{
	convert_native_arguments(argc, (const char **)argv);

	int ret = EXIT_SUCCESS;

	printf("*****************************************************************\n");
	printf("*                                                               *\n");
	printf("*                   Elara Renderer Standalone                   *\n");
	printf("*                                                               *\n");
	printf("*                       Version: %8s                       *\n", EI_VERSION_STRING);
#ifdef EI_ARCH_X86
	printf("*                       Build:     32-bit                       *\n");
#else
	printf("*                       Build:     64-bit                       *\n");
#endif
	printf("*                                                               *\n");
	printf("*     Copyright (C) 2013-2016 Rendease. All rights reserved.    *\n");
	printf("*                                                               *\n");
	printf("*****************************************************************\n");
	printf("\n");

	-- argc, ++ argv;

	if ((argc == 1 || argc == 2) && strcmp(argv[0], "-licsvr") == 0)
	{
		const char *uuid_str = NULL;
		if (argc == 2)
		{
			uuid_str = argv[1];
		}

		ei_run_license_server(uuid_str);
	}
	else if (argc == 1 && strcmp(argv[0], "-id") == 0)
	{
		ei_print_machine_id();
	}
	else if (argc == 1 && strcmp(argv[0], "-dongle") == 0)
	{
		ei_print_dongle_id();
	}
	else if (argc == 1 && strcmp(argv[0], "-identify") == 0)
	{
		ei_local_license_print_id();
	}
	else if (argc == 1 && strcmp(argv[0], "-nodes") == 0)
	{
		ei_context();
		ei_print_nodes();
		ei_end_context();
	}
	else if (argc == 2 && strcmp(argv[0], "-info") == 0)
	{
		ei_context();
		ei_print_node_info(argv[1]);
		ei_end_context();
	}
	else if (argc == 2 && strcmp(argv[0], "-list_refs") == 0)
	{
		ei_context();
		ei_verbose("warning");
		printf("\nFile references:\n\n");
		ei_get_ess_file_refs(argv[1], print_ref_callback);
		ei_end_context();
	}
	else
	{
		ei_context();

		const char *filename = NULL;
		eiTag output_list = EI_NULL_TAG;
		eiBool ignore_render = EI_FALSE;
		eiBool display = EI_FALSE;
		eiBool interactive = EI_FALSE;
		eiBool debug_adaptive = EI_FALSE;
		eiBool resolution_overridden = EI_FALSE;
		eiBool force_progressive = EI_FALSE;
		eiInt res_x;
		eiInt res_y;
		std::string lens_shader;
		eiBool force_render = EI_FALSE;
		std::string force_render_root_name;
		std::string force_render_cam_name;
		std::string force_render_option_name;
		std::string aov_name;

		for (int i = 0; i < argc; ++i)
		{
			if (argv[i][0] == '-')
			{
				if (strcmp(argv[i], "-verbose") == 0)
				{
					// -verbose level
					if ((i + 1) < argc)
					{
						const char *level = argv[i + 1];

						ei_verbose(level);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -verbose\n");
					}
				}
				else if (strcmp(argv[i], "-window") == 0)
				{
					// -window xmin xmax ymin ymax
					//
					if ((i + 4) < argc)
					{
						const char *xmin = argv[i + 1];
						const char *xmax = argv[i + 2];
						const char *ymin = argv[i + 3];
						const char *ymax = argv[i + 4];

						ei_override_int("camera", "window_xmin", atoi(xmin));
						ei_override_int("camera", "window_xmax", atoi(xmax));
						ei_override_int("camera", "window_ymin", atoi(ymin));
						ei_override_int("camera", "window_ymax", atoi(ymax));

						i += 4;
					}
					else
					{
						ei_error("No enough arguments specified for command: -window\n");
					}
				}
				else if (strcmp(argv[i], "-output") == 0)
				{
					// -output name type filter file
					// -output name type filter gamma file
					// -output name type filter gamma exposure file
					//
					if ((i + 4) < argc)
					{
						const char *name = argv[i + 1];
						const char *type = argv[i + 2];
						const char *filter = argv[i + 3];
						const char *use_gamma;
						const char *use_exposure;
						const char *file;
						if ((i + 5) < argc && 
							(strcmp(argv[i + 4], "on") == 0 || strcmp(argv[i + 4], "off") == 0))
						{
							if ((i + 6) < argc && 
								(strcmp(argv[i + 5], "on") == 0 || strcmp(argv[i + 5], "off") == 0))
							{
								use_gamma = argv[i + 4];
								use_exposure = argv[i + 5];
								file = argv[i + 6];

								i += 6;
							}
							else
							{
								use_gamma = argv[i + 4];
								use_exposure = g_str_on; // assume exposure is on by default
								file = argv[i + 5];

								i += 5;
							}
						}
						else
						{
							use_gamma = g_str_on; // assume gamma is on by default
							use_exposure = g_str_on; // assume exposure is on by default
							file = argv[i + 4];

							i += 4;
						}

						char out_name[EI_MAX_NODE_NAME_LEN];

						sprintf(out_name, "out_%s", name);

						ei_node("outvar", name);
							ei_param_token("name", name);
							ei_param_int("type", strcmp(type, "scalar") == 0 ? EI_TYPE_SCALAR : EI_TYPE_COLOR);
							ei_param_bool("filter", strcmp(filter, "on") == 0 ? EI_TRUE : EI_FALSE);
							ei_param_bool("use_gamma", strcmp(use_gamma, "on") == 0 ? EI_TRUE : EI_FALSE);
							ei_param_bool("use_exposure", strcmp(use_exposure, "on") == 0 ? EI_TRUE : EI_FALSE);
						ei_end_node();

						ei_node("output", out_name);
							ei_param_token("filename", file);
							ei_param_enum("data_type", "rgb");
							ei_param_array("var_list", ei_tab(EI_TYPE_TAG_NODE, 1));
								ei_tab_add_node(name);
							ei_end_tab();
						ei_end_node();

						eiTag out_tag = ei_find_node(out_name);

						if (out_tag != EI_NULL_TAG)
						{
							if (output_list == EI_NULL_TAG)
							{
								output_list = ei_create_data_table(EI_TYPE_TAG_NODE, 1);
							}

							ei_data_table_push_back(output_list, &out_tag);
						}
					}
					else
					{
						ei_error("No enough arguments specified for command: -output\n");
					}
				}
				else if (strcmp(argv[i], "-adaptive") == 0)
				{
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "adaptive_sampling_rate", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -adaptive\n");
					}
				}
				else if (strcmp(argv[i], "-samples") == 0)
				{
					// -samples min_samples max_samples
					//
					if ((i + 2) < argc)
					{
						const char *min_samples = argv[i + 1];
						const char *max_samples = argv[i + 2];

						ei_override_int("options", "min_samples", atoi(min_samples));
						ei_override_int("options", "max_samples", atoi(max_samples));

						i += 2;
					}
					else
					{
						ei_error("No enough arguments specified for command: -samples\n");
					}
				}
				else if (strcmp(argv[i], "-diffuse_samples") == 0)
				{
					// -diffuse_samples value
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "diffuse_samples", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -diffuse_samples\n");
					}
				}
				else if (strcmp(argv[i], "-sss_samples") == 0)
				{
					// -sss_samples value
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "sss_samples", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -sss_samples\n");
					}
				}
				else if (strcmp(argv[i], "-volume_indirect_samples") == 0)
				{
					// -volume_indirect_samples value
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "volume_indirect_samples", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -volume_indirect_samples\n");
					}
				}
				else if (strcmp(argv[i], "-random_lights") == 0)
				{
					// -random_lights value
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "random_lights", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -random_lights\n");
					}
				}
				else if (strcmp(argv[i], "-light_cutoff") == 0)
				{
					// -light_cutoff value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "light_cutoff", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -light_cutoff\n");
					}
				}
				else if (strcmp(argv[i], "-light_sample_quality") == 0)
				{
					// -light_sample_quality value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "light_sample_quality", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -light_sample_quality\n");
					}
				}
				else if (strcmp(argv[i], "-diffuse_depth") == 0)
				{
					// -diffuse_depth value
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "diffuse_depth", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -diffuse_depth\n");
					}
				}
				else if (strcmp(argv[i], "-sum_depth") == 0)
				{
					// -sum_depth value
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "sum_depth", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -sum_depth\n");
					}
				}
				else if (strcmp(argv[i], "-rr_depth") == 0)
				{
					// -rr_depth value
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "rr_depth", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -rr_depth\n");
					}
				}
				else if (strcmp(argv[i], "-ignore_emission") == 0)
				{
					// -ignore_emission value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "ignore_emission", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -ignore_emission\n");
					}
				}
				else if (strcmp(argv[i], "-clamp") == 0)
				{
					// -clamp value
					// -clamp off
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						if (strcmp(value, "on") == 0 || 
							strcmp(value, "off") == 0)
						{
							ei_override_bool("options", "use_clamp", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);
						}
						else
						{
							ei_override_bool("options", "use_clamp", EI_TRUE);
							ei_override_scalar("options", "clamp_value", (eiScalar)atof(value));
						}

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -clamp\n");
					}
				}
				else if (strcmp(argv[i], "-clamp_portal") == 0)
				{
					// -clamp_portal value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "clamp_portal", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -clamp_portal\n");
					}
				}
				else if (strcmp(argv[i], "-filter") == 0)
				{
					// -filter type size
					//
					if ((i + 2) < argc)
					{
						const char *filter_type = argv[i + 1];
						const char *filter_size = argv[i + 2];

						ei_override_enum("options", "filter", filter_type);
						ei_override_scalar("options", "filter_size", (eiScalar)atof(filter_size));

						i += 2;
					}
					else
					{
						ei_error("No enough arguments specified for command: -filter\n");
					}
				}
				else if (strcmp(argv[i], "-exposure") == 0)
				{
					// -exposure value highlight shadow saturation whitepoint
					// -exposure off
					//
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						if (strcmp(value, "on") == 0 || 
							strcmp(value, "off") == 0)
						{
							ei_override_bool("options", "exposure", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

							i += 1;
						}
						else
						{
							if ((i + 5) < argc)
							{
								const char *highlight = argv[i + 2];
								const char *shadow = argv[i + 3];
								const char *saturation = argv[i + 4];
								const char *whitepoint = argv[i + 5];

								ei_override_bool("options", "exposure", EI_TRUE);
								ei_override_scalar("options", "exposure_value", (eiScalar)atof(value));
								ei_override_scalar("options", "exposure_highlight", (eiScalar)atof(highlight));
								ei_override_scalar("options", "exposure_shadow", (eiScalar)atof(shadow));
								ei_override_scalar("options", "exposure_saturation", (eiScalar)atof(saturation));
								ei_override_scalar("options", "exposure_whitepoint", (eiScalar)atof(whitepoint));

								i += 5;
							}
							else
							{
								ei_error("No enough arguments specified for command: -exposure\n");
							}
						}
					}
					else
					{
						ei_error("No enough arguments specified for command: -exposure\n");
					}
				}
				else if (strcmp(argv[i], "-display_gamma") == 0)
				{
					// -display_gamma value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "display_gamma", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -display_gamma\n");
					}
				}
				else if (strcmp(argv[i], "-engine") == 0)
				{
					// -engine path
					// -engine bidirectional
					// -engine hybrid
					// -engine cache
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						if (strcmp(value, "path") == 0)
						{
							ei_override_enum("options", "engine", "path tracer");
						}
						else if (strcmp(value, "bidirectional") == 0)
						{
							ei_override_enum("options", "engine", "bidirectional path tracer");
						}
						else if (strcmp(value, "hybrid") == 0)
						{
							ei_override_enum("options", "engine", "hybrid path tracer");
						}
						else if (strcmp(value, "cache") == 0)
						{
							ei_override_enum("options", "engine", "GI cache");
						}

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -engine\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_light_cutoff") == 0)
				{
					// -GI_cache_light_cutoff value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_light_cutoff", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_light_cutoff\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_density") == 0)
				{
					// -GI_cache_density value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_density", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_density\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_normal_density") == 0)
				{
					// -GI_cache_normal_density value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_normal_density", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_normal_density\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_passes") == 0)
				{
					// -GI_cache_passes value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "GI_cache_passes", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_passes\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_radius") == 0)
				{
					// -GI_cache_radius value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_radius", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_radius\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_points") == 0)
				{
					// -GI_cache_points value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "GI_cache_points", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_points\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_screen_scale") == 0)
				{
					// -GI_cache_screen_scale value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_screen_scale", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_screen_scale\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_preview") == 0)
				{
					// -GI_cache_preview value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_enum("options", "GI_cache_preview", value);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_preview\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_no_leak") == 0)
				{
					// -GI_cache_no_leak value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "GI_cache_no_leak", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_no_leak\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_indirect_glossy") == 0)
				{
					// -GI_cache_indirect_glossy value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "GI_cache_indirect_glossy", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_indirect_glossy\n");
					}
				}
				else if (strcmp(argv[i], "-min_light_importance") == 0)
				{
					// -min_light_importance value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "min_light_importance", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -min_light_importance\n");
					}
				}
				else if (strcmp(argv[i], "-light_nonuniform_scaling") == 0)
				{
					// -light_nonuniform_scaling value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "light_nonuniform_scaling", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -light_nonuniform_scaling\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_adaptive") == 0)
				{
					// -GI_cache_adaptive value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_adaptive", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_adaptive\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_samples") == 0)
				{
					// -GI_cache_samples min_samples max_samples
					//
					if ((i + 2) < argc)
					{
						const char *min_samples = argv[i + 1];
						const char *max_samples = argv[i + 2];

						ei_override_int("options", "GI_cache_min_samples", atoi(min_samples));
						ei_override_int("options", "GI_cache_max_samples", atoi(max_samples));

						i += 2;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_samples\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_gradient") == 0)
				{
					// -GI_cache_gradient value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_enum("options", "GI_cache_gradient", value);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_gradient\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_show_samples") == 0)
				{
					// -GI_cache_show_samples value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "GI_cache_show_samples", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_show_samples\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_sample_scale") == 0)
				{
					// -GI_cache_sample_scale value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_int("options", "GI_cache_sample_scale", atoi(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_sample_scale\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_normal_quality") == 0)
				{
					// -GI_cache_normal_quality value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_normal_quality", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_normal_quality\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_front_test") == 0)
				{
					// -GI_cache_front_test value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_front_test", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_front_test\n");
					}
				}
				else if (strcmp(argv[i], "-GI_cache_behind_test") == 0)
				{
					// -GI_cache_behind_test value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_scalar("options", "GI_cache_behind_test", (eiScalar)atof(value));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -GI_cache_behind_test\n");
					}
				}
				else if (strcmp(argv[i], "-progressive") == 0)
				{
					// -progressive value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];
						force_progressive = (strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						ei_override_bool("options", "progressive", force_progressive);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -progressive\n");
					}
				}
				else if (strcmp(argv[i], "-caustic") == 0)
				{
					// -caustic value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "caustic", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -caustic\n");
					}
				}
				else if (strcmp(argv[i], "-shadow") == 0)
				{
					// -shadow value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "shadow", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -shadow\n");
					}
				}
				else if (strcmp(argv[i], "-resolution") == 0)
				{
					// -resolution width height
					//
					if ((i + 2) < argc)
					{
						const char *width = argv[i + 1];
						const char *height = argv[i + 2];
						
						resolution_overridden = EI_TRUE;
						res_x = atoi(width);
						res_y = atoi(height);

						ei_override_int("camera", "res_x", res_x);
						ei_override_int("camera", "res_y", res_y);
						ei_override_scalar("camera", "aspect", (eiScalar)res_x / (eiScalar)res_y);

						i += 2;
					}
					else
					{
						ei_error("No enough arguments specified for command: -resolution\n");
					}
				}
				else if (strcmp(argv[i], "-lens") == 0)
				{
					// -lens shader stereo eye_distance
					//
					if ((i + 3) < argc)
					{
						ei_link("liber_shader");

						const char *shader = argv[i + 1];
						const char *stereo = argv[i + 2];
						const char *eye_distance = argv[i + 3];

						lens_shader = std::string(shader) + std::string("_OverrideLensShaderInstance");

						ei_node(shader, lens_shader.data());
							ei_param_bool("stereo", strcmp(stereo, "on") == 0 ? EI_TRUE : EI_FALSE);
							ei_param_scalar("eye_distance", (eiScalar)atof(eye_distance));
						ei_end_node();

						i += 3;
					}
					else
					{
						ei_error("No enough arguments specified for command: -lens\n");
					}
				}
				else if (strcmp(argv[i], "-bucket_size") == 0)
				{
					// -bucket_size size
					//
					if ((i + 1) < argc)
					{
						const char *size = argv[i + 1];

						ei_override_int("options", "bucket_size", atoi(size));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -bucket_size\n");
					}
				}
				else if (strcmp(argv[i], "-ignore_render") == 0)
				{
					// -ignore_render
					ignore_render = EI_TRUE;
				}
				else if (strcmp(argv[i], "-display") == 0)
				{
					// -display
					display = EI_TRUE;
				}
				else if (strcmp(argv[i], "-interactive") == 0)
				{
					// -interactive
					interactive = EI_TRUE;
				}
				else if (strcmp(argv[i], "-debug_adaptive") == 0)
				{
					// -debug_adaptive
					display = EI_TRUE;
					debug_adaptive = EI_TRUE;
				}
				else if (strcmp(argv[i], "-parse1") == 0)
				{
				}
				else if (strcmp(argv[i], "-parse2") == 0)
				{
				}
				else if (strcmp(argv[i], "-shader_searchpath") == 0)
				{
					// -shader_searchpath path
					//
					if ((i + 1) < argc)
					{
						const char *path = argv[i + 1];

						ei_add_shader_searchpath(path);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -shader_searchpath\n");
					}
				}
				else if (strcmp(argv[i], "-texture_searchpath") == 0)
				{
					// -texture_searchpath path
					//
					if ((i + 1) < argc)
					{
						const char *path = argv[i + 1];

						ei_add_texture_searchpath(path);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -texture_searchpath\n");
					}
				}
				else if (strcmp(argv[i], "-scene_searchpath") == 0)
				{
					// -scene_searchpath path
					//
					if ((i + 1) < argc)
					{
						const char *path = argv[i + 1];

						ei_add_scene_searchpath(path);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -scene_searchpath\n");
					}
				}
				else if (strcmp(argv[i], "-texture_memlimit") == 0)
				{
					// -texture_memlimit size
					//
					if ((i + 1) < argc)
					{
						const char *size = argv[i + 1];

						ei_set_texture_memlimit(atoi(size));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -texture_memlimit\n");
					}
				}
				else if (strcmp(argv[i], "-texture_openfiles") == 0)
				{
					// -texture_openfiles count
					//
					if ((i + 1) < argc)
					{
						const char *count = argv[i + 1];

						ei_set_texture_openfiles(atoi(count));

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -texture_openfiles\n");
					}
				}
				else if (strcmp(argv[i], "-page_file_dir") == 0)
				{
					// -page_file_dir dir
					//
					if ((i + 1) < argc)
					{
						const char *dir = argv[i + 1];

						ei_set_page_file_dir(dir);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -page_file_dir\n");
					}
				}
				else if (strcmp(argv[i], "-ultra_texcache") == 0)
				{
					// -ultra_texcache value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "ultra_texcache", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -ultra_texcache\n");
					}
				}
				else if (strcmp(argv[i], "-shader_specialization") == 0)
				{
					// -shader_specialization value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "shader_specialization", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -shader_specialization\n");
					}
				}
				else if (strcmp(argv[i], "-use_mis") == 0)
				{
					// -use_mis value
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_bool("options", "use_mis", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -use_mis\n");
					}
				}
				else if (strcmp(argv[i], "-dongle_activate") == 0)
				{
					// -dongle_activate code1 code2 license_code
					//
					if ((i + 3) < argc)
					{
						const char *code1 = argv[i + 1];
						const char *code2 = argv[i + 2];
						const char *license_code = argv[i + 3];

						ei_dongle_license_set(
							strtoul(code1, NULL, 0), 
							strtoul(code2, NULL, 0), 
							license_code);

						i += 3;
					}
					else
					{
						ei_error("No enough arguments specified for command: -dongle_activate\n");
					}
				}
				else if (strcmp(argv[i], "-login") == 0)
				{
					if ((i + 1) < argc)
					{
						const char *uuid_str = argv[i + 1];

						ei_login_with_uuid(uuid_str);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -login\n");
					}
				}
				else if (strcmp(argv[i], "-activate") == 0)
				{
					if ((i + 1) < argc)
					{
						const char *code = argv[i + 1];

						ei_local_license_set(code);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -activate\n");
					}
				}
				else if (strcmp(argv[i], "-licsvr_addr") == 0)
				{
					if ((i + 1) < argc)
					{
						const char *server_addr = argv[i + 1];

						ei_set_license_server(server_addr);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -licsvr_addr\n");
					}
				}
				else if (strcmp(argv[i], "-render") == 0)
				{
					if ((i + 3) < argc)
					{
						force_render_root_name = argv[i + 1];
						force_render_cam_name = argv[i + 2];
						force_render_option_name = argv[i + 3];

						force_render = EI_TRUE;						

						i += 3;
					}
					else
					{
						ei_error("No enough arguments specified for command: -render\n");
					}
				}
				else if (strcmp(argv[i], "-aov") == 0)
				{
					if ((i + 1) < argc)
					{
						aov_name = argv[i + 1];

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -aov\n");
					}
				}
				else if (strcmp(argv[i], "-accel_mode") == 0)
				{
					if ((i + 1) < argc)
					{
						const char *value = argv[i + 1];

						ei_override_enum("options", "accel_mode", value);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -accel_mode\n");
					}
				}
				else
				{
					ei_error("Unknown command: %s\n", argv[i]);
				}
			}
			else
			{
				filename = argv[i];
			}
		}

		if (output_list != EI_NULL_TAG)
		{
			ei_override_array("camera", "output_list", output_list);
		}

		if (filename != NULL)
		{
			ei_info("Start parsing file: %s\n", filename);

			if (!ei_parse2(filename, ignore_render || display || interactive || force_render))
			{
				ei_error("Failed to parse file: %s\n", filename);

				ret = EXIT_FAILURE;
			}

			ei_info("Finished parsing file: %s\n", filename);

			if (!lens_shader.empty())
			{
				eiRenderParameters render_params;
				if (ei_get_last_render_params(&render_params))
				{
					eiTag cam_inst_tag = ei_find_node(render_params.camera_inst);
					if (cam_inst_tag != EI_NULL_TAG)
					{
						eiDataAccessor<eiNode> cam_inst(cam_inst_tag);
						eiTag cam_item_tag = ei_node_get_node(cam_inst.get(), ei_node_find_param(cam_inst.get(), "element"));
						if (cam_item_tag != EI_NULL_TAG)
						{
							eiDataAccessor<eiNode> cam_item(cam_item_tag);
							eiTag lens_shader_tag = ei_find_node(lens_shader.data());
							if (lens_shader_tag != EI_NULL_TAG)
							{
								ei_node_node(cam_item.get(), "lens_shader", lens_shader_tag);
							}
						}
					}
				}
			}

			if (display || interactive || force_render)
			{
				ei_info("Start display and rendering...\n");

				eiRenderParameters render_params;
				memset(&render_params, 0, sizeof(render_params));
				eiBool get_render_params = EI_FALSE;
				if (force_render)
				{
					strncpy(render_params.root_instgroup, force_render_root_name.c_str(), EI_MAX_NODE_NAME_LEN - 1);
					strncpy(render_params.camera_inst, force_render_cam_name.c_str(), EI_MAX_NODE_NAME_LEN - 1);
					strncpy(render_params.options, force_render_option_name.c_str(), EI_MAX_NODE_NAME_LEN - 1);
					get_render_params = EI_TRUE;
				}
				else
				{
					get_render_params = ei_get_last_render_params(&render_params);
				}
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
							if (!resolution_overridden)
							{
								res_x = ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_x"));
								res_y = ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_y"));
							}
							eiBool progressive = EI_FALSE;
							eiTag opt_item_tag = ei_find_node(render_params.options);
							if (opt_item_tag != EI_NULL_TAG)
							{
								eiDataAccessor<eiNode> opt_item(opt_item_tag);
								progressive = ei_node_get_bool(opt_item.get(), ei_node_find_param(opt_item.get(), "progressive"));
							}
							eiBool new_progressive = progressive;
							if (force_progressive || interactive)
							{
								new_progressive = EI_TRUE;
							}
							RenderProcess rp(res_x, res_y, &render_params, interactive, new_progressive, debug_adaptive, aov_name);

							if (interactive)
							{
								ei_verbose("warning");

								if (ei_find_node(render_params.options) != EI_NULL_TAG)
								{
									eiBool need_init;
									eiNode *opt_node = ei_edit_node(render_params.options, &need_init);

									rp.last_render_is_interactive = EI_TRUE;
									rp.org_engine = ei_node_get_int(opt_node, ei_node_find_param(opt_node, "engine"));
									rp.org_min_samples = ei_node_get_int(opt_node, ei_node_find_param(opt_node, "min_samples"));
									rp.org_max_samples = ei_node_get_int(opt_node, ei_node_find_param(opt_node, "max_samples"));
									rp.org_diffuse_samples = ei_node_get_int(opt_node, ei_node_find_param(opt_node, "diffuse_samples"));
									rp.org_sss_samples = ei_node_get_int(opt_node, ei_node_find_param(opt_node, "sss_samples"));
									rp.org_volume_indirect_samples = ei_node_get_int(opt_node, ei_node_find_param(opt_node, "volume_indirect_samples"));
									rp.org_random_lights = ei_node_get_int(opt_node, ei_node_find_param(opt_node, "random_lights"));
									rp.org_light_sample_quality = ei_node_get_scalar(opt_node, ei_node_find_param(opt_node, "light_sample_quality"));
									rp.org_progressive = progressive;

									ei_node_enum(opt_node, "accel_mode", "large");

									ei_node_int(opt_node, "engine", EI_ENGINE_HYBRID_PATH_TRACER);
									ei_node_int(opt_node, "min_samples", -3);
									ei_node_int(opt_node, "max_samples", 1);
									ei_node_int(opt_node, "diffuse_samples", 1);
									ei_node_int(opt_node, "sss_samples", 1);
									ei_node_int(opt_node, "volume_indirect_samples", 1);
									ei_node_int(opt_node, "random_lights", 16);
									ei_node_scalar(opt_node, "light_sample_quality", 0.05f);
									ei_node_bool(opt_node, "progressive", EI_TRUE);

									ei_end_edit_node(opt_node);
								}
							}

							ei_set_custom_trace(custom_trace);
							ei_job_set_process(&(rp.base));
							ei_timer_reset(&(rp.first_pixel_timer));
							ei_timer_start(&(rp.first_pixel_timer));
							rp.is_first_pass = EI_TRUE;
							ei_render_prepare();
							{
								rp.renderThread = ei_create_thread(render_callback, &render_params, NULL);
								ei_set_low_thread_priority(rp.renderThread);

								if (display || interactive)
								{
									ei_display(display_callback, &rp, res_x, res_y);
								}

								ei_wait_thread(rp.renderThread);
								ei_delete_thread(rp.renderThread);
								rp.renderThread = NULL;
							}
							ei_render_cleanup();
							ei_job_set_process(NULL);
							ei_set_custom_trace(NULL);
						}
					}
				}

				ei_info("Finished display and rendering.\n");
			}
		}
		else
		{
			ei_error("Scene file is not specified.\n");

			ret = EXIT_FAILURE;
		}

		ei_end_context();
	}

	return ret;
}

#ifdef EI_OS_WINDOWS

/* Windows specific exception handling and minidump */
#include <dbghelp.h>

#pragma comment(lib, "dbghelp.lib")

BOOL CALLBACK MyMiniDumpCallback(
	PVOID                            pParam, 
	const PMINIDUMP_CALLBACK_INPUT   pInput, 
	PMINIDUMP_CALLBACK_OUTPUT        pOutput)
{
	BOOL bRet = FALSE;

	if (pInput == 0)
		return FALSE; 

	if (pOutput == 0) 
		return FALSE;

	switch (pInput->CallbackType)
	{
	case IncludeModuleCallback:
		{
			bRet = TRUE;
		}
		break;

	case IncludeThreadCallback:
		{
			bRet = TRUE;
		}
		break;

	case ModuleCallback:
		{
			// Does the module have ModuleReferencedByMemory flag set?
			if (!(pOutput->ModuleWriteFlags & ModuleReferencedByMemory))
			{
				// No, it does not - exclude it
				/* printf("Excluding module: %s \n", pInput->Module.FullPath); */
				pOutput->ModuleWriteFlags &= (~ModuleWriteModule);
			}
			bRet = TRUE;
		}
		break;

	case ThreadCallback:
		{
			bRet = TRUE;
		}
		break;

	case ThreadExCallback:
		{
			bRet = TRUE;
		}
		break;

	case MemoryCallback:
		{
			bRet = FALSE;
		}
		break;

	case CancelCallback:
		break;
	}

	return bRet;
}

void CreateMiniDump(EXCEPTION_POINTERS *pep)
{
	char cur_dir[ EI_MAX_FILE_NAME_LEN ];
	char dump_filename[ EI_MAX_FILE_NAME_LEN ];
	char dump_filepath[ EI_MAX_FILE_NAME_LEN ];
	char time_str[ EI_MAX_FILE_NAME_LEN ];
	
	time_t now = time(0);
	tm *localtm = localtime(&now);
	strftime(time_str, EI_MAX_FILE_NAME_LEN, "%Y-%m-%d-%H-%M-%S", localtm);
	sprintf(dump_filename, "elara_minidump_%d-%d-%d_%s.dmp", 
		EI_VERSION_MAJOR, EI_VERSION_MINOR, EI_VERSION_PATCH, 
		time_str);
	ei_get_current_directory(cur_dir);
	ei_append_filename(dump_filepath, cur_dir, dump_filename);

	HANDLE hFile = CreateFile(dump_filepath, GENERIC_READ | GENERIC_WRITE, 
		0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hFile != NULL && hFile != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION mdei;
		mdei.ThreadId = GetCurrentThreadId();
		mdei.ExceptionPointers = pep;
		mdei.ClientPointers = FALSE;

		MINIDUMP_CALLBACK_INFORMATION mci;
		mci.CallbackRoutine = (MINIDUMP_CALLBACK_ROUTINE)MyMiniDumpCallback;
		mci.CallbackParam = 0;

		MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);

		BOOL rv = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), 
			hFile, mdt, (pep != 0) ? &mdei : 0, 0, &mci);

		if (!rv)
			printf("Minidump failed. Error: %u\n", GetLastError());
		else 
			printf("Minidump created.\n");

		CloseHandle(hFile);
	}
	else
	{
		printf("Create file failed. Error: %u \n", GetLastError());
	}
}

void ctrl_c_handler(int param)
{
	printf("Ctrl + C detected.\n");
	ei_handle_exception();
}

void ctrl_break_handler(int param)
{
	printf("Ctrl + Break detected.\n");
	ei_handle_exception();
}

void term_handler(int param)
{
	printf("Software termination detected.\n");
	ei_handle_exception();
}

int main(int argc, char *argv[])
{
	int retcode = EXIT_FAILURE;

	signal(SIGINT, ctrl_c_handler);
	signal(SIGBREAK, ctrl_break_handler);
	signal(SIGTERM, term_handler);

	__try
	{
		retcode = main_body(argc, argv);
	}
	__except (CreateMiniDump(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
	{
		printf("C/C++ exception detected.\n");
		ei_handle_exception();
	}

	return retcode;
}

#else

int main(int argc, char *argv[])
{
	return main_body(argc, argv);
}

#endif
