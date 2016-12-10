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

#include <ei.h>
#include <ei_data_table.h>
#include <ei_license.h>
#include <ei_verbose.h>
#include <ei_base_bucket.h>
#include <ei_timer.h>
#include <vector>
#include <deque>
#include <OpenImageIO/filesystem.h>

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
	eiVector					up_vector;
	eiVector					camera_target;
	eiScalar					min_target_dist;
	eiScalar					last_job_percent;
	eiTimer						first_pixel_timer;
	eiBool						is_first_pass;

	RenderProcess(
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

		if (drag_mode != EI_DRAG_MODE_NONE && 
			drag_button != EI_DRAG_BUTTON_RIGHT && 
			(offset[0] != 0.0f || offset[1] != 0.0f))
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
					eiVector obj_center = ei_vector(0.0f, 0.0f, 0.0f);
					/* use absolute value to ensure camera target is always in front of camera direction */
					eiScalar focal_dist = absf(point_plane_dist(camera_pos, -cam_dir, obj_center));
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

int main_body(int argc, char *argv[])
{
	OIIO::Filesystem::convert_native_arguments (argc, (const char **)argv);

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

	if (argc == 1 && strcmp(argv[0], "-licsvr") == 0)
	{
		ei_run_license_server();
	}
	else if (argc == 1 && strcmp(argv[0], "-id") == 0)
	{
		ei_print_machine_id();
	}
	else if (argc == 1 && strcmp(argv[0], "-dongle") == 0)
	{
		ei_print_dongle_id();
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
	else
	{
		ei_context();

		const char *filename = NULL;
		eiTag output_list = EI_NULL_TAG;
		eiBool ignore_render = EI_FALSE;
		eiBool display = EI_FALSE;
		eiBool interactive = EI_FALSE;
		eiBool resolution_overridden = EI_FALSE;
		eiBool force_progressive = EI_FALSE;
		eiInt res_x;
		eiInt res_y;
		std::string lens_shader;

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

						ei_override_bool("options", "GI_cache_preview", strcmp(value, "on") == 0 ? EI_TRUE : EI_FALSE);

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
						const char *code1 = argv[i + 1];
						ei_login_with_uuid(code1);

						i += 1;
					}
					else
					{
						ei_error("No enough arguments specified for command: -login\n");
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

			if (!ei_parse2(filename, ignore_render || display || interactive))
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

			if (display || interactive)
			{
				ei_info("Start display and rendering...\n");

				eiRenderParameters render_params;
				if (!ei_get_last_render_params(&render_params))
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
							if (force_progressive || interactive)
							{
								progressive = EI_TRUE;
							}
							RenderProcess rp(res_x, res_y, &render_params, interactive, progressive);

							if (interactive)
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

								ei_display(display_callback, &rp, res_x, res_y);

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
				printf("Excluding module: %s \n", pInput->Module.FullPath);
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
	HANDLE hFile = CreateFile("elara_minidump.dmp", GENERIC_READ | GENERIC_WRITE, 
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

int main(int argc, char *argv[])
{
	int retcode = EXIT_FAILURE;

	__try
	{
		retcode = main_body(argc, argv);
	}
	__except (CreateMiniDump(GetExceptionInformation()), EXCEPTION_EXECUTE_HANDLER)
	{
	}

	return retcode;
}

#else

int main(int argc, char *argv[])
{
	return main_body(argc, argv);
}

#endif
