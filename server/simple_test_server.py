#!/usr/bin/env python3
"""
Simple WebSocket test server - minimal dependencies
"""

import asyncio
import websockets
import socket

HOST = "0.0.0.0"
PORT = 8765

async def handler(websocket):
    client = f"{websocket.remote_address[0]}:{websocket.remote_address[1]}"
    print(f"[CONNECTED] {client}")
    
    try:
        async for message in websocket:
            print(f"[MESSAGE] from {client}: {len(message)} bytes")
            await websocket.send(b"OK")
    except websockets.exceptions.ConnectionClosed as e:
        print(f"[CLOSED] {client}: {e}")
    except Exception as e:
        print(f"[ERROR] {client}: {e}")

async def main():
    # Get local IP
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
    except:
        local_ip = "unknown"
    finally:
        s.close()
    
    print(f"")
    print(f"=" * 50)
    print(f"  SIMPLE WEBSOCKET TEST SERVER")
    print(f"=" * 50)
    print(f"  Listening on: {HOST}:{PORT}")
    print(f"  Local IP:     {local_ip}")
    print(f"  Connect URL:  ws://{local_ip}:{PORT}")
    print(f"=" * 50)
    print(f"  Waiting for connections...")
    print(f"")
    
    async with websockets.serve(handler, HOST, PORT):
        await asyncio.Future()

if __name__ == "__main__":
    asyncio.run(main())
