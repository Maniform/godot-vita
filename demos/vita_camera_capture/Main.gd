extends Control

const AR_PROCESS_INTERVAL := 1.0 / 15.0

var camera_feed: CameraFeed
var camera_texture := CameraTexture.new()
var capture_index := 0
var ar_process_accumulator := 0.0
var last_luminance_frame_id := -1

func _ready() -> void:
	for feed in CameraServer.feeds():
		if feed.get_position() == CameraFeed.FEED_BACK:
			camera_feed = feed
			break

	if camera_feed == null:
		$Margin/VBox/Status.text = "Rear camera not found"
		$Margin/VBox/Capture.disabled = true
		return

	var format_error := camera_feed.set_capture_format(Vector2(320, 240), 30)
	if format_error != OK:
		$Margin/VBox/Status.text = "Could not select 320x240 at 30 FPS: %d" % format_error
		return

	camera_texture.camera_feed_id = camera_feed.get_id()
	camera_texture.camera_is_active = true
	$Margin/VBox/Preview.texture = camera_texture
	$Margin/VBox/Status.text = "Rear camera active; AR frames limited to 15 FPS"

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
	image.lock()
	var center_luminance := image.get_pixel(image.get_width() / 2, image.get_height() / 2).r
	image.unlock()
	var diagnostics := camera_feed.get_diagnostics()
	var calibration := camera_feed.get_calibration()
	var orientation_quality := "unavailable"
	if frame.orientation_available:
		orientation_quality = "interpolated" if frame.orientation_interpolated else "nearest (%d us)" % frame.orientation_error_usec

	$Margin/VBox/Diagnostics.text = "Frame %d | Y %.3f | orientation %s | age %d us | dropped %d | calibration %s" % [
		frame.frame_id,
		center_luminance,
		orientation_quality,
		diagnostics.get("latest_frame_age_usec", 0),
		diagnostics.get("dropped_frames", 0),
		"valid" if calibration.get("valid", false) else "unset"
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
