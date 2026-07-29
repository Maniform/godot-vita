# PlayStation Vita Camera Support

Status: Proposed

## Summary

This document describes the design for exposing the PlayStation Vita front and
rear cameras to Godot. The implementation covers:

- live preview through Godot's existing `CameraServer`, `CameraFeed`, and
  `CameraTexture` APIs;
- still-image capture from the active stream;
- timestamped CPU-accessible frames suitable for augmented-reality processing;
- synchronization of camera frames with the Vita motion sensors.

Video recording and a built-in computer-vision or SLAM implementation are not
part of this design.

## Goals

- Expose the front and rear cameras as stable `CameraFeed` instances.
- Support live preview in a `CameraTexture` and as an `Environment` background.
- Capture an immutable `Image` without interrupting the preview.
- Provide RGBA and luminance frames to native or GDScript consumers.
- Associate every published frame with its camera timestamp and device
  orientation.
- Keep all rendering API calls on Godot's main thread.
- Work with both VitaGL and the PVR GLES2 backend.
- Release all camera resources reliably on deactivation and engine shutdown.

## Non-goals

- Video or audio recording.
- JPEG encoding in the camera backend.
- Simultaneous front and rear camera operation in the first version.
- Marker detection, object recognition, or SLAM inside `CameraServer`.
- A Vita-specific scene node that duplicates `CameraServer` ownership.

AR algorithms should consume the frames and metadata provided by this backend.
They may later be implemented as a separate module or `ARVRInterface`.

## Existing Godot Integration

Godot already provides the platform-independent pieces needed for preview:

- `CameraServer` owns and enumerates camera feeds.
- `CameraFeed` owns the texture RIDs and display transform.
- `CameraTexture` exposes a selected feed as a `Texture`.
- the GLES2 scene renderer can draw a feed as an environment background.

The camera module currently only builds platform backends for Windows and
macOS. On Vita, `CameraServer` therefore exists but contains no feeds.

`CameraTexture::get_data()` currently returns an empty `Image`. Still capture
and CPU vision consequently require an explicit CPU-frame API rather than a GPU
readback.

## Vita Camera Capabilities

The VitaSDK `sceCamera` API exposes:

- front and rear devices;
- ABGR, ARGB, planar and packed YUV422, planar YUV420, and RAW8 output;
- resolutions from 160x120 through 640x480;
- multiple frame rates, including 30 FPS at the target resolutions;
- a monotonically increasing frame number and a timestamp in
  `SceCameraRead`;
- exposure, white balance, ISO, gain, zoom, reverse, and other controls.

The VitaSDK camera sample uses ABGR output backed by a CDRAM memory block. The
first implementation will follow that proven path and add an optimized YUV path
after preview and capture are stable.

## Proposed Public API

### Existing API

The two cameras are exposed through `CameraServer`:

```gdscript
func find_camera(position: int) -> CameraFeed:
    for feed in CameraServer.feeds():
        if feed.get_position() == position:
            return feed
    return null
```

Feed positions use the existing `CameraFeed.FEED_FRONT` and
`CameraFeed.FEED_BACK` constants. Scripts must select feeds by position rather
than relying on their array index or generated ID.

Preview continues to use `CameraTexture`:

```gdscript
var feed := find_camera(CameraFeed.FEED_BACK)
var texture := CameraTexture.new()
texture.camera_feed_id = feed.get_id()
texture.camera_is_active = true
$Preview.texture = texture
```

### CameraFeed additions

The following platform-independent virtual methods are proposed for
`CameraFeed`. Other backends may initially retain default implementations that
return `ERR_UNAVAILABLE` or an empty result.

```cpp
enum FrameFormat {
	FRAME_RGBA,
	FRAME_LUMINANCE,
};

virtual Array get_formats() const;
virtual Error set_capture_format(const Size2 &p_size, int p_fps);
virtual Ref<CameraFrame> get_latest_frame(FrameFormat p_format) const;
virtual Ref<Image> capture_image() const;
```

`set_capture_format()` may only be called while the feed is inactive. The first
Vita version should expose these combinations:

- 640x480 at 30 FPS;
- 640x360 at 30 FPS;
- 320x240 at 30 FPS;
- 160x120 at 30 FPS.

Additional combinations can be exposed after device validation.

`capture_image()` returns an immutable RGBA copy of the most recently completed
frame. It does not trigger a separate camera exposure and does not stall or
restart the stream.

### CameraFrame

`CameraFrame` is a `Reference` registered with `ClassDB` and contains:

```cpp
Ref<Image> image;
uint64_t frame_id;
uint64_t timestamp_usec;
Quat device_orientation;
CameraFeed::FeedPosition camera_position;
bool orientation_available;
```

The returned image format is selected by `get_latest_frame()`:

