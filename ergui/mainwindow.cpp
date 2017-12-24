
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "optiondialog.h"
#include "ergui_buildnum.h"
#include <QFileDialog>
#include <QDebug>
#include <QDrag>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QImageReader>
#include <QImageWriter>
#include <QMessageBox>
#include <QProcess>
#include <QFileInfo>
#include <QClipboard>
#include <QSettings>
#include <QAction>
#include <QDesktopServices>
#include <QStyleFactory>
#include <QUrl>
#include <QtXml/QtXml>

#define W_ICONSIZE 236
#define H_ICONSIZE 130

#define MSG_QUEUE_WAIT_TIME		50

static const char *g_str_on = "on";
static const char *g_str_off = "off";

// Render thread function
static EI_THREAD_FUNC render_callback(void *param);

// Elara dongle license APIs (not in SDK headers)
EI_API void ei_dongle_license_set(eiUint code1, eiUint code2, const char *license_code);
EI_API void ei_dongle_license_release();

// Utilities for render preset parsing
enum {
	is_ei_bool,
	is_ei_scalar,
	is_ei_int,
	is_ei_enum,
	is_ei_token,
	type_count
};

typedef std::map<std::string, int> ParamTypeMap;

ParamTypeMap param_type_map;

void SplitString(
	const std::string & src, 
	const std::string & sep, 
	std::vector<std::string> & result)
{
	if (src.empty())
	{
		return;
	}
	std::string::size_type lastPos = 0;
	std::string::size_type matchPos = src.find_first_of(sep);
	while (matchPos != src.npos)
	{
		result.push_back(src.substr(lastPos, matchPos - lastPos));
		lastPos = matchPos + 1;
		matchPos = src.find_first_of(sep, lastPos);
	}
	if (lastPos < src.length())
	{
		result.push_back(src.substr(lastPos, src.length() - lastPos));
	}
}

std::pair<std::string, std::string> SplitParam(const std::string & src)
{
	std::string::size_type idx = src.find_first_of('=');
	std::string name = src.substr(0, idx);
	std::string temp = src.substr(idx + 1, src.length() - idx - 1);
	std::string value;
	value.resize(temp.length());
	// To lower case
	transform(temp.begin(), temp.end(), temp.begin(), ::tolower);
	// Remove space
	std::string::size_type charStart = temp.find_first_not_of(" ");
	std::string::size_type charEnd = temp.find_last_not_of(" ");
	value = temp.substr(charStart, charEnd - charStart + 1);
    return std::pair<std::string, std::string>(name, value);
}

void ValidateName(const char *node_name, std::string & name)
{
	if (param_type_map.find(name) == param_type_map.end())
	{
		EI_ASSERT(0);
	}
}

void SetBool(const char *node_name, std::string & name, std::string & value)
{
	ValidateName(node_name, name);
	if (value == "on" || 
		value == "true")
	{
		ei_override_bool(node_name, name.data(), EI_TRUE);
	}
	else if (value == "off" || 
		value == "false")
	{
        ei_override_bool(node_name, name.data(), EI_FALSE);
	}
	else
	{
		EI_ASSERT(0);
	}
}

void SetScalar(const char *node_name, std::string & name, std::string & value)
{
	ValidateName(node_name, name);
	float scalar = (float)atof(value.data());
	ei_override_scalar(node_name, name.data(), scalar);
}

void SetInt(const char *node_name, std::string & name, std::string & value)
{
	ValidateName(node_name, name);
	int intvalue = atoi(value.data());
	ei_override_int(node_name, name.data(), intvalue);
}

void SetEnum(const char *node_name, std::string & name, std::string & value)
{
	ValidateName(node_name, name);
	ei_override_enum(node_name, name.data(), value.data());
}

std::vector<std::pair<std::string, std::string> > ParseParameters(
	const char *parameter)
{
	std::string strParam(parameter);
	int lastPos = 0;
	std::vector<std::string> allParams;
	SplitString(strParam, ";", allParams);
	std::vector<std::pair<std::string, std::string> > paramPairs;
    for (std::vector<std::string>::iterator it = allParams.begin(); 
		it != allParams.end(); ++it)
	{
        paramPairs.push_back(SplitParam(*it));
	}
	return paramPairs;
}

void SetParameters(
	const char *node_name, 
	std::vector<std::pair<std::string, std::string> > & paramPairs)
{
    for (std::vector<std::pair<std::string, std::string> >::iterator it = paramPairs.begin(); 
		it != paramPairs.end(); ++it)
	{
        ParamTypeMap::iterator mapData = param_type_map.find(it->first);
		if (mapData == param_type_map.end())
		{
			EI_ASSERT(0);
            continue;
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
			EI_ASSERT(0);
			break;
		}
	}	
}

QAtomicInt g_logDirty;
QMutex g_logMutex;
QString g_logMsg;

static void verbose_callback(eiInt level, const char *format, va_list args)
{
	QString localMsg;

	switch (level)
	{
	case EI_VERBOSE_FATAL:
		{
			localMsg = "FATAL> ";
		}
		break;
	case EI_VERBOSE_ERROR:
		{
			localMsg = "ERROR> ";
		}
		break;
	case EI_VERBOSE_WARNING:
		{
			localMsg = "WARNING> ";
		}
		break;
	case EI_VERBOSE_INFO:
		{
			localMsg = "INFO> ";
		}
		break;
	case EI_VERBOSE_DEBUG:
	default:
		{
			localMsg = "DEBUG> ";
		}
		break;
	}

	localMsg += QString::vasprintf(format, args);

	g_logMutex.lock();
	{
		g_logMsg += localMsg;
		g_logDirty = EI_TRUE;
	}
	g_logMutex.unlock();
}

// Render process management
RenderProcess::RenderProcess()
{
	init_callbacks();
	renderThread = NULL;
	bufferLock = ei_create_rwlock();
	ei_atomic_swap(&bufferDirty, EI_FALSE);
	imageWidth = -1;
	imageHeight = -1;
	memset(&render_params, 0, sizeof(eiRenderParameters));
	last_job_percent = 0.0f;
	ei_timer_reset(&first_pixel_timer);
	is_first_pass = EI_FALSE;
	has_license = EI_FALSE;
}

RenderProcess::~RenderProcess()
{
	ei_delete_rwlock(bufferLock);
}

