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

#include <QSharedMemory>
#include <ei.h>
#include <ei_data_table.h>
#include <ei_license.h>
#include <ei_verbose.h>
#include <ei_base_bucket.h>
#include <ei_timer.h>
#include <vector>
#include <deque>
#include <string>
#include <map>
#include <algorithm>
#include <shellapi.h>
#include <QMutex>
#include <QDebug>

#ifdef _DEBUG   //Debug mode  
#pragma comment (lib,"Qt5PlatformSupportd.lib")  
#pragma comment (lib,"qwindowsd.lib")  
#else           //Release mode 
#pragma comment (lib,"qtpcre.lib")  
#pragma comment (lib,"ws2_32.lib")  
#endif  

using namespace std;
static const char *g_str_on = "on";

static EI_THREAD_FUNC render_callback(void *param);
EI_API void ei_dongle_license_set(eiUint code1, eiUint code2, const char *license_code);
EI_API void ei_dongle_license_release();

enum {
	is_ei_bool,
	is_ei_scalar,
	is_ei_int,
	is_ei_enum,
	is_ei_token,
	type_count
};
typedef map<string, int> ParamTypeMap;

struct GUICommand
{
    UINT cmd; //0 means nothing, 1 means cancel render, 2 means mouse command
    INT mouseX;
    INT mouseY;
    UINT mouseBtn;
    UINT modifierKey;
};

ParamTypeMap param_type_map;

std::vector<string> argvList;

std::string utf16_to_utf8(const WCHAR* str)
{
	std::string utf8;
	utf8.resize(WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL));
	WideCharToMultiByte(CP_UTF8, 0, str, -1, &utf8[0], (int)utf8.size(), NULL, NULL);
	return utf8;
}


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

#define MSG_QUEUE_WAIT_TIME		50

struct RenderProcess
{
	eiProcess					base;
	eiThreadHandle				renderThread;
	eiRWLock					*bufferLock;
	std::vector<eiColor>		originalBuffer;
    std::vector<eiScalar>		opacityBuffer;
	eiInt						imageWidth;
	eiInt						imageHeight;
	eiRenderParameters			*render_params;
	eiBool						interactive;
	eiBool						target_set;
	eiVector					up_vector;
	eiVector					camera_target;
	eiScalar					min_target_dist;
	eiScalar					last_job_percent;
	eiTimer						first_pixel_timer;
	eiBool						is_first_pass;
	eiBool						renderToMem;

