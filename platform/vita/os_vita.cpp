/**************************************************************************/
/*  os_vita.cpp                                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "os_vita.h"

#include "core/array.h"
#include "core/os/keyboard.h"
#include "drivers/dummy/rasterizer_dummy.h"
#include "drivers/dummy/texture_loader_dummy.h"
#include "drivers/gles2/rasterizer_gles2.h"
#include "drivers/unix/dir_access_unix.h"
#include "drivers/unix/file_access_unix.h"
#include "drivers/unix/ip_unix.h"
#include "drivers/unix/net_socket_posix.h"
#include "drivers/unix/thread_posix.h"
#include "main/main.h"
#include "servers/audio_server.h"
#include "servers/visual/visual_server_raster.h"
#include "servers/visual/visual_server_wrap_mt.h"

#include <dlfcn.h>
#include <time.h>

/// Clock Setup function (used by get_ticks_usec)
static uint64_t _clock_start = 0;

static void _setup_clock() {
	struct timespec tv_now = { 0, 0 };
	ERR_FAIL_COND_MSG(clock_gettime(CLOCK_MONOTONIC, &tv_now) != 0, "OS CLOCK IS NOT WORKING!");
	_clock_start = ((uint64_t)tv_now.tv_nsec / 1000L) + (uint64_t)tv_now.tv_sec * 1000000L;
}

static bool _get_ned_heading(const SceFMatrix4 &p_ned, real_t &r_heading) {
	// SceFMatrix4 exposes the matrix columns as x/y/z vectors. Build the
	// corresponding Godot rows, then transform the console's local forward axis.
	const Basis ned_orientation(
			Vector3(p_ned.x.x, p_ned.y.x, p_ned.z.x),
			Vector3(p_ned.x.y, p_ned.y.y, p_ned.z.y),
			Vector3(p_ned.x.z, p_ned.y.z, p_ned.z.z));
	Vector3 forward_ned = ned_orientation.xform(Vector3(0.0f, 0.0f, -1.0f));
	forward_ned.z = 0.0f;
	if (forward_ned.length_squared() <= CMP_EPSILON) {
		return false;
	}

	forward_ned.normalize();
	// NED X is north and NED Y is east. Positive Godot yaw turns west.
	r_heading = Math::atan2(-forward_ned.y, forward_ned.x);
	return true;
}

int OS_Vita::get_video_driver_count() const {
	return 1;
}

int OS_Vita::get_audio_driver_count() const {
	return 1;
}

const char *OS_Vita::get_audio_driver_name(int p_driver) const {
	return "Vita";
}

void OS_Vita::initialize_core() {
#if !defined(NO_THREADS)
	init_thread_posix();
#endif

	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_RESOURCES);
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_USERDATA);
	FileAccess::make_default<FileAccessUnix>(FileAccess::ACCESS_FILESYSTEM);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_RESOURCES);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_USERDATA);
	DirAccess::make_default<DirAccessUnix>(DirAccess::ACCESS_FILESYSTEM);

#ifndef NO_NETWORK
	NetSocketPosix::make_default();
	IP_Unix::make_default();
#endif

	_setup_clock();

	int app_util_module_result = sceSysmoduleLoadModule(SCE_SYSMODULE_APPUTIL);
	if (app_util_module_result >= 0 || app_util_module_result == SCE_SYSMODULE_LOADED) {
		SceAppUtilInitParam init_param = {};
		SceAppUtilBootParam boot_param = {};
		int app_util_result = sceAppUtilInit(&init_param, &boot_param);
		if (app_util_result >= 0) {
			app_util_initialized = true;
		} else {
			ERR_PRINT("Could not initialize the Vita AppUtil library. Save data will use the fallback data directory.");
		}
	} else {
		ERR_PRINT("Could not load the Vita AppUtil system module. Save data will use the fallback data directory.");
	}
}

void OS_Vita::finalize_core() {
	if (app_util_initialized) {
		sceAppUtilShutdown();
		app_util_initialized = false;
	}
	sceSysmoduleUnloadModule(SCE_SYSMODULE_APPUTIL);

#ifndef NO_NETWORK
	NetSocketPosix::cleanup();
#endif
}

int OS_Vita::get_current_video_driver() const {
	return video_driver_index;
}

Error OS_Vita::initialize(const VideoMode &p_desired, int p_video_driver, int p_audio_driver) {
	bool gl_initialization_error = false;
	bool gles2 = false;
	gl_context = NULL;

	if (p_video_driver == VIDEO_DRIVER_GLES2) {
		gles2 = true;
	} else if (GLOBAL_GET("rendering/quality/driver/fallback_to_gles2")) {
		p_video_driver = VIDEO_DRIVER_GLES2;
		gles2 = true;
	} else {
		OS::get_singleton()->alert("OpenGL ES 3 is not supported on this device.\n\n"
								   "Please enable the option \"Fallback to OpenGL ES 2.0\" in the options menu.\n",
				"OpenGL ES 3 Not Supported");
		//gl_initialization_error = true;
		p_video_driver = VIDEO_DRIVER_GLES2;
		gles2 = true;
	}

	if (!gl_initialization_error) {
		gl_context = memnew(ContextEGL_Vita(gles2));
		if (gl_context->initialize()) {
			OS::get_singleton()->alert("Failed to initialize OpenGL ES 2.0\n"
									   "OpenGL ES 2.0 Initialization Failed");
			memdelete(gl_context);
			gl_context = NULL;
			gl_initialization_error = true;
		}
		if (RasterizerGLES2::is_viable() == OK) {
			RasterizerGLES2::register_config();
			RasterizerGLES2::make_current();
		} else {
			OS::get_singleton()->alert("RasterizerGLES2::is_viable() failed\n"
									   "RasterizerGLES2 Not Viable");
			memdelete(gl_context);
			gl_context = NULL;
			gl_initialization_error = true;
		}
	}

	if (gl_initialization_error) {
		OS::get_singleton()->alert("Your device does not support any of the supported OpenGL versions.\n"
								   "Please check your graphics drivers and try again.\n",
				"Graphics Driver Error");
		return ERR_UNAVAILABLE;
	}

	video_driver_index = p_video_driver;

	visual_server = memnew(VisualServerRaster);
	if (get_render_thread_mode() != RENDER_THREAD_UNSAFE) {
		visual_server = memnew(VisualServerWrapMT(visual_server, false));
	}

	visual_server->init();

	AudioDriverManager::initialize(p_audio_driver);

	input = memnew(InputDefault);
	input->set_use_input_buffering(true);
	input->set_emulate_mouse_from_touch(true);
	joypad = memnew(JoypadVita(input));

	sceSysmoduleLoadModule(SCE_SYSMODULE_IME); // Enable the IME module for Keyboard input

	// Rear touch indices start at SCE_TOUCH_MAX_REPORT (8), leaving 0-7 for
	// front-panel touches so games can distinguish both surfaces.
	for (int port = 0; port < TOUCH_PORT_COUNT; port++) {
		touch_sampling[port] = sceTouchGetPanelInfo(port, &touch_panel_info[port]) >= 0 &&
				sceTouchSetSamplingState(port, SCE_TOUCH_SAMPLING_STATE_START) >= 0;
	}

	orientation_enabled = GLOBAL_GET("input_devices/sensors/vita/orientation/enabled");
	VitaOrientationTracker::Settings orientation_settings;
	orientation_settings.initialization_sample_count = GLOBAL_GET("input_devices/sensors/vita/orientation/initialization_sample_count");
	orientation_settings.initialization_timeout = GLOBAL_GET("input_devices/sensors/vita/orientation/initialization_timeout");
	orientation_settings.accelerometer_correction_time = GLOBAL_GET("input_devices/sensors/vita/orientation/accelerometer_correction_time");
	orientation_settings.absolute_correction_time = GLOBAL_GET("input_devices/sensors/vita/orientation/absolute_correction_time");
	orientation_settings.absolute_max_correction_rate = Math::deg2rad((real_t)GLOBAL_GET("input_devices/sensors/vita/orientation/absolute_max_correction_rate"));
	orientation_settings.absolute_outlier_angle = Math::deg2rad((real_t)GLOBAL_GET("input_devices/sensors/vita/orientation/absolute_outlier_angle"));
	orientation_settings.absolute_validation_sample_count = GLOBAL_GET("input_devices/sensors/vita/orientation/absolute_validation_sample_count");
	orientation_settings.absolute_validation_tolerance = Math::deg2rad((real_t)GLOBAL_GET("input_devices/sensors/vita/orientation/absolute_validation_tolerance"));
	orientation_settings.stationary_gyro_threshold = GLOBAL_GET("input_devices/sensors/vita/orientation/stationary_gyro_threshold");
	orientation_settings.acceleration_tolerance = GLOBAL_GET("input_devices/sensors/vita/orientation/acceleration_tolerance");
	orientation_settings.gyro_bias_learning_time = GLOBAL_GET("input_devices/sensors/vita/orientation/gyro_bias_learning_time");
	orientation_tracker.set_settings(orientation_settings);
	orientation_tracker.reset();

	motion_sampling = sceMotionStartSampling() >= 0;
	if (motion_sampling && orientation_enabled) {
		sceMotionSetTiltCorrection(1);
		sceMotionSetGyroBiasCorrection(1);
		// VitaSDK exposes a calculated NED orientation, not raw magnetic-field
		// strength in microteslas, so it must not be sent to set_magnetometer().
		magnetometer_sampling = sceMotionMagnetometerOn() >= 0;
	}

	return OK;
}

void OS_Vita::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
	input->set_main_loop(p_main_loop);
}

void OS_Vita::delete_main_loop() {
	memdelete(main_loop);
}

void OS_Vita::finalize() {
	if (magnetometer_sampling) {
		sceMotionMagnetometerOff();
		magnetometer_sampling = false;
	}
	if (motion_sampling) {
		sceMotionStopSampling();
		motion_sampling = false;
	}

	memdelete(joypad);
	memdelete(input);
	visual_server->finish();
	memdelete(visual_server);
	memdelete(gl_context);
}

void OS_Vita::alert(const String &p_alert, const String &p_title) {
	sceClibPrintf(p_alert.ascii().get_data());
}

Point2 OS_Vita::get_mouse_position() const {
	return Point2(0, 0);
}

int OS_Vita::get_mouse_button_state() const {
	return 0;
}

void OS_Vita::set_window_title(const String &p_title) {
}

void OS_Vita::set_video_mode(const VideoMode &p_video_mode, int p_screen) {
}

OS::VideoMode OS_Vita::get_video_mode(int p_screen) const {
	return video_mode;
}

void OS_Vita::get_fullscreen_mode_list(List<VideoMode> *p_list, int p_screen) const {
	p_list->push_back(video_mode);
}

Size2 OS_Vita::get_window_size() const {
	return Size2(video_mode.width, video_mode.height);
}

String OS_Vita::get_name() const {
	return "Vita";
}

MainLoop *OS_Vita::get_main_loop() const {
	return main_loop;
}

void OS_Vita::swap_buffers() {
	gl_context->swap_buffers();
}

bool OS_Vita::can_draw() const {
	return true;
}

static bool libime_active = false;
void OS_Vita::run() {
	if (!main_loop)
		return;

	main_loop->init();

	while (true) {
		joypad->process_joypads();
		process_touch();
		process_motion();
		if (libime_active) {
			sceImeUpdate();
		}

		if (Main::iteration())
			break;
	};

	main_loop->finish();
}

void OS_Vita::process_touch() {
	process_touch_port(SCE_TOUCH_PORT_FRONT);
	process_touch_port(SCE_TOUCH_PORT_BACK);
}

Vector2 OS_Vita::get_touch_position(SceTouchPortType p_port, const SceTouchReport &p_report) const {
	const SceTouchPanelInfo &panel = touch_panel_info[p_port];
	const Vector2 origin(panel.minAaX, panel.minAaY);
	const Vector2 size(panel.maxAaX - panel.minAaX, panel.maxAaY - panel.minAaY);
	const Vector2 position(p_report.x, p_report.y);
	return ((position - origin) / size) * Vector2(video_mode.width, video_mode.height);
}

void OS_Vita::process_touch_port(SceTouchPortType p_port) {
	SceTouchData touch;
	if (!touch_sampling[p_port] || sceTouchPeek(p_port, &touch, 1) < 0) {
		return;
	}

	bool seen[TOUCHES_PER_PORT] = {};
	for (uint32_t report_index = 0; report_index < touch.reportNum; report_index++) {
		const SceTouchReport &report = touch.report[report_index];
		int slot = -1;
		for (int i = 0; i < TOUCHES_PER_PORT; i++) {
			if (touch_points[p_port][i].active && touch_points[p_port][i].id == report.id) {
				slot = i;
				break;
			}
		}
		if (slot < 0) {
			for (int i = 0; i < TOUCHES_PER_PORT; i++) {
				if (!touch_points[p_port][i].active) {
					slot = i;
					break;
				}
			}
		}
		if (slot < 0) {
			continue;
		}

		VitaTouchPoint &point = touch_points[p_port][slot];
		const Vector2 position = get_touch_position(p_port, report);
		const int event_index = p_port * TOUCHES_PER_PORT + slot;
		seen[slot] = true;
		if (!point.active) {
			point.active = true;
			point.id = report.id;
			point.position = position;
			Ref<InputEventScreenTouch> event;
			event.instance();
			event->set_index(event_index);
			event->set_position(position);
			event->set_pressed(true);
			input->parse_input_event(event);
		} else if (position != point.position) {
			Ref<InputEventScreenDrag> event;
			event.instance();
			event->set_index(event_index);
			event->set_position(position);
			event->set_relative(position - point.position);
			point.position = position;
			input->parse_input_event(event);
		}
	}

	for (int slot = 0; slot < TOUCHES_PER_PORT; slot++) {
		VitaTouchPoint &point = touch_points[p_port][slot];
		if (point.active && !seen[slot]) {
			Ref<InputEventScreenTouch> event;
			event.instance();
			event->set_index(p_port * TOUCHES_PER_PORT + slot);
			event->set_position(point.position);
			event->set_pressed(false);
			input->parse_input_event(event);
			point.active = false;
		}
	}
}

void OS_Vita::process_motion() {
	if (!motion_sampling || sceMotionGetSensorState(&motion_sensor_state, 1) < 0) {
		return;
	}

	const Vector3 acceleration(motion_sensor_state.accelerometer.x, motion_sensor_state.accelerometer.y, motion_sensor_state.accelerometer.z);
	const Vector3 gyroscope(motion_sensor_state.gyro.x, motion_sensor_state.gyro.y, motion_sensor_state.gyro.z);
	process_accelerometer(acceleration);
	gravity = gravity.linear_interpolate(acceleration, 0.2f);
	process_gravity(gravity);
	process_gyroscope(gyroscope);

	bool absolute_heading_valid = false;
	real_t absolute_heading = 0.0f;
	if (orientation_enabled && magnetometer_sampling && sceMotionGetState(&motion_state) >= 0 && motion_state.magFieldStability == SCE_MOTION_MAGFIELD_STABLE) {
		absolute_heading_valid = _get_ned_heading(motion_state.nedMatrix, absolute_heading);
	}

	if (orientation_enabled) {
		orientation_tracker.update(acceleration, gyroscope, motion_sensor_state.timestamp, absolute_heading_valid, absolute_heading);
		process_device_orientation(orientation_tracker.get_orientation(), orientation_tracker.is_orientation_available());
	}
}

void OS_Vita::process_accelerometer(const Vector3 &m_accelerometer) {
	input->set_accelerometer(m_accelerometer);
}

void OS_Vita::process_gravity(const Vector3 &m_gravity) {
	input->set_gravity(m_gravity);
}

void OS_Vita::process_gyroscope(const Vector3 &m_gyroscope) {
	input->set_gyroscope(m_gyroscope);
}

void OS_Vita::process_device_orientation(const Quat &p_orientation, bool p_available) {
	input->set_device_orientation_quaternion(p_orientation, p_available);
}

String OS_Vita::get_data_path() const {
	return "ux0:/data";
}

String OS_Vita::get_title_id() const {
	if (title_id != "") {
		return title_id;
	}

	char title_id_buffer[10] = {};
	int result = sceAppMgrAppParamGetString(sceKernelGetProcessId(), 12, title_id_buffer, sizeof(title_id_buffer));
	if (result < 0) {
		ERR_PRINT("Could not read the Vita TITLE_ID from the application parameters.");
		return "";
	}

	String candidate = String::utf8(title_id_buffer).to_upper();
	if (candidate.length() != 9) {
		ERR_PRINT("The Vita TITLE_ID returned by the system is not 9 characters long.");
		return "";
	}
	for (int i = 0; i < candidate.length(); i++) {
		const CharType character = candidate[i];
		if (!((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9'))) {
			ERR_PRINT("The Vita TITLE_ID returned by the system contains an invalid character.");
			return "";
		}
	}

	title_id = candidate;
	return title_id;
}

String OS_Vita::get_user_data_dir() const {
	if (app_util_initialized && get_title_id() != "") {
		bool use_custom_dir = ProjectSettings::get_singleton()->get("application/config/use_custom_user_dir");
		if (use_custom_dir) {
			String custom_dir = get_safe_dir_name(ProjectSettings::get_singleton()->get("application/config/custom_user_dir_name"), true);
			if (custom_dir != "") {
				return String("savedata0:").plus_file(custom_dir);
			}
		}
		return "savedata0:";
	}

	String fallback_id = get_title_id();
	if (fallback_id == "") {
		fallback_id = "__unknown";
	}
	return get_data_path().plus_file(get_godot_dir_name()).plus_file("app_userdata").plus_file(fallback_id);
}

String OS_Vita::get_model_name() const {
	return "Sony Playstation Vita";
}

void utf16_to_utf8(const uint16_t *src, uint8_t *dst) {
	int i;
	for (i = 0; src[i]; i++) {
		if ((src[i] & 0xFF80) == 0) {
			*(dst++) = src[i] & 0xFF;
		} else if ((src[i] & 0xF800) == 0) {
			*(dst++) = ((src[i] >> 6) & 0xFF) | 0xC0;
			*(dst++) = (src[i] & 0x3F) | 0x80;
		} else if ((src[i] & 0xFC00) == 0xD800 && (src[i + 1] & 0xFC00) == 0xDC00) {
			*(dst++) = (((src[i] + 64) >> 8) & 0x3) | 0xF0;
			*(dst++) = (((src[i] >> 2) + 16) & 0x3F) | 0x80;
			*(dst++) = ((src[i] >> 4) & 0x30) | 0x80 | ((src[i + 1] << 2) & 0xF);
			*(dst++) = (src[i + 1] & 0x3F) | 0x80;
			i += 1;
		} else {
			*(dst++) = ((src[i] >> 12) & 0xF) | 0xE0;
			*(dst++) = ((src[i] >> 6) & 0x3F) | 0x80;
			*(dst++) = (src[i] & 0x3F) | 0x80;
		}
	}

	*dst = '\0';
}

static char libime_initval[8] = { 1 };
static unsigned int libime_height = 0;
static char libime_out[SCE_IME_MAX_PREEDIT_LENGTH * 2 + 8];
static unsigned int libime_work[SCE_IME_WORK_BUFFER_SIZE / sizeof(unsigned int)];
static SceImeCaret caret_rev;

void vita_ime_event_handler(void *arg, const SceImeEventData *e) {
	uint8_t utf8_buffer[SCE_IME_MAX_TEXT_LENGTH] = { '\0' };
	switch (e->id) {
		case SCE_IME_EVENT_OPEN:
			libime_height = e->param.rect.height;
			break;
		case SCE_IME_EVENT_UPDATE_TEXT:
			if (e->param.text.caretIndex == 0) {
				OS_Vita::get_singleton()->key(KEY_BACKSPACE, true);
				OS_Vita::get_singleton()->key(KEY_BACKSPACE, false);
				sceImeSetText((SceWChar16 *)libime_initval, 4);
			} else {
				String character;
				utf16_to_utf8((uint16_t *)&libime_out[2], utf8_buffer);
				character.parse_utf8(utf8_buffer);
				OS_Vita::get_singleton()->key(character[0], true);
				OS_Vita::get_singleton()->key(character[0], false);
				sceClibMemset(&caret_rev, 0, sizeof(SceImeCaret));
				caret_rev.index = 1;
				sceImeSetCaret(&caret_rev);
				sceImeSetText((SceWChar16 *)libime_initval, 4);
			}
			break;
		case SCE_IME_EVENT_PRESS_ENTER:
			OS_Vita::get_singleton()->key(KEY_ENTER, true);
			OS_Vita::get_singleton()->key(KEY_ENTER, false);
		case SCE_IME_EVENT_PRESS_CLOSE:
			libime_active = false;
			libime_height = 0;
			sceImeClose();
			break;
	}
}

void OS_Vita::key(uint32_t p_key, bool p_pressed) {
	Ref<InputEventKey> ev;
	ev.instance();
	ev->set_echo(false);
	ev->set_pressed(p_pressed);
	ev->set_scancode(p_key);
	ev->set_unicode(p_key);
	input->parse_input_event(ev);
}

bool OS_Vita::has_touchscreen_ui_hint() const {
	return true;
};

bool OS_Vita::has_virtual_keyboard() const {
	return true;
}

int OS_Vita::get_virtual_keyboard_height() const {
	return (int)libime_height;
}

void OS_Vita::show_virtual_keyboard(const String &p_existing_text, const Rect2 &p_screen_rect, bool p_multiline, int p_max_input_length, int p_cursor_start, int p_cursor_end) {
	if (!libime_active) {
		SceImeParam param;
		sceImeParamInit(&param);

		sceClibMemset(libime_out, 0, (SCE_IME_MAX_PREEDIT_LENGTH * 2 + 6));

		param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
		param.languagesForced = false;
		param.type = SCE_IME_TYPE_DEFAULT;
		param.option = SCE_IME_OPTION_NO_ASSISTANCE;
		param.inputTextBuffer = (SceWChar16 *)libime_out;
		param.maxTextLength = 4;
		param.handler = vita_ime_event_handler;
		param.filter = NULL;
		param.initialText = (SceWChar16 *)libime_initval;
		param.arg = NULL;
		param.work = libime_work;

		sceImeOpen(&param);
		libime_active = true;
	}
}

void OS_Vita::hide_virtual_keyboard() {
	if (libime_active) {
		libime_active = false;
		sceImeClose();
	}
}

void OS_Vita::set_offscreen_gl_available(bool p_available) {
	secondary_gl_available = false;
}

bool OS_Vita::is_offscreen_gl_available() const {
	return secondary_gl_available;
}

void OS_Vita::set_offscreen_gl_current(bool p_current) {
}

bool OS_Vita::_check_internal_feature_support(const String &p_feature) {
	if (p_feature == "mobile") {
		return true;
	}
	if (p_feature == "armeabi-v7a" || p_feature == "armeabi") {
		return true;
	}
	return false;
}

OS_Vita *OS_Vita::get_singleton() {
	return (OS_Vita *)OS::get_singleton();
};

Error OS_Vita::open_dynamic_library(const String p_path, void *&p_library_handle, bool p_also_set_library_path) {
	String path = p_path;

	if (FileAccess::exists(path) && path.is_rel_path()) {
		// dlopen expects a slash, in this case a leading ./ for it to be interpreted as a relative path,
		//  otherwise it will end up searching various system directories for the lib instead and finally failing.
		path = "app0:" + path;
	}

	if (!FileAccess::exists(path)) {
		//this code exists so gdnative can load .suprx files from within the executable path
		path = get_executable_path().get_base_dir().plus_file("app0:").plus_file(p_path.get_file());
	}

	p_library_handle = dlopen(path.utf8().get_data(), RTLD_NOW);
	ERR_FAIL_COND_V_MSG(!p_library_handle, ERR_CANT_OPEN, "Can't open dynamic library: " + p_path + ". Error: " + dlerror());
	return OK;
}

Error OS_Vita::close_dynamic_library(void *p_library_handle) {
	if (dlclose(p_library_handle)) {
		return FAILED;
	}
	return OK;
}

Error OS_Vita::get_dynamic_library_symbol_handle(void *p_library_handle, const String p_name, void *&p_symbol_handle, bool p_optional) {
	const char *error;
	dlerror(); // Clear existing errors

	p_symbol_handle = dlsym(p_library_handle, p_name.utf8().get_data());

	error = dlerror();
	if (error != nullptr) {
		ERR_FAIL_COND_V_MSG(!p_optional, ERR_CANT_RESOLVE, "Can't resolve symbol " + p_name + ". Error: " + error + ".");

		return ERR_CANT_RESOLVE;
	}
	return OK;
}

OS_Vita::OS_Vita() {
	video_mode.width = 960;
	video_mode.height = 544;
	video_mode.fullscreen = true;
	video_mode.resizable = false;

	video_driver_index = 0;
	main_loop = nullptr;
	visual_server = nullptr;
	gl_context = nullptr;
	motion_sampling = false;
	magnetometer_sampling = false;
	orientation_enabled = true;
	gravity = Vector3();
	for (int port = 0; port < TOUCH_PORT_COUNT; port++) {
		touch_sampling[port] = false;
		for (int slot = 0; slot < TOUCHES_PER_PORT; slot++) {
			touch_points[port][slot].active = false;
			touch_points[port][slot].id = 0;
			touch_points[port][slot].position = Vector2();
		}
	}

	AudioDriverManager::add_driver(&driver_vita);
}

OS_Vita::~OS_Vita() {
	video_driver_index = 0;
	main_loop = nullptr;
	visual_server = nullptr;
	input = nullptr;
	gl_context = nullptr;
}

// Misc

Error OS_Vita::shell_open(String p_uri) {
	const char *uri = p_uri.utf8().get_data();
	if (strncmp(uri, "http://", 7) || strncmp(uri, "https://", 8)) {
		sceAppMgrLaunchAppByUri(0xFFFFF, uri);
	}
	return FAILED;
}

Error OS_Vita::execute(const String &p_path, const List<String> &p_arguments, bool p_blocking = true, ProcessID *r_child_id = nullptr, String *r_pipe = nullptr, int *r_exitcode = nullptr, bool read_stderr = false, Mutex *p_pipe_mutex = nullptr, bool p_open_console = false) {
	return FAILED;
}

Error OS_Vita::kill(const ProcessID &p_pid) {
	return FAILED;
}

bool OS_Vita::is_process_running(const ProcessID &p_pid) const {
	return false;
}

bool OS_Vita::has_environment(const String &p_var) const {
	return false;
}

String OS_Vita::get_environment(const String &p_var) const {
	return "";
}

bool OS_Vita::set_environment(const String &p_var, const String &p_value) const {
	return false;
}

OS::Date OS_Vita::get_date(bool local) const {
	SceDateTime sceDateTime;
	if (local)
		sceRtcGetCurrentClockUtc(&sceDateTime);
	else
		sceRtcGetCurrentClockLocalTime(&sceDateTime);
	// TODO: Daylight calculation.
	bool daylight = false;
	Date date;
	date.day = sceDateTime.day;
	date.month = Month(sceDateTime.month);
	date.year = sceDateTime.year;
	date.weekday = Weekday(sceRtcGetDayOfWeek(sceDateTime.year, sceDateTime.month, sceDateTime.day));
	date.dst = daylight;
	return date;
}

OS::Time OS_Vita::get_time(bool local) const {
	SceDateTime sceDateTime;
	if (local)
		sceRtcGetCurrentClockUtc(&sceDateTime);
	else
		sceRtcGetCurrentClockLocalTime(&sceDateTime);
	Time time;
	time.hour = sceDateTime.hour;
	time.min = sceDateTime.minute;
	time.sec = sceDateTime.second;
	return time;
}

OS::TimeZoneInfo OS_Vita::get_time_zone_info() const {
	OS::TimeZoneInfo timeZoneInfo;
	SceDateTime sceDateTimeUtc;
	SceDateTime sceDateTimeLocal;
	sceRtcGetCurrentClockUtc(&sceDateTimeUtc);
	sceRtcGetCurrentClockLocalTime(&sceDateTimeLocal);
	int hourBias = sceDateTimeLocal.hour - sceDateTimeUtc.hour;
	if (hourBias < 0) {
		hourBias += 24;
	}
	String sign = (hourBias >= 0) ? "+" : "-";
	int local_offset = (hourBias > 0 ? hourBias : -hourBias);
	String offset = (local_offset < 10) ? "0" + String::num_int64(local_offset) : String::num_int64(local_offset);
	timeZoneInfo.name = "UTC" + sign + offset;
	timeZoneInfo.bias = hourBias * 60;
	return timeZoneInfo;
}

uint64_t OS_Vita::get_unix_time() const {
	uint64_t unixTime;
	SceDateTime sceDateTimeUtc;
	sceRtcGetCurrentClockUtc(&sceDateTimeUtc);
	sceRtcConvertDateTimeToTime64_t(&sceDateTimeUtc, &unixTime);
	return unixTime;
}

uint64_t OS_Vita::get_system_time_secs() const {
	return get_system_time_msecs() / 1000L;
}

uint64_t OS_Vita::get_system_time_msecs() const {
	struct timespec tv_now = { 0, 0 };
	clock_gettime(CLOCK_MONOTONIC, &tv_now);
	uint64_t longtime = ((uint64_t)tv_now.tv_nsec / 1000000L) + (uint64_t)tv_now.tv_sec * 1000L;
	return longtime;
}

double OS_Vita::get_subsecond_unix_time() const {
	uint64_t unixTime;
	SceDateTime sceDateTimeUtc;
	sceRtcGetCurrentClockUtc(&sceDateTimeUtc);
	sceRtcConvertDateTimeToTime64_t(&sceDateTimeUtc, &unixTime);
	return (double)unixTime + (double(sceDateTimeUtc.microsecond) / 1000000.0);
}

void OS_Vita::delay_usec(uint32_t p_usec) const {
	sceKernelDelayThread(p_usec);
}

uint64_t OS_Vita::get_ticks_usec() const {
	// Unchecked return. Static analyzers might complain.
	// If _setup_clock() succeeded, we assume clock_gettime() works.
	struct timespec tv_now = { 0, 0 };
	clock_gettime(CLOCK_MONOTONIC, &tv_now);
	uint64_t longtime = ((uint64_t)tv_now.tv_nsec / 1000L) + (uint64_t)tv_now.tv_sec * 1000000L;
	longtime -= _clock_start;

	return longtime;
}

String OS_Vita::get_stdin_string() {
	return "";
}
