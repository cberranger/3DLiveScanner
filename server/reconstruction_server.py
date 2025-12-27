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
import numpy as np
import open3d as o3d
import msgpack
import lz4.frame
import time
import argparse
import logging
from dataclasses import dataclass, field
from typing import Dict, Optional
from threading import Lock

logging.basicConfig(level=logging.INFO)
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
    
    def to_tensor(self, device):
        """Convert to Open3D tensor intrinsic matrix"""
        intrinsic = o3d.core.Tensor(
            [[self.fx, 0, self.cx],
             [0, self.fy, self.cy],
             [0, 0, 1]], dtype=o3d.core.Dtype.Float64, device=device
        )
        return intrinsic


@dataclass
class ClientSession:
    """Per-client reconstruction session"""
    client_id: str
    volume: any = None
    intrinsics: Optional[CameraIntrinsics] = None
    frame_count: int = 0
    last_mesh_frame: int = 0
    mesh_interval: int = 10
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
        self.use_gpu = False
        self.device = None
        
        self._init_gpu()

    def _init_gpu(self):
        """Initialize GPU if available"""
        try:
            if o3d.core.cuda.is_available():
                self.device = o3d.core.Device(f"CUDA:{self.gpu_id}")
                self.use_gpu = True
                logger.info(f"GPU enabled: CUDA device {self.gpu_id}")
            else:
                self.device = o3d.core.Device("CPU:0")
                self.use_gpu = False
                logger.info("CUDA not available, using CPU")
        except Exception as e:
            self.device = o3d.core.Device("CPU:0")
            self.use_gpu = False
            logger.warning(f"GPU init failed: {e}, using CPU")

    def create_volume(self):
        """Create a TSDF volume for reconstruction"""
        if self.use_gpu:
            # Use tensor-based VoxelBlockGrid for GPU
            volume = o3d.t.geometry.VoxelBlockGrid(
                attr_names=('tsdf', 'weight', 'color'),
                attr_dtypes=(o3d.core.float32, o3d.core.float32, o3d.core.uint16),
                attr_channels=((1,), (1,), (3,)),
                voxel_size=self.voxel_size,
                block_resolution=16,
                block_count=50000,
                device=self.device
            )
            logger.debug(f"Created GPU VoxelBlockGrid on {self.device}")
            return volume
        else:
            # Use legacy ScalableTSDFVolume for CPU
            volume = o3d.pipelines.integration.ScalableTSDFVolume(
                voxel_length=self.voxel_size,
                sdf_trunc=self.sdf_trunc,
                color_type=o3d.pipelines.integration.TSDFVolumeColorType.RGB8
            )
            logger.debug("Created CPU ScalableTSDFVolume")
            return volume

    async def handle_client(self, websocket):
        """Handle a connected client"""
        client_id = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
        logger.info("=" * 50)
        logger.info(f"NEW CONNECTION from {client_id}")
        logger.info("=" * 50)

        session = ClientSession(client_id=client_id, volume=self.create_volume())
        self.sessions[client_id] = session
        logger.info(f"Session created for {client_id}, total active: {len(self.sessions)}")

        try:
            async for message in websocket:
                await self._process_message(websocket, session, message)
        except websockets.exceptions.ConnectionClosed as e:
            logger.info(f"Client {client_id} disconnected: {e.reason if e.reason else 'closed'}")
        except Exception as e:
            logger.error(f"Client {client_id} error: {e}")
        finally:
            logger.info(f"Cleanup session for {client_id}, frames: {session.frame_count}")
            del self.sessions[client_id]
            logger.info(f"Remaining sessions: {len(self.sessions)}")

    async def _process_message(self, websocket, session: ClientSession, message: bytes):
        """Process an incoming message"""
        try:
            # Decompress if LZ4 frame
            if len(message) >= 4 and message[:4] == b'\x04\x22\x4d\x18':
                message = lz4.frame.decompress(message)

            # Decode msgpack
            unpacker = msgpack.Unpacker(raw=False, strict_map_key=False)
            unpacker.feed(message)
            data = next(iter(unpacker))
            msg_type = data.get("type")

            if msg_type == "init":
                await self._handle_init(websocket, session, data)
            elif msg_type == "frame":
                await self._handle_frame(websocket, session, data)
            elif msg_type == "get_mesh":
                await self._handle_get_mesh(websocket, session)
            elif msg_type == "reset":
                await self._handle_reset(websocket, session)
            elif msg_type == "ping":
                await websocket.send(msgpack.packb({"type": "pong", "time": time.time()}))
            else:
                logger.warning(f"Unknown message type: {msg_type}")

        except Exception as e:
            logger.error(f"Error processing message: {e}")
            await websocket.send(msgpack.packb({"type": "error", "message": str(e)}))

    async def _handle_init(self, websocket, session: ClientSession, data: dict):
        """Handle initialization with camera intrinsics"""
        session.intrinsics = CameraIntrinsics(
            width=data["width"],
            height=data["height"],
            fx=data["fx"],
            fy=data["fy"],
            cx=data["cx"],
            cy=data["cy"],
        )
        session.mesh_interval = data.get("mesh_interval", 10)

        logger.info(f"[{session.client_id}] INIT: {session.intrinsics.width}x{session.intrinsics.height}")
        logger.info(f"[{session.client_id}] Intrinsics: fx={session.intrinsics.fx:.1f}, fy={session.intrinsics.fy:.1f}")
        logger.info(f"[{session.client_id}] Mesh interval: {session.mesh_interval} frames, GPU: {self.use_gpu}")

        await websocket.send(msgpack.packb({
            "type": "init_ack",
            "voxel_size": self.voxel_size,
            "sdf_trunc": self.sdf_trunc,
            "gpu": self.use_gpu
        }))

    async def _handle_frame(self, websocket, session: ClientSession, data: dict):
        """Handle incoming depth frame"""
        if session.intrinsics is None:
            await websocket.send(msgpack.packb({"type": "error", "message": "Not initialized"}))
            return

        timestamp = data["timestamp"]
        depth_bytes = data.get("depth")
        pose_data = data["pose"]

        # Log periodically
        if session.frame_count % 30 == 0:
            has_depth = "yes" if depth_bytes and len(depth_bytes) > 0 else "no"
            logger.info(f"[{session.client_id}] Frame {session.frame_count}: depth={has_depth}")

        # No depth data - just ack
        if not depth_bytes or len(depth_bytes) == 0:
            session.frame_count += 1
            await websocket.send(msgpack.packb({
                "type": "frame_ack",
                "frame_id": session.frame_count,
                "timestamp": timestamp
            }))
            return

        # Calculate actual dimensions from data size
        num_pixels = len(depth_bytes) // 2  # uint16 = 2 bytes
        
        # Try to infer dimensions - common ToF resolutions
        possible_dims = [
            (480, 240), (240, 480),  # 115200 pixels - S20 Ultra ToF
            (640, 480), (480, 640),  # 307200 pixels
            (320, 240), (240, 320),  # 76800 pixels - common VGA quarter
            (256, 192), (192, 256),  # 49152 pixels
            (240, 180), (180, 240),  # 43200 pixels - P30 Pro ToF
            (224, 172), (172, 224),  # 38528 pixels
        ]
        
        width, height = session.intrinsics.width, session.intrinsics.height
        if width * height != num_pixels:
            # Find matching dimensions
            for w, h in possible_dims:
                if w * h == num_pixels:
                    width, height = w, h
                    # Update session intrinsics for future frames
                    if session.intrinsics.width != width or session.intrinsics.height != height:
                        logger.info(f"[{session.client_id}] Updated depth dims: {width}x{height}")
                        session.intrinsics.width = width
                        session.intrinsics.height = height
                        # Scale fx/fy proportionally
                        session.intrinsics.cx = width / 2.0
                        session.intrinsics.cy = height / 2.0
                    break
            else:
                logger.warning(f"[{session.client_id}] Unknown depth size: {num_pixels} pixels")
                session.frame_count += 1
                await websocket.send(msgpack.packb({
                    "type": "frame_ack", 
                    "frame_id": session.frame_count,
                    "timestamp": timestamp
                }))
                return

        # Convert depth (uint16 mm -> appropriate format)
        depth_np = np.frombuffer(depth_bytes, dtype=np.uint16).reshape(height, width)
        
        # Convert pose
        pose = np.array(pose_data, dtype=np.float64).reshape(4, 4)
        extrinsic = np.linalg.inv(pose)  # world-to-camera

        # Integrate based on backend
        with session.lock:
            if self.use_gpu:
                self._integrate_gpu(session, depth_np, extrinsic)
            else:
                self._integrate_cpu(session, depth_np, extrinsic)
            session.frame_count += 1

        # Response
        response = {
            "type": "frame_ack",
            "frame_id": session.frame_count,
            "timestamp": timestamp
        }

        # Periodic mesh extraction
        if session.frame_count - session.last_mesh_frame >= session.mesh_interval:
            mesh_data = self._extract_mesh(session)
            if mesh_data:
                response["has_mesh"] = True
                response["vertex_count"] = mesh_data["vertex_count"]
                response["triangle_count"] = mesh_data["triangle_count"]
                session.last_mesh_frame = session.frame_count

        await websocket.send(msgpack.packb(response))

    def _integrate_gpu(self, session: ClientSession, depth_np: np.ndarray, extrinsic: np.ndarray):
        """GPU-accelerated TSDF integration using tensor API"""
        # Convert to tensor depth image (keep as uint16, scale is 1000 for mm->m)
        depth_tensor = o3d.t.geometry.Image(
            o3d.core.Tensor(depth_np, device=self.device)
        )
        
        # Intrinsic matrix
        intrinsic = session.intrinsics.to_tensor(self.device)
        
        # Extrinsic
        extrinsic_tensor = o3d.core.Tensor(extrinsic, dtype=o3d.core.Dtype.Float64, device=self.device)
        
        # Integrate
        session.volume.integrate(
            depth_tensor,
            intrinsic,
            extrinsic_tensor,
            depth_scale=1000.0,  # mm to m
            depth_max=4.0
        )

    def _integrate_cpu(self, session: ClientSession, depth_np: np.ndarray, extrinsic: np.ndarray):
        """CPU TSDF integration using legacy API"""
        # Convert to float meters
        depth_float = depth_np.astype(np.float32) / 1000.0
        depth_img = o3d.geometry.Image(depth_float)
        
        # Dummy color
        color_np = np.ones((session.intrinsics.height, session.intrinsics.width, 3), dtype=np.uint8) * 128
        color_img = o3d.geometry.Image(color_np)
        
        # Create RGBD
        rgbd = o3d.geometry.RGBDImage.create_from_color_and_depth(
            color_img, depth_img,
            depth_scale=1.0,
            depth_trunc=4.0,
            convert_rgb_to_intensity=False
        )
        
        # Integrate
        intrinsic = session.intrinsics.to_open3d()
        session.volume.integrate(rgbd, intrinsic, extrinsic)

    def _extract_mesh(self, session: ClientSession) -> Optional[dict]:
        """Extract mesh from TSDF volume"""
        try:
            with session.lock:
                if self.use_gpu:
                    mesh = session.volume.extract_triangle_mesh()
                    # Convert tensor mesh to legacy for processing
                    mesh = mesh.to_legacy()
                else:
                    mesh = session.volume.extract_triangle_mesh()

            if len(mesh.vertices) == 0:
                return None

            mesh.compute_vertex_normals()

            vertices = np.asarray(mesh.vertices, dtype=np.float32)
            triangles = np.asarray(mesh.triangles, dtype=np.int32)
            normals = np.asarray(mesh.vertex_normals, dtype=np.float32)

            return {
                "vertices": vertices.tobytes(),
                "triangles": triangles.tobytes(),
                "normals": normals.tobytes(),
                "vertex_count": len(vertices),
                "triangle_count": len(triangles)
            }

        except Exception as e:
            logger.error(f"Mesh extraction failed: {e}")
            return None

    async def _handle_get_mesh(self, websocket, session: ClientSession):
        """Handle full mesh extraction request"""
        mesh_data = self._extract_mesh(session)
        if mesh_data:
            response = msgpack.packb({"type": "mesh", **mesh_data})
            compressed = lz4.frame.compress(response)
            await websocket.send(compressed)
        else:
            await websocket.send(msgpack.packb({"type": "mesh", "error": "No mesh available"}))

    async def _handle_reset(self, websocket, session: ClientSession):
        """Reset reconstruction volume"""
        with session.lock:
            session.volume = self.create_volume()
            session.frame_count = 0
            session.last_mesh_frame = 0
        await websocket.send(msgpack.packb({"type": "reset_ack"}))

    async def start(self):
        """Start the server"""
        logger.info("")
        logger.info("=" * 60)
        logger.info("  3D LIVE SCANNER - RECONSTRUCTION SERVER")
        logger.info("=" * 60)
        logger.info(f"  Host:       {self.host}")
        logger.info(f"  Port:       {self.port}")
        logger.info(f"  Voxel size: {self.voxel_size}m")
        logger.info(f"  SDF trunc:  {self.sdf_trunc}m")
        logger.info(f"  GPU:        {'CUDA:' + str(self.gpu_id) if self.use_gpu else 'CPU'}")
        logger.info("=" * 60)
        logger.info("  Waiting for connections...")
        logger.info("")
        
        async with websockets.serve(
            self.handle_client,
            self.host,
            self.port,
            max_size=50 * 1024 * 1024,
            compression=None
        ):
            await asyncio.Future()


def main():
    parser = argparse.ArgumentParser(description="3D Reconstruction Server")
    parser.add_argument("--host", default="0.0.0.0", help="Host address")
    parser.add_argument("--port", type=int, default=8765, help="Port number")
    parser.add_argument("--gpu", type=int, default=0, help="GPU device ID")
    parser.add_argument("--voxel-size", type=float, default=0.01, help="Voxel size (m)")
    parser.add_argument("--sdf-trunc", type=float, default=0.04, help="SDF truncation")
    args = parser.parse_args()

    server = ReconstructionServer(
        host=args.host,
        port=args.port,
        voxel_size=args.voxel_size,
        sdf_trunc=args.sdf_trunc,
        gpu_id=args.gpu
    )

    asyncio.run(server.start())


if __name__ == "__main__":
    main()
