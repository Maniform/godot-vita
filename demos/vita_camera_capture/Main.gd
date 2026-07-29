extends Control

var camera_feed: CameraFeed
var camera_texture := CameraTexture.new()
var capture_index := 0

func _ready() -> void:
	for feed in CameraServer.feeds():
		if feed.get_position() == CameraFeed.FEED_BACK:
			camera_feed = feed
			break

	if camera_feed == null:
		$Margin/VBox/Status.text = "Rear camera not found"
		$Margin/VBox/Capture.disabled = true
		return

	var format_error := camera_feed.set_capture_format(Vector2(640, 480), 30)
	if format_error != OK:
		$Margin/VBox/Status.text = "Could not select 640x480 at 30 FPS: %d" % format_error
		return

	camera_texture.camera_feed_id = camera_feed.get_id()
	camera_texture.camera_is_active = true
	$Margin/VBox/Preview.texture = camera_texture
	$Margin/VBox/Status.text = "Rear camera active"

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