void RenderProcess::start_render(
	const char *ESS_filename, 
	const char *code1, 
	const char *code2, 
	const char *license, 
	const char *texture_searchpath, 
	const char *image_filename, 
	bool use_filter, 
	bool use_gamma, 
	bool use_exposure, 
	const char *options_params, 
	const char *camera_params, 
	bool use_panorama)
{
	// Don't start a new render if last one is still rendering
	if (renderThread != NULL)
	{
		return;
	}

	// New rendering context
	ei_context();

	// Redirect logs to text edit
	ei_verbose_set_callback(verbose_callback);

	// Set dongle license if available
	has_license = EI_FALSE;
	if (code1 != NULL && strlen(code1) > 0 && 
		code2 != NULL && strlen(code2) > 0 && 
		license != NULL && strlen(license) > 0)
	{
		eiUint c1, c2;
		sscanf(code1, "%x", &c1);
		sscanf(code2, "%x", &c2);
		ei_dongle_license_set(c1, c2, license);
		has_license = EI_TRUE;
	}

	// Add texture search path
	if (texture_searchpath != NULL && strlen(texture_searchpath) > 0)
	{
		ei_add_texture_searchpath(texture_searchpath);
	}

	// Setup image output
	eiTag output_list = EI_NULL_TAG;
	imageWidth = -1;
	imageHeight = -1;

	const char *name = "color";
	char var_name[EI_MAX_NODE_NAME_LEN];
	char out_name[EI_MAX_NODE_NAME_LEN];

	sprintf(var_name, "var_%s", name);
	sprintf(out_name, "out_%s", name);

	ei_node("outvar", var_name);
		ei_param_token("name", name);
		ei_param_int("type", EI_TYPE_COLOR);
		ei_param_bool("filter", use_filter ? EI_TRUE : EI_FALSE);
		ei_param_bool("use_gamma", use_gamma ? EI_TRUE : EI_FALSE);
		ei_param_bool("use_exposure", use_exposure ? EI_TRUE : EI_FALSE);
	ei_end_node();

	ei_node("output", out_name);
		ei_param_token("filename", (image_filename != NULL) ? image_filename : "");
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

	if (output_list != EI_NULL_TAG)
	{
		ei_override_array("camera", "output_list", output_list);
	}

	// Parse options parameters
	if (options_params != NULL)
	{
		SetParameters("options", ParseParameters(options_params));
	}

	// Parse camera parameters
	if (camera_params != NULL)
	{
		std::vector<std::pair<std::string, std::string> > parseResult = ParseParameters(camera_params);
		std::vector<std::pair<std::string, std::string> > tempParams;
        for (std::vector<std::pair<std::string, std::string> >::iterator it = parseResult.begin(); 
			it != parseResult.end(); ++it)
		{
            if (it->first == "res_x")
			{
                imageWidth = atoi(it->second.data());
				if (imageWidth != -1)
				{
                    tempParams.push_back(*it);
				}
			}
            else if (it->first == "res_y")
			{
                imageHeight = atoi(it->second.data());
				if (imageHeight != -1)
				{
                    tempParams.push_back(*it);
				}
			}
		}
        if (imageWidth > 0 && imageHeight > 0)
        {
            ei_override_scalar("camera", "aspect", (float)imageWidth / (float)imageHeight);
        }
		SetParameters("camera", tempParams);
	}

	// Setup panorama rendering if required
	std::string lens_shader;
	if (use_panorama)
    {
		ei_link("liber_shader");

        const char *shader = "cubemap_camera";
        lens_shader = std::string(shader) + std::string("_OverrideLensShaderInstance");

        ei_node(shader, lens_shader.data());
            ei_param_bool("stereo", EI_FALSE);
            ei_param_scalar("eye_distance", 0.0f);
        ei_end_node();
    }

	// Load ESS file, ignoring render command in it
	if (!ei_parse2(ESS_filename, EI_TRUE))
	{
		return;
	}

	// Get parameters of the last render command
	if (!ei_get_last_render_params(&render_params))
    {
		return;
	}

	// Override camera parameters
    eiTag cam_inst_tag = ei_find_node(render_params.camera_inst);
    if (cam_inst_tag != EI_NULL_TAG)
    {
        eiDataAccessor<eiNode> cam_inst(cam_inst_tag);
        eiTag cam_item_tag = ei_node_get_node(cam_inst.get(), ei_node_find_param(cam_inst.get(), "element"));
        if (cam_item_tag != EI_NULL_TAG)
        {
            eiDataAccessor<eiNode> cam_item(cam_item_tag);

			// Get existing resolution parameters in ESS
			imageWidth = (imageWidth == -1) ? ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_x")) : imageWidth;
			imageHeight = (imageHeight == -1) ? ei_node_get_int(cam_item.get(), ei_node_find_param(cam_item.get(), "res_y")) : imageHeight;

			// Apply panorama parameters
            eiTag lens_shader_tag = ei_find_node(lens_shader.data());
            if (lens_shader_tag != EI_NULL_TAG)
            {
				// Use lens shader for panorama rendering
                ei_node_node(cam_item.get(), "lens_shader", lens_shader_tag);

				// Use special resolution for panorama rendering
                imageWidth = imageHeight * 6;
                if (imageHeight > 0)
                {
                    ei_override_scalar("camera", "aspect", (float)imageWidth / (float)imageHeight);
                    std::vector<std::pair<std::string, std::string> > tempParams;
                    std::string sx, sy;
                    sx.resize(255);
                    sy.resize(255);
                    itoa(imageWidth, &sx[0], 10);
                    itoa(imageHeight, &sy[0], 10);
                    tempParams.push_back(std::pair<std::string, std::string>("res_x", sx));
                    tempParams.push_back(std::pair<std::string, std::string>("res_y", sy));
                    SetParameters("camera", tempParams);
                }
            }
        }
    }

	if (imageWidth <= 0 || imageHeight <= 0)
	{
		return;
	}

	// Setup render progress
	last_job_percent = 0.0f;
	// Setup image buffer
	const eiRGBA transpColor = {0.0f, 0.0f, 0.0f, 0.0f};
	ei_write_lock(bufferLock);
	{
		originalBuffer.resize(imageWidth * imageHeight, transpColor);
		ei_atomic_swap(&bufferDirty, EI_TRUE);
	}
	ei_write_unlock(bufferLock);

	// Setup render process callbacks
	ei_job_set_process(&base);

	// Setup pixel timer
	ei_timer_reset(&first_pixel_timer);
	ei_timer_start(&first_pixel_timer);
	is_first_pass = EI_TRUE;

	// Prepare rendering in main thread (important)
	ei_render_prepare();

	// Spawn a separate render thread to be non-blocking
	renderThread = ei_create_thread(render_callback, &render_params, NULL);
	ei_set_low_thread_priority(renderThread);
}

void RenderProcess::stop_render()
{
	// Return if not in rendering
	if (renderThread == NULL)
	{
		return;
	}

	// Signal to abort the current render
	ei_job_abort(EI_TRUE);

	// Wait for render thread to finish
	ei_wait_thread(renderThread);
	ei_delete_thread(renderThread);
	renderThread = NULL;

	// Cleanup rendering in main thread (important)
	ei_render_cleanup();

	// Remove render process callbacks
	ei_job_set_process(NULL);

	// Release dongle license if activated
	if (has_license)
	{
		ei_dongle_license_release();
	}

	// Shutdown rendering context
	ei_end_context();
}