- `FRAME_RGBA`: `Image::FORMAT_RGBA8`;
- `FRAME_LUMINANCE`: `Image::FORMAT_L8`.

Frames returned to script are immutable snapshots. Internal camera buffers must
never be exposed directly or reused while referenced by a caller.

The API is polling-based. Consumers compare `frame_id` with the last processed
ID and can process at a lower frequency than the preview. A per-frame signal
carrying an image is intentionally avoided because it would create unnecessary
allocations and main-thread work.

### CameraTexture CPU access

Once `CameraFeed::capture_image()` exists, `CameraTexture::get_data()` should
delegate to the selected feed and return its latest RGBA snapshot. This is a
convenience API; AR consumers should use `get_latest_frame()` to retain the
timestamp and orientation metadata.

## Backend Classes

The Vita backend adds:

```text
CameraVita : CameraServer
  ├── CameraFeedVita(front)
  └── CameraFeedVita(back)
```

`CameraVita` creates both feeds during construction and owns arbitration between
them. In the first version, activating one feed deactivates the other. This
avoids relying on undocumented simultaneous-camera behavior and limits memory
and bandwidth use.

`CameraFeedVita` owns:

- the `sceCamera` device number;
- a camera acquisition thread;
- the CDRAM camera memory block;
- a small native frame-buffer pool;
- synchronization primitives and atomic state;
- the last frame published on the main thread;
- counters for captured, published, and dropped frames.

## Frame Pipeline

```text
sceCameraRead worker
        |
        | copy newest completed frame
        v
two-slot CPU mailbox
        |
        | CameraServer::update(), main thread
        v
ABGR-to-RGBA conversion and texture upload
        |
        +--> CameraTexture / Environment preview
        |
        +--> latest RGBA snapshot source
        |
        +--> optional luminance snapshot source
```

Only the newest completed frame is retained in the mailbox. If the main thread
has not consumed a frame when another arrives, the older pending frame is
dropped. Camera acquisition must never wait for rendering or AR processing.

### Main-thread update

Add a no-op virtual `CameraServer::update()` and call it once per engine frame
from `Main::iteration()`. `CameraVita::update()` performs:

1. a non-blocking check for a completed native frame;
2. acquisition of the newest mailbox slot;
3. pixel conversion;
4. timestamp/orientation association;
5. `CameraFeed::set_RGB_img()` and texture upload;
6. publication of frame metadata;
7. release of the mailbox slot.

This general hook is preferable to issuing deferred calls for every camera
frame and makes frame dropping deterministic.

### Camera thread

Activation performs the following operations:

1. validate the requested resolution and frame rate;
2. allocate an aligned CDRAM block with `sceKernelAllocMemBlock`;
3. initialize `SceCameraInfo`;
4. call `sceCameraOpen`;
5. call `sceCameraStart`;
6. start the blocking read loop.

The worker repeatedly calls blocking `sceCameraRead`, validates its status, and
publishes the completed buffer to the mailbox. It must not create Godot objects,
emit signals, or call `VisualServer`.

Deactivation:

1. requests worker termination;
2. unblocks or stops camera acquisition;
3. joins the worker thread;
4. calls `sceCameraStop` when required;
5. calls `sceCameraClose`;
6. frees the CDRAM block and CPU buffers;
7. clears published frame state.

All partial initialization paths must use the same cleanup routine.

## Pixel Formats

### Initial ABGR path

The initial implementation uses `SCE_CAMERA_FORMAT_ABGR` because it is used by
the installed VitaSDK sample and produces a single packed plane.

A NEON-optimized conversion writes `Image::FORMAT_RGBA8`. A scalar reference
implementation must remain available for tests and as a fallback. The
conversion also accounts for camera pitch and may apply physical row reversal
if device tests show that it is required.

The front-camera mirror is represented by `CameraFeed::transform` for preview.
Captured images remain in the camera's canonical, unmirrored orientation unless
the API explicitly gains a transformed-capture option later.

### Optimized YUV420 path

After the ABGR path is validated, add `SCE_CAMERA_FORMAT_YUV420_PLANE`:

- Y is retained as `Image::FORMAT_L8` for AR;
- U and V are interleaved for the preview shader;
- GLES2 performs the YUV-to-RGB conversion for preview;
- RGBA conversion occurs only when `capture_image()` or an RGBA frame is
  requested.

The existing separated-YUV path cannot be used unchanged on Vita GLES2:
`Image::FORMAT_R8` is mapped to `GL_ALPHA`, while the camera copy shader samples
the red channel. MR4 therefore uploads Y as `Image::FORMAT_L8` and packs U/V in
the red and green channels of a half-resolution `Image::FORMAT_RGB8` texture.
Both are streaming textures with linear filtering. Environment backgrounds use
the engine's separated-YCbCr shader; canvas previews combine two
`CameraTexture` resources with the same BT.601 matrix.

