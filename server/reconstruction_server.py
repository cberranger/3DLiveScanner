#!/usr/bin/env python3
"""
3D Live Scanner - Reconstruction Server

Receives depth frames and camera poses from mobile devices,
performs GPU-accelerated TSDF fusion, and returns mesh chunks.

Usage:
    python reconstruction_server.py --port 8765 --gpu 0
"""

import asyncio
import websockets
import json
import numpy as np
import open3d as o3d
import msgpack
import lz4.frame
import time
import argparse
import logging
from dataclasses import dataclass, field
from typing import Dict, Optional, Tuple, List
from collections import deque
from concurrent.futures import ThreadPoolExecutor
from threading import Lock

logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger("ReconstructionServer")


@dataclass
class CameraIntrinsics:
    """Camera intrinsic parameters"""

    width: int
    height: int
    fx: float
    fy: float
    cx: float
    cy: float

    def to_open3d(self) -> o3d.camera.PinholeCameraIntrinsic:
        return o3d.camera.PinholeCameraIntrinsic(
            self.width, self.height, self.fx, self.fy, self.cx, self.cy
        )


@dataclass
class DepthFrame:
    """A single depth frame with pose"""

    timestamp: float
    depth: np.ndarray  # HxW float32, meters
    color: Optional[np.ndarray]  # HxWx3 uint8, RGB
    pose: np.ndarray  # 4x4 float64, camera-to-world
    intrinsics: CameraIntrinsics


@dataclass
class ClientSession:
    """State for a connected client"""

    client_id: str
    volume: o3d.t.geometry.VoxelBlockGrid
    intrinsics: Optional[CameraIntrinsics] = None
    frame_count: int = 0
    last_mesh_frame: int = 0
    mesh_interval: int = 10  # Extract mesh every N frames
    pending_frames: deque = field(default_factory=lambda: deque(maxlen=30))
    frame_buffer: List[dict] = field(default_factory=list)
    batch_size: int = 5
    last_extrinsic: Optional[np.ndarray] = None
    last_pose: Optional[np.ndarray] = None
    lock: Lock = field(default_factory=Lock)


