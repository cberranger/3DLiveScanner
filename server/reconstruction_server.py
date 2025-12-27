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
            # Using efficient SLAM types: tsdf=float32, weight=uint16, color=uint16
            # This is the recommended configuration for real-time SLAM
            # block_count=10000 should be ~1-2GB VRAM for typical indoor scenes
            volume = o3d.t.geometry.VoxelBlockGrid(
                attr_names=('tsdf', 'weight', 'color'),
                attr_dtypes=(o3d.core.float32, o3d.core.uint16, o3d.core.uint16),
                attr_channels=((1,), (1,), (3,)),
                voxel_size=self.voxel_size,
                block_resolution=16,
                block_count=10000,  # Reduced - will grow as needed
                device=self.device
            )
            logger.info(f"Created GPU VoxelBlockGrid on {self.device}, voxel_size={self.voxel_size}, blocks=10000")
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
            elif msg_type == "checkpoint":
                await self._handle_checkpoint(websocket, session, data)
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

        # Ensure depth_bytes is bytes, not list
        if isinstance(depth_bytes, list):
            depth_bytes = bytes(depth_bytes)
        elif not isinstance(depth_bytes, (bytes, bytearray)):
            depth_bytes = bytes(depth_bytes)

        # Calculate actual dimensions from data size
        num_pixels = len(depth_bytes) // 2  # uint16 = 2 bytes
        
        # Debug: log first time we see depth
        if session.frame_count == 0:
            logger.info(f"[{session.client_id}] Depth buffer: {len(depth_bytes)} bytes = {num_pixels} pixels")
        
        # Try to infer dimensions - common ToF resolutions
        possible_dims = [
            (480, 480),              # 230400 pixels - S20 Ultra square crop?
            (640, 360), (360, 640),  # 230400 pixels - 16:9 crop
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
            found = False
            for w, h in possible_dims:
                if w * h == num_pixels:
                    width, height = w, h
                    found = True
                    # Update session intrinsics for future frames
                    if session.intrinsics.width != width or session.intrinsics.height != height:
                        logger.info(f"[{session.client_id}] Updated depth dims: {width}x{height}")
                        session.intrinsics.width = width
                        session.intrinsics.height = height
                        # Recalculate intrinsics for new dimensions
                        # Assume ~70 degree FOV typical for ToF
                        session.intrinsics.fx = width * 0.7
                        session.intrinsics.fy = height * 0.7
                        session.intrinsics.cx = width / 2.0
                        session.intrinsics.cy = height / 2.0
                        logger.info(f"[{session.client_id}] Updated intrinsics: fx={session.intrinsics.fx:.1f}, fy={session.intrinsics.fy:.1f}")
                    break
            
            if not found:
                # Try to find any reasonable rectangle
                import math
                sqrt_pixels = int(math.sqrt(num_pixels))
                for w in range(sqrt_pixels, sqrt_pixels + 200):
                    if num_pixels % w == 0:
                        h = num_pixels // w
                        if 100 < h < 1000 and 100 < w < 1000:
                            width, height = w, h
                            logger.info(f"[{session.client_id}] Inferred depth dims: {width}x{height}")
                            session.intrinsics.width = width
                            session.intrinsics.height = height
                            session.intrinsics.fx = width * 0.7
                            session.intrinsics.fy = height * 0.7
                            session.intrinsics.cx = width / 2.0
                            session.intrinsics.cy = height / 2.0
                            found = True
                            break
                
                if not found:
                    logger.warning(f"[{session.client_id}] Unknown depth size: {num_pixels} pixels ({len(depth_bytes)} bytes)")
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
        import gc
        
        height, width = depth_np.shape
        
        try:
            # Depth stays as uint16 (mm) - Open3D handles the scale
            # Create tensor depth image on GPU
            depth_tensor = o3d.t.geometry.Image(
                o3d.core.Tensor(depth_np.astype(np.uint16), device=self.device)
            )
            
            # Create dummy color image (gray) - uint8 format as expected by SLAM mode
            color_np = np.full((height, width, 3), 128, dtype=np.uint8)
            color_tensor = o3d.t.geometry.Image(
                o3d.core.Tensor(color_np, device=self.device)
            )
            
            # Intrinsic matrix (3x3) - on CPU for the API
            intrinsic_cpu = o3d.core.Tensor(
                [[session.intrinsics.fx, 0, session.intrinsics.cx],
                 [0, session.intrinsics.fy, session.intrinsics.cy],
                 [0, 0, 1]], 
                dtype=o3d.core.Dtype.Float64, 
                device=o3d.core.Device("CPU:0")
            )
            
            # Extrinsic (4x4) - on CPU
            extrinsic_cpu = o3d.core.Tensor(extrinsic, dtype=o3d.core.Dtype.Float64, device=o3d.core.Device("CPU:0"))
            
            # Get frustum block coordinates - required for VoxelBlockGrid
            frustum_block_coords = session.volume.compute_unique_block_coordinates(
                depth_tensor, intrinsic_cpu, extrinsic_cpu, 
                depth_scale=1000.0, depth_max=4.0
            )
            
            # Debug: log block coords on first few frames
            if session.frame_count < 3:
                num_blocks = frustum_block_coords.shape[0]
                # Check depth stats
                depth_valid = depth_np[depth_np > 0]
                if len(depth_valid) > 0:
                    logger.info(f"[{session.client_id}] Frame {session.frame_count}: {num_blocks} blocks, "
                               f"depth range: {depth_valid.min()}-{depth_valid.max()}mm, "
                               f"valid pixels: {len(depth_valid)}/{depth_np.size}")
            
            # Integrate with block coords, depth, and color
            session.volume.integrate(
                frustum_block_coords,
                depth_tensor,
                color_tensor,
                intrinsic_cpu,
                extrinsic_cpu,
                depth_scale=1000.0,  # mm to m
                depth_max=4.0
            )
        finally:
            # Explicit cleanup to prevent memory leak
            del depth_tensor, color_tensor, intrinsic_cpu, extrinsic_cpu, frustum_block_coords
            gc.collect()
            # Force CUDA memory cleanup
            if hasattr(o3d.core.cuda, 'release_cache'):
                o3d.core.cuda.release_cache()

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
                    # Extract tensor mesh - try GPU first, fall back to CPU if memory issues
                    try:
                        t_mesh = session.volume.extract_triangle_mesh()
                    except RuntimeError as e:
                        if "Unable to allocate" in str(e):
                            # GPU memory issue - extract on CPU
                            logger.warning("GPU mesh extraction failed, falling back to CPU")
                            cpu_volume = session.volume.cpu()
                            t_mesh = cpu_volume.extract_triangle_mesh()
                        else:
                            raise
                    
                    # Check if we have vertices
                    if not hasattr(t_mesh.vertex, 'positions') or t_mesh.vertex.positions.shape[0] == 0:
                        return None
                    
                    # Get vertices and triangles as numpy arrays
                    vertices = t_mesh.vertex.positions.cpu().numpy().astype(np.float32)
                    triangles = t_mesh.triangle.indices.cpu().numpy().astype(np.int32)
                    
                    # Compute normals on the tensor mesh, then extract
                    t_mesh.compute_vertex_normals()
                    normals = t_mesh.vertex.normals.cpu().numpy().astype(np.float32)
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
            import traceback
            traceback.print_exc()
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

    async def _handle_checkpoint(self, websocket, session: ClientSession, data: dict):
        """
        Checkpoint: Extract mesh, optimize, optionally rebuild at new resolution.
        
        This consolidates the current scan state:
        1. Extract mesh from TSDF
        2. Apply smoothing and decimation
        3. Optionally rebuild TSDF at new resolution (for resolution changes)
        4. Return optimized mesh stats
        """
        new_resolution = data.get("resolution")  # Optional: new voxel size in meters
        target_reduction = data.get("decimation", 0.5)  # Target triangle reduction ratio
        smooth_iterations = data.get("smooth_iterations", 2)
        
        logger.info(f"[{session.client_id}] CHECKPOINT: decimation={target_reduction}, smooth={smooth_iterations}")
        if new_resolution:
            logger.info(f"[{session.client_id}] Resolution change: {self.voxel_size}m -> {new_resolution}m")
        
        try:
            # Step 1: Extract current mesh
            with session.lock:
                if self.use_gpu:
                    # Try GPU first, fall back to CPU if memory issues
                    try:
                        t_mesh = session.volume.extract_triangle_mesh()
                    except RuntimeError as e:
                        if "Unable to allocate" in str(e):
                            logger.warning(f"[{session.client_id}] GPU mesh extraction failed, using CPU")
                            cpu_volume = session.volume.cpu()
                            t_mesh = cpu_volume.extract_triangle_mesh()
                        else:
                            raise
                    
                    # Check if we have geometry
                    if not hasattr(t_mesh.vertex, 'positions') or t_mesh.vertex.positions.shape[0] == 0:
                        await websocket.send(msgpack.packb({
                            "type": "checkpoint_ack",
                            "success": False,
                            "error": "No geometry to checkpoint"
                        }))
                        return
                    
                    # Convert to legacy mesh for processing (without colors)
                    mesh = o3d.geometry.TriangleMesh()
                    mesh.vertices = o3d.utility.Vector3dVector(t_mesh.vertex.positions.cpu().numpy())
                    mesh.triangles = o3d.utility.Vector3iVector(t_mesh.triangle.indices.cpu().numpy())
                else:
                    mesh = session.volume.extract_triangle_mesh()
            
            if len(mesh.vertices) == 0:
                await websocket.send(msgpack.packb({
                    "type": "checkpoint_ack",
                    "success": False,
                    "error": "No geometry to checkpoint"
                }))
                return
            
            original_vertices = len(mesh.vertices)
            original_triangles = len(mesh.triangles)
            
            # Step 2: Smooth the mesh (reduces noise from tracking jitter)
            if smooth_iterations > 0:
                mesh = mesh.filter_smooth_laplacian(number_of_iterations=smooth_iterations)
            
            # Step 3: Decimate (reduce triangle count)
            if target_reduction < 1.0:
                target_triangles = int(len(mesh.triangles) * target_reduction)
                if target_triangles > 100:  # Don't over-decimate
                    mesh = mesh.simplify_quadric_decimation(target_number_of_triangles=target_triangles)
            
            # Step 4: Clean up
            mesh.remove_degenerate_triangles()
            mesh.remove_duplicated_triangles()
            mesh.remove_duplicated_vertices()
            mesh.remove_unreferenced_vertices()
            mesh.compute_vertex_normals()
            
            final_vertices = len(mesh.vertices)
            final_triangles = len(mesh.triangles)
            
            logger.info(f"[{session.client_id}] Optimized: {original_triangles} -> {final_triangles} triangles "
                       f"({100*final_triangles/original_triangles:.1f}%)")
            
            # Step 5: Handle resolution change (rebuild TSDF from optimized mesh)
            if new_resolution and new_resolution != self.voxel_size:
                # TODO: Future - rebuild TSDF at new resolution
                # This would involve:
                # 1. Create new volume at new_resolution
                # 2. Voxelize the optimized mesh into the new volume
                # 3. Replace session.volume
                # For now, just update the voxel_size for new frames
                # session.custom_voxel_size = new_resolution
                logger.info(f"[{session.client_id}] Resolution change requested but not yet implemented")
                pass
            
            # Store the optimized mesh for potential later use
            session.checkpoint_mesh = mesh
            session.checkpoint_count = session.checkpoint_count + 1 if hasattr(session, 'checkpoint_count') else 1
            
            # Return mesh data with the response
            vertices = np.asarray(mesh.vertices, dtype=np.float32)
            triangles = np.asarray(mesh.triangles, dtype=np.int32)
            normals = np.asarray(mesh.vertex_normals, dtype=np.float32)
            
            response = {
                "type": "checkpoint_ack",
                "success": True,
                "checkpoint_id": session.checkpoint_count if hasattr(session, 'checkpoint_count') else 1,
                "original_vertices": original_vertices,
                "original_triangles": original_triangles,
                "final_vertices": final_vertices,
                "final_triangles": final_triangles,
                "reduction_ratio": final_triangles / original_triangles if original_triangles > 0 else 1.0,
                # Include mesh data so client can update display
                "vertices": vertices.tobytes(),
                "triangles": triangles.tobytes(),
                "normals": normals.tobytes(),
                "vertex_count": final_vertices,
                "triangle_count": final_triangles
            }
            
            compressed = lz4.frame.compress(msgpack.packb(response))
            await websocket.send(compressed)
            
        except Exception as e:
            logger.error(f"[{session.client_id}] Checkpoint failed: {e}")
            import traceback
            traceback.print_exc()
            await websocket.send(msgpack.packb({
                "type": "checkpoint_ack",
                "success": False,
                "error": str(e)
            }))

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
