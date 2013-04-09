/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HW_EMULATOR_CAMERA_EMULATED_FAKE_CAMERA3_H
#define HW_EMULATOR_CAMERA_EMULATED_FAKE_CAMERA3_H

/**
 * Contains declaration of a class EmulatedCamera that encapsulates
 * functionality of a fake camera that implements version 3 of the camera device
 * interace.
 */

#include "EmulatedCamera3.h"
#include "fake-pipeline2/Base.h"
#include "fake-pipeline2/Sensor.h"
#include "fake-pipeline2/JpegCompressor.h"
#include <camera/CameraMetadata.h>
#include <utils/List.h>
#include <utils/Mutex.h>

namespace android {

/* Encapsulates functionality common to all version 3.0 emulated camera devices
 *
 * Note that EmulatedCameraFactory instantiates an object of this class just
 * once, when EmulatedCameraFactory instance gets constructed. Connection to /
 * disconnection from the actual camera device is handled by calls to
 * connectDevice(), and closeCamera() methods of this class that are invoked in
 * response to hw_module_methods_t::open, and camera_device::close callbacks.
 */
class EmulatedFakeCamera3 : public EmulatedCamera3 {
public:

    EmulatedFakeCamera3(int cameraId, bool facingBack,
            struct hw_module_t* module);

    virtual ~EmulatedFakeCamera3();

    /****************************************************************************
     * EmulatedCamera3 virtual overrides
     ***************************************************************************/

public:

    virtual status_t Initialize();

    /****************************************************************************
     * Camera module API and generic hardware device API implementation
     ***************************************************************************/

public:
    virtual status_t connectCamera(hw_device_t** device);

    virtual status_t closeCamera();

    virtual status_t getCameraInfo(struct camera_info *info);

    /****************************************************************************
     * EmualtedCamera3 abstract API implementation
     ***************************************************************************/

protected:

    virtual status_t configureStreams(
        camera3_stream_configuration *streamList);

    virtual status_t registerStreamBuffers(
        const camera3_stream_buffer_set *bufferSet) ;

    virtual const camera_metadata_t* constructDefaultRequestSettings(
        int type);

    virtual status_t processCaptureRequest(camera3_capture_request *request);

    /** Debug methods */

    virtual void dump(int fd);

    /** Tag query methods */
    virtual const char *getVendorSectionName(uint32_t tag);

    virtual const char *getVendorTagName(uint32_t tag);

    virtual int getVendorTagType(uint32_t tag);

private:

    status_t constructStaticInfo();

    /** Signal from readout thread that it doesn't have anything to do */
    void     signalReadoutIdle();

    /****************************************************************************
     * Static configuration information
     ***************************************************************************/
private:
    static const uint32_t kMaxRawStreamCount = 1;
    static const uint32_t kMaxProcessedStreamCount = 3;
    static const uint32_t kMaxJpegStreamCount = 1;
    static const uint32_t kMaxReprocessStreamCount = 2;
    static const uint32_t kMaxBufferCount = 4;
    static const uint32_t kAvailableFormats[];
    static const uint32_t kAvailableRawSizes[];
    static const uint64_t kAvailableRawMinDurations[];
    static const uint32_t kAvailableProcessedSizesBack[];
    static const uint32_t kAvailableProcessedSizesFront[];
    static const uint64_t kAvailableProcessedMinDurations[];
    static const uint32_t kAvailableJpegSizesBack[];
    static const uint32_t kAvailableJpegSizesFront[];
    static const uint64_t kAvailableJpegMinDurations[];

    static const int64_t  kSyncWaitTimeout     = 10000000; // 10 ms
    static const int32_t  kMaxSyncTimeoutCount = 1000; // 1000 kSyncWaitTimeouts
    static const uint32_t kFenceTimeoutMs      = 2000; // 2 s

    /****************************************************************************
     * Data members.
     ***************************************************************************/

    /* HAL interface serialization lock. */
    Mutex mLock;

    /* Facing back (true) or front (false) switch. */
    bool mFacingBack;

    /**
     * Cache for default templates. Once one is requested, the pointer must be
     * valid at least until close() is called on the device
     */
    camera_metadata_t *mDefaultTemplates[CAMERA3_TEMPLATE_COUNT];

    /**
     * Private stream information, stored in camera3_stream_t->priv.
     */
    struct PrivateStreamInfo {
        bool alive;
        bool registered;
    };

    // Shortcut to the input stream
    camera3_stream_t* mInputStream;

    // All streams, including input stream
    List<camera3_stream_t*> mStreams;

    typedef List<camera3_stream_t*>::iterator StreamIterator;

    // Cached settings from latest submitted request
    CameraMetadata mPrevSettings;

    /** Fake hardware interfaces */
    sp<Sensor> mSensor;
    sp<JpegCompressor> mJpegCompressor;

    /** Processing thread for sending out results */

    class ReadoutThread : public Thread {
      public:
        ReadoutThread(EmulatedFakeCamera3 *parent);
        ~ReadoutThread();

        struct Request {
            uint32_t frameNumber;
            CameraMetadata settings;
            Vector<camera3_stream_buffer> *buffers;
            Buffers *sensorBuffers;
        };

        void queueCaptureRequest(const Request &r);
        bool isIdle();
        status_t waitForReadout();

      private:
        static const nsecs_t kWaitPerLoop  = 10000000L; // 10 ms
        static const nsecs_t kMaxWaitLoops = 1000;
        static const size_t  kMaxQueueSize = 2;

        EmulatedFakeCamera3 *mParent;
        Mutex mLock;

        List<Request> mInFlightQueue;
        Condition     mInFlightSignal;
        bool          mThreadActive;

        virtual bool threadLoop();

        // Only accessed by threadLoop

        Request mCurrentRequest;

    };

    sp<ReadoutThread> mReadoutThread;
};

} // namespace android

#endif // HW_EMULATOR_CAMERA_EMULATED_CAMERA3_H