class ReconstructionServer:
    """WebSocket server for remote 3D reconstruction"""

    def __init__(
        self,
        host: str = "0.0.0.0",
        port: int = 8765,
        voxel_size: float = 0.01,
        sdf_trunc: float = 0.04,
        gpu_id: int = 0,
    ):
        self.host = host
        self.port = port
        self.voxel_size = voxel_size
        self.sdf_trunc = sdf_trunc
        self.gpu_id = gpu_id
        self.sessions: Dict[str, ClientSession] = {}

        # Try to enable GPU
        self._init_gpu()

    def _init_gpu(self):
        """Initialize GPU if available"""
        try:
            if o3d.core.cuda.is_available():
                self.device = o3d.core.Device(f"CUDA:{self.gpu_id}")
                logger.info(f"Using CUDA device {self.gpu_id}")
            else:
                self.device = o3d.core.Device("CPU:0")
                logger.info("CUDA not available, using CPU")
        except Exception as e:
            self.device = o3d.core.Device("CPU:0")
            logger.warning(f"GPU init failed: {e}, using CPU")

    def create_volume(self):
        """Create a new GPU-accelerated TSDF volume using VoxelBlockGrid"""
        volume = o3d.t.geometry.VoxelBlockGrid(
            attr_names=["tsdf", "weight", "color"],
            attr_dtypes=[
                o3d.core.Dtype.Float32,
                o3d.core.Dtype.Float32,
                o3d.core.Dtype.UInt8,
            ],
            attr_channels=[
                o3d.core.SizeVector([1]),
                o3d.core.SizeVector([1]),
                o3d.core.SizeVector([3]),
            ],
            voxel_size=self.voxel_size,
            device=self.device,
        )
        return volume.to(self.device)

    async def handle_client(self, websocket):
        """Handle a connected client"""
        client_id = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
        logger.info(f"=" * 50)
        logger.info(f"NEW CONNECTION from {client_id}")
        logger.info(f"=" * 50)

        # Create session
        session = ClientSession(client_id=client_id, volume=self.create_volume())
        self.sessions[client_id] = session
        logger.info(
            f"Session created for {client_id}, total active: {len(self.sessions)}"
        )

        try:
            async for message in websocket:
                await self._process_message(websocket, session, message)
        except websockets.exceptions.ConnectionClosed as e:
            logger.info(
                f"Client {client_id} disconnected: {e.reason if e.reason else 'connection closed'}"
            )
        except Exception as e:
            logger.error(f"Client {client_id} error: {e}")
        finally:
            # Cleanup
            logger.info(
                f"Cleaning up session for {client_id}, frames processed: {session.frame_count}"
            )
            del self.sessions[client_id]
            logger.info(f"Remaining active sessions: {len(self.sessions)}")

    async def _process_message(self, websocket, session: ClientSession, message: bytes):
        """Process an incoming message"""
        try:
            logger.debug(
                f"[{session.client_id}] Received {len(message)} bytes, first 20: {message[:20].hex()}"
            )

            # Decompress if needed (LZ4 frame format)
            if len(message) >= 4 and message[:4] == b"\x04\x22\x4d\x18":
                logger.debug(
                    f"[{session.client_id}] Detected LZ4 frame, decompressing..."
                )
                message = lz4.frame.decompress(message)

            # Decode - use Unpacker to consume exactly one message
            unpacker = msgpack.Unpacker(raw=False, strict_map_key=False)
            unpacker.feed(message)
            try:
                data = next(iter(unpacker))
            except StopIteration:
                logger.error(f"[{session.client_id}] Empty message")
                return
            msg_type = data.get("type")
            logger.debug(f"[{session.client_id}] Message type: {msg_type}")

            if msg_type == "init":
                await self._handle_init(websocket, session, data)
            elif msg_type == "frame":
                await self._handle_frame(websocket, session, data)
            elif msg_type == "get_mesh":
                await self._handle_get_mesh(websocket, session, data)
            elif msg_type == "reset":
                await self._handle_reset(websocket, session, data)
            elif msg_type == "ping":
                await websocket.send(
                    msgpack.packb({"type": "pong", "time": time.time()})
                )
            else:
                logger.warning(f"Unknown message type: {msg_type}")

        except Exception as e:
            logger.error(f"Error processing message: {e}")
            await websocket.send(msgpack.packb({"type": "error", "message": str(e)}))

    async def _handle_init(self, websocket, session: ClientSession, data: dict):
        """Handle initialization message with camera intrinsics"""
        session.intrinsics = CameraIntrinsics(
            width=data["width"],
            height=data["height"],
            fx=data["fx"],
            fy=data["fy"],
            cx=data["cx"],
            cy=data["cy"],
        )
        session.mesh_interval = data.get("mesh_interval", 10)

        logger.info(
            f"[{session.client_id}] INIT: {session.intrinsics.width}x{session.intrinsics.height}, fx={session.intrinsics.fx:.1f}, fy={session.intrinsics.fy:.1f}"
        )
        logger.info(
            f"[{session.client_id}] Mesh interval: {session.mesh_interval} frames"
        )

        await websocket.send(
            msgpack.packb(
                {
                    "type": "init_ack",
                    "voxel_size": self.voxel_size,
                    "sdf_trunc": self.sdf_trunc,
                }
            )
        )

    async def _handle_frame(self, websocket, session: ClientSession, data: dict):
        """Handle incoming depth frame"""
        if session.intrinsics is None:
            logger.warning(f"[{session.client_id}] Frame received but not initialized!")
            await websocket.send(
                msgpack.packb({"type": "error", "message": "Not initialized"})
            )
            return

        # Decode frame
        timestamp = data["timestamp"]
        depth_bytes = data.get("depth")
        pose_data = data["pose"]

        # Log frame receipt periodically
        if session.frame_count % 30 == 0:
            logger.info(
                f"[{session.client_id}] Frame {session.frame_count}: timestamp={timestamp:.3f}, depth={'yes' if depth_bytes else 'no'}"
            )

        # Handle case where depth data is missing
        if not depth_bytes:
            # Just acknowledge pose-only frame
            session.frame_count += 1
            await websocket.send(
                msgpack.packb(
                    {
                        "type": "frame_ack",
                        "frame_id": session.frame_count,
                        "timestamp": timestamp,
                    }
                )
            )
            return

        # Convert depth (uint16 mm -> float32 meters)
        depth = (
            np.frombuffer(depth_bytes, dtype=np.uint16)
            .reshape(session.intrinsics.height, session.intrinsics.width)
            .astype(np.float32)
            / 1000.0
        )

        # Convert pose (column-major flat array -> 4x4 matrix)
        pose = np.array(pose_data, dtype=np.float64).reshape(4, 4)

        # Optional color
        color = None
        if "color" in data and data["color"]:
            color = np.frombuffer(data["color"], dtype=np.uint8).reshape(
                session.intrinsics.height, session.intrinsics.width, 3
            )

        # Create Open3D images
        o3d_depth = o3d.geometry.Image(depth)
        o3d_color = o3d.geometry.Image(color) if color is not None else None

        # Create RGBD image
        if o3d_color is not None:
            rgbd = o3d.geometry.RGBDImage.create_from_color_and_depth(
                o3d_color,
                o3d_depth,
                depth_scale=1.0,
                depth_trunc=4.0,
                convert_rgb_to_intensity=False,
            )
        else:
            # Create dummy color
            dummy_color = (
                np.ones(
                    (session.intrinsics.height, session.intrinsics.width, 3),
                    dtype=np.uint8,
                )
                * 128
            )
            rgbd = o3d.geometry.RGBDImage.create_from_color_and_depth(
                o3d.geometry.Image(dummy_color),
                o3d_depth,
                depth_scale=1.0,
                depth_trunc=4.0,
                convert_rgb_to_intensity=False,
            )

        # Integrate into volume
        intrinsic = session.intrinsics.to_open3d()
        extrinsic = np.linalg.inv(pose)  # Open3D wants world-to-camera

        with session.lock:
            session.volume.integrate(rgbd, intrinsic, extrinsic)
            session.frame_count += 1

        # Send acknowledgment
        response = {
            "type": "frame_ack",
            "frame_id": session.frame_count,
            "timestamp": timestamp,
        }

        # Extract and send mesh periodically
        if session.frame_count - session.last_mesh_frame >= session.mesh_interval:
            mesh_data = self._extract_mesh_data(session)
            if mesh_data:
                response["mesh"] = mesh_data
                session.last_mesh_frame = session.frame_count

        await websocket.send(msgpack.packb(response))

    async def _handle_get_mesh(self, websocket, session: ClientSession, data: dict):
        """Handle mesh extraction request"""
        mesh_data = self._extract_mesh_data(session, full=True)

        # Compress mesh data
        response = msgpack.packb({"type": "mesh", "mesh": mesh_data})
        compressed = lz4.frame.compress(response)

        await websocket.send(compressed)

    async def _handle_reset(self, websocket, session: ClientSession, data: dict):
        """Reset reconstruction volume"""
        with session.lock:
            session.volume = self.create_volume()
            session.frame_count = 0
            session.last_mesh_frame = 0

        await websocket.send(msgpack.packb({"type": "reset_ack"}))

    def _extract_mesh_data(
        self, session: ClientSession, full: bool = False
    ) -> Optional[dict]:
        """Extract mesh from TSDF volume"""
        try:
            with session.lock:
                mesh = session.volume.extract_triangle_mesh()

            if len(mesh.vertices) == 0:
                return None

            # Compute normals
            mesh.compute_vertex_normals()

            # Simplify if not full extraction
            if not full and len(mesh.triangles) > 50000:
                mesh = mesh.simplify_quadric_decimation(50000)

            # Convert to serializable format
            vertices = np.asarray(mesh.vertices).astype(np.float32)
            triangles = np.asarray(mesh.triangles).astype(np.uint32)
            normals = np.asarray(mesh.vertex_normals).astype(np.float32)

            colors = None
            if mesh.has_vertex_colors():
                colors = (np.asarray(mesh.vertex_colors) * 255).astype(np.uint8)

            return {
                "vertices": vertices.tobytes(),
                "triangles": triangles.tobytes(),
                "normals": normals.tobytes(),
                "colors": colors.tobytes() if colors is not None else None,
                "vertex_count": len(vertices),
                "triangle_count": len(triangles),
            }

        except Exception as e:
            logger.error(f"Mesh extraction failed: {e}")
            return None

    async def start(self):
        """Start the server"""
        logger.info(f"")
        logger.info(f"=" * 60)
        logger.info(f"  3D LIVE SCANNER - RECONSTRUCTION SERVER")
        logger.info(f"=" * 60)
        logger.info(f"  Host:       {self.host}")
        logger.info(f"  Port:       {self.port}")
        logger.info(f"  Voxel size: {self.voxel_size}m")
        logger.info(f"  SDF trunc:  {self.sdf_trunc}m")
        logger.info(f"  Device:     {self.device}")
        logger.info(f"=" * 60)
        logger.info(f"  Waiting for connections...")
        logger.info(f"")
        async with websockets.serve(
            self.handle_client,
            self.host,
            self.port,
            max_size=50 * 1024 * 1024,  # 50MB max message
            compression=None,  # We handle compression ourselves
        ):
            await asyncio.Future()  # Run forever


def main():
    parser = argparse.ArgumentParser(description="3D Reconstruction Server")
    parser.add_argument("--host", default="0.0.0.0", help="Host address")
    parser.add_argument("--port", type=int, default=8765, help="Port number")
    parser.add_argument("--gpu", type=int, default=0, help="GPU device ID")
    parser.add_argument(
        "--voxel-size", type=float, default=0.01, help="Voxel size in meters"
    )
    parser.add_argument(
        "--sdf-trunc", type=float, default=0.04, help="SDF truncation distance"
    )
    args = parser.parse_args()

    server = ReconstructionServer(
        host=args.host,
        port=args.port,
        voxel_size=args.voxel_size,
        sdf_trunc=args.sdf_trunc,
        gpu_id=args.gpu,
    )

    asyncio.run(server.start())


if __name__ == "__main__":
    main()