void RenderProcess::update_render_view(MainWindow *mainWindow)
{
	// Update render image if buffer is dirty
	if (ei_atomic_read(&bufferDirty))
	{
		eiRGBA *rawData = &originalBuffer[0];

		// Reduce the locking scope as much as possible to 
		// improve multi-threading performance
		bool bufferUpdated = false;
		ei_write_lock(bufferLock);
		{
			// Check again in locking scope
			if (ei_atomic_read(&bufferDirty))
			{
				mainWindow->UpdateRenderImage(imageWidth, imageHeight, (float *)rawData);
				ei_atomic_swap(&bufferDirty, EI_FALSE);
				bufferUpdated = true;
			}
		}
		ei_write_unlock(bufferLock);

		if (bufferUpdated)
		{
			mainWindow->RefreshRenderWindow();
		}
	}
}

void RenderProcess::update_render(MainWindow *mainWindow)
{
	if (!ei_job_aborted())
	{
		// Rendering, update job progress
		const eiScalar job_percent = (eiScalar)ei_job_get_percent();
		if (absf(last_job_percent - job_percent) >= 0.5f)
		{
			mainWindow->UpdateRenderProgress((int)(job_percent * 100.0f));

			last_job_percent = job_percent;
		}

		update_render_view(mainWindow);
	}
	else
	{
		// Not rendering, try to abort and cleanup
		stop_render();
		update_render_view(mainWindow);

		mainWindow->onRenderFinished();
	}
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

	// Access cached image buckets
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

    const eiRect4i & fb_rect = infoFrameBufferCache.m_rect;

	// Write bucket updates into the original buffer
	const eiInt imageWidth = rp->imageWidth;
	const eiInt imageHeight = rp->imageHeight;
	eiRGBA *originalBuffer = &(rp->originalBuffer[0]);
	originalBuffer += ((imageHeight - 1 - pJob->rect.top) * imageWidth + pJob->rect.left);
	ei_read_lock(rp->bufferLock);
	{
		for (eiInt j = fb_rect.top; j < fb_rect.bottom; ++j)
		{
			for (eiInt i = fb_rect.left; i < fb_rect.right; ++i)
			{
				eiRGBA & pixel = originalBuffer[i - fb_rect.left];
				ei_framebuffer_cache_get_final(&colorFrameBufferCache, i, j, &(pixel.rgb));
                eiColor alpha;
                ei_framebuffer_cache_get_final(&opacityFrameBufferCache, i, j, &alpha);
				pixel.a = alpha.average();
			}
			originalBuffer -= imageWidth;
		}
		// Mark buffer dirty
		ei_atomic_swap(&(rp->bufferDirty), EI_TRUE);
	}
	ei_read_unlock(rp->bufferLock);

	ei_framebuffer_cache_exit(&colorFrameBufferCache);
    ei_framebuffer_cache_exit(&opacityFrameBufferCache);
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

	// Register user thread to access scene database
	ei_job_register_thread();

	// Call the blocking render function in render thread
	ei_render_run(render_params->root_instgroup, render_params->camera_inst, render_params->options);

	// Unregister user thread
	ei_job_unregister_thread();

	return (EI_THREAD_FUNC_RESULT)EI_TRUE;
}

inline QString GetResultPath(const QString& sceneFile)
{
    return sceneFile.left(sceneFile.lastIndexOf('.'));
}

