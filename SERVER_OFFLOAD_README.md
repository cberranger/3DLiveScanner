# 3D Live Scanner - Server Offload System

## Overview

This system offloads GPU-intensive TSDF reconstruction from the phone to a powerful server with dedicated GPU (RTX 3090/4090). The phone captures depth frames and camera poses, streams them over WebSocket, and the server performs high-quality reconstruction using Open3D.

## Architecture

```
Phone (capture)  ──WebSocket──>  Server (reconstruction)
     │                                │
     ├─ Depth frames (ToF)            ├─ Open3D TSDF fusion
     ├─ Camera poses (ARCore)         ├─ GPU-accelerated (CUDA)
     ├─ RGB images (optional)         ├─ Mesh extraction
     │                                │
     └─────────< Mesh chunks <────────┘
```

## Files Changed/Added

### Server Side (Python)

| File | Description |
|------|-------------|
| `server/reconstruction_server.py` | Main WebSocket server with Open3D TSDF fusion |
| `server/requirements.txt` | Python dependencies |
| `server/test_client.py` | Test client for server verification |
| `server/simple_test_server.py` | Minimal WebSocket test server |
| `server/simple_test_client.py` | Minimal WebSocket test client |
| `server/README.md` | Server documentation |

### Android Client (Java)

| File | Description |
|------|-------------|
| `scanner/app/src/main/java/.../ServerStreamClient.java` | WebSocket client, MessagePack encoding |
| `scanner/app/src/main/java/.../Main.java` | Integration: init, streaming, UI status |
| `scanner/app/src/main/java/.../JNI.java` | Native methods for depth/pose extraction |

### Android Native (C++)

| File | Description |
|------|-------------|
| `scanner/app/src/main/jni/app.h` | Added GetDepthData, GetCameraPose, GetDepthIntrinsics |
| `scanner/app/src/main/jni/app.cc` | JNI implementations |

### Android Resources

| File | Description |
|------|-------------|
| `scanner/app/src/main/res/xml/settings.xml` | Server settings UI (enable, URL) |
| `scanner/app/src/main/res/values/strings.xml` | Server-related strings |
| `scanner/app/src/main/res/layout/activity_main.xml` | Server status TextView |

### Build Files

| File | Description |
|------|-------------|
| `scanner/app/build.gradle` | Added WebSocket, MessagePack, LZ4 dependencies |
| `scanner/app/src/main/AndroidManifest.xml` | Added ACCESS_NETWORK_STATE permission |

---

## Current Status

### ✅ Working
- WebSocket connection from phone to server
- Server receives connection and creates session
- Phone sends camera poses every frame
- Server logs frame receipt
- UI shows connection status
- Tap-to-reconnect functionality
- MessagePack encoding/decoding

### ⚠️ Partial
- Depth data sent as empty (native capture not hooked up)
- Intrinsics show fx=0, fy=0 (need to read from ARCore)
- Server TSDF fusion ready but no depth to process

### ❌ TODO
- [ ] Hook ARCore depth callback to capture raw depth buffer
- [ ] Store depth in native layer for GetDepthData()
- [ ] Fix GetDepthIntrinsics() to return real camera parameters
- [ ] Server mesh extraction and return to phone
- [ ] Display server-reconstructed mesh on phone
- [ ] Re-enable LZ4 compression (format mismatch fixed)
- [ ] Optional: RGB frame streaming

---

## Usage

### Start Server
```bash
cd E:\code\3dlivescanner-opus\server
pip install -r requirements.txt
python reconstruction_server.py --port 7823 --gpu 0
```

### Configure Phone
1. Open 3D Live Scanner app
2. Go to Settings → Server Offload
3. Enable "Server Mode"
4. Set Server URL: `ws://192.168.1.10:7823` (your server IP)
5. Start scanning - status shows at top of screen

### Test Server
```bash
python simple_test_client.py ws://localhost:7823
```

---

## Protocol

### Messages (MessagePack encoded)

**init** (phone → server)
```json
{
  "type": "init",
  "width": 240,
  "height": 180,
  "fx": 200.0, "fy": 200.0,
  "cx": 120.0, "cy": 90.0,
  "mesh_interval": 5
}
```

**frame** (phone → server)
```json
{
  "type": "frame",
  "timestamp": 1234.567,
  "depth": <binary uint16 mm>,
  "pose": [16 floats, column-major 4x4]
}
```

**frame_ack** (server → phone)
```json
{
  "type": "frame_ack",
  "frame_id": 42,
  "timestamp": 1234.567
}
```

**mesh** (server → phone)
```json
{
  "type": "mesh",
  "vertices": <binary float32>,
  "triangles": <binary int32>,
  "normals": <binary float32>,
  "vertex_count": 10000,
  "triangle_count": 5000
}
```

---

## Next Steps (Priority Order)

### 1. Capture Real Depth Data
Hook into ARCore's depth image callback:
- In `arcore_app.cc` or equivalent, when depth frame arrives
- Copy to buffer accessible by `GetDepthData()`
- Return actual uint16 mm values

### 2. Fix Camera Intrinsics
Read from ARCore session:
```cpp
ArCameraIntrinsics* intrinsics;
ArCameraIntrinsics_create(ar_session, &intrinsics);
ArCamera_getImageIntrinsics(ar_session, ar_camera, intrinsics);
// Extract fx, fy, cx, cy
```

### 3. Server Mesh Return
- Server already extracts mesh every N frames
- Send compressed mesh data back
- Phone receives and renders (or replaces local mesh)

### 4. Optimization
- Re-enable LZ4 compression with matching formats
- Reduce frame rate when bandwidth limited
- Delta compression for poses

---

## Performance Notes

- Typical latency: 20-50ms on LAN
- Frame size without depth: ~150 bytes (pose only)
- Frame size with depth: ~86KB (240×180×2 bytes)
- With LZ4: ~40-50KB typical
- Server can handle multiple clients (separate TSDF volumes)

---

## Troubleshooting

### "Disconnected" immediately
- Check server is running: `netstat -tlnp | grep 7823`
- Check firewall allows port
- Verify URL format: `ws://IP:PORT` (not `http://`)

### "unknown scheme: we"
- URL is corrupted - check settings, clear app data

### Server shows "unpack(b) received extra data"
- MessagePack version mismatch - fixed with Unpacker iterator

### No frames received
- Check `mServerInitialized` is true
- Verify scanning is active (m3drRunning)
- Check logcat for ServerStreamClient errors
