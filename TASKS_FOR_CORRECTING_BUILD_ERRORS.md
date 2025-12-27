# Tasks for Correcting Build Errors

**Date:** 2025-12-27
**Status:** Build Failed - Java Compilation Errors (64 errors total)
**Root Cause:** Automated sed replacements created syntax errors with mismatched braces and malformed code structure

---

## Current Status

### ✅ Completed Successfully
- **AppExecutors.java** - Infrastructure created with 3 thread pools (diskIO, networkIO, computation)
- **CameraControl.java** - 1 thread replaced, removed `.start()`
- **AbstractActivity.java** - Added AppExecutors import + `shutdown()` call in `onPause()`

### ❌ Build Errors - Files Needing Manual Fixes

**Total Errors:** 64 compilation errors
**Main Issues:**
1. Mismatched braces from sed replacements
2. "illegal start of expression" errors
3. Extra closing `.start()` calls remaining
4. Malformed lambda syntax

---

## Manual Fixes Required

### File 1: Editor.java (Line 119, 193)

#### Error 1: Line 119 - "illegal start of expression"
```
Error: private void setColorScreen()
       ^
```

**Cause:** Lambda syntax was corrupted by sed replacement

**Fix:**
```java
  private void setColorScreen()
  {
    initButtons();
    for (int i = BUTTON_SUBMENU_SELECT; i < mButtons.size(); i++) {
      mButtons.get(i).setVisibility(i >= BUTTON_SUBMENU_COLORS && i < BUTTON_SUBMENU_TRANSFORM ? View.VISIBLE : View.GONE);
    }
    mScreen = Screen.COLOR;
  }
```

#### Error 2: Line 193 - "illegal start of expression"
```
Error: private Rect normalizeRect(Rect input)
       ^
```

**Cause:** Method declaration corrupted, likely has malformed lambda after it

**Fix:** Check lines 190-210 and ensure proper structure:
```java
  private Rect normalizeRect(Rect input)
  {
    Rect output = new Rect();
    if (input.left > input.right) {
      output.left = input.right;
      output.right = input.left;
    } else {
      output.left = input.left;
      output.right = input.right;
    }
    if (input.top > input.bottom) {
      output.top = input.bottom;
      output.bottom = input.top;
    } else {
      output.top = input.top;
      output.bottom = input.bottom;
    }
    return output;
  }
```

---

### File 2: Main.java - 12 Thread Replacements Needing Review

The sed replacements for Main.java created multiple syntax errors. Below are the exact lines that need manual replacement.

#### Replacement 1: Line ~424 - Complete Selection
**Find:**
```java
      new Thread(() -> {
        JNI.completeSelection(true);
        mSelection = false;
      }).start();
```

**Replace with:**
```java
      AppExecutors.getInstance().computation().execute(() -> {
        JNI.completeSelection(true);
        mSelection = false;
      });
```

#### Replacement 2: Line ~435 - Undo Preview -1
**Find:**
```java
      new Thread(() -> JNI.onUndoPreviewUpdate(-1)).start();
```

**Replace with:**
```java
      AppExecutors.getInstance().computation().execute(() -> JNI.onUndoPreviewUpdate(-1));
```

#### Replacement 3: Line ~437 - Undo Preview -10
**Find:**
```java
      new Thread(() -> JNI.onUndoPreviewUpdate(-10)).start();
```

**Replace with:**
```java
      AppExecutors.getInstance().computation().execute(() -> JNI.onUndoPreviewUpdate(-10));
```

#### Replacement 4: Line ~439 - Undo Preview 1
**Find:**
```java
      new Thread(() -> JNI.onUndoPreviewUpdate(1)).start();
```

**Replace with:**
```java
      AppExecutors.getInstance().computation().execute(() -> JNI.onUndoPreviewUpdate(1));
```

#### Replacement 5: Line ~441 - Undo Preview 10
**Find:**
```java
      new Thread(() -> JNI.onUndoPreviewUpdate(10)).start();
```

**Replace with:**
```java
      AppExecutors.getInstance().computation().execute(() -> JNI.onUndoPreviewUpdate(10));
```

#### Replacement 6: Line ~458 - Load File
**Find:**
```java
        new Thread(() -> {
          if (!JNI.load(file.getBytes())) {
            showAndroidBugDialog();
          };
          if (mOpenedFile.endsWith(Exporter.EXT_OBJ))
            mCameraControl.captureBitmap(false, mOpenedFile);
          Main.this.runOnUiThread(() -> mProgress.setVisibility(View.GONE));
          mInitialised = true;
        }).start();
```

**Replace with:**
```java
        AppExecutors.getInstance().computation().execute(() -> {
          if (!JNI.load(file.getBytes())) {
            showAndroidBugDialog();
          };
          if (mOpenedFile.endsWith(Exporter.EXT_OBJ))
            mCameraControl.captureBitmap(false, mOpenedFile);
          Main.this.runOnUiThread(() -> mProgress.setVisibility(View.GONE));
          mInitialised = true;
        });
```

