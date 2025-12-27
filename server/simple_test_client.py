#!/usr/bin/env python3
"""
Simple WebSocket test client
Usage: python simple_test_client.py ws://192.168.1.10:8765
"""

import asyncio
import websockets
import sys

async def test(url):
    print(f"Connecting to {url}...")
    try:
        async with websockets.connect(url, open_timeout=5) as ws:
            print(f"Connected!")
            await ws.send(b"Hello from test client")
            response = await ws.recv()
            print(f"Response: {response}")
            print("SUCCESS!")
    except asyncio.TimeoutError:
        print(f"TIMEOUT - could not connect within 5 seconds")
    except ConnectionRefusedError:
        print(f"CONNECTION REFUSED - server not running or port blocked")
    except Exception as e:
        print(f"ERROR: {type(e).__name__}: {e}")

if __name__ == "__main__":
    url = sys.argv[1] if len(sys.argv) > 1 else "ws://localhost:8765"
    asyncio.run(test(url))
