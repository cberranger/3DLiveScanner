# Application Documentation

## 1. Overview

This application is a 3D scanner that uses ARCore (or HUAWEI AR Engine) to capture camera and sensor data and reconstruct it into a 3D model. The application allows users to scan their surroundings, view the reconstructed 3D model, and export it in various formats.

## 2. Architecture

The application consists of the following main components:

*   **AR Module:** Handles the AR session, including camera and sensor data acquisition, motion tracking, and plane detection.
*   **3D Reconstruction Module:** Processes the data from the AR module to create a 3D model of the scanned environment.
*   **Data Handling Module:** Manages the storage and retrieval of scan data, including datasets and exported models.
*   **UI Module:** Provides the user interface for interacting with the application, including controls for starting/stopping scans, adjusting settings, and viewing models.
*   **Native Interface (JNI):** Bridges the Java code with the C++ code, which implements the core AR and 3D reconstruction functionality.

## 3. Modules

### 3.1. AR Module

*   **Functionality:**
    *   Initializes and manages the AR session (ARCore or HUAWEI AR Engine).
    *   Acquires camera images, depth data, and sensor readings (e.g., accelerometer, gyroscope).
    *   Tracks the device's motion in the real world.
    *   Detects planes and other features in the environment.
*   **Key Files:**
    *   `common/arcore/arcore.cc`: Implements ARCore integration.
    *   `common/arcore/arengine.cc`: Implements HUAWEI AR Engine integration (if applicable).
    *   `common/arcore/camera.cc`: Handles camera-related operations.

### 3.2. 3D Reconstruction Module

*   **Functionality:**
    *   Takes the AR data (point clouds, camera poses) as input.
    *   Performs 3D reconstruction algorithms to generate a mesh or point cloud model.
    *   Supports post-processing operations like texturing, simplification, and optimization.
*   **Key Files:**
    *   `common/tango/scan.cc`: Manages the 3D scanning process.
    *   `common/postproc/`: Contains files related to post-processing operations (e.g., `optimizer.cc`, `poisson.cc`, `texturize.cc`).
    *   `third_party/poisson/`: Contains the Poisson surface reconstruction library.

### 3.3. Data Handling Module

*   **Functionality:**
    *   Saves and loads scan data as datasets (collections of sensor readings, point clouds, and poses).
    *   Exports 3D models in various formats (e.g., OBJ, PLY).
    *   Manages file operations and storage locations.
*   **Key Files:**
    *   `common/data/dataset.cc`: Handles dataset operations.
    *   `common/data/file3d.cc`: Provides utilities for working with 3D file formats.
    *   `common/exporter/`: Contains files related to model exporting (e.g., `exporter.cc`, `ply.cc`).

### 3.4. UI Module

*   **Functionality:**
    *   Provides the main user interface for the application.
    *   Handles user input (e.g., button clicks, touch gestures).
    *   Displays the camera feed, 3D model, and other relevant information.
    *   Allows users to configure settings and control the scanning process.
*   **Key Files:**
    *   `scanner/app/src/main/java/com/lvonasek/arcore3dscanner/main/Main.java`: The main activity of the application.
    *   `scanner/app/src/main/res/layout/activity_main.xml`: Defines the layout of the main activity.
    *   Other Java files in `scanner/app/src/main/java/com/lvonasek/arcore3dscanner/` for UI elements and logic.

### 3.5. Native Interface (JNI)

*   **Functionality:**
    *   Provides a bridge between the Java code (UI, app logic) and the C++ code (AR, 3D reconstruction).
    *   Defines native methods that can be called from Java to perform low-level operations.
    *   Handles data exchange between Java and C++.
*   **Key Files:**
    *   `scanner/app/src/main/java/com/lvonasek/arcore3dscanner/main/JNI.java`: Defines the Java-side JNI methods.
    *   C++ files that implement the corresponding native functions (e.g., in `common/arcore`, `common/tango`).

## 4. Data Flow

1.  The AR module captures camera images, depth data, and sensor readings.
2.  This data is passed to the 3D reconstruction module.
3.  The 3D reconstruction module processes the data to generate a 3D model.
4.  The UI module displays the camera feed and the reconstructed model.
5.  Users can interact with the UI to control the scanning process, adjust settings, and save/export the model.
6.  The data handling module manages the storage and retrieval of scan data and exported models.

## 5. File Formats

*   **Datasets:** The application can save scan data as datasets, which are collections of files that include:
    *   Point clouds (`.pcl`)
    *   Camera poses (`.mat`)
    *   Distortion parameters (`distortion.txt`)
    *   Other metadata (`state.txt`, `rotation.txt`)
*   **Exported Models:** The application can export 3D models in the following formats:
    *   OBJ (`.obj`): A widely supported 3D model format that includes geometry, materials, and textures.
    *   PLY (`.ply`): A format for storing 3D data, often used for point clouds.

## 6. How to Use

*   **Starting a Scan:**
    1.  Launch the application.
    2.  Point your device at the area you want to scan.
    3.  Tap the record button to start scanning.
    4.  Move your device around slowly and steadily to capture the environment.
*   **Stopping a Scan:**
    1.  Tap the pause button to stop scanning.
*   **Viewing the Model:**
    1.  After stopping the scan, the reconstructed 3D model will be displayed on the screen.
    2.  You can use touch gestures to rotate, pan, and zoom the model.