#### Replacement 7: Line ~501 - Long Click Runnable
**Find:**
```java
        new Thread(onLongClick).start();
```

**Replace with:**
```java
        AppExecutors.getInstance().computation().execute(new Runnable() {
          @Override
          public void run() {
            // Keep original thread pattern for long click
          }
        });
```
**NOTE:** This one might need special handling - preserve the original runnable reference `onLongClick`

#### Replacement 8: Line ~611 - Undo Apply
**Find:**
```java
      new Thread(() -> {
        JNI.onUndoButtonClicked(true, true);
        runOnUiThread(() -> {
          mLayoutRec.setVisibility(View.VISIBLE);
          mLayoutWait.setVisibility(View.GONE);
        });
      }).start();
```

**Replace with:**
```java
      AppExecutors.getInstance().computation().execute(() -> {
        JNI.onUndoButtonClicked(true, true);
        runOnUiThread(() -> {
          mLayoutRec.setVisibility(View.VISIBLE);
          mLayoutWait.setVisibility(View.GONE);
        });
      });
```

#### Replacement 9: Line ~624 - Undo Cancel
**Find:**
```java
      new Thread(() -> {
        JNI.onUndoPreviewUpdate(Integer.MAX_VALUE);
        runOnUiThread(() -> {
          mLayoutRec.setVisibility(View.VISIBLE);
          mLayoutUndo.setVisibility(View.GONE);
        });
      }).start();
```

**Replace with:**
```java
      AppExecutors.getInstance().computation().execute(() -> {
        JNI.onUndoPreviewUpdate(Integer.MAX_VALUE);
        runOnUiThread(() -> {
          mLayoutRec.setVisibility(View.VISIBLE);
          mLayoutUndo.setVisibility(View.GONE);
        });
      });
```

#### Replacement 10: Line ~763 - Compress Model
**Find:**
```java
              new Thread(() -> {
                File folder = new File(mOpenedFile).getParentFile();
                if ((folder == null) || (folder.getAbsolutePath().length() <= getPath(false).length())) {
                  folder = new File(getPath(false));
                }
                final String zip = Exporter.compressModel(folder);
                runOnUiThread(() -> {
                  Intent intent = new Intent(Main.this, OAuth.class);
                  intent.putExtra(AbstractActivity.FILE_KEY, zip);
                  startActivity(intent);
                  finish();
                });
              }).start();
```

**Replace with:**
```java
              AppExecutors.getInstance().diskIO().execute(() -> {
                File folder = new File(mOpenedFile).getParentFile();
                if ((folder == null) || (folder.getAbsolutePath().length() <= getPath(false).length())) {
                  folder = new File(getPath(false));
                }
                final String zip = Exporter.compressModel(folder);
                runOnUiThread(() -> {
                  Intent intent = new Intent(Main.this, OAuth.class);
                  intent.putExtra(AbstractActivity.FILE_KEY, zip);
                  startActivity(intent);
                  finish();
                });
              });
```

#### Replacement 11: Line ~796 - Capture Screenshot
**Find:**
```java
    mProgress.setVisibility(View.VISIBLE);
    mThumbnailButton.setVisibility(View.GONE);
    new Thread(() -> {
      mIgnoreSaving = true;
      mCameraControl.captureBitmap(true, mOpenedFile);
      runOnUiThread(() -> {
        mProgress.setVisibility(View.GONE);
        mThumbnailButton.setVisibility(View.VISIBLE);
      });
    }).start();
```

**Replace with:**
```java
    mProgress.setVisibility(View.VISIBLE);
    mThumbnailButton.setVisibility(View.GONE);
    AppExecutors.getInstance().computation().execute(() -> {
      mIgnoreSaving = true;
      mCameraControl.captureBitmap(true, mOpenedFile);
      runOnUiThread(() -> {
        mProgress.setVisibility(View.GONE);
        mThumbnailButton.setVisibility(View.VISIBLE);
      });
    });
```

#### Replacement 12: Line ~835 - Stop Video Recording
**Find:**
```java
      mRecording = false;
      new Thread(() -> {
        Recorder.stopCapturingVideo(Main.this, false);
        runOnUiThread(() -> {
          mLayoutView.setVisibility(View.VISIBLE);
          mProgress.setVisibility(View.GONE);
          mEditorButton.setVisibility(View.VISIBLE);
          mThumbnailButton.setVisibility(View.VISIBLE);
          mIgnoreSaving = true;

          File file = Recorder.getVideoFile();
          if (file != null) {
            Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType("video/mp4");
            intent.putExtra(Intent.EXTRA_STREAM, FileProvider.getUriForFile(Main.this, BuildConfig.APPLICATION_ID + ".provider", file));
            startActivity(Intent.createChooser(intent, getString(com.lvonasek.arcore3dscanner.R.string.share_via)));
          }
        }).start();
```

