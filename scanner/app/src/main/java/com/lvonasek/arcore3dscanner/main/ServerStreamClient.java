package com.lvonasek.arcore3dscanner.main;

import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;

import org.java_websocket.client.WebSocketClient;
import org.java_websocket.handshake.ServerHandshake;
import org.msgpack.core.MessagePack;
import org.msgpack.core.MessageBufferPacker;
import org.msgpack.core.MessageUnpacker;

import java.net.URI;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import net.jpountz.lz4.LZ4Compressor;
import net.jpountz.lz4.LZ4Factory;

/**
 * Client for streaming depth frames to reconstruction server.
 * 
 * Usage:
 *   ServerStreamClient client = new ServerStreamClient("ws://192.168.1.100:8765");
 *   client.connect();
 *   client.initialize(width, height, fx, fy, cx, cy);
 *   client.sendFrame(depthData, pose, timestamp);
 *   ...
 *   client.disconnect();
 */
public class ServerStreamClient {
    private static final String TAG = "ServerStreamClient";
    
    public interface Listener {
        void onConnected();
        void onDisconnected();
        void onError(String message);
        void onMeshReceived(byte[] vertices, byte[] triangles, byte[] normals, 
                           int vertexCount, int triangleCount);
        void onFrameAck(int frameId);
    }
    
    private final String serverUrl;
    private WebSocketClient webSocket;
    private Listener listener;
    private HandlerThread sendThread;
    private Handler sendHandler;
    
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private final AtomicBoolean initialized = new AtomicBoolean(false);
    private final AtomicInteger pendingFrames = new AtomicInteger(0);
    
    private final LZ4Compressor compressor;
    private static final int MAX_PENDING_FRAMES = 5;
    
    public ServerStreamClient(String serverUrl) {
        this.serverUrl = serverUrl;
        this.compressor = LZ4Factory.fastestInstance().fastCompressor();
    }
    
    public void setListener(Listener listener) {
        this.listener = listener;
    }
    
    public void connect() {
        if (connected.get()) return;
        
        Log.i(TAG, "Attempting to connect to: " + serverUrl);
        
        // Start send thread
        sendThread = new HandlerThread("ServerStreamSender");
        sendThread.start();
        sendHandler = new Handler(sendThread.getLooper());
        
        try {
            URI uri = new URI(serverUrl);
            Log.i(TAG, "URI parsed: host=" + uri.getHost() + " port=" + uri.getPort());
            
            webSocket = new WebSocketClient(uri) {
                @Override
                public void onOpen(ServerHandshake handshake) {
                    Log.i(TAG, "WebSocket onOpen called - connected!");
                    connected.set(true);
                    if (listener != null) listener.onConnected();
                }
                
                @Override
                public void onMessage(String message) {
                    Log.d(TAG, "Received string message: " + message);
                }
                
                @Override
                public void onMessage(ByteBuffer bytes) {
                    Log.d(TAG, "Received binary message: " + bytes.remaining() + " bytes");
                    handleMessage(bytes);
                }
                
                @Override
                public void onClose(int code, String reason, boolean remote) {
                    Log.i(TAG, "WebSocket onClose: code=" + code + " reason=" + reason + " remote=" + remote);
                    connected.set(false);
                    initialized.set(false);
                    if (listener != null) listener.onDisconnected();
                }
                
                @Override
                public void onError(Exception ex) {
                    Log.e(TAG, "WebSocket onError: " + ex.getClass().getName() + ": " + ex.getMessage(), ex);
                    if (listener != null) listener.onError(ex.getMessage());
                }
            };
            
            // Set connection timeout
            webSocket.setConnectionLostTimeout(10);
            
            Log.i(TAG, "Calling webSocket.connect()...");
            webSocket.connect();
            Log.i(TAG, "webSocket.connect() returned (async)");
            
        } catch (Exception e) {
            Log.e(TAG, "Connect failed with exception: " + e.getClass().getName() + ": " + e.getMessage(), e);
            if (listener != null) listener.onError(e.getMessage());
        }
    }
    
    public void disconnect() {
        if (webSocket != null) {
            webSocket.close();
        }
        if (sendThread != null) {
            sendThread.quitSafely();
        }
        connected.set(false);
        initialized.set(false);
    }
    
    public boolean isConnected() {
        return connected.get();
    }
    
    public boolean isInitialized() {
        return initialized.get();
    }
    
    /**
     * Initialize session with camera intrinsics
     */
    public void initialize(int width, int height, float fx, float fy, float cx, float cy) {
        initialize(width, height, fx, fy, cx, cy, 10);
    }
    
    public void initialize(int width, int height, float fx, float fy, float cx, float cy,
                          int meshInterval) {
        if (!connected.get()) {
            Log.w(TAG, "Not connected");
            return;
        }
        
        sendHandler.post(() -> {
            try {
                MessageBufferPacker packer = MessagePack.newDefaultBufferPacker();
                packer.packMapHeader(7);
                packer.packString("type"); packer.packString("init");
                packer.packString("width"); packer.packInt(width);
                packer.packString("height"); packer.packInt(height);
                packer.packString("fx"); packer.packFloat(fx);
                packer.packString("fy"); packer.packFloat(fy);
                packer.packString("cx"); packer.packFloat(cx);
                packer.packString("cy"); packer.packFloat(cy);
                packer.packString("mesh_interval"); packer.packInt(meshInterval);
                
                webSocket.send(packer.toByteArray());
                packer.close();
            } catch (Exception e) {
                Log.e(TAG, "Initialize failed", e);
            }
        });
    }
    
