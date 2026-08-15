# anonrt

Real time face anonymization tool built in C++
Point it at a video file or use your webcam and it pixelates live
Export the video or webcam feed as an MP4 file

This is built to explore use cases like face redaction in CCTV footage, dashcam clips or other video workflows

![demo](demo.gif)

## Features

- Open a video or your webcam stream and see faces get pixelated live, in real time
- Full playback controls on video files - play, pause, restart, scrub to any frame
- Tune detection on the fly, score threshold, nms threshold, top-k and pixelation aggressiveness
- Export the anonymized result to MP4 file, works for both sources

## Build Design

- Uses sokol + ImGui for the UI
- tinyfiledialogs for dialog boxes and whereami to figure out binary file location
- Face detection using OpenCV's `FaceDetectorYN` (YuNet), running from a bundled ONNX model, runs locally on CPU
- Anonymization is straightforward pixelation, downsample the face region, upsample it back with nearest-neighbor interpolation
- Video decoding and face detection happen on a background thread, and hand frames to ui thread using lock protected ring buffer
  which means no UI stutters
- Recording runs on its separate thread if a video file, doesn't freeze UI

## Build

Needs CMake 3.25+, a C++20 compiler, and OpenCV (built with FFmpeg support if you want H.264 export to work).

```bash
git clone https://github.com/jatindalal/anonrt.git
cd anonrt
mkdir build && cd build
cmake ..
cmake --build .
```

The YuNet model gets copied next to the built executable automatically

**Platform specifics:** Metal on macOS, OpenGL on Linux (needs X11/Xi/Xcursor), Direct3D 11 on Windows

## Using it

1. Launch it
2. Hit **Open Video** for a file, or **Open Webcam** for live
3. Flip on **Anonymize** and tune the sliders until detection feels right for your footage
4. Scrub around with the playback controls if you're working with a file
5. To save the anonymized output: **Choose** a save path, then **Save**. For webcam recording, **Stop Recording** when you're done.

## Built with

- [sokol](https://github.com/floooh/sokol) for windowing and the GPU layer
- [Dear ImGui](https://github.com/ocornut/imgui) for the UI
- [tinyfiledialogs](https://github.com/native-toolkit/tinyfiledialogs) for native file pickers
- [whereami](https://github.com/gpakosz/whereami) to locate the bundled model at runtime relative to the executable
- OpenCV for video I/O and face detection

All vendored directly in `thirdparty/` — no package manager, no submodules, clone and build.