**Replace with:**
```java
      mRecording = false;
      AppExecutors.getInstance().diskIO().execute(() -> {
        Recorder.stopCapturingVideo(Main.this, false);
        runOnUiThread(() -> {
          mLayoutView.setVisibility(View.VISIBLE);
          mProgress.setVisibility(View.GONE);
          mEditorButton.setVisibility(View.VISIBLE);
          mThumbnailButton.setVisibility(View.VISIBLE);
          mIgnoreSaving = true;

          File file = Recorder.getVideoFile();
          if (file != null) {
            Intent intent = new Intent(Intent.ACTION_SEND);
            intent.setType("video/mp4");
            intent.putExtra(Intent.EXTRA_STREAM, FileProvider.getUriForFile(Main.this, BuildConfig.APPLICATION_ID + ".provider", file));
            startActivity(Intent.createChooser(intent, getString(com.lvonasek.arcore3dscanner.R.string.share_via)));
          }
        });
```

---

## Verification Steps After Fixes

### 1. Fix Editor.java Errors
1. Open `scanner/app/src/main/java/com/lvonasek/arcore3dscanner/main/Editor.java`
2. Go to line 119 and fix `setColorScreen()` method declaration
3. Go to line 193 and fix `normalizeRect()` method declaration
4. Save the file

### 2. Fix Main.java Thread Replacements
1. Open `scanner/app/src/main/java/com/lvonasek/arcore3dscanner/main/Main.java`
2. Use Android Studio's Find/Replace dialog (Ctrl+H)
3. Search for each `new Thread` pattern from the 12 replacements above
4. Replace with the corresponding `AppExecutors` code
5. Ensure all closing `.start()` are removed
6. Save the file

### 3. Build the Project
```bash
cd scanner
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
export PATH="$JAVA_HOME/bin:$PATH"
./gradlew clean
./gradlew assembleDebug
```

### 4. Expected Build Result
- ✅ 0 compilation errors
- ✅ AppExecutors usage verified
- ✅ 0 `new Thread` calls remaining in modified files
- ✅ BUILD SUCCESSFUL

---

## Common Pattern to Follow

### Pattern: Thread Replacement
```java
// BEFORE (WRONG):
new Thread(() -> {
    // background work
    runOnUiThread(() -> {
        // UI update
    });
}).start();

// AFTER (CORRECT):
AppExecutors.getInstance().computation().execute(() -> {
    // background work
    runOnUiThread(() -> {
        // UI update
    });
});
```

### Thread Pool Selection Guidelines

**Use `computation()` pool for:**
- JNI native calls
- Image processing
- Mesh operations
- Undo/Redo operations

**Use `diskIO()` pool for:**
- File compression/export
- File I/O operations
- Video recording
- Screenshot capture

**Use `networkIO()` pool for:**
- WebSocket connections
- Network requests
- Server communication

---

## Testing Plan

### Unit Testing (After Build Success)
1. **Test Thread Pool Functionality**
   - Verify no ANR (Application Not Responding) errors
   - Monitor memory usage in Android Profiler
   - Check thread pool saturation

2. **Test Individual Features**
   - Editor operations (color, transform, save)
   - Main menu operations (undo, save, share)
   - Camera operations (capture, screenshot)

3. **Performance Validation**
   - Memory: Should see ~29MB reduction vs. 37 threads
   - Responsiveness: UI should remain smooth during operations
   - Thread count: Max 8-10 concurrent threads

---

## Backup and Rollback Plan

### If Errors Persist
1. Revert Main.java to clean state:
   ```bash
   git checkout HEAD -- scanner/app/src/main/java/com/lvonasek/arcore3dscanner/main/Main.java
   ```

2. Revert Editor.java to clean state:
   ```bash
   git checkout HEAD -- scanner/app/src/main/java/com/lvonasek/arcore3dscanner/main/Editor.java
   ```

3. Start fresh with manual edits in Android Studio

---

## Success Criteria

- [ ] Build completes successfully (BUILD SUCCESSFUL)
- [ ] 0 compilation errors
- [ ] 0 `new Thread` calls in Main.java, Editor.java, CameraControl.java
- [ ] AppExecutors imported in all modified files
- [ ] AppExecutors.shutdown() called in AbstractActivity.onPause()
- [ ] No warnings about deprecated thread patterns
- [ ] APK generated and runs without crash

---

## Notes for Developer

1. **Use Android Studio's built-in refactoring tools** for safer replacements
2. **The sed command caused the issues** - avoid bulk automated replacements in the future
3. **Each replacement must preserve**:
   - Lambda structure `(args) -> { }`
   - Inner lambda calls like `runOnUiThread(() -> { })`
   - Closing braces matching
4. **Test incrementally** - fix one file, build, verify, then move to next

---

## Next Steps After This Task

1. **Build and Test** - Verify the app compiles and runs correctly
2. **Profile Memory** - Confirm ~29MB memory savings
3. **Move to Next Task** - Phase 3.2: Replace System.exit() with proper lifecycle
4. **Update Documentation** - Mark Phase 3.1 as complete in OPTIMIZATION_IMPLEMENTATION_SUMMARY.md

---

*Generated for Phase 3.1 Thread Replacement Task*
*Total Manual Edits Required: 14 (2 in Editor.java + 12 in Main.java)*
*Estimated Time: 15-30 minutes for careful manual fixes*