The native mailbox retains 1.5 bytes per pixel instead of four. Packing the
preview chroma texture costs 0.75 bytes per source pixel, while RGBA allocation
and conversion are deferred until `capture_image()` or
`get_latest_frame(FRAME_RGBA)` is called. Diagnostics expose native and CDRAM
buffer sizes, YUV publication time, and RGBA conversion count/time for hardware
comparison on VitaGL and PVR.

## Still-image Capture

Still capture uses the video stream because `sceCamera` does not expose a
separate higher-resolution still-photo operation in the API used here.

The contract is:

- the feed must be active and have published at least one frame;
- the returned image matches the configured stream resolution;
- the returned `Image` owns its pixel data;
- the preview continues without interruption;
- failure returns an empty `Ref<Image>` and reports a descriptive engine error.

File encoding is outside the camera backend:

```gdscript
var image := feed.capture_image()
if image:
    image.save_png("user://capture.png")
```

PNG is sufficient for the initial implementation. Hardware-assisted JPEG
encoding can be proposed separately without changing the capture API.

## Augmented-reality Support

### Luminance processing

Most marker and feature detectors operate efficiently on grayscale data.
`FRAME_LUMINANCE` therefore avoids allocating or converting a full RGBA image.

Recommended initial AR settings:

- rear camera;
- 320x240 at 30 FPS;
- preview at 30 FPS;
- CPU vision processing at 10 to 15 FPS;
- always discard stale frames.

Native code or GDNative is recommended for per-pixel vision processing.
GDScript is suitable for orchestration and consuming detection results, but not
for high-rate image traversal.

### Timestamp and motion synchronization

The Vita port already publishes a device-orientation quaternion through
`Input`. AR requires the orientation that corresponds to the camera exposure,
not merely the most recent value during rendering.

Extend the Vita orientation path with a fixed-size history containing:

```cpp
struct TimestampedOrientation {
	uint64_t timestamp_usec;
	Quat orientation;
	bool available;
};
```

When publishing a camera frame:

1. convert camera and motion timestamps to the same time domain;
2. find the two surrounding orientation samples;
3. use spherical interpolation when both are valid;
4. otherwise use the nearest valid sample and mark reduced accuracy;
5. store the result in `CameraFrame`.

The hardware prototype must verify whether the two Vita timestamps already use
the same epoch and units. If not, maintain a measured monotonic offset.

### Calibration

Reliable pose estimation requires camera intrinsics and lens distortion values
that are not supplied by the `sceCamera` interface.

Calibration data must include:

- image width and height;
- `fx`, `fy`, `cx`, and `cy`;
- radial distortion coefficients;
- tangential distortion coefficients;
- camera-to-device rotation;
- camera-to-device translation when known.

Maintain separate profiles for front and rear cameras at 640x480. Intrinsics
may be scaled for proportional lower resolutions, while cropped aspect ratios
such as 640x360 require an adjusted principal point.

Initial calibration should use multiple physical devices to determine whether a
single profile is sufficiently accurate. If unit-to-unit variation is
significant, expose calibration overrides in project or user data.

MR3 stores separate front and rear reference profiles under
`camera/vita/calibration/{front,rear}/`. A profile contains the 640x480 image
size, focal length, principal point, distortion coefficients, and the rigid
camera-to-device transform. Zero focal lengths intentionally mark the built-in
profile as invalid until measurements from physical hardware are available;
the backend must not invent calibration values. `CameraFeed.get_calibration()`
returns a copy adjusted to the selected stream size, using proportional scaling
and a centered crop for 640x360.

### Tracking scope

This backend is sufficient for:

- marker-based pose estimation;
- QR or barcode recognition;
- image-target tracking;
- orientation-stabilized overlays;
- feeding a later native visual tracker.

World-scale 6DoF tracking and SLAM are separate features. Inertial orientation
alone cannot recover stable translation, and a production SLAM implementation
would require additional CPU optimization, mapping, relocalization, and
calibration work.

## Error Handling

Every negative `sceCamera` result should be logged with:

- operation name;
- camera device;
- requested resolution and frame rate;
- hexadecimal Vita error code.

Activation returns `false` for unsupported configuration, allocation failure,
open failure, or start failure. Repeated read failures stop the feed and queue
a main-thread state update. A transient dropped frame does not deactivate the
feed.

The backend must tolerate:

- activation before the first rendered frame;
- repeated activate/deactivate cycles;
- project shutdown while a read is pending;
- switching between front and rear feeds;
- failure after partial initialization;
- application suspend and resume.

Suspend/resume behavior must be verified on hardware. If camera state is not
preserved by the system, active feeds should be stopped before suspend and
reopened on resume.

## Build Changes

The implementation is expected to modify:

- `modules/camera/config.py`: allow the camera module on Vita;
- `modules/camera/SCsub`: compile the Vita source files;
- `modules/camera/register_types.cpp`: select `CameraVita`;
- `modules/camera/camera_vita.h`;
- `modules/camera/camera_vita.cpp`;
- `platform/vita/detect.py`: link `SceCamera_stub`;
- `servers/camera_server.h` and `.cpp`: main-thread update hook;
- `servers/camera/camera_feed.h` and `.cpp`: CPU-frame API;
- `servers/camera/camera_frame.h` and `.cpp`: immutable frame metadata;
- `scene/resources/texture.cpp`: optional `CameraTexture::get_data()` support;
- `main/main.cpp`: invoke `CameraServer::update()`;
- Vita sensor code: retain timestamped orientation history.

Documentation must update the `CameraServer`, `CameraFeed`, `CameraTexture`,
`Input`, and project-setting class references as applicable.

## Implementation Phases

### Phase 0: hardware probe

- Validate both camera devices.
- Validate ABGR byte order, pitch, transforms, resolutions, and frame rates.
- Compare camera and motion timestamp domains.
- Test simultaneous-camera behavior without depending on it.
- Record CPU time and memory bandwidth for conversion and upload.

Exit criteria: each camera runs for ten minutes with correct colors and no
corruption at the selected default format.

### Phase 1: preview backend

- Build and register `CameraVita`.
- Add both feeds.
- Implement activation, worker acquisition, main-thread update, and cleanup.
- Implement ABGR-to-RGBA conversion.
- Support `CameraTexture` and environment preview.

Exit criteria: stable preview from both cameras with repeatable switching.

### Phase 2: still capture

- Add `CameraFrame`.
- Add immutable RGBA capture.
- Implement `CameraTexture::get_data()` if accepted.
- Add a demonstration scene that saves PNG files.

Exit criteria: captured images have correct color, orientation, dimensions, and
ownership while preview remains active.

### Phase 3: AR frame path

- Add luminance frames.
- Add timestamped orientation history and interpolation.
- Define calibration storage.
- Add diagnostics for frame age and dropped frames.

Exit criteria: a test application processes unique luminance frames at a
controlled rate and receives synchronized orientation metadata.

### Phase 4: optimized YUV preview

- Add planar YUV420 acquisition.
- Correct the GLES2 luminance/chroma texture path.
- Convert to RGBA only on demand.
- Benchmark against the ABGR implementation on VitaGL and PVR.

Exit criteria: lower CPU or memory cost without preview or capture regressions.

### Phase 5: AR demonstration

- Calibrate front and rear cameras.
- Render a projection using the calibrated intrinsics.
- Add a marker-based pose-estimation demonstration.
- Verify overlay stability while rotating and translating the console.

This phase validates the camera foundation but does not make the marker tracker
part of `CameraServer`.

## Testing

### Host tests

- scalar ABGR-to-RGBA conversion;
- NEON output equivalence using captured fixture data;
- pitch and row-orientation handling;
- mailbox state transitions and frame dropping;
- frame snapshot ownership;
- calibration scaling and cropping;
- timestamp lookup and quaternion interpolation;
- error cleanup after every initialization step.

### Device tests

- front and rear preview;
- 320x240, 640x360, and 640x480;
- VitaGL and PVR render paths;
- at least 100 activation/deactivation cycles;
- at least 30 minutes of continuous preview;
- capture during rendering load;
- switching feeds while processing AR frames;
- suspend/resume;
- normal engine shutdown with an active feed;
- memory, acquisition FPS, upload time, conversion time, and dropped frames.

Suggested initial performance targets:

- preview: 30 FPS at 640x480 where supported;
- acquisition thread: no main-thread waits;
- main-thread conversion and upload: below 4 ms at 640x480;
- AR luminance retrieval: below 1 ms at 320x240, excluding vision work;
- no unbounded queue or memory growth.

## Risks and Open Questions

- Exact ABGR byte order and pitch behavior must be confirmed on hardware.
- Camera and motion timestamps may require time-domain alignment.
- Front-camera mirroring and physical sensor orientation need device tests.
- Suspend/resume hooks in the current Vita port may need extension.
- PVR and VitaGL may differ in supported GLES2 texture formats and upload cost.
- Simultaneous camera access is intentionally deferred.
- Calibration may vary enough between consoles to require per-device overrides.
- A 640x480 RGBA conversion and upload every frame may be too expensive under
  heavy rendering load; YUV420 is the planned optimization.

## Delivery Structure

The work should be submitted in independently testable changes:

1. Vita backend and preview.
2. Still capture and frame metadata.
3. Luminance, motion synchronization, and calibration.
4. YUV optimization.
5. AR demonstration.

The first three changes provide the complete camera foundation requested by
this design. The final two improve performance and demonstrate its AR use.
