#!/usr/bin/env python3
"""
Script to add server streaming integration to Main.java
"""

import sys

original_file = sys.argv[1]
new_file = sys.argv[2]

# Read original file
with open(original_file, "r") as f:
    lines = f.readlines()

# Find insertion points
imports_end = None
member_vars_end = None
methods_end = None

server_import = "import com.lvonasek.arcore3dscanner.main.ServerStreamClient;\n"
server_member_vars = """
  private ServerStreamClient mServerClient;
  private boolean mServerEnabled = false;
  private boolean mServerInitialized = false;
  private TextView mServerStatus;
"""
server_methods = """
  private void initServerStreaming(String serverUrl) {
    if (mServerClient == null) {
      mServerClient = new ServerStreamClient(serverUrl);
      mServerClient.setListener(new ServerStreamClient.Listener() {
        @Override
        public void onConnected() {
          mServerInitialized = true;
          runOnUiThread(() -> {
            mServerStatus.setTextColor(Color.GREEN);
            mServerStatus.setText("Server: Connected");
          });
          Log.i(TAG, "Server connected");
        }

        @Override
        public void onDisconnected() {
          mServerInitialized = false;
          runOnUiThread(() -> {
            mServerStatus.setTextColor(Color.RED);
            mServerStatus.setText("Server: Disconnected");
          });
          Log.w(TAG, "Server disconnected");
        }

        @Override
        public void onError(String message) {
          runOnUiThread(() -> {
            mServerStatus.setTextColor(Color.RED);
            mServerStatus.setText("Server: Error");
          });
          Log.e(TAG, "Server error: " + message);
        }

        @Override
        public void onMeshReceived(byte[] vertices, byte[] triangles, 
                                   byte[] normals, int vertexCount, int triangleCount) {
          Log.i(TAG, "Mesh received: " + vertexCount + " vertices");
        }

        @Override
        public void onFrameAck(int frameId) {
          Log.d(TAG, "Frame ack: " + frameId);
        }
      });
      mServerClient.connect();
    }
  }

  private void reconnectServer() {
    if (mServerClient != null) {
      mServerClient.disconnect();
    }
    
    SharedPreferences pref = PreferenceManager.getDefaultSharedPreferences(this);
    String serverUrl = pref.getString(getString(R.string.pref_server_url), 
        "ws://192.168.1.10:7822");
    initServerStreaming(serverUrl);
  }

  private void sendDepthFrameToServer() {
    if (!mServerEnabled || !mServerInitialized || mServerClient == null) {
      return;
    }
    
    if (!m3drRunning) {
      return;
    }
    
    short[] depthData = JNI.getDepthData();
    float[] pose = JNI.getCameraPose();
    double timestamp = System.currentTimeMillis() / 1000.0;
    
    if (depthData != null && depthData.length > 0 && pose != null && pose.length == 16) {
      mServerClient.sendFrame(depthData, null, pose, timestamp);
    }
  }
"""

server_init = """    SharedPreferences pref = PreferenceManager.getDefaultSharedPreferences(this);
    mServerEnabled = pref.getBoolean(getString(R.string.pref_server_enabled), false);
    if (mServerEnabled && !texturize) {
      String serverUrl = pref.getString(getString(R.string.pref_server_url), 
            "ws://192.168.1.10:7822");
      Log.i(TAG, "Server URL from prefs: '" + serverUrl + "'");
      initServerStreaming(serverUrl);
    }
"""

freeze_button = """
    
    mServerStatus = findViewById(R.id.server_status);
    if (mServerStatus != null) {
      mServerStatus.setOnClickListener(v -> {
        if (mServerEnabled && (mServerClient == null || !mServerClient.isConnected())) {
          reconnectServer();
        }
      });
    }
    
    Button freezeButton = findViewById(R.id.freeze_button);
    if (freezeButton != null) {
      freezeButton.setOnClickListener(view -> {
        int frozenCount = JNI.getFrozenChunkCount();
        if (frozenCount > 0) {
          JNI.unfreezeScan();
          freezeButton.setText(R.string.freeze_scan);
        } else {
          JNI.freezeScan();
          int count = JNI.getFrozenChunkCount();
          freezeButton.setText(getString(R.string.frozen_chunks, count));
        }
      });
    }
"""

send_frame = """      sendDepthFrameToServer();
    }"""

disconnect_server = """    
    // Disconnect server
    if (mServerClient != null) {
      mServerClient.disconnect();
    }
"""

# Write new file
with open(new_file, "w") as f:
    i = 0
    while i < len(lines):
        line = lines[i]

        # Add imports after AppExecutors import
        if imports_end is None and line.strip().startswith(
            "import com.lvonasek.arcore3dscanner.utils.AppExecutors;"
        ):
            lines[i] = line + server_import
            imports_end = i
            continue

        # Add member variables after mCameraRecordYaw
        if member_vars_end is None and line.strip().startswith(
            "  float mCameraRecordYaw = 0;"
        ):
            lines[i] = line + "\n" + server_member_vars
            member_vars_end = i + 2
            continue

        # Add server init in onCreate after mToggleButton.setOnClickListener
        if methods_end is None and "JNI.onToggleButtonClicked(m3drRunning);" in line:
            lines[i] = server_init
            methods_end = i
            continue

        # Add server status click listener and freeze button after CheckBox showNormals
        if "showNormals.setOnCheckedChangeListener" in line:
            lines[i] = line + "\n" + freeze_button
            continue

        # Add sendDepthFrameToServer call in onDrawFrame
        if "mCameraControl.updateButtons();" in line:
            lines[i] = line + send_frame
            continue

        # Add disconnect in onPause after mGPS.stop()
        if "mGPS.stop();" in line:
            lines[i] = line + "\n" + disconnect_server
            continue

        i += 1

    f.writelines(lines)

print(f"Server integration added to {new_file}")
print(f"Total lines: {len(lines)}")