QIcon CreateThumb(const QImage* image)
{
    if (nullptr == image) return QIcon();
    if ((float)image->width() / (float)image->height() < (196.0f / 108.0f))
    {
        float height = (float)image->width() * H_ICONSIZE / W_ICONSIZE;
        int y = (image->height() - height) / 2;
        QImage tmpthumb = image->copy(0, y, image->width(), height);
        return QIcon(QPixmap::fromImage(tmpthumb.scaledToWidth(W_ICONSIZE)));
    }
    else
    {
        float width = (float)image->height() * W_ICONSIZE / H_ICONSIZE;
        int x = (image->width() - width) / 2;
        QImage tmpthumb = image->copy(x, 0, width, image->height());
        return QIcon(QPixmap::fromImage(tmpthumb.scaledToHeight(H_ICONSIZE)));
    }
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    mProjectName("Untitled"),
    mbProjectDirty(false),
    mLastLayout(0)
{
	// Setup options parameters
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
	param_type_map["GI_cache_screen_scale"] = is_ei_scalar;
    param_type_map["GI_cache_passes"] = is_ei_int;
    param_type_map["GI_cache_points"] = is_ei_int;
    param_type_map["GI_cache_preview"] = is_ei_enum;
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
    // Camera parameters
    param_type_map["res_x"] = is_ei_int;
    param_type_map["res_y"] = is_ei_int;

    ui->setupUi(this);

	// We use a timer to update progress and image
    mSharedMemTimer.setInterval(MSG_QUEUE_WAIT_TIME);

    connect(&mSharedMemTimer,
            &QTimer::timeout,
            this,
            &MainWindow::onSharedMemTimer);

    ui->lstFiles->setIconSize(QSize(W_ICONSIZE, H_ICONSIZE));
    ui->lstFiles->setViewMode(QListView::IconMode);

    QAction *actionDeleteFile = new QAction("delete", this);
    connect(actionDeleteFile,
            &QAction::triggered,
            this,
            &MainWindow::onActionDeleteFile_triggered);
    ui->lstFiles->addAction(actionDeleteFile);
    ui->tvPreset->setColumnWidth(0, 190);
    ui->tvPreset->setColumnWidth(1, 70);

    tabifyDockWidget(ui->dockFileView, ui->dockPreset);
    ui->dockFileView->raise();
    ui->statusBar->addWidget(&mScaleText);
    ui->statusBar->addWidget(&mScaleText);
    ui->statusBar->addWidget(&mStatusText);
    ui->statusBar->addWidget(&mpgsRender);
    mStatusText.setText(tr("    Ready    "));
    mScaleText.setText(tr("    Image Scale: ") + "100%  ");
    mpgsRender.setMaximum(10000);
    mpgsRender.setVisible(false);

    ui->imageViewer->SetToneEnabled(ui->grpExposureCtrl->isChecked());
    QSettings setting(QApplication::applicationDirPath() + "/ergui.ini", QSettings::IniFormat);
    if (setting.contains("Tools/TexturePath"))
    {
        mTexturePath = setting.value("Tools/TexturePath").toString();
    }

    InitializePresets();
    ApplyToneMapper();
    QApplication::setStyle(QStyleFactory::create("Fusion"));
}

MainWindow::~MainWindow()
{
    mRenderProcess.stop_render();
    delete ui;
}

void MainWindow::RenderTasks()
{
    for (int i = 0; i < ui->lstFiles->count(); ++i)
    {
        QListWidgetItem* pItem = ui->lstFiles->item(i);
        QFileInfo info(pItem->data(Qt::UserRole).toString());
        if (info.exists())
        {
            mQueue.push(info.absoluteFilePath());
            mFilesInQueue.insert(info.absoluteFilePath());
        }
    }
    ui->btnRender->setEnabled(mFilesInQueue.size() > 0);
    RenderNext();
}

void MainWindow::SetPreset(QString &presetName)
{
    if (presetName.isEmpty()) return;
    int idx = ui->cmbPreset->findText(presetName, Qt::MatchStartsWith);
    if (idx != -1)
    {
        ui->cmbPreset->setCurrentIndex(idx);
        ui->smpPreset->setCurrentIndex(idx);
    }
}

void MainWindow::SetCode1(QString &c)
{
	mCode1 = c;
}

void MainWindow::SetCode2(QString &c)
{
	mCode2 = c;
}

void MainWindow::SetLicense(QString &l)
{
    mLicense = l;
}

void MainWindow::EnablePanorama(bool value)
{
    ui->chkPanorama->setChecked(value);
}

void MainWindow::SafeClose()
{
    while(!mQueue.empty()) mQueue.pop();
    mFilesInQueue.clear();
    close();
}

void MainWindow::AddESSFiles(QStringList &list, bool dontDirty)
{
    QStringList::Iterator it = list.begin();
    for (; it != list.end(); ++it)
    {
        QFileInfo info(*it);
        if (info.exists())
        {
            AddFileItem(*it);
        }
    }
    if (!dontDirty)
    {
        mbProjectDirty = list.count() > 0;
    }
}

void MainWindow::UpdateImageStatus(QListWidgetItem* item, bool resetView)
{
    if (nullptr == item)
    {
        ui->imageViewer->Reset();
        return;
    }
    QString& sceneFile = item->data(Qt::UserRole).toString();
    if (mFilesInQueue.find(sceneFile) != mFilesInQueue.end()
            || mCurrentScene == sceneFile)
    {
        ui->btnRender->setText(tr("Cancel(Esc)"));
        ui->smpRender->setText(tr("Cancel(Esc)"));
    }
    else
    {
        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
    }

    if (!mCurrentScene.isEmpty()) return;

    QString resultPath = GetResultPath(sceneFile) + "/temp.erc";
    QFileInfo info(resultPath);
    if (!info.exists())
    {
        ui->imageViewer->Reset();
        return;
    }

    ui->imageViewer->LoadFromCache(resultPath);
    if (resetView) ui->imageViewer->FitImage();
    ui->imageViewer->Refresh();
    item->setIcon(CreateThumb(ui->imageViewer->image()));

    QString title(tr("Elara Renderer"));
    title += " - ";
    title += sceneFile;
    this->setWindowTitle(title);
}

void MainWindow::FileItemChanged(QListWidgetItem * item)
{
    UpdateImageStatus(item, true);
}

void MainWindow::FitImage()
{
    ui->imageViewer->FitImage();
}

void MainWindow::FitControl()
{
    ui->imageViewer->FitControl();
}

void MainWindow::ShowOption()
{
    OptionDialog optDlg(this);
    optDlg.exec();
    QSettings setting(QApplication::applicationDirPath() + "/ergui.ini", QSettings::IniFormat);
    if (setting.contains("Tools/TexturePath"))
    {
        mTexturePath = setting.value("Tools/TexturePath").toString();
    }
}

void UpdateThumbnail(QListWidget* lstFiles, QString& key, QCustomLabel* imgViewer, bool saveCache)
{
    for (int i = 0; i < lstFiles->count(); ++i)
    {
        auto item = lstFiles->item(i);
        QString itemScene = item->data(Qt::UserRole).toString();
        if (itemScene != key) continue;
        item->setIcon(CreateThumb(imgViewer->image()));
        if (saveCache)
        {
            QString sceneResultPath = GetResultPath(key);
                imgViewer->SaveToCache(sceneResultPath + "/temp.erc");
                imgViewer->Refresh();
            }
        }
}

void MainWindow::UpdateRenderProgress(int value)
{
	mpgsRender.setValue(value);
}

void MainWindow::UpdateRenderImage(int width, int height, float *data)
{
	ui->imageViewer->SetRawData(width, height, data);
}

void MainWindow::RefreshRenderWindow()
{
	ui->imageViewer->Refresh();
}

void MainWindow::onRenderFinished()
{
    mSharedMemTimer.stop();
	FlushRenderLog();
    UpdateThumbnail(ui->lstFiles, mCurrentScene, ui->imageViewer, true);
    if (ui->lstFiles->currentItem()->data(Qt::UserRole).toString() == mCurrentScene)
    {
        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
    }
    mpgsRender.setValue(mpgsRender.maximum());
    mpgsRender.setVisible(false);
    mCurrentScene = "";
    int espTime = mRenderTime.elapsed() / 1000;
    QString outState = tr("    Last render time: ");
    outState += QString::number(espTime / 3600) + ":";
    espTime %= 3600;
    outState += QString::number(espTime / 60) + ":";
    espTime %= 60;
    outState += QString::number(espTime);
    outState += "   ";
    mStatusText.setText(outState);
    UpdateImageStatus(ui->lstFiles->currentItem(), false);
    RenderNext();
}

void MainWindow::onImageScaleChanged(float value)
{
    mScaleText.setText(tr("Image Scale: ") + QString::number((int)(value * 100)) + "%  ");
}

void MainWindow::FlushRenderLog()
{
	// Qt only allows updating UI in main thread, so 
	// we have to use this complicated buffering
	if (g_logDirty.load())
	{
		QString msgToAppend;

		g_logMutex.lock();
		{
			// Check again in locking scope
			if (g_logDirty.load())
			{
				if (!g_logMsg.isEmpty())
				{
					msgToAppend = g_logMsg;
					g_logMsg.clear();
				}
				g_logDirty = EI_FALSE;
			}
		}
		g_logMutex.unlock();

		if (!msgToAppend.isEmpty())
		{
			// Must use append function here, which seems 
			// to be faster than setPlainText and moveCursor
			ui->txtConsole->append(msgToAppend);
		}
	}
}

void MainWindow::onSharedMemTimer()
{
	mRenderProcess.update_render(this);

	FlushRenderLog();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasText())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasText() && !e->mimeData()->text().isEmpty())
    {
        QString& path = e->mimeData()->text();
        QString fn = path.right(path.length() - path.lastIndexOf('/') - 1);
        QListWidgetItem* fileItem = new QListWidgetItem(QIcon(path), fn);
        fileItem->setData(Qt::UserRole, path);
        ui->lstFiles->addItem(fileItem);
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    NeedCancelAction() ? e->ignore() : e->accept();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape)
    {
        on_btnRender_clicked();
    }
}

