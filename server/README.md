# 3D Live Scanner - Reconstruction Server

GPU-accelerated TSDF reconstruction server for offloading heavy computation from mobile devices.

## Requirements

- Python 3.8+
- CUDA toolkit (optional, for GPU acceleration)
- ~2GB RAM per connected client

## Installation

```bash
cd server
pip install -r requirements.txt
```

For GPU support, ensure Open3D is built with CUDA:
```bash
pip install open3d-cuda  # If available for your platform
```

## Usage

### Basic
```bash
python reconstruction_server.py
```

### With Options
```bash
python reconstruction_server.py \
    --host 0.0.0.0 \
    --port 8765 \
    --gpu 0 \
    --voxel-size 0.01 \
    --sdf-trunc 0.04
```

### Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `--host` | 0.0.0.0 | Listen address |
| `--port` | 8765 | WebSocket port |
| `--gpu` | 0 | CUDA device ID |
| `--voxel-size` | 0.01 | Voxel size in meters (smaller = more detail) |
| `--sdf-trunc` | 0.04 | SDF truncation distance |

## Protocol

The server uses WebSocket with MessagePack encoding and optional LZ4 compression.

### Message Types

#### Client → Server

**init** - Initialize session with camera intrinsics
```json
{
    "type": "init",
    "width": 240,
    "height": 180,
    "fx": 200.0,
    "fy": 200.0,
    "cx": 120.0,
    "cy": 90.0,
    "mesh_interval": 10
}
```

**frame** - Send depth frame
```json
{
    "type": "frame",
    "timestamp": 1234567890.123,
    "depth": <binary uint16 mm>,
    "pose": [<16 floats, column-major 4x4>],
    "color": <optional binary RGB>
}
```

**get_mesh** - Request full mesh
```json
{
    "type": "get_mesh"
}
```

**reset** - Reset reconstruction
```json
{
    "type": "reset"
}
```

#### Server → Client

**frame_ack** - Frame processed
```json
{
    "type": "frame_ack",
    "frame_id": 42,
    "timestamp": 1234567890.123,
    "mesh": <optional mesh data>
}
```

**mesh** - Full mesh response
```json
{
    "type": "mesh",
    "mesh": {
        "vertices": <binary float32>,
        "triangles": <binary uint32>,
        "normals": <binary float32>,
        "colors": <optional binary uint8>,
        "vertex_count": 10000,
        "triangle_count": 20000
    }
}
```

## Android Client

The `ServerStreamClient.java` class provides easy integration:

```java
ServerStreamClient client = new ServerStreamClient("ws://192.168.1.100:8765");
client.connect();
client.initialize(width, height, fx, fy, cx, cy);

// In your frame callback:
client.sendFrame(depthData, pose, timestamp);

// When done:
client.requestMesh();
client.disconnect();
```

## Performance Tips

1. **Network**: Use 5GHz WiFi or wired connection for best latency
2. **Resolution**: Lower depth resolution (240x180) streams faster
3. **Mesh Interval**: Higher values reduce server load but less real-time feedback
4. **GPU**: Ensure CUDA is enabled for 5-10x faster reconstruction
5. **Multiple Clients**: Each client gets isolated reconstruction volume

## Troubleshooting

### "CUDA not available"
- Check CUDA toolkit installation
- Verify Open3D CUDA build: `python -c "import open3d; print(open3d.core.cuda.is_available())"`

### High Latency
- Reduce `mesh_interval` or disable real-time mesh feedback
- Check network bandwidth (need ~5 Mbps per client)
- Use LZ4 compression (enabled by default)

### Out of Memory
- Increase `voxel_size` (0.02 uses 1/8 the memory of 0.01)
- Limit scan area
- Restart server between large scans