	RenderProcess(eiInt res_x, eiInt res_y, eiRenderParameters *_render_params, eiBool _interactive)
	{
		init_callbacks();
		renderThread = NULL;
		bufferLock = ei_create_rwlock();
		imageWidth = res_x;
		imageHeight = res_y;
		render_params = _render_params;
		interactive = _interactive;
		target_set = EI_FALSE;
		up_vector = ei_vector(0.0f, 0.0f, 1.0f);
		camera_target = 0.0f;
		min_target_dist = 0.0f;
		last_job_percent = 0.0f;
        renderToMem = EI_FALSE;
		const eiColor blackColor = ei_color(0.0f);
		originalBuffer.resize(imageWidth * imageHeight, blackColor);
        opacityBuffer.resize(imageWidth * imageHeight, 0.0f);
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
	fflush(stdout);
}


QSharedMemory sharedMem;
QSharedMemory guiCmdMem;

void ProcessInteractive(RenderProcess *rp, GUICommand* guiCmd = nullptr)
{
    if (rp->interactive)
    {
        eiScalar offset[2] = {0};
        eiInt drag_button = EI_DRAG_BUTTON_NONE;
        eiInt drag_mode = EI_DRAG_MODE_NONE;
        if (guiCmd == nullptr)
        {
            drag_mode = ei_display_get_mouse_move(offset, &drag_button);
        }
        else
        {
            if (guiCmd && guiCmd->cmd == 2)
            {
                offset[0] = guiCmd->mouseX;
                offset[1] = guiCmd->mouseY;
                drag_button = guiCmd->mouseBtn;
                drag_mode = guiCmd->modifierKey;
                guiCmd->mouseX = 0;
                guiCmd->mouseY = 0;
                guiCmd->cmd = 0; //Reset command to avoid redundent process
                qDebug()<<offset[0]<<" "<<offset[1];
            }
        }
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
                    point_on_plane(rp->camera_target, camera_pos, -cam_dir, obj_center);
                    rp->min_target_dist = 0.01f * dist(camera_pos, rp->camera_target);
                    rp->target_set = EI_TRUE;
                }
                eiVector target_vec = camera_pos - rp->camera_target;
                eiScalar target_dist = normalize_len(target_vec, target_vec);

                if ((drag_button == EI_DRAG_BUTTON_LEFT && drag_mode == EI_DRAG_MODE_CTRL) ||
                    (drag_button == EI_DRAG_BUTTON_MIDDLE && drag_mode == EI_DRAG_MODE_NORMAL)) /* pan */
                {
                    rp->camera_target += cam_right * (-0.001f * offset[0] * ::max(rp->min_target_dist, target_dist));
                    rp->camera_target += cam_up * (0.001f * offset[1] * ::max(rp->min_target_dist, target_dist));
                }
                else if (drag_button == EI_DRAG_BUTTON_LEFT && drag_mode == EI_DRAG_MODE_SHIFT) /* zoom */
                {
                    target_dist += 0.002f * offset[1] * ::max(rp->min_target_dist, target_dist);
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
}

static void rprocess_job_started(
	eiProcess *process, 
	const eiTag job, 
	const eiThreadID threadId)
{
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
    eiFrameBufferCache  opacityFrameBufferCache;

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
    ei_framebuffer_cache_init(
        &opacityFrameBufferCache,
        pJob->opacityFrameBuffer,
        pJob->pos_i,
        pJob->pos_j,
        pJob->point_spacing,
        pJob->pass_id,
        &infoFrameBufferCache);

    const eiInt tile_width = pJob->rect.right - pJob->rect.left + 1;
    const eiInt tile_height = pJob->rect.bottom - pJob->rect.top + 1;

	/* write bucket updates into the original buffer */
	const eiInt imageWidth = rp->imageWidth;
	const eiInt imageHeight = rp->imageHeight;
	eiColor *originalBuffer = &(rp->originalBuffer[0]);
    float *opacityBuffer = &(rp->opacityBuffer[0]);
	originalBuffer += ((imageHeight - 1 - pJob->rect.top) * imageWidth + pJob->rect.left);
    opacityBuffer += ((imageHeight - 1 - pJob->rect.top) * imageWidth + pJob->rect.left);
	ei_read_lock(rp->bufferLock);
	{
		for (eiInt j = 0; j < tile_height; ++j)
		{
			for (eiInt i = 0; i < tile_width; ++i)
			{
				ei_framebuffer_cache_get_final(&colorFrameBufferCache, i, j, &(originalBuffer[i]));
                eiColor alpha;
                ei_framebuffer_cache_get_final(&opacityFrameBufferCache, i, j, &alpha);
                opacityBuffer[i] = (alpha.r + alpha.g + alpha.b) / 3.0f;
			}
			originalBuffer -= imageWidth;
            opacityBuffer -= imageWidth;
		}
	}
	ei_read_unlock(rp->bufferLock);

	ei_framebuffer_cache_exit(&colorFrameBufferCache);
    ei_framebuffer_cache_exit(&opacityFrameBufferCache);
	ei_framebuffer_cache_exit(&infoFrameBufferCache);

    const eiScalar job_percent = (eiScalar)ei_job_get_percent();
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
	fflush(stdout);
}

static void rprocess_info(
	eiProcess *process, 
	const char *text)
{
	fflush(stdout);
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

    ProcessInteractive(rp);

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

static bool WindowlessProc(void *param)
{
    ei_sleep(MSG_QUEUE_WAIT_TIME);

    RenderProcess *rp = (RenderProcess *)param;

    bool abortRender = false;
    guiCmdMem.lock();
    GUICommand* guiCmd = (GUICommand*)guiCmdMem.data();
    if (guiCmd)
    {
        abortRender = (guiCmd->cmd == 1);
    }
    guiCmdMem.unlock();

    if (abortRender)
    {
        ei_job_abort(EI_TRUE);
        return false;
    }

    if (!rp->renderToMem) return true;

    //ProcessInteractive(rp, guiCmd);

    if (sharedMem.isAttached())
    {
        sharedMem.detach();
    }
    if (!sharedMem.create(3 * sizeof(size_t) + rp->originalBuffer.size() * sizeof(eiScalar) * 4))
    {
        return true;
    }
    const eiScalar job_percent = (eiScalar)ei_job_get_percent();

    if (sharedMem.lock())
    {
        BYTE* bmpPtr = (BYTE*)sharedMem.data();
        *(size_t*)(bmpPtr)= rp->imageWidth;
        bmpPtr += sizeof(size_t);
        *(size_t*)(bmpPtr)= rp->imageHeight;
        bmpPtr += sizeof(size_t);
        *(size_t*)(bmpPtr)= (size_t)(job_percent * 100);
        bmpPtr += sizeof(size_t);
        float* pColor = (float*)bmpPtr;
        for (int i = 0; i < rp->originalBuffer.size(); ++i)
        {
            pColor[0] = rp->originalBuffer[i].r;
            pColor[1] = rp->originalBuffer[i].g;
            pColor[2] = rp->originalBuffer[i].b;
            pColor[3] = rp->opacityBuffer[i];
            pColor += 4;
        }
        sharedMem.unlock();
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
    else
    {
        return false;
    }
    return job_percent < 100.0f;
}

void SplitString(const string& src, string sep, vector<string>& result)
{
	if (src.length() == 0) return;
	int lastPos = 0;
	int matchPos = (int)src.find_first_of(sep);
	while (matchPos != src.npos)
	{
		result.push_back(src.substr(lastPos, matchPos - lastPos));
		lastPos = matchPos + 1;
		matchPos = (int)src.find_first_of(sep, lastPos);
	}
	if (lastPos < src.length())
	{
		result.push_back(src.substr(lastPos, src.length() - lastPos));
	}
}

pair<string, string> SplitParam(const string& src)
{
	int idx = (int)src.find_first_of('=');
	string name = src.substr(0, idx);
	string temp = src.substr(idx + 1, src.length() - idx - 1);
	string value;
	value.resize(temp.length());
	//to lower
	transform(temp.begin(), temp.end(), temp.begin(), ::tolower);
	//remove space
	int charStart = (int)temp.find_first_not_of(" ");
	int charEnd = (int)temp.find_last_not_of(" ");
	value = temp.substr(charStart, charEnd - charStart + 1);
    return pair<string, string>(name, value);
}

void TeminateNow()
{
	ei_end_context();
	exit(1);
}

void ValidateName(const char* node_name, string& name)
{
	if (param_type_map.find(name) == param_type_map.end())
	{
		printf("%s doesn't have parameter \"%s\"!\r\n", node_name, name.data());
		TeminateNow();
	}
}

void SetBool(const char* node_name, string& name, string& value)
{
	ValidateName(node_name, name);
	if (value == "on"
		|| value == "true")
	{
		ei_override_bool(node_name, name.data(), EI_TRUE);
	}
	else if (value == "off"
		|| value == "false")
	{
        ei_override_bool(node_name, name.data(), EI_FALSE);
	}
	else
	{
		printf("param \"%s\"'s value is incorrect!\n", name.data());
		TeminateNow();
	}
}

void SetScalar(const char* node_name, string& name, string& value)
{
	ValidateName(node_name, name);
	float scalar = (float)atof(value.data());
	ei_override_scalar(node_name, name.data(), scalar);
}

void SetInt(const char* node_name, string& name, string& value)
{
	ValidateName(node_name, name);
	int intvalue = atoi(value.data());
	ei_override_int(node_name, name.data(), intvalue);
}

void SetEnum(const char* node_name, string& name, string& value)
{
	ValidateName(node_name, name);
	ei_override_enum(node_name, name.data(), value.data());
}

vector<pair<string, string>> ParseParameters(const char* parameter)
{
	string strParam(parameter);
	int lastPos = 0;
	vector<string> allParams;
	SplitString(strParam, ";", allParams);
	vector<pair<string, string>> paramPairs;
    vector<string>::iterator it = allParams.begin();
    for (; it != allParams.end(); ++it)
	{
        paramPairs.push_back(SplitParam(*it));
	}
	return paramPairs;
}

void SetParameters(const char* node_name, vector<pair<string, string>>& paramPairs)
{
    vector<pair<string, string>>::iterator it = paramPairs.begin();
    for (; it != paramPairs.end(); ++it)
	{
        auto mapData = param_type_map.find(it->first);
		if (mapData == param_type_map.end())
		{
            printf("Unknow parameter: %s\n", it->first.data());
			exit(1);
		}
		switch (mapData->second)
		{
		case is_ei_bool:
            SetBool(node_name, it->first, it->second);
			break;
		case is_ei_int:
            SetInt(node_name, it->first, it->second);
			break;
		case is_ei_enum:
            SetEnum(node_name, it->first, it->second);
			break;
		case is_ei_scalar:
            SetScalar(node_name, it->first, it->second);
			break;
		default:
			printf("Unknow parameter: %s\n", mapData->first.data());
			exit(1);
			break;
		}
	}	
}

int main(int argc, const char *argv[])
{
    param_type_map["min_samples"] = is_ei_int;
    param_type_map["max_samples"] = is_ei_int;
    param_type_map["progressive"] = is_ei_bool;
    param_type_map["bucket_size"] = is_ei_int;
    param_type_map["filter"] = is_ei_enum;
    param_type_map["filter_size"] = is_ei_scalar;
    param_type_map["max_displace"] = is_ei_scalar;
    param_type_map["motion"] = is_ei_bool;
    param_type_map["diffuse_samples"] = is_ei_int;
    param_type_map["sss_samples"] = is_ei_int;
    param_type_map["volume_indirect_samples"] = is_ei_int;
    param_type_map["diffuse_depth"] = is_ei_int;
    param_type_map["sum_depth"] = is_ei_int;
    param_type_map["shadow"] = is_ei_bool;
    param_type_map["caustic"] = is_ei_bool;
    param_type_map["bias"] = is_ei_scalar;
    param_type_map["step_size"] = is_ei_scalar;
    param_type_map["lens"] = is_ei_bool;
    param_type_map["volume"] = is_ei_bool;
    param_type_map["geometry"] = is_ei_bool;
    param_type_map["displace"] = is_ei_bool;
    param_type_map["imager"] = is_ei_bool;
    param_type_map["texture"] = is_ei_bool;
    param_type_map["use_clamp"] = is_ei_bool;
    param_type_map["clamp_value"] = is_ei_scalar;
    param_type_map["light_cutoff"] = is_ei_scalar;
    param_type_map["engine"] = is_ei_enum;
    param_type_map["accel_mode"] = is_ei_enum;
    param_type_map["GI_cache_density"] = is_ei_scalar;
    param_type_map["GI_cache_radius"] = is_ei_scalar;
    param_type_map["GI_cache_passes"] = is_ei_int;
    param_type_map["GI_cache_points"] = is_ei_int;
    param_type_map["GI_cache_preview"] = is_ei_bool;
    param_type_map["display_gamma"] = is_ei_scalar;
    param_type_map["texture_gamma"] = is_ei_scalar;
    param_type_map["shader_gamma"] = is_ei_scalar;
    param_type_map["light_gamma"] = is_ei_scalar;
    param_type_map["exposure"] = is_ei_bool;
    param_type_map["exposure_value"] = is_ei_scalar;
    param_type_map["exposure_highlight"] = is_ei_scalar;
    param_type_map["exposure_shadow"] = is_ei_scalar;
    param_type_map["exposure_saturation"] = is_ei_scalar;
    param_type_map["exposure_whitepoint"] = is_ei_scalar;
    param_type_map["random_lights"] = is_ei_int;
    //Camera
    param_type_map["res_x"] = is_ei_int;
    param_type_map["res_y"] = is_ei_int;

    convert_native_arguments(argc, argv);
	int ret;

	ret = EXIT_SUCCESS;

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
	fflush(stdout);
	-- argc, ++ argv;

	if (argc == 1 && strcmp(argv[0], "-licsvr") == 0)
	{
		ei_run_license_server();
	}
	else if (argc == 1 && strcmp(argv[0], "-id") == 0)
	{
		ei_print_machine_id();
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
        eiBool sharemem = EI_FALSE;
		eiBool interactive = EI_FALSE;
		eiBool resolution_overridden = EI_FALSE;
		eiInt res_x = -1;
		eiInt res_y = -1;
		eiBool has_license = EI_FALSE;
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

						char var_name[EI_MAX_NODE_NAME_LEN];
						char out_name[EI_MAX_NODE_NAME_LEN];

						sprintf(var_name, "var_%s", name);
						sprintf(out_name, "out_%s", name);

						ei_node("outvar", var_name);
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
								ei_tab_add_node(var_name);
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
				else if (strcmp(argv[i], "-options") == 0)
				{
					i += 1;
					SetParameters("options", ParseParameters(argv[i]));
				}
				else if (strcmp(argv[i], "-camera") == 0)
				{
					i += 1;
					auto parseResult = ParseParameters(argv[i]);
					decltype(parseResult) tempParams;
                    vector<pair<string, string>>::iterator it = parseResult.begin();
                    for (; it != parseResult.end(); ++it)
					{
                        if (it->first == "res_x")
						{
                            res_x = atoi(it->second.data());
							if (res_x != -1)
							{
                                tempParams.push_back(*it);
							}
						}
                        else if (it->first == "res_y")
						{
                            res_y = atoi(it->second.data());
							if (res_y != -1)
							{
                                tempParams.push_back(*it);
							}
						}
					}
                    if (res_x > 0 && res_y > 0)
                    {
                        ei_override_scalar("camera", "aspect", (float)res_x / (float)res_y);
                    }
					SetParameters("camera", tempParams);
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
                else if (strcmp(argv[i], "-sharemem") == 0)
				{
                    sharemem= EI_TRUE;
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
				else if (strcmp(argv[i], "-license") == 0)
				{
					// -texture_openfiles count
					//
					if ((i + 3) < argc)
					{
						const char *code1 = argv[i + 1];
						const char *code2 = argv[i + 2];
						const char *license = argv[i + 3];

						eiUint c1, c2;
						sscanf(code1, "%x", &c1);
						sscanf(code2, "%x", &c2);

						ei_dongle_license_set(c1, c2, license);
						has_license = EI_TRUE;

						i += 3;
					}
					else
					{
						ei_error("No enough arguments specified for command: -license\n");
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

		fflush(stdout);
		if (filename != NULL)
		{
			ei_info("Start parsing file: %s\n", filename);

            if (!ei_parse2(filename, ignore_render || display || interactive || sharemem))
				{
					ei_error("Failed to parse file: %s\n", filename);

					ret = EXIT_FAILURE;
				}

			ei_info("Finished parsing file: %s\n", filename);
			fflush(stdout);

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
                                res_y = (res_y == -1) ? ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_y")) : res_y;
                                res_x = res_y * 6;
                                if (res_y > 0)
                                {
                                    ei_override_scalar("camera", "aspect", (float)res_x / (float)res_y);
                                    vector<pair<string, string>> tempParams;
                                    string sx, sy;
                                    sx.resize(255);
                                    sy.resize(255);
                                    itoa(res_x, &sx[0], 10);
                                    itoa(res_y, &sy[0], 10);
                                    tempParams.push_back(pair<string, string>("res_x", sx));
                                    tempParams.push_back(pair<string, string>("res_y", sy));
                                    SetParameters("camera", tempParams);
                                }
                            }
                        }
                    }
                }
            }

            if (display || interactive || sharemem)
			{
				ei_info("Start display and rendering...\n");
				fflush(stdout);
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
							res_x = (res_x == -1) ? ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_x")) : res_x;
							res_y = (res_y == -1) ? ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_y")) : res_y;
                            RenderProcess rp(res_x, res_y, &render_params, interactive);
                            rp.renderToMem = sharemem;
                            sharedMem.setKey("erConsole_data");


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

                                    eiInt max_dist_samples = ::max(diffuse_samples, ::max(sss_samples, volume_indirect_samples));
									if (max_samples > 16)
									{
                                        max_samples *= ::max(1, max_dist_samples / (max_samples / 16));
									}
									else
									{
										max_samples *= max_dist_samples;
									}

									if (random_lights <= 0)
									{
										max_samples *= 4;
									}
									else
									{
                                        max_samples *= ::max(1, random_lights / 16);
									}

									printf("Interactive samples: %d\n", max_samples);
									ei_node_set_int(opt_node, max_samples_pid, max_samples);

									ei_node_int(opt_node, "diffuse_samples", 1);
									ei_node_int(opt_node, "sss_samples", 1);
									ei_node_int(opt_node, "volume_indirect_samples", 1);
									ei_node_int(opt_node, "random_lights", 16);
                                    ei_node_bool(opt_node, "progressive", EI_TRUE);

									ei_end_edit_node(opt_node);
								}
							}

							ei_job_set_process(&(rp.base));
							ei_timer_reset(&(rp.first_pixel_timer));
							ei_timer_start(&(rp.first_pixel_timer));
							rp.is_first_pass = EI_TRUE;
							ei_render_prepare();
                            guiCmdMem.setKey("erGUI_signal");
                            guiCmdMem.create(sizeof(GUICommand));
							{
								rp.renderThread = ei_create_thread(render_callback, &render_params, NULL);
								ei_set_low_thread_priority(rp.renderThread);
                                if (display)
								{
									ei_display(display_callback, &rp, res_x, res_y);
                                }
                                else
                                {
                                    while(WindowlessProc(&rp))
                                    {

                                    }
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
				fflush(stdout);
			}
		}
		else
		{
			ei_error("Scene file is not specified.\n");

			ret = EXIT_FAILURE;
		}

		if (has_license)
		{
			ei_dongle_license_release();
		}

		ei_end_context();
	}
    guiCmdMem.detach();

	return ret;
}