void MainWindow::RenderNext()
{
    if (mCurrentScene != "")
	{
		return;
	}
    if (mQueue.empty())
	{
		return;
	}
    QString nextFile = mQueue.front();
    mQueue.pop();
    mFilesInQueue.erase(nextFile);
    QString sceneResultPath = GetResultPath(nextFile);

    QDir dirMaker;
    if (!dirMaker.exists(sceneResultPath))
    {
        dirMaker.mkpath(sceneResultPath);
    }
    else if (QFileInfo::exists(sceneResultPath + "/temp.erc"))
    {
        QFile::remove(sceneResultPath + "/temp.erc");
    }
    mCurrentScene = nextFile;

	const char *image_filename = "temp.png";
	bool use_filter = ui->chkEnableFilter->isChecked();
	bool use_gamma = false; // Assume gamma added by exposure control
	bool use_exposure = false; // We do exposure control in realtime by ourselves

	if (!ui->imageViewer->IsToneEnabled())
	{
		// Exposure control is off, do gamma by ourselves
		use_gamma = ui->chkEnableGamma->isChecked();
	}

    if (ui->lstFiles->currentItem()->data(Qt::UserRole).toString() == nextFile)
    {
        ui->btnRender->setText(tr("Cancel(Esc)"));
        ui->smpRender->setText(tr("Cancel(Esc)"));
    }
    ui->txtConsole->setText("");
    ui->imageViewer->Reset();

	QTreeWidgetItem* pCurItem = ui->tvPreset->currentItem();
    if (pCurItem)
    {
        on_tvPreset_currentItemChanged(nullptr, pCurItem);
    }
	QString options_params;
    QString camera_params;
    bool usePresetCam = ui->actionSimple_Style->isChecked();
    bool progressiveAdded = false;
    for (int i = 0; i < ui->tvPreset->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *pGroupItem = ui->tvPreset->topLevelItem(i);
		if (pGroupItem->text(0) == "options")
		{
			options_params += " -" + pGroupItem->text(0) + " \"";
			for (int childIdx = 0; childIdx < pGroupItem->childCount(); ++childIdx)
			{
				QTreeWidgetItem *pChildItem = pGroupItem->child(childIdx);
				options_params += pChildItem->text(0) + "=" + pChildItem->text(1) +";";
			}
			if (!progressiveAdded)
			{
				options_params += "progressive=";
				options_params += ui->smpProgressive->isChecked() ? "on;" : "off;";
				progressiveAdded = true;
			}
			options_params += "\"";
		}
		else if (pGroupItem->text(0) == "camera")
		{
			if (usePresetCam)
			{
				continue;
			}

			camera_params += " -" + pGroupItem->text(0) + " \"";
			for (int childIdx = 0; childIdx < pGroupItem->childCount(); ++childIdx)
			{
				QTreeWidgetItem *pChildItem = pGroupItem->child(childIdx);
				camera_params += pChildItem->text(0) + "=" + pChildItem->text(1) +";";
			}
			camera_params += "\"";
		}
		else
		{
			EI_ASSERT(0);
		}
    }
    bool usePano = false;
    if (usePresetCam)
    {
        QString preStr = ui->smpResolution->currentText();
        int x = -1;
        int y = -1;
        if (preStr.contains(':'))
        {
            usePano = true;
            preStr = preStr.split(": ")[1];
            y = preStr.toInt();
            x = y * 6;
        }
        else
        {
            QStringList strRes = preStr.split(" x ");
            x = strRes[0].toInt();
            y = strRes[1].toInt();
        }

        camera_params += " -camera \"res_x=" + QString::number(x) + ";res_y=" + QString::number(y) + ";\"";
    }
    else if (ui->chkPanorama->isChecked())
    {
        usePano = true;
    }

	mRenderProcess.start_render(
		// ESS file to render
		nextFile.toUtf8().data(), 
		// License parameters
		mCode1.toUtf8().data(), 
		mCode2.toUtf8().data(), 
		mLicense.toUtf8().data(), 
		// Configurations
		mTexturePath.toUtf8().data(), 
		// Output parameters
		image_filename, 
		use_filter, 
		use_gamma, 
		use_exposure, 
		// Options parameters
		options_params.toUtf8().data(), 
		// Camera parameters
		camera_params.toUtf8().data(), 
		usePano);
    mSharedMemTimer.start();
    mpgsRender.setValue(0);
    mpgsRender.setVisible(true);
    mStatusText.setText(tr("    Rendering...   "));
    mRenderTime.start();
}

void MainWindow::AddFileItem(QString &filename)
{
    QFileInfo fileInfo(filename);
    if (!fileInfo.exists()) return;
    QString fn = fileInfo.fileName();
    QPixmap defaultIcon(":/images/default_logo");
    QListWidgetItem* fileItem = new QListWidgetItem(QIcon(defaultIcon.scaled(QSize(W_ICONSIZE,H_ICONSIZE))), fn);
    fileItem->setData(Qt::UserRole, fileInfo.absoluteFilePath());
    ui->lstFiles->addItem(fileItem);
    ui->lstFiles->clearSelection();
    ui->lstFiles->setCurrentItem(fileItem, QItemSelectionModel::Select);
    fileItem->setToolTip(filename);
    FileItemChanged(fileItem);
    ui->btnRender->setEnabled(true);
}

void MainWindow::on_btnRender_clicked()
{
    if (ui->lstFiles->currentItem() == nullptr) return;
    QString fileToRender = ui->lstFiles->currentItem()->data(Qt::UserRole).toString();
    if (!CancelJob())//Launch new job
    {
        mQueue.push(fileToRender);
        mFilesInQueue.insert(fileToRender);
        ui->btnRender->setText(tr("Cancel(Esc)"));
        ui->smpRender->setText(tr("Cancel(Esc)"));
    }
    RenderNext();
}

