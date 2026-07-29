/**************************************************************************/
/*  vita_ar_marker_tracker.cpp                                            */
/**************************************************************************/

#include "vita_ar_marker_tracker.h"

#include "core/math/math_funcs.h"
#include "core/os/os.h"

#include <string.h>

static const int MARKER_CELLS = 8;

static bool _marker_cell_is_dark(int p_x, int p_y) {
	static const char *rows[MARKER_CELLS] = {
		"11111111",
		"10000001",
		"10110001",
		"10010001",
		"10101001",
		"10011001",
		"10000001",
		"11111111",
	};
	return rows[p_y][p_x] == '1';
}

static void _observed_to_canonical(int p_rotation, int p_x, int p_y, int &r_x, int &r_y) {
	switch (p_rotation) {
		case 1:
			r_x = p_y;
			r_y = MARKER_CELLS - 1 - p_x;
			break;
		case 2:
			r_x = MARKER_CELLS - 1 - p_x;
			r_y = MARKER_CELLS - 1 - p_y;
			break;
		case 3:
			r_x = MARKER_CELLS - 1 - p_y;
			r_y = p_x;
			break;
		default:
			r_x = p_x;
			r_y = p_y;
			break;
	}
}

static Vector2 _sample_quad(const Vector2 p_corners[4], real_t p_u, real_t p_v) {
	return p_corners[0] * ((1.0f - p_u) * (1.0f - p_v)) +
			p_corners[1] * (p_u * (1.0f - p_v)) +
			p_corners[2] * (p_u * p_v) +
			p_corners[3] * ((1.0f - p_u) * p_v);
}

static real_t _quad_area(const Vector2 p_corners[4]) {
	real_t twice_area = 0.0f;
	for (int i = 0; i < 4; i++) {
		const Vector2 &a = p_corners[i];
		const Vector2 &b = p_corners[(i + 1) & 3];
		twice_area += a.x * b.y - b.x * a.y;
	}
	return Math::abs(twice_area) * 0.5f;
}

static Vector2 _undistort_point(const Vector2 &p_pixel, const Vector2 &p_focal, const Vector2 &p_principal, const Vector3 &p_radial, const Vector2 &p_tangential) {
	const Vector2 distorted((p_pixel.x - p_principal.x) / p_focal.x, (p_pixel.y - p_principal.y) / p_focal.y);
	Vector2 point = distorted;
	for (int iteration = 0; iteration < 6; iteration++) {
		const real_t radius2 = point.length_squared();
		const real_t radial = 1.0f + p_radial.x * radius2 + p_radial.y * radius2 * radius2 + p_radial.z * radius2 * radius2 * radius2;
		if (Math::abs(radial) < CMP_EPSILON) {
			break;
		}
		const real_t delta_x = 2.0f * p_tangential.x * point.x * point.y + p_tangential.y * (radius2 + 2.0f * point.x * point.x);
		const real_t delta_y = p_tangential.x * (radius2 + 2.0f * point.y * point.y) + 2.0f * p_tangential.y * point.x * point.y;
		point.x = (distorted.x - delta_x) / radial;
		point.y = (distorted.y - delta_y) / radial;
	}
	return point;
}

static bool _square_to_quad(const Vector2 p_points[4], Vector3 &r_column_0, Vector3 &r_column_1, Vector3 &r_column_2) {
	const real_t dx1 = p_points[1].x - p_points[2].x;
	const real_t dx2 = p_points[3].x - p_points[2].x;
	const real_t dx3 = p_points[0].x - p_points[1].x + p_points[2].x - p_points[3].x;
	const real_t dy1 = p_points[1].y - p_points[2].y;
	const real_t dy2 = p_points[3].y - p_points[2].y;
	const real_t dy3 = p_points[0].y - p_points[1].y + p_points[2].y - p_points[3].y;
	const real_t denominator = dx1 * dy2 - dx2 * dy1;
	if (Math::abs(denominator) < CMP_EPSILON) {
		return false;
	}

	const real_t projective_x = (dx3 * dy2 - dx2 * dy3) / denominator;
	const real_t projective_y = (dx1 * dy3 - dx3 * dy1) / denominator;
	r_column_0 = Vector3(p_points[1].x - p_points[0].x + projective_x * p_points[1].x,
			p_points[1].y - p_points[0].y + projective_x * p_points[1].y, projective_x);
	r_column_1 = Vector3(p_points[3].x - p_points[0].x + projective_y * p_points[3].x,
			p_points[3].y - p_points[0].y + projective_y * p_points[3].y, projective_y);
	r_column_2 = Vector3(p_points[0].x, p_points[0].y, 1.0f);
	return true;
}