*   **Saving and Exporting:**
    1.  Tap the save button to save the scan data or export the model.
    2.  Choose the desired file format and location.
*   **Other Features:**
    *   **Clear:** Discards the current scan data.
    *   **Undo:** Reverts the last action.
    *   **Settings:** Allows you to configure various scanning parameters (e.g., resolution, noise reduction).

## 7. Functionality Expansion: Server-Side Processing

This section outlines a plan to enable sending camera/sensor data to a server for processing, effectively splitting the application and offloading the computationally intensive tasks from the mobile device.

### 7.1. Data to be Sent

The following data should be considered for sending to the server:

*   **Raw Sensor Data:** Accelerometer, gyroscope, and magnetometer readings with timestamps. This allows for server-side sensor fusion and pose estimation.
*   **Camera Images:** Compressed camera frames (e.g., JPEG) with timestamps and camera intrinsics/extrinsics.
*   **Depth Data:** Raw depth maps from the TOF sensor (if available) or depth estimated by ARCore, along with timestamps and associated camera poses.
*   **Point Clouds:** Periodically generated point clouds from the device's local reconstruction, which can be merged and refined on the server.
*   **Camera Poses:** Device poses (translation and rotation) as estimated by the AR system, along with timestamps.

### 7.2. Data Format

*   **Serialization:** Protocol Buffers (Protobuf) or FlatBuffers are recommended for serializing the data. They offer efficient data representation and schema evolution.
*   **Compression:** Data should be compressed before transmission (e.g., using gzip) to reduce bandwidth usage. Individual data types like images can use their own compression (JPEG).

### 7.3. Data Transmission

*   **Protocol:** HTTPS should be used for secure communication.
*   **Mechanism:**
    *   **Batching:** Data can be batched and sent periodically to the server to reduce the number of network requests.
    *   **Streaming (Optional):** For near real-time processing, WebSockets or gRPC streaming could be considered, though this adds complexity. A persistent HTTP/2 connection might also be suitable.
*   **Endpoints:** Dedicated API endpoints will be needed for uploading different types of data (e.g., `/upload/sensor_data`, `/upload/image`, `/upload/depth`).

### 7.4. Security Considerations

*   **Authentication:** Implement a secure authentication mechanism (e.g., API keys, OAuth 2.0) to ensure only authorized devices can send data to the server.
*   **Encryption:** All data in transit must be encrypted using TLS/SSL (provided by HTTPS).
*   **Data Privacy:** Consider user consent and data anonymization if applicable, especially if personal data is being collected.

### 7.5. Codebase Changes

*   **`Main.java` / UI Module:**
    *   Add UI elements for configuring server settings (e.g., server URL, API key).
    *   Implement logic for initiating and managing the data upload process (e.g., start/stop uploading, display progress).
    *   Handle user authentication.
*   **`JNI.java` / Native Interface:**
    *   Add new JNI methods to trigger the collection and sending of specific data types from the C++ layer.
    *   Potentially add callbacks from C++ to Java to indicate data availability or upload status.
*   **C++ Modules (`arcore.cc`, `scan.cc`, `dataset.cc`):**
    *   Modify data acquisition parts to buffer and prepare data for sending (e.g., in `arcore.cc` for sensor/image data, `scan.cc` for point clouds).
    *   Implement networking capabilities in C++ (e.g., using a library like cURL or a platform-specific networking API) or pass raw data buffers to Java for transmission.
    *   Add logic for serializing data using the chosen format (Protobuf/FlatBuffers).
    *   Implement batching and compression.
*   **New Networking Module (Java or C++):**
    *   A dedicated module could be created to handle all server communication, including request formation, sending data, and handling responses.

### 7.6. Server-Side API Definition

The server will need to expose endpoints to:

*   **Receive Sensor Data:**
    *   `POST /api/v1/data/sensor`
    *   Body: Serialized batch of sensor readings.
*   **Receive Camera Images:**
    *   `POST /api/v1/data/image`
    *   Body: Image file (e.g., JPEG) and associated metadata (timestamp, pose, intrinsics). Could be multipart/form-data.
*   **Receive Depth Data:**
    *   `POST /api/v1/data/depth`
    *   Body: Serialized depth map and metadata.
*   **Receive Point Clouds:**
    *   `POST /api/v1/data/pointcloud`
    *   Body: Serialized point cloud data.
*   **Session Management (Optional):**
    *   `POST /api/v1/session/start`
    *   `POST /api/v1/session/end`

The server should respond with appropriate status codes (e.g., `200 OK` for success, `4xx` for client errors, `5xx` for server errors).

### 7.7. Error Handling and Data Integrity

*   **Client-Side:**
    *   Implement retry mechanisms for failed uploads with exponential backoff.
    *   Cache data locally if the server is unreachable and upload when the connection is restored.
    *   Validate data before sending to the extent possible.
    *   Log upload successes and failures.
*   **Server-Side:**
    *   Validate incoming data (e.g., checksums, format validation).
    *   Implement robust error handling and logging.
    *   Provide clear error messages to the client.
*   **Data Integrity:**
    *   Use checksums (e.g., MD5, SHA256) for large data chunks to ensure they are not corrupted during transit. The server can verify these checksums.
    *   Timestamps and sequence numbers can help in ordering and verifying the completeness of the data on the server.
