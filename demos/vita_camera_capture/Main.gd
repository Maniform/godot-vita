extends Control

const AR_PROCESS_INTERVAL := 1.0 / 15.0
const MARKER_SIZE_METERS := 0.08
const YUV_PREVIEW_SHADER := preload("res://Yuv420Preview.shader")

var camera_feed: CameraFeed
var camera_texture := CameraTexture.new()
var chroma_texture := CameraTexture.new()
var preview_material := ShaderMaterial.new()
var marker_tracker = null
var capture_index := 0
var ar_process_accumulator := 0.0
var last_luminance_frame_id := -1
var last_tracking_result := {}

func _ready() -> void:
	for feed in CameraServer.feeds():
		if feed.get_position() == CameraFeed.FEED_BACK:
			camera_feed = feed
			break

	if camera_feed == null:
		$Margin/VBox/Status.text = "Rear camera not found"
		$Margin/VBox/Capture.disabled = true
		return

	if ClassDB.class_exists("VitaARMarkerTracker"):
		marker_tracker = ClassDB.instance("VitaARMarkerTracker")

	var format_error := camera_feed.set_capture_format(Vector2(320, 240), 30)
	if format_error != OK:
		$Margin/VBox/Status.text = "Could not select 320x240 at 30 FPS: %d" % format_error
		return

	camera_texture.camera_feed_id = camera_feed.get_id()
	camera_texture.which_feed = CameraServer.FEED_Y_IMAGE
	chroma_texture.camera_feed_id = camera_feed.get_id()
	chroma_texture.which_feed = CameraServer.FEED_CBCR_IMAGE

	preview_material.shader = YUV_PREVIEW_SHADER
	preview_material.set_shader_param("cbcr_texture", chroma_texture)
	$Margin/VBox/Preview.texture = camera_texture
	$Margin/VBox/Preview.material = preview_material
	camera_texture.camera_is_active = true

	var calibration := camera_feed.get_calibration()
	if not calibration.get("valid", false):
		$Margin/VBox/Status.text = "Camera active; configure rear calibration before AR tracking"
	elif marker_tracker == null:
		$Margin/VBox/Status.text = "Camera active; VitaARMarkerTracker is unavailable in this build"
	else:
		$Margin/VBox/Status.text = "Point the rear camera at marker.svg (black square = 80 mm)"

func _process(delta: float) -> void:
	if camera_feed == null or not camera_feed.is_active():
		return

	ar_process_accumulator += delta
	if ar_process_accumulator < AR_PROCESS_INTERVAL:
		return
	ar_process_accumulator = fmod(ar_process_accumulator, AR_PROCESS_INTERVAL)

	var frame := camera_feed.get_latest_frame(CameraFeed.FRAME_LUMINANCE)
	if frame == null or frame.frame_id == last_luminance_frame_id:
		return
	last_luminance_frame_id = frame.frame_id

	var image: Image = frame.image
	var calibration := camera_feed.get_calibration()
	if marker_tracker != null and calibration.get("valid", false):
		last_tracking_result = marker_tracker.track(image, calibration, MARKER_SIZE_METERS)
		$Margin/VBox/Preview/AROverlay.set_tracking(last_tracking_result, calibration, Vector2(image.get_width(), image.get_height()))
	else:
		last_tracking_result = {}
		$Margin/VBox/Preview/AROverlay.clear_tracking()

	var diagnostics := camera_feed.get_diagnostics()
	var orientation_quality := "unavailable"
	if frame.orientation_available:
		orientation_quality = "interpolated" if frame.orientation_interpolated else "nearest (%d us)" % frame.orientation_error_usec
	var tracking_quality := "calibration unset"
	if calibration.get("valid", false):
		tracking_quality = "marker %.0f%%/%d us" % [
			last_tracking_result.get("confidence", 0.0) * 100.0,
			last_tracking_result.get("processing_usec", 0)
		] if last_tracking_result.get("found", false) else "marker not found (%d us)" % last_tracking_result.get("processing_usec", 0)

	$Margin/VBox/Diagnostics.text = "Frame %d | %s | orientation %s | age %d us | dropped %d | YUV %d us | %s" % [
		frame.frame_id,
		diagnostics.get("native_format", "unknown"),
		orientation_quality,
		diagnostics.get("latest_frame_age_usec", 0),
		diagnostics.get("dropped_frames", 0),
		diagnostics.get("last_yuv_publish_usec", 0),
		tracking_quality
	]

func _on_Capture_pressed() -> void:
	if camera_feed == null:
		return

	var image := camera_feed.capture_image()
	if image == null:
		$Margin/VBox/Status.text = "No completed frame is available yet"
		return

	var path := "user://vita_camera_%03d.png" % capture_index
	capture_index += 1
	var error := image.save_png(path)
	if error == OK:
		$Margin/VBox/Status.text = "Saved %s" % path
	else:
		$Margin/VBox/Status.text = "PNG save failed: %d" % error

func _exit_tree() -> void:
	if camera_feed != null:
		camera_feed.set_active(false)