bool MainWindow::CancelJob()
{
    if (nullptr == ui->lstFiles->currentItem()) return false;

    QString fileToRender = ui->lstFiles->currentItem()->data(Qt::UserRole).toString();
    if (mCurrentScene == fileToRender) //Cancel current job
    {
        mRenderProcess.stop_render();
		mRenderProcess.update_render_view(this);
		FlushRenderLog();

        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
        return true;
    }
    else if (mFilesInQueue.find(fileToRender) != mFilesInQueue.end()) //Already in queue, we need to cancel it.
    {
        RenderQueue newQueue;
        while(!mQueue.empty())
        {
            QString it = mQueue.front();
            mQueue.pop();
            if (it != fileToRender)
            {
                newQueue.push(it);
            }
        }
        mQueue.swap(newQueue);
        mFilesInQueue.erase(fileToRender);
        ui->btnRender->setText(tr("Render"));
        ui->smpRender->setText(tr("Render"));
        return true;
    }
    return false;
}

void MainWindow::InitializePresets()
{
    QDir pstDir(QApplication::applicationDirPath());
    QStringList pstFilter;
    pstFilter << "*.preset";
    QFileInfoList fiList = pstDir.entryInfoList(pstFilter);
    ui->cmbPreset->addItem(tr("None"));
    ui->smpPreset->addItem(tr("None"));
    QFileInfoList::Iterator it = fiList.begin();
    for (; it != fiList.end(); ++it)
    {
        ui->cmbPreset->addItem(it->baseName());
        ui->smpPreset->addItem(it->baseName());
    }
}

void MainWindow::onActionDeleteFile_triggered()
{
    if (nullptr == ui->lstFiles->currentItem()) return;
    CancelJob();
    int curRow = ui->lstFiles->currentRow();
    QListWidgetItem* curItem = ui->lstFiles->takeItem(curRow);
    delete curItem;
    if (ui->lstFiles->count() == 0)
    {
        ui->btnRender->setEnabled(false);
    }
    FileItemChanged(ui->lstFiles->currentItem());
    mbProjectDirty = true;
}

