# Server Integration Status & Fix Plan

**Date:** 2025-12-27
**Status:** ❌ Server Integration Code Missing from Main.java

---

## Root Cause Analysis

Based on investigation of the codebase, git history, and CODEMAP files:

### Current State

**Files Present:**
- ✅ `scanner/app/src/main/java/.../ServerStreamClient.java` (12.4KB, created Dec 25)
- ✅ `SERVER_OFFLOAD_README.md` (documents how server should work)
- ✅ `scanner/app/src/main/java/.../Main.java` (941 lines, current working copy)

**Missing from Main.java:**
- ❌ No `import ServerStreamClient`
- ❌ No `ServerStreamClient mServerClient` member variable
- ❌ No `mServerEnabled` boolean flag
- ❌ No `mServerInitialized` boolean flag
- ❌ No `mServerStatus` TextView (status bar)
- ❌ No `initServerStreaming()` method
- ❌ No server connection listener
- ❌ No server toggle in preferences handling
- ❌ No depth frame streaming to server
- ❌ No pause/freeze button logic

### Git History Analysis

**Findings:**
- `ServerStreamClient.java` was **never committed** to any branch
- `Main.java` has **no history** of server integration
- Only 1 commit mentions "server": `a10309f` which added documentation
- The "doc-and-server-plan" branch also doesn't have server integration
- No stashed or backup files found with server code

**Conclusion:** The server integration was **planned and documented** but **never actually implemented** in Main.java.

---

## What's Broken

Based on your report, the following features are not working:

### 1. Server Connection (No connection at all)
**Expected:**
- WebSocket connection to server when "Server Offload" is enabled
- Status bar at top of screen showing connection state (connected/disconnected)
- Ability to tap status bar to reconnect

**Actual:**
- No WebSocket initialization
- No status bar UI element
- No connection attempt

**Missing Code:**
```java
// Missing: Member variable
private ServerStreamClient mServerClient;
private boolean mServerEnabled = false;
private boolean mServerInitialized = false;
private TextView mServerStatus;

// Missing: Initialization in onCreate()
if (mServerEnabled && !texturize) {
    String serverUrl = pref.getString(getString(R.string.pref_server_url), 
        "ws://192.168.1.10:8765");
    initServerStreaming(serverUrl);
}

// Missing: Server status tap handler
mServerStatus = findViewById(R.id.server_status);
mServerStatus.setOnClickListener(v -> {
    if (mServerEnabled && (mServerClient == null || !mServerClient.isConnected())) {
        reconnectServer();
    }
});
```

### 2. Pause/Freeze Button Not Working
**Expected:**
- Button to pause/resume scanning
- Freeze geometry when paused
- Unfreeze when resumed
- Button text updates to show state

**Actual:**
- Button doesn't respond
- No pause/resume functionality

**Missing Code:**
```java
// Missing: Button setup
Button freezeButton = findViewById(R.id.freeze_button);
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
```

---

## Required Integration

To restore server functionality, the following code must be added to Main.java:

### 1. Add Imports
```java
import com.lvonasek.arcore3dscanner.main.ServerStreamClient;
```

### 2. Add Member Variables (after line ~90)
```java
private ServerStreamClient mServerClient;
private boolean mServerEnabled = false;
private boolean mServerInitialized = false;
private TextView mServerStatus;
```

### 3. Add Methods to Main.java

#### initServerStreaming()
```java
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
                // Handle mesh from server
                Log.i(TAG, "Mesh received: " + vertexCount + " vertices");
            }

            @Override
            public void onFrameAck(int frameId) {
                // Server confirmed frame receipt
                Log.d(TAG, "Frame ack: " + frameId);
            }
        });
        mServerClient.connect();
    }
}
```

#### reconnectServer()
```java
private void reconnectServer() {
    if (mServerClient != null) {
        mServerClient.disconnect();
    }
    
    String serverUrl = pref.getString(getString(R.string.pref_server_url), 
        "ws://192.168.1.10:8765");
    initServerStreaming(serverUrl);
}
```

#### sendDepthFrameToServer()
```java
private void sendDepthFrameToServer() {
    if (!mServerEnabled || !mServerInitialized || mServerClient == null) {
        return;
    }
    
    // Get depth data from native layer
    short[] depthData = JNI.getDepthData();  // Would need to implement this in JNI
    float[] pose = JNI.getCameraPose();      // Would need to implement this in JNI
    double timestamp = System.currentTimeMillis() / 1000.0;
    
    mServerClient.sendFrame(depthData, pose, timestamp);
}
```

### 4. Modify onCreate() (after line ~200)
```java
// Server streaming
mServerEnabled = pref.getBoolean(getString(R.string.pref_server_enabled), false);
if (mServerEnabled && !texturize) {
    String serverUrl = pref.getString(getString(R.string.pref_server_url), "ws://192.168.1.10:8765");
    Log.i(TAG, "Server URL from prefs: '" + serverUrl + "'");
    initServerStreaming(serverUrl);
}
```

