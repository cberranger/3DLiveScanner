#!/usr/bin/env python3
"""
Test client for reconstruction server.
Sends synthetic depth frames to verify server functionality.
"""

import asyncio
import websockets
import msgpack
import numpy as np
import time

async def test_server(host="localhost", port=8765):
    uri = f"ws://{host}:{port}"
    print(f"Connecting to {uri}...")
    
    async with websockets.connect(uri, max_size=50*1024*1024) as ws:
        print("Connected!")
        
        # Initialize
        width, height = 240, 180
        init_msg = msgpack.packb({
            "type": "init",
            "width": width,
            "height": height,
            "fx": 200.0,
            "fy": 200.0,
            "cx": width / 2,
            "cy": height / 2,
            "mesh_interval": 5
        })
        await ws.send(init_msg)
        
        response = await ws.recv()
        data = msgpack.unpackb(response, raw=False)
        print(f"Init response: {data}")
        
        # Send some test frames
        for i in range(20):
            # Create synthetic depth (a plane at 1m with some noise)
            depth = np.ones((height, width), dtype=np.float32) * 1.0
            depth += np.random.randn(height, width).astype(np.float32) * 0.01
            depth_mm = (depth * 1000).astype(np.uint16)
            
            # Create pose (move camera along X axis)
            pose = np.eye(4, dtype=np.float64)
            pose[0, 3] = i * 0.05  # Move 5cm per frame
            
            frame_msg = msgpack.packb({
                "type": "frame",
                "timestamp": time.time(),
                "depth": depth_mm.tobytes(),
                "pose": pose.flatten().tolist()
            })
            
            await ws.send(frame_msg)
            response = await ws.recv()
            data = msgpack.unpackb(response, raw=False)
            
            frame_id = data.get("frame_id", "?")
            has_mesh = "mesh" in data
            print(f"Frame {i}: ack={frame_id}, mesh={has_mesh}")
            
            await asyncio.sleep(0.033)  # ~30fps
        
        # Request final mesh
        print("\nRequesting final mesh...")
        await ws.send(msgpack.packb({"type": "get_mesh"}))
        response = await ws.recv()
        
        # Handle potential LZ4 compression
        if response[:4] == b'\x04\x22\x4d\x18':
            import lz4.frame
            response = lz4.frame.decompress(response)
        
        data = msgpack.unpackb(response, raw=False)
        
        if "mesh" in data and data["mesh"]:
            mesh = data["mesh"]
            print(f"Received mesh:")
            print(f"  Vertices: {mesh['vertex_count']}")
            print(f"  Triangles: {mesh['triangle_count']}")
        else:
            print("No mesh data received")
        
        print("\nTest complete!")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    
    asyncio.run(test_server(args.host, args.port))