    /**
     * Send a depth frame to the server
     * 
     * @param depthData Depth in millimeters (uint16)
     * @param pose 4x4 camera-to-world matrix (column-major)
     * @param timestamp Frame timestamp
     */
    public void sendFrame(short[] depthData, float[] pose, double timestamp) {
        sendFrame(depthData, null, pose, timestamp);
    }
    
    /**
     * Send depth frame with optional color
     */
    public void sendFrame(short[] depthData, byte[] colorData, float[] pose, double timestamp) {
        if (!connected.get() || !initialized.get()) {
            return;
        }
        
        // Backpressure - skip if too many pending
        if (pendingFrames.get() >= MAX_PENDING_FRAMES) {
            Log.w(TAG, "Dropping frame - server behind");
            return;
        }
        
        pendingFrames.incrementAndGet();
        
        sendHandler.post(() -> {
            try {
                // Convert depth to bytes (handle null)
                byte[] depthBytes;
                if (depthData != null && depthData.length > 0) {
                    ByteBuffer depthBuffer = ByteBuffer.allocate(depthData.length * 2);
                    depthBuffer.asShortBuffer().put(depthData);
                    depthBytes = depthBuffer.array();
                } else {
                    depthBytes = new byte[0];  // Empty depth
                }
                
                // Pack message
                MessageBufferPacker packer = MessagePack.newDefaultBufferPacker();
                int fields = (colorData != null) ? 5 : 4;
                packer.packMapHeader(fields);
                packer.packString("type"); packer.packString("frame");
                packer.packString("timestamp"); packer.packDouble(timestamp);
                packer.packString("depth"); packer.packBinaryHeader(depthBytes.length);
                if (depthBytes.length > 0) {
                    packer.writePayload(depthBytes);
                }
                
                // Pose as array
                packer.packString("pose"); 
                packer.packArrayHeader(16);
                for (float v : pose) {
                    packer.packDouble(v);
                }
                
                if (colorData != null) {
                    packer.packString("color");
                    packer.packBinaryHeader(colorData.length);
                    packer.writePayload(colorData);
                }
                
                byte[] data = packer.toByteArray();
                packer.close();
                
                // Send uncompressed for now (LZ4 format mismatch with Python)
                webSocket.send(data);
                
            } catch (Exception e) {
                Log.e(TAG, "Send frame failed", e);
                pendingFrames.decrementAndGet();
            }
        });
    }
    
    /**
     * Request full mesh extraction
     */
    public void requestMesh() {
        if (!connected.get()) return;
        
        sendHandler.post(() -> {
            try {
                MessageBufferPacker packer = MessagePack.newDefaultBufferPacker();
                packer.packMapHeader(1);
                packer.packString("type"); packer.packString("get_mesh");
                webSocket.send(packer.toByteArray());
                packer.close();
            } catch (Exception e) {
                Log.e(TAG, "Request mesh failed", e);
            }
        });
    }
    
    /**
     * Reset server reconstruction
     */
    public void reset() {
        if (!connected.get()) return;
        
        sendHandler.post(() -> {
            try {
                MessageBufferPacker packer = MessagePack.newDefaultBufferPacker();
                packer.packMapHeader(1);
                packer.packString("type"); packer.packString("reset");
                webSocket.send(packer.toByteArray());
                packer.close();
            } catch (Exception e) {
                Log.e(TAG, "Reset failed", e);
            }
        });
    }
    
    private void handleMessage(ByteBuffer buffer) {
        try {
            byte[] data = new byte[buffer.remaining()];
            buffer.get(data);
            
            // Check for LZ4 compression (magic bytes)
            if (data.length > 4 && data[0] == 0x04 && data[1] == 0x22) {
                // Decompress - would need LZ4 decompressor here
                // For now, assume uncompressed responses
            }
            
            MessageUnpacker unpacker = MessagePack.newDefaultUnpacker(data);
            int mapSize = unpacker.unpackMapHeader();
            
            String type = null;
            for (int i = 0; i < mapSize; i++) {
                String key = unpacker.unpackString();
                if (key.equals("type")) {
                    type = unpacker.unpackString();
                } else {
                    unpacker.skipValue();
                }
            }
            
            if (type == null) return;
            
            switch (type) {
                case "init_ack":
                    initialized.set(true);
                    Log.i(TAG, "Server initialized");
                    break;
                    
                case "frame_ack":
                    pendingFrames.decrementAndGet();
                    // Could parse frame_id and mesh data here
                    break;
                    
                case "mesh":
                    // Parse mesh data and call listener
                    // This would need more complex unpacking
                    break;
                    
                case "error":
                    if (listener != null) {
                        listener.onError("Server error");
                    }
                    break;
            }
            
            unpacker.close();
        } catch (Exception e) {
            Log.e(TAG, "Handle message failed", e);
        }
    }
}