### 5. Add Server Status UI (in onCreate after mEditorButton setup)
```java
// Server status indicator - tap to reconnect
mServerStatus = findViewById(R.id.server_status);
if (mServerStatus != null) {
    mServerStatus.setOnClickListener(v -> {
        if (mServerEnabled && (mServerClient == null || !mServerClient.isConnected())) {
            reconnectServer();
        }
    });
}
```

### 6. Add Pause/Freeze Button (in onCreate, near other button setups)
```java
// Freeze/Unfreeze button for pause/resume scanning
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
```

### 7. Integrate Depth Streaming (in onDrawFrame or similar)
Call `sendDepthFrameToServer()` from appropriate callback when scanning is active and server is enabled.

### 8. Cleanup in onDestroy() or onPause()
```java
if (mServerClient != null) {
    mServerClient.disconnect();
    mServerClient = null;
}
```

---

## Required JNI Native Methods

To support server streaming, add these to JNI.java/app.cc:

```java
// Get current depth frame (uint16, millimeters)
public static native short[] getDepthData();

// Get current camera pose (4x4 matrix, column-major)
public static native float[] getCameraPose();

// Freeze current scan geometry
public static native void freezeScan();

// Unfreeze scan geometry
public static native void unfreezeScan();

// Get frozen chunk count
public static native int getFrozenChunkCount();
```

---

## Required Resources

### scanner/app/src/main/res/values/strings.xml
```xml
<string name="pref_server_enabled">Server Offload Mode</string>
<string name="pref_server_url">Server URL</string>
<string name="pref_server_url_default">ws://192.168.1.10:8765</string>
<string name="freeze_scan">Freeze Scan</string>
<string name="frozen_chunks">Frozen (%d chunks)</string>
<string name="server_connected">Server: Connected</string>
<string name="server_disconnected">Server: Disconnected</string>
```

### scanner/app/src/main/res/xml/settings.xml
```xml
<SwitchPreference
    android:key="pref_server_enabled"
    android:title="@string/pref_server_enabled"
    android:defaultValue="false" />

<EditTextPreference
    android:key="pref_server_url"
    android:title="@string/pref_server_url"
    android:defaultValue="@string/pref_server_url_default"
    android:inputType="textUri" />
```

### scanner/app/src/main/res/layout/activity_main.xml
```xml
<TextView
    android:id="@+id/server_status"
    android:layout_width="wrap_content"
    android:layout_height="wrap_content"
    android:text="Server: Off"
    android:textColor="#FF0000"
    android:textSize="14sp"
    android:padding="8dp" />

<Button
    android:id="@+id/freeze_button"
    android:layout_width="wrap_content"
    android:layout_height="wrap_content"
    android:text="@string/freeze_scan"
    android:visibility="visible" />
```

---

## Implementation Options

### Option A: Restore from Backup (RECOMMENDED)
If you have a backup of Main.java with server integration:
1. Restore that file
2. Verify it compiles
3. Test all features

### Option B: Manual Integration
If no backup exists:
1. Follow the "Required Integration" steps above
2. Add all missing code to Main.java
3. Add required native methods
4. Add required resources
5. Test thoroughly

### Option C: I Can Implement (If You Approve)
I can:
1. Read the current Main.java completely
2. Add all server integration code at correct locations
3. Ensure proper integration with existing code
4. Add missing JNI native methods
5. Add required resource files
6. Verify compiles

---

## Questions for You

1. **Do you have a backup** of Main.java with server integration code?
   - If yes, where is it?
   - I can help restore it.

2. **Should I implement** the full server integration as outlined above?
   - This requires ~200-300 lines of code
   - Modifies Main.java, JNI files, and resources
   - Takes ~30-60 minutes

3. **Did the server feature ever work** on your device?
   - If yes, what was the last time it worked?
   - This helps identify what changed.

4. **Are there other branches** or repositories where server integration exists?
   - Maybe on a different machine or repository?

---

## Next Steps

### Immediate Actions Needed
1. ✅ Choose integration approach (restore backup OR implement fresh)
2. ✅ Add server integration code to Main.java
3. ✅ Add required JNI native methods
4. ✅ Add required resources (strings, layout, settings)
5. ✅ Test server connection
6. ✅ Test pause/freeze button
7. ✅ Verify all functionality works

### Verification Checklist
- [ ] ServerStreamClient properly instantiated
- [ ] Server connection established when enabled
- [ ] Status bar shows connection state
- [ ] Tap-to-reconnect works
- [ ] Depth frames sent to server
- [ ] Server acknowledges frames
- [ ] Pause/freeze button works
- [ ] All UI elements visible
- [ ] No compilation errors
- [ ] App runs without crashes

---

## Summary

**Root Cause:** Server integration code exists as documentation and separate file, but was **never integrated into Main.java**

**Fix Required:** Add ~200-300 lines of server integration code across multiple files

**Estimated Time:** 30-60 minutes for complete implementation

**Priority:** High - affects core scanning functionality

---

*Generated to diagnose broken server communication*
*Status: Awaiting user decision on integration approach*
