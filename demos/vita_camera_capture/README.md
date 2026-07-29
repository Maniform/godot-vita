# Vita camera AR demonstration

This project previews the rear Vita camera through the YUV420 path, processes
unique 320x240 luminance frames at up to 15 FPS, detects the supplied fiducial
marker, estimates its planar pose, and projects a cube using the calibrated
camera intrinsics.

## Minimal color preview

A color preview does not require a YUV shader when the automatic stream is
enabled:

```gdscript
var texture := CameraTexture.new()
texture.camera_feed_id = feed.get_id()
var error := texture.set_color_stream_enabled(true)
if error == OK:
    $Preview.texture = texture
    texture.camera_is_active = true
```

The Vita backend converts and uploads RGBA only while at least one
`CameraTexture` requests this stream. The two-texture YUV shader used by this
AR demo remains the lower-bandwidth option.

## Camera calibration

The engine intentionally ships with invalid zero focal lengths: camera
intrinsics must be measured on real hardware. Calibrate the front and rear
camera separately.

1. Print a flat checkerboard with 9 by 6 **inner** corners and measure its square
   size.
2. Capture at least 15 sharp 640x480 images spanning the field of view and a
   range of tilts. Use the demo's PNG button and avoid changing focus or zoom.
3. On a workstation with OpenCV and NumPy, run:

   ```sh
   python3 calibrate_vita_camera.py --camera rear --square-mm 20 captures/*.png
   python3 calibrate_vita_camera.py --camera front --square-mm 20 front/*.png
   ```

4. Copy each generated key into the existing `[camera]` section of the game
   project's `project.godot`. Do not add a second section with the same name.
5. Re-run the capture set and reject a profile with high reprojection error or
   visibly drifting cube edges.

Checkerboard calibration determines intrinsics and lens distortion. The
camera-to-device transform remains a hardware measurement and is not used by
this marker demo.

## Marker tracking

Print `marker.svg` at 100% scale. Its black square is exactly 80 mm wide,
matching `MARKER_SIZE_METERS` in `Main.gd`; the outer white quiet zone makes
the page graphic 100 mm wide. If it is printed at another size, change the
constant to the measured black-square width.

`VitaARMarkerTracker` is a deliberately small demonstration tracker, not part
of `CameraServer`. It uses Otsu thresholding, connected-component candidate
selection, the asymmetric 8x8 payload, distortion-aware homography
decomposition, and rejects frames with more than eight sampled bit errors. The
overlay applies light temporal smoothing and projects through the calibrated
intrinsics. It is suitable for validating the camera foundation, not for
production tracking or SLAM.

Device validation still needs to cover both cameras, VitaGL and PVR, varied
lighting, rotation and translation, and comparison of projected edges against
the physical marker.