bool MainWindow::NeedCancelAction()
{
    if (mbProjectDirty)
    {
        int question = QMessageBox::question(this,
                                             tr("Confirm Save"),
                                             tr("Do you want to save the project?"),
                                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (question == QMessageBox::Yes)
        {
            on_action_Save_triggered();
        }
        else if (question == QMessageBox::Cancel)
        {
            return true;
        }
    }
    return false;
}

void MainWindow::on_action_New_triggered()
{
    if (NeedCancelAction()) return;
    mProjectName = tr("Untitled");
    CancelJob();
    FileItemChanged(nullptr);
    ui->lstFiles->clear();
}

void MainWindow::on_action_Document_triggered()
{
    QDesktopServices::openUrl(QUrl("https://github.com/ElaraFX/elaradoc/wiki"));
}

void MainWindow::on_action_Contact_Us_triggered()
{
    QDesktopServices::openUrl(QUrl("mailto:marketing@rendease.com"));
}

void MainWindow::on_action_Homepage_triggered()
{
    QDesktopServices::openUrl(QUrl("http://rendease.com/"));
}

void MainWindow::PresetChanged(const QString& arg1)
{
    ui->tvPreset->clear();
    QString pstFile = QApplication::applicationDirPath() + "/" + arg1;
    if (!QFile::exists(pstFile))
    {
        pstFile += ".preset";
        if (!QFile::exists(pstFile))
        {
            return;
        }
    }

    QSettings setting(pstFile, QSettings::IniFormat);
    QStringList groups = setting.childGroups();
    for (QStringList::Iterator it = groups.begin();
         it != groups.end();
         ++it)
    {
        QTreeWidgetItem* pGroupItem = new QTreeWidgetItem(QStringList() << *it);
        ui->tvPreset->addTopLevelItem(pGroupItem);
        setting.beginGroup(*it);
        QStringList keys = setting.allKeys();
        for (QStringList::Iterator keyIt = keys.begin();
             keyIt != keys.end();
             ++keyIt)
        {
            QString val = setting.value(*keyIt).toString();
            QTreeWidgetItem* paramItem = new QTreeWidgetItem(QStringList() << *keyIt << val);
            paramItem->setFlags(paramItem->flags() | Qt::ItemIsEditable);
            pGroupItem->addChild(paramItem);
        }
        setting.endGroup();
    }
    ui->tvPreset->expandAll();
}

void MainWindow::on_cmbPreset_currentIndexChanged(const QString &arg1)
{
    PresetChanged(arg1);
}

void MainWindow::on_btnSavePreset_pressed()
{
    QFileDialog dialog(this, tr("Save Preset As"));
    dialog.setNameFilter(tr("Elara Preset (*.preset)"));
    dialog.setDefaultSuffix("preset");
    QString presetName = ui->cmbPreset->currentText();
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.selectFile(presetName);
    dialog.exec();
    if (dialog.selectedFiles().count() == 0)
    {
        return;
    }
    QFileInfo info(dialog.selectedFiles().first());
    QSettings* presetWriter = new QSettings(info.absoluteFilePath(), QSettings::IniFormat);
    presetWriter->clear();
    for (int i = 0; i < ui->tvPreset->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* pGroupItem = ui->tvPreset->topLevelItem(i);
        presetWriter->beginGroup(pGroupItem->text(0));
        for (int childIdx = 0; childIdx < pGroupItem->childCount(); ++childIdx)
        {
            QTreeWidgetItem* pChildItem = pGroupItem->child(childIdx);
            presetWriter->setValue(pChildItem->text(0), pChildItem->text(1));
        }
        presetWriter->endGroup();
    }
    delete presetWriter; //Delete QSettings to force writing to disk
    presetWriter = nullptr;

    if (ui->cmbPreset->findText(info.baseName(), Qt::MatchStartsWith) == -1)
    {
        ui->cmbPreset->addItem(info.baseName());
        ui->cmbPreset->setCurrentIndex(ui->cmbPreset->count() - 1);
    }
}

void MainWindow::on_action_Copy_triggered()
{
    if (mCurrentScene != "") return;
    if (ui->imageViewer->image() == nullptr) return;
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setImage(*(ui->imageViewer->image()));
}

void MainWindow::on_actionAdd_Ess_File_triggered()
{
    QStringList essFiles = QFileDialog::getOpenFileNames(
                this,
                tr("Open ESS File"),
                ".",
                tr("Elara Scene Script File (*.ess)"));

    AddESSFiles(essFiles, false);
}

void MainWindow::on_action_Save_triggered()
{
    if (!mbProjectDirty) return;
    if (mProjectName == "Untitled")
    {
        mProjectName = QFileDialog::getSaveFileName(
                    this,
                    tr("Save Project File"),
                    ".",
                    tr("Elara Project File (*.erp)"));
    }
    if (mProjectName.isEmpty())
    {
        mProjectName = "Untitled";
        return;
    }

    QFile projFile(mProjectName);
    if (!projFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Warning"), tr("Failed to save project!"));
        return;
    }
    for (int i = 0; i < ui->lstFiles->count(); ++i)
    {
        QListWidgetItem* pItem = ui->lstFiles->item(i);
        QString essFile = pItem->data(Qt::UserRole).toString() + "\r\n";
        projFile.write(essFile.toStdString().data());
    }
    projFile.close();
    mbProjectDirty = false;
}

void MainWindow::on_action_SaveImage_triggered()
{
    if (!ui->lstFiles->currentItem()
        || ui->imageViewer->image() == nullptr)
    {
        return;
    }
    const QImage* img = ui->imageViewer->image();
    bool splitImage = false;
    if (img->width() == img->height() * 6)
    {
        int question = QMessageBox::question(this,
                                             tr("Split Image"),
                                             tr("Do you want save cubemap faces as 6 files?"),
                                             QMessageBox::Yes | QMessageBox::No);
        splitImage = question == QMessageBox::Yes;
    }
    QFileDialog dialog(this, tr("Save Image As"));
    QStringList mimeTypeFilters;
    const QByteArrayList supportedMimeTypes = QImageWriter::supportedMimeTypes();
    foreach (const QByteArray &mimeTypeName, supportedMimeTypes)
    {
        mimeTypeFilters.append(mimeTypeName);
    }
    mimeTypeFilters.sort();
    dialog.setMimeTypeFilters(mimeTypeFilters);
    dialog.selectMimeTypeFilter("image/png");
    dialog.setDefaultSuffix("png");
    QString origName = ui->lstFiles->currentItem()->data(Qt::UserRole).toString();
    if (origName.lastIndexOf('.') > 0)
    {
        origName = origName.left(origName.lastIndexOf('.'));
    }
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.selectFile(origName);
    dialog.exec();
    if (dialog.result() == QDialog::Rejected)
    {
        return;
    }

    static const QString cubeExt[] = {"_LF", "_RT", "_UP_WEB", "_DN_WEB", "_FR", "_BK"};
    const QString fn = dialog.selectedFiles().first();
    if (splitImage)
    {
        QFileInfo fi(fn);
        int counter = 0;
        for (int x = 0; x < img->width(); x += img->height())
        {
            QImage temp = img->copy(x, 0, img->height(), img->height());
            QString filename = fi.absolutePath() + fi.baseName() + cubeExt[counter] + "." + fi.suffix();
            temp.save(filename);
            ++counter;
        }
    }
    else if (!ui->imageViewer->SaveImage(fn))
    {
        QMessageBox::information(this, QGuiApplication::applicationDisplayName(),
                                 tr("Cannot write %1").arg(QDir::toNativeSeparators(fn)));
    }
}

void MainWindow::OpenProject(QString &projFilename)
{
    if (!QFile::exists(projFilename)) return;
    mProjectName = projFilename;

    QFile projFile(mProjectName);
    if (!projFile.exists()) return;

    if (!projFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Open Failed"), tr("Failed to open the project!"));
        return;
    }
    QStringList essFiles;
    QTextStream stream(&projFile);
    while(!stream.atEnd())
    {
        essFiles << stream.readLine();
    }
    projFile.close();
    AddESSFiles(essFiles, false);
    mbProjectDirty = false;
}

void MainWindow::on_action_Open_triggered()
{
    if (NeedCancelAction()) return;
    QString projFile = QFileDialog::getOpenFileName(this,
                                                    tr("Open Project File"),
                                                    ".",
                                                    tr("Elara Project File (*.erp)"));
    if (!projFile.isEmpty())
    {
        mbProjectDirty = false;
        on_action_New_triggered();
        OpenProject(projFile);
    }
}

void MainWindow::on_action_About_triggered()
{
    QMessageBox::information(this, tr("About"), tr("Elara GUI version ") + "1.0 Build " + (__TIMESTAMP__));
}

void MainWindow::ApplyToneMapper()
{
    float highlight = (float)ui->sldHighLight->value() / 100.0f;
    float midtones = (float)ui->sldMidTones->value() / 10.0f;
    float shadows = (float)ui->sldShadows->value() / 100.0f;
    float expValues = (float)ui->sldExpValue->value() / 100.0f;
    float colorSat = (float)ui->sldColorSat->value() / 100.0f;
    int whitePt = ui->sldWhitePt->value();
    ui->txtHighLight->setText(QString::number(highlight));
    ui->txtMidTones->setText(QString::number(midtones));
    ui->txtShadows->setText(QString::number(shadows));
    ui->txtExpValue->setText(QString::number(expValues));
    ui->txtColorSat->setText(QString::number(colorSat));
    ui->txtWhitePt->setText(QString::number(whitePt));
    ui->smpTxtExposure->setText(ui->txtExpValue->text());
    if (ui->imageViewer->IsToneEnabled())
    {
		float displayGamma = 2.2f;
		if (!ui->chkEnableGamma->isChecked())
		{
			displayGamma = 1.0f;
		}
        ui->imageViewer->SetToneParameters(expValues, highlight, displayGamma * midtones, shadows, colorSat, whitePt);
    }
    ui->imageViewer->Refresh();
    if (!mCurrentScene.isEmpty())
    {
        UpdateThumbnail(ui->lstFiles, mCurrentScene, ui->imageViewer, false);
    }
    else if (ui->lstFiles->currentItem() != nullptr)
    {
        UpdateThumbnail(ui->lstFiles, ui->lstFiles->currentItem()->data(Qt::UserRole).toString()
                        , ui->imageViewer, false);
    }
}

void SyncSlider(QSlider* slider, QLineEdit* edit, float multiplier)
{
    int value = (int)(edit->text().toFloat() * multiplier);
    int vclamp = value;
    vclamp = value > slider->maximum() ? slider->maximum() : value;
    vclamp = value < slider->minimum() ? slider->minimum() : value;
    if (vclamp != slider->value())
    {
        slider->setValue(value);
    }
    if (vclamp != value)
    {
        edit->setText(QString::number((float)value / multiplier));
    }
}

void MainWindow::on_txtHighLight_editingFinished()
{
    SyncSlider(ui->sldHighLight, ui->txtHighLight, 100.0f);
}

void MainWindow::on_txtMidTones_editingFinished()
{
    SyncSlider(ui->sldMidTones, ui->txtMidTones, 10.0f);
}

void MainWindow::on_txtShadows_editingFinished()
{
    SyncSlider(ui->sldShadows, ui->txtShadows, 100.0f);
}

void MainWindow::on_txtExpValue_editingFinished()
{
    SyncSlider(ui->sldExpValue, ui->txtExpValue, 100.0f);
    SyncSlider(ui->smpExpSlider, ui->txtExpValue, 100.0f);
}

void MainWindow::on_txtColorSat_editingFinished()
{
    SyncSlider(ui->sldColorSat, ui->txtColorSat, 100.0f);
}

void MainWindow::on_txtWhitePt_editingFinished()
{
    SyncSlider(ui->sldWhitePt, ui->txtWhitePt, 1.0f);
}

void MainWindow::on_smpTxtExposure_editingFinished()
{
    SyncSlider(ui->smpExpSlider, ui->smpTxtExposure, 100.0f);
    SyncSlider(ui->sldExpValue, ui->smpTxtExposure, 100.0f);
}


void MainWindow::on_smpExposure_toggled(bool checked)
{
    ui->smpExpSlider->setEnabled(checked);
    ui->grpExposureCtrl->setChecked(checked);
    ui->smpTxtExposure->setEnabled(checked);
}

void MainWindow::on_smpExpSlider_valueChanged(int value)
{
    ui->sldExpValue->setValue(value);
    ApplyToneMapper();
}

void MainWindow::on_grpExposureCtrl_toggled(bool arg1)
{
    ui->imageViewer->SetToneEnabled(arg1);
    ui->smpExposure->setChecked(arg1);
    ui->smpExpSlider->setEnabled(arg1);
    ui->smpTxtExposure->setEnabled(arg1);
    ApplyToneMapper();
}

void MainWindow::on_sldHighLight_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldMidTones_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldShadows_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldExpValue_valueChanged(int value)
{
    ui->smpExpSlider->setValue(value);
    ApplyToneMapper();
}

void MainWindow::on_sldColorSat_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_sldWhitePt_valueChanged(int)
{
    ApplyToneMapper();
}

void MainWindow::on_smpRender_clicked()
{
    on_btnRender_clicked();
}

void MainWindow::on_smpPreset_currentIndexChanged(const QString &arg1)
{
    PresetChanged(arg1);
}

void MainWindow::on_actionSimple_Style_triggered()
{
    ui->dockFileView->setVisible(false);
    ui->dockPreset->setVisible(false);
    ui->dockRenderControl->setVisible(false);
    ui->dockConsole->setVisible(false);
    ui->dockSimple->setVisible(true);
    ui->dockSimple->setMaximumHeight(50);
    ui->imageViewer->FitImage();
    ui->actionSimple_Style->setChecked(true);
    ui->actionExpert_Style->setChecked(false);
    mLastLayout = 0;
}

void MainWindow::on_actionExpert_Style_triggered()
{
    ui->dockFileView->setVisible(true);
    ui->dockPreset->setVisible(true);
    ui->dockRenderControl->setVisible(true);
    ui->dockConsole->setVisible(true);
    ui->dockSimple->setVisible(false);
    ui->actionSimple_Style->setChecked(false);
    ui->actionExpert_Style->setChecked(true);
    ui->imageViewer->FitImage();
    mLastLayout = 1;
}

void MainWindow::SetLayout(int layout)
{
    if (layout == 0)
    {
        on_actionSimple_Style_triggered();
    }
    else if (layout == 1)
    {
        on_actionExpert_Style_triggered();
    }
}

void MainWindow::on_tvPreset_itemDoubleClicked(QTreeWidgetItem *item, int)
{
    if (item->text(1).compare("off", Qt::CaseInsensitive) == 0
            || item->text(1).compare("fast", Qt::CaseInsensitive) == 0
			|| item->text(1).compare("accurate", Qt::CaseInsensitive) == 0)
    {
        QComboBox* boolCombo = new QComboBox(ui->tvPreset);
		boolCombo->addItems(QStringList() << "off"
                             << "fast"
                             << "accurate");

		 if (item->text(1).compare("off", Qt::CaseInsensitive) == 0)
        {
            boolCombo->setCurrentIndex(0);
        }
        else if (item->text(1).compare("fast", Qt::CaseInsensitive) == 0)
        {
            boolCombo->setCurrentIndex(1);
        }
        else if (item->text(1).compare("accurate", Qt::CaseInsensitive) == 0)
        {
            boolCombo->setCurrentIndex(2);
        }
        ui->tvPreset->setItemWidget(item, 1, boolCombo);
    }
    if (item->text(0).compare("filter", Qt::CaseInsensitive) == 0)
    {
        QComboBox* filterCombo = new QComboBox(ui->tvPreset);
        filterCombo->addItems(QStringList() << "box"
                             << "triangle"
                             << "catmull-rom"
                             << "gaussian"
                             << "sinc");
        if (item->text(1).compare("box", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(0);
        }
        else if (item->text(1).compare("triangle", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(1);
        }
        else if (item->text(1).compare("catmull-rom", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(2);
        }
        else if (item->text(1).compare("gaussian", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(3);
        }
        else if (item->text(1).compare("sinc", Qt::CaseInsensitive) == 0)
        {
            filterCombo->setCurrentIndex(4);
        }

        ui->tvPreset->setItemWidget(item, 1, filterCombo);
    }
}

void MainWindow::on_tvPreset_currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *previous)
{
    if (previous)
    {
        QWidget* editor = ui->tvPreset->itemWidget(previous, 1);
        QComboBox* combo = dynamic_cast<QComboBox*>(editor);
        if (combo)
        {
            previous->setText(1, combo->currentText());
            ui->tvPreset->setItemWidget(previous, 1, nullptr);
        }
    }
}

void MainWindow::on_btnShare_clicked()
{
    if (ui->imageViewer->image() == nullptr) return;
    const QImage* img = ui->imageViewer->image();
    static const QString cubeExt[] = {"CubeMap11_LF.jpg",
                                      "CubeMap11_RT.jpg",
                                      "CubeMap11_UP_WEB.jpg",
                                      "CubeMap11_DN_WEB.jpg",
                                      "CubeMap11_FR.jpg",
                                      "CubeMap11_BK.jpg"};
    QString basePath = qApp->applicationDirPath() + "/../Panorama/";
    if (!QFileInfo::exists(basePath))
    {
        QDir dir(basePath);
        dir.mkdir(basePath);
        if (!QFileInfo::exists(basePath))
        {
            QMessageBox::warning(this, tr("Warning"), "Failed to create path: " + basePath);
            return;
        }
    }
    QString app_path = QApplication::applicationDirPath();
    if (ui->chkPanorama->isChecked())
    {
        int counter = 0;
        for (int x = 0; x < img->width(); x += img->height())
        {
            QImage temp = img->copy(x, 0, img->height(), img->height());
            QString filename = basePath + cubeExt[counter];
            temp.save(filename);
            ++counter;
        }
		QString cmd =  app_path + "/../pictureupload.exe panorama " + basePath;
        QProcess::startDetached(cmd);
    }
    else
    {
        QString filename = app_path + "/temp.jpg";
        img->save(filename);
        QString cmd = app_path + "/../pictureupload.exe image share " + filename;
        QProcess::startDetached(cmd);
    }
}
