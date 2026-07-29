#!/usr/bin/env python3
"""Compute a Godot Vita camera profile from checkerboard photographs."""

import argparse
from pathlib import Path
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images", nargs="+", help="PNG/JPEG calibration photographs")
    parser.add_argument("--camera", choices=("front", "rear"), required=True)
    parser.add_argument("--columns", type=int, default=9, help="checkerboard inner corners per row")
    parser.add_argument("--rows", type=int, default=6, help="checkerboard inner corners per column")
    parser.add_argument("--square-mm", type=float, default=20.0)
    parser.add_argument("--output", type=Path, help="write the project.godot snippet here")
    args = parser.parse_args()

    try:
        import cv2
        import numpy as np
    except ImportError:
        print("OpenCV and NumPy are required: python3 -m pip install opencv-python numpy", file=sys.stderr)
        return 2

    pattern_size = (args.columns, args.rows)
    object_template = np.zeros((args.columns * args.rows, 3), np.float32)
    object_template[:, :2] = np.mgrid[0:args.columns, 0:args.rows].T.reshape(-1, 2)
    object_template *= args.square_mm / 1000.0

    object_points = []
    image_points = []
    image_size = None
    for filename in args.images:
        image = cv2.imread(filename, cv2.IMREAD_GRAYSCALE)
        if image is None:
            print(f"Skipping unreadable image: {filename}", file=sys.stderr)
            continue
        current_size = (image.shape[1], image.shape[0])
        if image_size is None:
            image_size = current_size
        elif current_size != image_size:
            print(f"Skipping {filename}: expected {image_size}, got {current_size}", file=sys.stderr)
            continue
        found, corners = cv2.findChessboardCorners(image, pattern_size)
        if not found:
            print(f"Checkerboard not found: {filename}", file=sys.stderr)
            continue
        corners = cv2.cornerSubPix(
            image,
            corners,
            (11, 11),
            (-1, -1),
            (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001),
        )
        object_points.append(object_template.copy())
        image_points.append(corners)

    if image_size is None or len(image_points) < 10:
        print(f"Need at least 10 usable views; found {len(image_points)}", file=sys.stderr)
        return 1

    rms, matrix, distortion, _, _ = cv2.calibrateCamera(
        object_points, image_points, image_size, None, None
    )
    k1, k2, p1, p2, k3 = distortion.ravel()[:5]
    prefix = f"vita/calibration/{args.camera}"
    snippet = f"""# RMS reprojection error: {rms:.6f} pixels ({len(image_points)} views)
[camera]

{prefix}/image_size=Vector2( {image_size[0]}, {image_size[1]} )
{prefix}/focal_length=Vector2( {matrix[0, 0]:.9g}, {matrix[1, 1]:.9g} )
{prefix}/principal_point=Vector2( {matrix[0, 2]:.9g}, {matrix[1, 2]:.9g} )
{prefix}/radial_distortion=Vector3( {k1:.9g}, {k2:.9g}, {k3:.9g} )
{prefix}/tangential_distortion=Vector2( {p1:.9g}, {p2:.9g} )
"""
    if args.output:
        args.output.write_text(snippet, encoding="utf-8")
        print(f"Wrote {args.output}")
    else:
        print(snippet)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
