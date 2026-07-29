extends Control

const SMOOTHING := 0.35

var calibration := {}
var image_size := Vector2(320, 240)
var pose := {}
var visible_tracking := false

func set_tracking(result: Dictionary, new_calibration: Dictionary, new_image_size: Vector2) -> void:
	calibration = new_calibration
	image_size = new_image_size
	if not result.get("found", false):
		visible_tracking = false
		update()
		return

	if visible_tracking and not pose.empty():
		var smoothed := result.duplicate(true)
		smoothed.translation = pose.translation.linear_interpolate(result.translation, SMOOTHING)
		var old_columns: PoolVector3Array = pose.rotation_columns
		var new_columns: PoolVector3Array = result.rotation_columns
		var x_axis := old_columns[0].linear_interpolate(new_columns[0], SMOOTHING).normalized()
		var y_axis := old_columns[1].linear_interpolate(new_columns[1], SMOOTHING)
		y_axis = (y_axis - x_axis * x_axis.dot(y_axis)).normalized()
		smoothed.rotation_columns = PoolVector3Array([x_axis, y_axis, x_axis.cross(y_axis).normalized()])
		pose = smoothed
	else:
		pose = result.duplicate(true)
	visible_tracking = true
	update()

func clear_tracking() -> void:
	visible_tracking = false
	pose.clear()
	update()

func _camera_to_image(point: Vector3) -> Vector2:
	if point.z <= 0.0001:
		return Vector2(-10000, -10000)
	var normalized := Vector2(point.x / point.z, point.y / point.z)
	var radius2 := normalized.length_squared()
	var radial: Vector3 = calibration.get("radial_distortion", Vector3())
	var tangential: Vector2 = calibration.get("tangential_distortion", Vector2())
	var radial_scale := 1.0 + radial.x * radius2 + radial.y * radius2 * radius2 + radial.z * radius2 * radius2 * radius2
	var distorted := Vector2(
		normalized.x * radial_scale + 2.0 * tangential.x * normalized.x * normalized.y + tangential.y * (radius2 + 2.0 * normalized.x * normalized.x),
		normalized.y * radial_scale + tangential.x * (radius2 + 2.0 * normalized.y * normalized.y) + 2.0 * tangential.y * normalized.x * normalized.y
	)
	var focal: Vector2 = calibration.focal_length
	var principal: Vector2 = calibration.principal_point
	return Vector2(focal.x * distorted.x + principal.x, focal.y * distorted.y + principal.y)

func _image_to_control(point: Vector2) -> Vector2:
	var scale := min(rect_size.x / image_size.x, rect_size.y / image_size.y)
	var displayed_size := image_size * scale
	return (rect_size - displayed_size) * 0.5 + point * scale

func _project_marker_point(local_point: Vector3) -> Vector2:
	var columns: PoolVector3Array = pose.rotation_columns
	var camera_point: Vector3 = pose.translation + columns[0] * local_point.x + columns[1] * local_point.y + columns[2] * local_point.z
	return _image_to_control(_camera_to_image(camera_point))

func _draw() -> void:
	if not visible_tracking or pose.empty():
		return

	var marker_size: float = pose.marker_size
	var half := marker_size * 0.5
	var height := marker_size * 0.75
	var base := [
		_project_marker_point(Vector3(-half, -half, 0.0)),
		_project_marker_point(Vector3(half, -half, 0.0)),
		_project_marker_point(Vector3(half, half, 0.0)),
		_project_marker_point(Vector3(-half, half, 0.0))
	]
	var top := [
		_project_marker_point(Vector3(-half, -half, -height)),
		_project_marker_point(Vector3(half, -half, -height)),
		_project_marker_point(Vector3(half, half, -height)),
		_project_marker_point(Vector3(-half, half, -height))
	]

	for index in range(4):
		draw_line(base[index], base[(index + 1) % 4], Color(0.1, 1.0, 0.25), 3.0, true)
		draw_line(base[index], top[index], Color(1.0, 0.65, 0.05), 3.0, true)
		draw_line(top[index], top[(index + 1) % 4], Color(1.0, 0.65, 0.05), 3.0, true)