void VitaARMarkerTracker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("track", "image", "calibration", "marker_size"), &VitaARMarkerTracker::track, DEFVAL(0.08));
}

Dictionary VitaARMarkerTracker::track(const Ref<Image> &p_image, const Dictionary &p_calibration, real_t p_marker_size) const {
	Dictionary result;
	result["found"] = false;
	const uint64_t start_usec = OS::get_singleton()->get_ticks_usec();

	if (p_image.is_null() || p_image->empty() || p_image->get_format() != Image::FORMAT_L8 || p_marker_size <= 0.0f ||
			!p_calibration.get("valid", false) || !p_calibration.has("focal_length") || !p_calibration.has("principal_point")) {
		result["processing_usec"] = OS::get_singleton()->get_ticks_usec() - start_usec;
		return result;
	}

	const int width = p_image->get_width();
	const int height = p_image->get_height();
	const PoolVector<uint8_t> pixels = p_image->get_data();
	if (pixels.size() != width * height) {
		result["processing_usec"] = OS::get_singleton()->get_ticks_usec() - start_usec;
		return result;
	}
	const PoolVector<uint8_t>::Read read = pixels.read();

	int histogram[256] = {};
	for (int i = 0; i < pixels.size(); i++) {
		histogram[read[i]]++;
	}
	const int pixel_count = width * height;
	real_t total_sum = 0.0f;
	for (int i = 0; i < 256; i++) {
		total_sum += i * histogram[i];
	}
	int background_count = 0;
	real_t background_sum = 0.0f;
	real_t best_variance = -1.0f;
	int threshold = 96;
	for (int value = 0; value < 255; value++) {
		background_count += histogram[value];
		if (background_count == 0) {
			continue;
		}
		const int foreground_count = pixel_count - background_count;
		if (foreground_count == 0) {
			break;
		}
		background_sum += value * histogram[value];
		const real_t background_mean = background_sum / background_count;
		const real_t foreground_mean = (total_sum - background_sum) / foreground_count;
		const real_t difference = background_mean - foreground_mean;
		const real_t variance = (real_t)background_count * foreground_count * difference * difference;
		if (variance > best_variance) {
			best_variance = variance;
			threshold = value;
		}
	}

	Vector<uint8_t> visited;
	visited.resize(pixel_count);
	memset(visited.ptrw(), 0, pixel_count);
	Vector<int> queue;
	queue.resize(pixel_count);

	Vector2 best_corners[4];
	int best_rotation = -1;
	int best_mismatches = MARKER_CELLS * MARKER_CELLS + 1;
	real_t best_area = 0.0f;
	const int minimum_component = MAX(48, pixel_count / 800);

	for (int seed = 0; seed < pixel_count; seed++) {
		if (visited[seed] || read[seed] > threshold) {
			continue;
		}

		int queue_read = 0;
		int queue_write = 0;
		queue.write[queue_write++] = seed;
		visited.write[seed] = 1;
		int component_size = 0;
		int minimum_x = width;
		int maximum_x = 0;
		int minimum_y = height;
		int maximum_y = 0;
		int extreme_values[4] = { 0x7fffffff, -0x7fffffff, -0x7fffffff, 0x7fffffff };
		Vector2 component_corners[4];

		while (queue_read < queue_write) {
			const int index = queue[queue_read++];
			const int x = index % width;
			const int y = index / width;
			component_size++;
			minimum_x = MIN(minimum_x, x);
			maximum_x = MAX(maximum_x, x);
			minimum_y = MIN(minimum_y, y);
			maximum_y = MAX(maximum_y, y);

			const int values[4] = { x + y, x - y, x + y, x - y };
			if (values[0] < extreme_values[0]) {
				extreme_values[0] = values[0];
				component_corners[0] = Vector2(x, y);
			}
			if (values[1] > extreme_values[1]) {
				extreme_values[1] = values[1];
				component_corners[1] = Vector2(x, y);
			}
			if (values[2] > extreme_values[2]) {
				extreme_values[2] = values[2];
				component_corners[2] = Vector2(x, y);
			}
			if (values[3] < extreme_values[3]) {
				extreme_values[3] = values[3];
				component_corners[3] = Vector2(x, y);
			}

			const int neighbors[4] = { index - 1, index + 1, index - width, index + width };
			for (int direction = 0; direction < 4; direction++) {
				const int neighbor = neighbors[direction];
				if (neighbor < 0 || neighbor >= pixel_count || visited[neighbor]) {
					continue;
				}
				const int neighbor_x = neighbor % width;
				if ((direction == 0 || direction == 1) && Math::abs(neighbor_x - x) != 1) {
					continue;
				}
				if (read[neighbor] <= threshold) {
					visited.write[neighbor] = 1;
					queue.write[queue_write++] = neighbor;
				}
			}
		}

		const int box_area = (maximum_x - minimum_x + 1) * (maximum_y - minimum_y + 1);
		const real_t area = _quad_area(component_corners);
		if (component_size < minimum_component || box_area <= 0 || area < pixel_count / 300.0f) {
			continue;
		}
		const real_t fill = (real_t)component_size / box_area;
		if (fill < 0.18f || fill > 0.72f) {
			continue;
		}

		for (int rotation = 0; rotation < 4; rotation++) {
			int mismatches = 0;
			for (int cell_y = 0; cell_y < MARKER_CELLS; cell_y++) {
				for (int cell_x = 0; cell_x < MARKER_CELLS; cell_x++) {
					const Vector2 sample = _sample_quad(component_corners, (cell_x + 0.5f) / MARKER_CELLS, (cell_y + 0.5f) / MARKER_CELLS);
					const int sample_x = CLAMP((int)(sample.x + 0.5f), 0, width - 1);
					const int sample_y = CLAMP((int)(sample.y + 0.5f), 0, height - 1);
					const bool observed_dark = read[sample_y * width + sample_x] <= threshold;
					int canonical_x;
					int canonical_y;
					_observed_to_canonical(rotation, cell_x, cell_y, canonical_x, canonical_y);
					if (observed_dark != _marker_cell_is_dark(canonical_x, canonical_y)) {
						mismatches++;
					}
				}
			}
			if (mismatches < best_mismatches || (mismatches == best_mismatches && area > best_area)) {
				best_mismatches = mismatches;
				best_rotation = rotation;
				best_area = area;
				for (int corner = 0; corner < 4; corner++) {
					best_corners[corner] = component_corners[corner];
				}
			}
		}
	}

	const int maximum_mismatches = 8;
	if (best_rotation < 0 || best_mismatches > maximum_mismatches) {
		result["threshold"] = threshold;
		result["processing_usec"] = OS::get_singleton()->get_ticks_usec() - start_usec;
		return result;
	}

	Vector2 canonical_corners[4];
	for (int corner = 0; corner < 4; corner++) {
		canonical_corners[corner] = best_corners[(corner + best_rotation) & 3];
	}

	const Vector2 focal = p_calibration["focal_length"];
	const Vector2 principal = p_calibration["principal_point"];
	const Vector3 radial = p_calibration.get("radial_distortion", Vector3());
	const Vector2 tangential = p_calibration.get("tangential_distortion", Vector2());
	if (focal.x <= 0.0f || focal.y <= 0.0f) {
		result["processing_usec"] = OS::get_singleton()->get_ticks_usec() - start_usec;
		return result;
	}

	Vector2 normalized_corners[4];
	for (int corner = 0; corner < 4; corner++) {
		normalized_corners[corner] = _undistort_point(canonical_corners[corner], focal, principal, radial, tangential);
	}

	Vector3 homography_x;
	Vector3 homography_y;
	Vector3 homography_origin;
	if (!_square_to_quad(normalized_corners, homography_x, homography_y, homography_origin)) {
		result["processing_usec"] = OS::get_singleton()->get_ticks_usec() - start_usec;
		return result;
	}

	Vector3 rotation_x = homography_x / p_marker_size;
	Vector3 rotation_y = homography_y / p_marker_size;
	Vector3 translation = homography_origin + homography_x * 0.5f + homography_y * 0.5f;
	const real_t pose_scale = 2.0f / (rotation_x.length() + rotation_y.length());
	rotation_x *= pose_scale;
	rotation_y *= pose_scale;
	translation *= pose_scale;
	rotation_x.normalize();
	rotation_y = (rotation_y - rotation_x * rotation_x.dot(rotation_y)).normalized();
	Vector3 rotation_z = rotation_x.cross(rotation_y).normalized();

	PoolVector2Array corners;
	corners.resize(4);
	PoolVector2Array::Write corner_write = corners.write();
	for (int corner = 0; corner < 4; corner++) {
		corner_write[corner] = canonical_corners[corner];
	}

	PoolVector3Array rotation_columns;
	rotation_columns.resize(3);
	PoolVector3Array::Write rotation_write = rotation_columns.write();
	rotation_write[0] = rotation_x;
	rotation_write[1] = rotation_y;
	rotation_write[2] = rotation_z;

	result["found"] = translation.z > 0.0f;
	result["corners"] = corners;
	result["rotation_columns"] = rotation_columns;
	result["translation"] = translation;
	result["marker_size"] = p_marker_size;
	result["confidence"] = 1.0f - (real_t)best_mismatches / (MARKER_CELLS * MARKER_CELLS);
	result["threshold"] = threshold;
	result["processing_usec"] = OS::get_singleton()->get_ticks_usec() - start_usec;
	return result;
}
