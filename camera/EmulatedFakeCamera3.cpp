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

/*
 * Contains implementation of a class EmulatedFakeCamera3 that encapsulates
 * functionality of an advanced fake camera.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_FakeCamera3"
#include <utils/Log.h>

#include "EmulatedFakeCamera3.h"
#include "EmulatedCameraFactory.h"
#include <ui/Fence.h>
#include <ui/Rect.h>
#include <ui/GraphicBufferMapper.h>
#include "gralloc_cb.h"
#include "fake-pipeline2/Sensor.h"
#include "fake-pipeline2/JpegCompressor.h"
#include <cmath>

namespace android {

/**
 * Constants for camera capabilities
 */

const int64_t USEC = 1000LL;
const int64_t MSEC = USEC * 1000LL;
const int64_t SEC = MSEC * 1000LL;

const uint32_t EmulatedFakeCamera3::kAvailableFormats[4] = {
        HAL_PIXEL_FORMAT_RAW_SENSOR,
        HAL_PIXEL_FORMAT_BLOB,
        HAL_PIXEL_FORMAT_RGBA_8888,
        //        HAL_PIXEL_FORMAT_YV12,
        HAL_PIXEL_FORMAT_YCrCb_420_SP
};

const uint32_t EmulatedFakeCamera3::kAvailableRawSizes[2] = {
    640, 480
    //    Sensor::kResolution[0], Sensor::kResolution[1]
};

const uint64_t EmulatedFakeCamera3::kAvailableRawMinDurations[1] = {
    (const uint64_t)Sensor::kFrameDurationRange[0]
};

const uint32_t EmulatedFakeCamera3::kAvailableProcessedSizesBack[4] = {
    640, 480, 320, 240
    //    Sensor::kResolution[0], Sensor::kResolution[1]
};

const uint32_t EmulatedFakeCamera3::kAvailableProcessedSizesFront[4] = {
    320, 240, 160, 120
    //    Sensor::kResolution[0], Sensor::kResolution[1]
};

const uint64_t EmulatedFakeCamera3::kAvailableProcessedMinDurations[1] = {
    (const uint64_t)Sensor::kFrameDurationRange[0]
};

const uint32_t EmulatedFakeCamera3::kAvailableJpegSizesBack[2] = {
    640, 480
    //    Sensor::kResolution[0], Sensor::kResolution[1]
};

const uint32_t EmulatedFakeCamera3::kAvailableJpegSizesFront[2] = {
    320, 240
    //    Sensor::kResolution[0], Sensor::kResolution[1]
};


const uint64_t EmulatedFakeCamera3::kAvailableJpegMinDurations[1] = {
    (const uint64_t)Sensor::kFrameDurationRange[0]
};

/**
 * 3A constants
 */

// Default exposure and gain targets for different scenarios
const nsecs_t EmulatedFakeCamera3::kNormalExposureTime       = 10 * MSEC;
const nsecs_t EmulatedFakeCamera3::kFacePriorityExposureTime = 30 * MSEC;
const int     EmulatedFakeCamera3::kNormalSensitivity        = 100;
const int     EmulatedFakeCamera3::kFacePrioritySensitivity  = 400;
const float   EmulatedFakeCamera3::kExposureTrackRate        = 0.1;
const int     EmulatedFakeCamera3::kPrecaptureMinFrames      = 10;
const int     EmulatedFakeCamera3::kStableAeMaxFrames        = 100;
const float   EmulatedFakeCamera3::kExposureWanderMin        = -2;
const float   EmulatedFakeCamera3::kExposureWanderMax        = 1;

/**
 * Camera device lifecycle methods
 */

EmulatedFakeCamera3::EmulatedFakeCamera3(int cameraId, bool facingBack,
        struct hw_module_t* module) :
        EmulatedCamera3(cameraId, module),
        mFacingBack(facingBack) {
    ALOGD("Constructing emulated fake camera 3 facing %s",
            facingBack ? "back" : "front");

    for (size_t i = 0; i < CAMERA3_TEMPLATE_COUNT; i++) {
        mDefaultTemplates[i] = NULL;
    }
}

EmulatedFakeCamera3::~EmulatedFakeCamera3() {
    for (size_t i = 0; i < CAMERA3_TEMPLATE_COUNT; i++) {
        if (mDefaultTemplates[i] != NULL) {
            free_camera_metadata(mDefaultTemplates[i]);
        }
    }
}

status_t EmulatedFakeCamera3::Initialize() {
    ALOGV("%s: E", __FUNCTION__);
    status_t res;

    if (mStatus != STATUS_ERROR) {
        ALOGE("%s: Already initialized!", __FUNCTION__);
        return INVALID_OPERATION;
    }

    res = constructStaticInfo();
    if (res != OK) {
        ALOGE("%s: Unable to allocate static info: %s (%d)",
                __FUNCTION__, strerror(-res), res);
        return res;
    }

    return EmulatedCamera3::Initialize();
}

status_t EmulatedFakeCamera3::connectCamera(hw_device_t** device) {
    ALOGV("%s: E", __FUNCTION__);
    Mutex::Autolock l(mLock);
    status_t res;

    if (mStatus != STATUS_CLOSED) {
        ALOGE("%s: Can't connect in state %d", __FUNCTION__, mStatus);
        return INVALID_OPERATION;
    }

    mSensor = new Sensor();

    res = mSensor->startUp();
    if (res != NO_ERROR) return res;

    mReadoutThread = new ReadoutThread(this);
    mJpegCompressor = new JpegCompressor(NULL);

    res = mReadoutThread->run("EmuCam3::readoutThread");
    if (res != NO_ERROR) return res;

    // Initialize fake 3A

    mControlMode  = ANDROID_CONTROL_MODE_AUTO;
    mFacePriority = false;
    mAeMode       = ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH;
    mAfMode       = ANDROID_CONTROL_AF_MODE_AUTO;
    mAwbMode      = ANDROID_CONTROL_AWB_MODE_AUTO;
    mAeState      = ANDROID_CONTROL_AE_STATE_INACTIVE;
    mAfState      = ANDROID_CONTROL_AF_STATE_INACTIVE;
    mAwbState     = ANDROID_CONTROL_AWB_STATE_INACTIVE;
    mAfTriggerId  = 0;
    mAeTriggerId  = 0;
    mAeCurrentExposureTime = kNormalExposureTime;
    mAeCurrentSensitivity  = kNormalSensitivity;

    return EmulatedCamera3::connectCamera(device);
}

status_t EmulatedFakeCamera3::closeCamera() {
    ALOGV("%s: E", __FUNCTION__);
    status_t res;
    {
        Mutex::Autolock l(mLock);
        if (mStatus == STATUS_CLOSED) return OK;

        res = mSensor->shutDown();
        if (res != NO_ERROR) {
            ALOGE("%s: Unable to shut down sensor: %d", __FUNCTION__, res);
            return res;
        }
        mSensor.clear();

        mReadoutThread->requestExit();
    }

    mReadoutThread->join();

    {
        Mutex::Autolock l(mLock);
        // Clear out private stream information
        for (StreamIterator s = mStreams.begin(); s != mStreams.end(); s++) {
            PrivateStreamInfo *privStream =
                    static_cast<PrivateStreamInfo*>((*s)->priv);
            delete privStream;
            (*s)->priv = NULL;
        }
        mStreams.clear();
        mReadoutThread.clear();
    }

    return EmulatedCamera3::closeCamera();
}

status_t EmulatedFakeCamera3::getCameraInfo(struct camera_info *info) {
    info->facing = mFacingBack ? CAMERA_FACING_BACK : CAMERA_FACING_FRONT;
    info->orientation = gEmulatedCameraFactory.getFakeCameraOrientation();
    return EmulatedCamera3::getCameraInfo(info);
}

/**
 * Camera3 interface methods
 */

status_t EmulatedFakeCamera3::configureStreams(
        camera3_stream_configuration *streamList) {
    Mutex::Autolock l(mLock);
    ALOGV("%s: %d streams", __FUNCTION__, streamList->num_streams);

    if (mStatus != STATUS_OPEN && mStatus != STATUS_READY) {
        ALOGE("%s: Cannot configure streams in state %d",
                __FUNCTION__, mStatus);
        return NO_INIT;
    }

    /**
     * Sanity-check input list.
     */
    if (streamList == NULL) {
        ALOGE("%s: NULL stream configuration", __FUNCTION__);
        return BAD_VALUE;
    }

    if (streamList->streams == NULL) {
        ALOGE("%s: NULL stream list", __FUNCTION__);
        return BAD_VALUE;
    }

    if (streamList->num_streams < 1) {
        ALOGE("%s: Bad number of streams requested: %d", __FUNCTION__,
                streamList->num_streams);
        return BAD_VALUE;
    }

    camera3_stream_t *inputStream = NULL;
    for (size_t i = 0; i < streamList->num_streams; i++) {
        camera3_stream_t *newStream = streamList->streams[i];
        if (newStream->stream_type == CAMERA3_STREAM_INPUT) {
            if (inputStream != NULL) {

                ALOGE("%s: Multiple input streams requested!", __FUNCTION__);
                return BAD_VALUE;
            }
            inputStream = newStream;
        }
    }
    mInputStream = inputStream;

    /**
     * Initially mark all existing streams as not alive
     */
    for (StreamIterator s = mStreams.begin(); s != mStreams.end(); ++s) {
        PrivateStreamInfo *privStream =
                static_cast<PrivateStreamInfo*>((*s)->priv);
        privStream->alive = false;
    }

    /**
     * Find new streams and mark still-alive ones
     */
    for (size_t i = 0; i < streamList->num_streams; i++) {
        camera3_stream_t *newStream = streamList->streams[i];
        if (newStream->priv == NULL) {
            // New stream, construct info
            PrivateStreamInfo *privStream = new PrivateStreamInfo();
            privStream->alive = true;
            privStream->registered = false;

            switch (newStream->stream_type) {
                case CAMERA3_STREAM_OUTPUT:
                    newStream->usage = GRALLOC_USAGE_HW_CAMERA_WRITE;
                    break;
                case CAMERA3_STREAM_INPUT:
                    newStream->usage = GRALLOC_USAGE_HW_CAMERA_READ;
                    break;
                case CAMERA3_STREAM_BIDIRECTIONAL:
                    newStream->usage = GRALLOC_USAGE_HW_CAMERA_READ |
                            GRALLOC_USAGE_HW_CAMERA_WRITE;
                    break;
            }
            newStream->max_buffers = kMaxBufferCount;
            newStream->priv = privStream;
            mStreams.push_back(newStream);
        } else {
            // Existing stream, mark as still alive.
            PrivateStreamInfo *privStream =
                    static_cast<PrivateStreamInfo*>(newStream->priv);
            privStream->alive = true;
        }
    }

    /**
     * Reap the dead streams
     */
    for (StreamIterator s = mStreams.begin(); s != mStreams.end();) {
        PrivateStreamInfo *privStream =
                static_cast<PrivateStreamInfo*>((*s)->priv);
        if (!privStream->alive) {
            (*s)->priv = NULL;
            delete privStream;
            s = mStreams.erase(s);
        } else {
            ++s;
        }
    }

    /**
     * Can't reuse settings across configure call
     */
    mPrevSettings.clear();

    return OK;
}

status_t EmulatedFakeCamera3::registerStreamBuffers(
        const camera3_stream_buffer_set *bufferSet) {
    ALOGV("%s: E", __FUNCTION__);
    Mutex::Autolock l(mLock);

    /**
     * Sanity checks
     */

    if (mStatus != STATUS_READY) {
        ALOGE("%s: Cannot register buffers in state %d",
                __FUNCTION__, mStatus);
        return NO_INIT;
    }

    if (bufferSet == NULL) {
        ALOGE("%s: NULL buffer set!", __FUNCTION__);
        return BAD_VALUE;
    }

    StreamIterator s = mStreams.begin();
    for (; s != mStreams.end(); ++s) {
        if (bufferSet->stream == *s) break;
    }
    if (s == mStreams.end()) {
        ALOGE("%s: Trying to register buffers for a non-configured stream!",
                __FUNCTION__);
        return BAD_VALUE;
    }

    /**
     * Register the buffers. This doesn't mean anything to the emulator besides
     * marking them off as registered.
     */

    PrivateStreamInfo *privStream =
            static_cast<PrivateStreamInfo*>((*s)->priv);
    privStream->registered = true;

    return OK;
}

const camera_metadata_t* EmulatedFakeCamera3::constructDefaultRequestSettings(
        int type) {
    ALOGV("%s: E", __FUNCTION__);
    Mutex::Autolock l(mLock);

    if (type < 0 || type >= CAMERA2_TEMPLATE_COUNT) {
        ALOGE("%s: Unknown request settings template: %d",
                __FUNCTION__, type);
        return NULL;
    }

    /**
     * Cache is not just an optimization - pointer returned has to live at
     * least as long as the camera device instance does.
     */
    if (mDefaultTemplates[type] != NULL) {
        return mDefaultTemplates[type];
    }

    CameraMetadata settings;

    /** android.request */

    static const uint8_t requestType = ANDROID_REQUEST_TYPE_CAPTURE;
    settings.update(ANDROID_REQUEST_TYPE, &requestType, 1);

    static const uint8_t metadataMode = ANDROID_REQUEST_METADATA_MODE_FULL;
    settings.update(ANDROID_REQUEST_METADATA_MODE, &metadataMode, 1);

    static const int32_t id = 0;
    settings.update(ANDROID_REQUEST_ID, &id, 1);

    static const int32_t frameCount = 0;
    settings.update(ANDROID_REQUEST_FRAME_COUNT, &frameCount, 1);

    /** android.lens */

    static const float focusDistance = 0;
    settings.update(ANDROID_LENS_FOCUS_DISTANCE, &focusDistance, 1);

    static const float aperture = 2.8f;
    settings.update(ANDROID_LENS_APERTURE, &aperture, 1);

    static const float focalLength = 5.0f;
    settings.update(ANDROID_LENS_FOCAL_LENGTH, &focalLength, 1);

    static const float filterDensity = 0;
    settings.update(ANDROID_LENS_FILTER_DENSITY, &filterDensity, 1);

    static const uint8_t opticalStabilizationMode =
            ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    settings.update(ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
            &opticalStabilizationMode, 1);

    // FOCUS_RANGE set only in frame

    /** android.sensor */

    static const int64_t exposureTime = 10 * MSEC;
    settings.update(ANDROID_SENSOR_EXPOSURE_TIME, &exposureTime, 1);

    static const int64_t frameDuration = 33333333L; // 1/30 s
    settings.update(ANDROID_SENSOR_FRAME_DURATION, &frameDuration, 1);

    static const int32_t sensitivity = 100;
    settings.update(ANDROID_SENSOR_SENSITIVITY, &sensitivity, 1);

    // TIMESTAMP set only in frame

    /** android.flash */

    static const uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    settings.update(ANDROID_FLASH_MODE, &flashMode, 1);

    static const uint8_t flashPower = 10;
    settings.update(ANDROID_FLASH_FIRING_POWER, &flashPower, 1);

    static const int64_t firingTime = 0;
    settings.update(ANDROID_FLASH_FIRING_TIME, &firingTime, 1);

    /** Processing block modes */
    uint8_t hotPixelMode = 0;
    uint8_t demosaicMode = 0;
    uint8_t noiseMode = 0;
    uint8_t shadingMode = 0;
    uint8_t geometricMode = 0;
    uint8_t colorMode = 0;
    uint8_t tonemapMode = 0;
    uint8_t edgeMode = 0;
    switch (type) {
      case CAMERA2_TEMPLATE_STILL_CAPTURE:
        // fall-through
      case CAMERA2_TEMPLATE_VIDEO_SNAPSHOT:
        // fall-through
      case CAMERA2_TEMPLATE_ZERO_SHUTTER_LAG:
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_HIGH_QUALITY;
        demosaicMode = ANDROID_DEMOSAIC_MODE_HIGH_QUALITY;
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_HIGH_QUALITY;
        shadingMode = ANDROID_SHADING_MODE_HIGH_QUALITY;
        geometricMode = ANDROID_GEOMETRIC_MODE_HIGH_QUALITY;
        colorMode = ANDROID_COLOR_CORRECTION_MODE_HIGH_QUALITY;
        tonemapMode = ANDROID_TONEMAP_MODE_HIGH_QUALITY;
        edgeMode = ANDROID_EDGE_MODE_HIGH_QUALITY;
        break;
      case CAMERA2_TEMPLATE_PREVIEW:
        // fall-through
      case CAMERA2_TEMPLATE_VIDEO_RECORD:
        // fall-through
      default:
        hotPixelMode = ANDROID_HOT_PIXEL_MODE_FAST;
        demosaicMode = ANDROID_DEMOSAIC_MODE_FAST;
        noiseMode = ANDROID_NOISE_REDUCTION_MODE_FAST;
        shadingMode = ANDROID_SHADING_MODE_FAST;
        geometricMode = ANDROID_GEOMETRIC_MODE_FAST;
        colorMode = ANDROID_COLOR_CORRECTION_MODE_FAST;
        tonemapMode = ANDROID_TONEMAP_MODE_FAST;
        edgeMode = ANDROID_EDGE_MODE_FAST;
        break;
    }
    settings.update(ANDROID_HOT_PIXEL_MODE, &hotPixelMode, 1);
    settings.update(ANDROID_DEMOSAIC_MODE, &demosaicMode, 1);
    settings.update(ANDROID_NOISE_REDUCTION_MODE, &noiseMode, 1);
    settings.update(ANDROID_SHADING_MODE, &shadingMode, 1);
    settings.update(ANDROID_GEOMETRIC_MODE, &geometricMode, 1);
    settings.update(ANDROID_COLOR_CORRECTION_MODE, &colorMode, 1);
    settings.update(ANDROID_TONEMAP_MODE, &tonemapMode, 1);
    settings.update(ANDROID_EDGE_MODE, &edgeMode, 1);

    /** android.noise */
    static const uint8_t noiseStrength = 5;
    settings.update(ANDROID_NOISE_REDUCTION_STRENGTH, &noiseStrength, 1);

    /** android.color */
    static const float colorTransform[9] = {
        1.0f, 0.f, 0.f,
        0.f, 1.f, 0.f,
        0.f, 0.f, 1.f
    };
    settings.update(ANDROID_COLOR_CORRECTION_TRANSFORM, colorTransform, 9);

    /** android.tonemap */
    static const float tonemapCurve[4] = {
        0.f, 0.f,
        1.f, 1.f
    };
    settings.update(ANDROID_TONEMAP_CURVE_RED, tonemapCurve, 4);
    settings.update(ANDROID_TONEMAP_CURVE_GREEN, tonemapCurve, 4);
    settings.update(ANDROID_TONEMAP_CURVE_BLUE, tonemapCurve, 4);

    /** android.edge */
    static const uint8_t edgeStrength = 5;
    settings.update(ANDROID_EDGE_STRENGTH, &edgeStrength, 1);

    /** android.scaler */
    static const int32_t cropRegion[3] = {
        0, 0, (int32_t)Sensor::kResolution[0]
    };
    settings.update(ANDROID_SCALER_CROP_REGION, cropRegion, 3);

    /** android.jpeg */
    static const uint8_t jpegQuality = 80;
    settings.update(ANDROID_JPEG_QUALITY, &jpegQuality, 1);

    static const int32_t thumbnailSize[2] = {
        640, 480
    };
    settings.update(ANDROID_JPEG_THUMBNAIL_SIZE, thumbnailSize, 2);

    static const uint8_t thumbnailQuality = 80;
    settings.update(ANDROID_JPEG_THUMBNAIL_QUALITY, &thumbnailQuality, 1);

    static const double gpsCoordinates[2] = {
        0, 0
    };
    settings.update(ANDROID_JPEG_GPS_COORDINATES, gpsCoordinates, 2);

    static const uint8_t gpsProcessingMethod[32] = "None";
    settings.update(ANDROID_JPEG_GPS_PROCESSING_METHOD, gpsProcessingMethod, 32);

    static const int64_t gpsTimestamp = 0;
    settings.update(ANDROID_JPEG_GPS_TIMESTAMP, &gpsTimestamp, 1);

    static const int32_t jpegOrientation = 0;
    settings.update(ANDROID_JPEG_ORIENTATION, &jpegOrientation, 1);

    /** android.stats */

    static const uint8_t faceDetectMode =
        ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
    settings.update(ANDROID_STATISTICS_FACE_DETECT_MODE, &faceDetectMode, 1);

    static const uint8_t histogramMode = ANDROID_STATISTICS_HISTOGRAM_MODE_OFF;
    settings.update(ANDROID_STATISTICS_HISTOGRAM_MODE, &histogramMode, 1);

    static const uint8_t sharpnessMapMode =
        ANDROID_STATISTICS_SHARPNESS_MAP_MODE_OFF;
    settings.update(ANDROID_STATISTICS_SHARPNESS_MAP_MODE, &sharpnessMapMode, 1);

    // faceRectangles, faceScores, faceLandmarks, faceIds, histogram,
    // sharpnessMap only in frames

    /** android.control */

    uint8_t controlIntent = 0;
    switch (type) {
      case CAMERA2_TEMPLATE_PREVIEW:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
        break;
      case CAMERA2_TEMPLATE_STILL_CAPTURE:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
        break;
      case CAMERA2_TEMPLATE_VIDEO_RECORD:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
        break;
      case CAMERA2_TEMPLATE_VIDEO_SNAPSHOT:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
        break;
      case CAMERA2_TEMPLATE_ZERO_SHUTTER_LAG:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
        break;
      default:
        controlIntent = ANDROID_CONTROL_CAPTURE_INTENT_CUSTOM;
        break;
    }
    settings.update(ANDROID_CONTROL_CAPTURE_INTENT, &controlIntent, 1);

    static const uint8_t controlMode = ANDROID_CONTROL_MODE_AUTO;
    settings.update(ANDROID_CONTROL_MODE, &controlMode, 1);

    static const uint8_t effectMode = ANDROID_CONTROL_EFFECT_MODE_OFF;
    settings.update(ANDROID_CONTROL_EFFECT_MODE, &effectMode, 1);

    static const uint8_t sceneMode = ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY;
    settings.update(ANDROID_CONTROL_SCENE_MODE, &sceneMode, 1);

    static const uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH;
    settings.update(ANDROID_CONTROL_AE_MODE, &aeMode, 1);

    static const uint8_t aeLock = ANDROID_CONTROL_AE_LOCK_OFF;
    settings.update(ANDROID_CONTROL_AE_LOCK, &aeLock, 1);

    static const int32_t controlRegions[5] = {
        0, 0, (int32_t)Sensor::kResolution[0], (int32_t)Sensor::kResolution[1],
        1000
    };
    settings.update(ANDROID_CONTROL_AE_REGIONS, controlRegions, 5);

    static const int32_t aeExpCompensation = 0;
    settings.update(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &aeExpCompensation, 1);

    static const int32_t aeTargetFpsRange[2] = {
        10, 30
    };
    settings.update(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, aeTargetFpsRange, 2);

    static const uint8_t aeAntibandingMode =
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
    settings.update(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &aeAntibandingMode, 1);

    static const uint8_t awbMode =
            ANDROID_CONTROL_AWB_MODE_AUTO;
    settings.update(ANDROID_CONTROL_AWB_MODE, &awbMode, 1);

    static const uint8_t awbLock = ANDROID_CONTROL_AWB_LOCK_OFF;
    settings.update(ANDROID_CONTROL_AWB_LOCK, &awbLock, 1);

    settings.update(ANDROID_CONTROL_AWB_REGIONS, controlRegions, 5);

    uint8_t afMode = 0;
    switch (type) {
      case CAMERA2_TEMPLATE_PREVIEW:
        afMode = ANDROID_CONTROL_AF_MODE_AUTO;
        break;
      case CAMERA2_TEMPLATE_STILL_CAPTURE:
        afMode = ANDROID_CONTROL_AF_MODE_AUTO;
        break;
      case CAMERA2_TEMPLATE_VIDEO_RECORD:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        break;
      case CAMERA2_TEMPLATE_VIDEO_SNAPSHOT:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
        break;
      case CAMERA2_TEMPLATE_ZERO_SHUTTER_LAG:
        afMode = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
        break;
      default:
        afMode = ANDROID_CONTROL_AF_MODE_AUTO;
        break;
    }
    settings.update(ANDROID_CONTROL_AF_MODE, &afMode, 1);

    settings.update(ANDROID_CONTROL_AF_REGIONS, controlRegions, 5);

    static const uint8_t vstabMode =
        ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
    settings.update(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &vstabMode, 1);

    // aeState, awbState, afState only in frame

    mDefaultTemplates[type] = settings.release();

    return mDefaultTemplates[type];
}

status_t EmulatedFakeCamera3::processCaptureRequest(
        camera3_capture_request *request) {

    Mutex::Autolock l(mLock);
    status_t res;

    /** Validation */

    if (mStatus < STATUS_READY) {
        ALOGE("%s: Can't submit capture requests in state %d", __FUNCTION__,
                mStatus);
        return INVALID_OPERATION;
    }

    if (request == NULL) {
        ALOGE("%s: NULL request!", __FUNCTION__);
        return BAD_VALUE;
    }

    uint32_t frameNumber = request->frame_number;

    if (request->settings == NULL && mPrevSettings.isEmpty()) {
        ALOGE("%s: Request %d: NULL settings for first request after"
                "configureStreams()", __FUNCTION__, frameNumber);
        return BAD_VALUE;
    }

    if (request->input_buffer != NULL &&
            request->input_buffer->stream != mInputStream) {
        ALOGE("%s: Request %d: Input buffer not from input stream!",
                __FUNCTION__, frameNumber);
        return BAD_VALUE;
    }

    if (request->num_output_buffers < 1 || request->output_buffers == NULL) {
        ALOGE("%s: Request %d: No output buffers provided!",
                __FUNCTION__, frameNumber);
        return BAD_VALUE;
    }

    // Validate all buffers, starting with input buffer if it's given

    ssize_t idx;
    const camera3_stream_buffer_t *b;
    if (request->input_buffer != NULL) {
        idx = -1;
        b = request->input_buffer;
    } else {
        idx = 0;
        b = request->output_buffers;
    }
    do {
        PrivateStreamInfo *priv =
                static_cast<PrivateStreamInfo*>(b->stream->priv);
        if (priv == NULL) {
            ALOGE("%s: Request %d: Buffer %d: Unconfigured stream!",
                    __FUNCTION__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (!priv->alive || !priv->registered) {
            ALOGE("%s: Request %d: Buffer %d: Unregistered or dead stream!",
                    __FUNCTION__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->status != CAMERA3_BUFFER_STATUS_OK) {
            ALOGE("%s: Request %d: Buffer %d: Status not OK!",
                    __FUNCTION__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->release_fence != -1) {
            ALOGE("%s: Request %d: Buffer %d: Has a release fence!",
                    __FUNCTION__, frameNumber, idx);
            return BAD_VALUE;
        }
        if (b->buffer == NULL) {
            ALOGE("%s: Request %d: Buffer %d: NULL buffer handle!",
                    __FUNCTION__, frameNumber, idx);
            return BAD_VALUE;
        }
        idx++;
        b = &(request->output_buffers[idx]);
    } while (idx < (ssize_t)request->num_output_buffers);

    // TODO: Validate settings parameters

    /**
     * Start processing this request
     */

    mStatus = STATUS_ACTIVE;

    CameraMetadata settings;

    if (request->settings == NULL) {
        settings.acquire(mPrevSettings);
    } else {
        settings = request->settings;
    }

    res = process3A(settings);
    if (res != OK) {
        return res;
    }

    // TODO: Handle reprocessing

    /**
     * Get ready for sensor config
     */

    nsecs_t  exposureTime;
    nsecs_t  frameDuration;
    uint32_t sensitivity;

    exposureTime = settings.find(ANDROID_SENSOR_EXPOSURE_TIME).data.i64[0];
    frameDuration = settings.find(ANDROID_SENSOR_FRAME_DURATION).data.i64[0];
    sensitivity = settings.find(ANDROID_SENSOR_SENSITIVITY).data.i32[0];

    Buffers *sensorBuffers = new Buffers();
    Vector<camera3_stream_buffer> *buffers = new Vector<camera3_stream_buffer>();

    sensorBuffers->setCapacity(request->num_output_buffers);
    buffers->setCapacity(request->num_output_buffers);

    // Process all the buffers we got for output, constructing internal buffer
    // structures for them, and lock them for writing.
    for (size_t i = 0; i < request->num_output_buffers; i++) {
        const camera3_stream_buffer &srcBuf = request->output_buffers[i];
        const cb_handle_t *privBuffer =
                static_cast<const cb_handle_t*>(*srcBuf.buffer);
        StreamBuffer destBuf;
        destBuf.streamId = kGenericStreamId;
        destBuf.width    = srcBuf.stream->width;
        destBuf.height   = srcBuf.stream->height;
        destBuf.format   = privBuffer->format; // Use real private format
        destBuf.stride   = srcBuf.stream->width; // TODO: query from gralloc
        destBuf.buffer   = srcBuf.buffer;

        // Wait on fence
        sp<Fence> bufferAcquireFence = new Fence(srcBuf.acquire_fence);
        res = bufferAcquireFence->wait(kFenceTimeoutMs);
        if (res == TIMED_OUT) {
            ALOGE("%s: Request %d: Buffer %d: Fence timed out after %d ms",
                    __FUNCTION__, frameNumber, i, kFenceTimeoutMs);
        }
        if (res == OK) {
            // Lock buffer for writing
            const Rect rect(destBuf.width, destBuf.height);
            res = GraphicBufferMapper::get().lock(*(destBuf.buffer),
                    GRALLOC_USAGE_HW_CAMERA_WRITE, rect, (void**)&(destBuf.img));
            if (res != OK) {
                ALOGE("%s: Request %d: Buffer %d: Unable to lock buffer",
                        __FUNCTION__, frameNumber, i);
            }
        }

        if (res != OK) {
            // Either waiting or locking failed. Unlock locked buffers and bail out.
            for (size_t j = 0; j < i; j++) {
                GraphicBufferMapper::get().unlock(
                        *(request->output_buffers[i].buffer));
            }
            return NO_INIT;
        }

        sensorBuffers->push_back(destBuf);
        buffers->push_back(srcBuf);
    }

    /**
     * Wait until the in-flight queue has room
     */
    res = mReadoutThread->waitForReadout();
    if (res != OK) {
        return NO_INIT;
    }

    /**
     * Wait until sensor's ready. This waits for lengthy amounts of time with
     * mLock held, but the interface spec is that no other calls may by done to
     * the HAL by the framework while process_capture_request is happening.
     */
    int syncTimeoutCount = 0;
    while(!mSensor->waitForVSync(kSyncWaitTimeout)) {
        if (mStatus == STATUS_ERROR) {
            return NO_INIT;
        }
        if (syncTimeoutCount == kMaxSyncTimeoutCount) {
            ALOGE("%s: Request %d: Sensor sync timed out after %lld ms",
                    __FUNCTION__, frameNumber,
                    kSyncWaitTimeout * kMaxSyncTimeoutCount / 1000000);
            return NO_INIT;
        }
        syncTimeoutCount++;
    }

    /**
     * Configure sensor and queue up the request to the readout thread
     */
    mSensor->setExposureTime(exposureTime);
    mSensor->setFrameDuration(frameDuration);
    mSensor->setSensitivity(sensitivity);
    mSensor->setDestinationBuffers(sensorBuffers);

    ReadoutThread::Request r;
    r.frameNumber = request->frame_number;
    r.settings = settings;
    r.sensorBuffers = sensorBuffers;
    r.buffers = buffers;

    mReadoutThread->queueCaptureRequest(r);

    // Cache the settings for next time
    mPrevSettings.acquire(settings);

    return OK;
}

/** Debug methods */

void EmulatedFakeCamera3::dump(int fd) {

}

/** Tag query methods */
const char* EmulatedFakeCamera3::getVendorSectionName(uint32_t tag) {
    return NULL;
}

const char* EmulatedFakeCamera3::getVendorTagName(uint32_t tag) {
    return NULL;
}

int EmulatedFakeCamera3::getVendorTagType(uint32_t tag) {
    return 0;
}

/**
 * Private methods
 */

status_t EmulatedFakeCamera3::constructStaticInfo() {

    CameraMetadata info;
    // android.lens

    // 5 cm min focus distance for back camera, infinity (fixed focus) for front
    const float minFocusDistance = mFacingBack ? 1.0/0.05 : 0.0;
    info.update(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
            &minFocusDistance, 1);

    // 5 m hyperfocal distance for back camera, infinity (fixed focus) for front
    const float hyperFocalDistance = mFacingBack ? 1.0/5.0 : 0.0;
    info.update(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
            &minFocusDistance, 1);

    static const float focalLength = 3.30f; // mm
    info.update(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
            &focalLength, 1);
    static const float aperture = 2.8f;
    info.update(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
            &aperture, 1);
    static const float filterDensity = 0;
    info.update(ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES,
            &filterDensity, 1);
    static const uint8_t availableOpticalStabilization =
            ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
    info.update(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
            &availableOpticalStabilization, 1);

    static const int32_t lensShadingMapSize[] = {1, 1};
    info.update(ANDROID_LENS_INFO_SHADING_MAP_SIZE, lensShadingMapSize,
            sizeof(lensShadingMapSize)/sizeof(int32_t));

    static const float lensShadingMap[3 * 1 * 1 ] =
            { 1.f, 1.f, 1.f };
    info.update(ANDROID_LENS_INFO_SHADING_MAP, lensShadingMap,
            sizeof(lensShadingMap)/sizeof(float));

    // Identity transform
    static const int32_t geometricCorrectionMapSize[] = {2, 2};
    info.update(ANDROID_LENS_INFO_GEOMETRIC_CORRECTION_MAP_SIZE,
            geometricCorrectionMapSize,
            sizeof(geometricCorrectionMapSize)/sizeof(int32_t));

    static const float geometricCorrectionMap[2 * 3 * 2 * 2] = {
            0.f, 0.f,  0.f, 0.f,  0.f, 0.f,
            1.f, 0.f,  1.f, 0.f,  1.f, 0.f,
            0.f, 1.f,  0.f, 1.f,  0.f, 1.f,
            1.f, 1.f,  1.f, 1.f,  1.f, 1.f};
    info.update(ANDROID_LENS_INFO_GEOMETRIC_CORRECTION_MAP,
            geometricCorrectionMap,
            sizeof(geometricCorrectionMap)/sizeof(float));

    uint8_t lensFacing = mFacingBack ?
            ANDROID_LENS_FACING_BACK : ANDROID_LENS_FACING_FRONT;
    info.update(ANDROID_LENS_FACING, &lensFacing, 1);

    float lensPosition[3];
    if (mFacingBack) {
        // Back-facing camera is center-top on device
        lensPosition[0] = 0;
        lensPosition[1] = 20;
        lensPosition[2] = -5;
    } else {
        // Front-facing camera is center-right on device
        lensPosition[0] = 20;
        lensPosition[1] = 20;
        lensPosition[2] = 0;
    }
    info.update(ANDROID_LENS_POSITION, lensPosition, sizeof(lensPosition)/
            sizeof(float));

    // android.sensor

    info.update(ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE,
            Sensor::kExposureTimeRange, 2);

    info.update(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
            &Sensor::kFrameDurationRange[1], 1);

    info.update(ANDROID_SENSOR_INFO_AVAILABLE_SENSITIVITIES,
            (int32_t*)Sensor::kAvailableSensitivities,
            sizeof(Sensor::kAvailableSensitivities)
            /sizeof(uint32_t));

    info.update(ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT,
            &Sensor::kColorFilterArrangement, 1);

    static const float sensorPhysicalSize[2] = {3.20f, 2.40f}; // mm
    info.update(ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
            sensorPhysicalSize, 2);

    info.update(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
            (int32_t*)Sensor::kResolution, 2);

    info.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
            (int32_t*)Sensor::kResolution, 2);

    info.update(ANDROID_SENSOR_INFO_WHITE_LEVEL,
            (int32_t*)&Sensor::kMaxRawValue, 1);

    static const int32_t blackLevelPattern[4] = {
            (int32_t)Sensor::kBlackLevel, (int32_t)Sensor::kBlackLevel,
            (int32_t)Sensor::kBlackLevel, (int32_t)Sensor::kBlackLevel
    };
    info.update(ANDROID_SENSOR_BLACK_LEVEL_PATTERN,
            blackLevelPattern, sizeof(blackLevelPattern)/sizeof(int32_t));

    //TODO: sensor color calibration fields

    // android.flash
    static const uint8_t flashAvailable = 0;
    info.update(ANDROID_FLASH_INFO_AVAILABLE, &flashAvailable, 1);

    static const int64_t flashChargeDuration = 0;
    info.update(ANDROID_FLASH_INFO_CHARGE_DURATION, &flashChargeDuration, 1);

    // android.tonemap

    static const int32_t tonemapCurvePoints = 128;
    info.update(ANDROID_TONEMAP_MAX_CURVE_POINTS, &tonemapCurvePoints, 1);

    // android.scaler

    info.update(ANDROID_SCALER_AVAILABLE_FORMATS,
            (int32_t*)kAvailableFormats,
            sizeof(kAvailableFormats)/sizeof(uint32_t));

    info.update(ANDROID_SCALER_AVAILABLE_RAW_SIZES,
            (int32_t*)kAvailableRawSizes,
            sizeof(kAvailableRawSizes)/sizeof(uint32_t));

    info.update(ANDROID_SCALER_AVAILABLE_RAW_MIN_DURATIONS,
            (int64_t*)kAvailableRawMinDurations,
            sizeof(kAvailableRawMinDurations)/sizeof(uint64_t));

    if (mFacingBack) {
        info.update(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
                (int32_t*)kAvailableProcessedSizesBack,
                sizeof(kAvailableProcessedSizesBack)/sizeof(uint32_t));
    } else {
        info.update(ANDROID_SCALER_AVAILABLE_PROCESSED_SIZES,
                (int32_t*)kAvailableProcessedSizesFront,
                sizeof(kAvailableProcessedSizesFront)/sizeof(uint32_t));
    }

    info.update(ANDROID_SCALER_AVAILABLE_PROCESSED_MIN_DURATIONS,
            (int64_t*)kAvailableProcessedMinDurations,
            sizeof(kAvailableProcessedMinDurations)/sizeof(uint64_t));

    if (mFacingBack) {
        info.update(ANDROID_SCALER_AVAILABLE_JPEG_SIZES,
                (int32_t*)kAvailableJpegSizesBack,
                sizeof(kAvailableJpegSizesBack)/sizeof(uint32_t));
    } else {
        info.update(ANDROID_SCALER_AVAILABLE_JPEG_SIZES,
                (int32_t*)kAvailableJpegSizesFront,
                sizeof(kAvailableJpegSizesFront)/sizeof(uint32_t));
    }

    info.update(ANDROID_SCALER_AVAILABLE_JPEG_MIN_DURATIONS,
            (int64_t*)kAvailableJpegMinDurations,
            sizeof(kAvailableJpegMinDurations)/sizeof(uint64_t));

    static const int32_t maxZoom = 10;
    info.update(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
            &maxZoom, 1);

    // android.jpeg

    static const int32_t jpegThumbnailSizes[] = {
            0, 0,
            160, 120,
            320, 240
     };
    info.update(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
            jpegThumbnailSizes, sizeof(jpegThumbnailSizes)/sizeof(int32_t));

    static const int32_t jpegMaxSize = JpegCompressor::kMaxJpegSize;
    info.update(ANDROID_JPEG_MAX_SIZE, &jpegMaxSize, 1);

    // android.stats

    static const uint8_t availableFaceDetectModes[] = {
        ANDROID_STATISTICS_FACE_DETECT_MODE_OFF,
        ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE,
        ANDROID_STATISTICS_FACE_DETECT_MODE_FULL
    };

    info.update(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
            availableFaceDetectModes,
            sizeof(availableFaceDetectModes));

    static const int32_t maxFaceCount = 8;
    info.update(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
            &maxFaceCount, 1);

    static const int32_t histogramSize = 64;
    info.update(ANDROID_STATISTICS_INFO_HISTOGRAM_BUCKET_COUNT,
            &histogramSize, 1);

    static const int32_t maxHistogramCount = 1000;
    info.update(ANDROID_STATISTICS_INFO_MAX_HISTOGRAM_COUNT,
            &maxHistogramCount, 1);

    static const int32_t sharpnessMapSize[2] = {64, 64};
    info.update(ANDROID_STATISTICS_INFO_SHARPNESS_MAP_SIZE,
            sharpnessMapSize, sizeof(sharpnessMapSize)/sizeof(int32_t));

    static const int32_t maxSharpnessMapValue = 1000;
    info.update(ANDROID_STATISTICS_INFO_MAX_SHARPNESS_MAP_VALUE,
            &maxSharpnessMapValue, 1);

    // android.control

    static const uint8_t availableSceneModes[] = {
            ANDROID_CONTROL_SCENE_MODE_UNSUPPORTED
    };
    info.update(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
            availableSceneModes, sizeof(availableSceneModes));

    static const uint8_t availableEffects[] = {
            ANDROID_CONTROL_EFFECT_MODE_OFF
    };
    info.update(ANDROID_CONTROL_AVAILABLE_EFFECTS,
            availableEffects, sizeof(availableEffects));

    int32_t max3aRegions = 0;
    info.update(ANDROID_CONTROL_MAX_REGIONS,
            &max3aRegions, 1);

    static const uint8_t availableAeModes[] = {
            ANDROID_CONTROL_AE_MODE_OFF,
            ANDROID_CONTROL_AE_MODE_ON
    };
    info.update(ANDROID_CONTROL_AE_AVAILABLE_MODES,
            availableAeModes, sizeof(availableAeModes));

    static const camera_metadata_rational exposureCompensationStep = {
            1, 3
    };
    info.update(ANDROID_CONTROL_AE_COMPENSATION_STEP,
            &exposureCompensationStep, 1);

    int32_t exposureCompensationRange[] = {-9, 9};
    info.update(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
            exposureCompensationRange,
            sizeof(exposureCompensationRange)/sizeof(int32_t));

    static const int32_t availableTargetFpsRanges[] = {
            5, 30, 15, 30
    };
    info.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
            availableTargetFpsRanges,
            sizeof(availableTargetFpsRanges)/sizeof(int32_t));

    static const uint8_t availableAntibandingModes[] = {
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF,
            ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO
    };
    info.update(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
            availableAntibandingModes, sizeof(availableAntibandingModes));

    static const uint8_t availableAwbModes[] = {
            ANDROID_CONTROL_AWB_MODE_OFF,
            ANDROID_CONTROL_AWB_MODE_AUTO,
            ANDROID_CONTROL_AWB_MODE_INCANDESCENT,
            ANDROID_CONTROL_AWB_MODE_FLUORESCENT,
            ANDROID_CONTROL_AWB_MODE_DAYLIGHT,
            ANDROID_CONTROL_AWB_MODE_SHADE
    };
    info.update(ANDROID_CONTROL_AWB_AVAILABLE_MODES,
            availableAwbModes, sizeof(availableAwbModes));

    static const uint8_t availableAfModesBack[] = {
            ANDROID_CONTROL_AF_MODE_OFF,
            ANDROID_CONTROL_AF_MODE_AUTO,
            ANDROID_CONTROL_AF_MODE_MACRO,
            ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO,
            ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE
    };

    static const uint8_t availableAfModesFront[] = {
            ANDROID_CONTROL_AF_MODE_OFF
    };

    if (mFacingBack) {
        info.update(ANDROID_CONTROL_AF_AVAILABLE_MODES,
                    availableAfModesBack, sizeof(availableAfModesBack));
    } else {
        info.update(ANDROID_CONTROL_AF_AVAILABLE_MODES,
                    availableAfModesFront, sizeof(availableAfModesFront));
    }

    static const uint8_t availableVstabModes[] = {
            ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF
    };
    info.update(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
            availableVstabModes, sizeof(availableVstabModes));

    mCameraInfo = info.release();

    return OK;
}

status_t EmulatedFakeCamera3::process3A(CameraMetadata &settings) {
    /**
     * Extract top-level 3A controls
     */
    status_t res;

    bool facePriority = false;

    camera_metadata_entry e;

    e = settings.find(ANDROID_CONTROL_MODE);
    if (e.count == 0) {
        ALOGE("%s: No control mode entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    uint8_t controlMode = e.data.u8[0];

    e = settings.find(ANDROID_CONTROL_SCENE_MODE);
    if (e.count == 0) {
        ALOGE("%s: No scene mode entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    uint8_t sceneMode = e.data.u8[0];

    if (controlMode == ANDROID_CONTROL_MODE_OFF) {
        mAeState  = ANDROID_CONTROL_AE_STATE_INACTIVE;
        mAfState  = ANDROID_CONTROL_AF_STATE_INACTIVE;
        mAwbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
        update3A(settings);
        return OK;
    } else if (controlMode == ANDROID_CONTROL_MODE_USE_SCENE_MODE) {
        switch(sceneMode) {
            case ANDROID_CONTROL_SCENE_MODE_FACE_PRIORITY:
                mFacePriority = true;
                break;
            default:
                ALOGE("%s: Emulator doesn't support scene mode %d",
                        __FUNCTION__, sceneMode);
                return BAD_VALUE;
        }
    } else {
        mFacePriority = false;
    }

    // controlMode == AUTO or sceneMode = FACE_PRIORITY
    // Process individual 3A controls

    res = doFakeAE(settings);
    if (res != OK) return res;

    res = doFakeAF(settings);
    if (res != OK) return res;

    res = doFakeAWB(settings);
    if (res != OK) return res;

    update3A(settings);
    return OK;
}

status_t EmulatedFakeCamera3::doFakeAE(CameraMetadata &settings) {
    camera_metadata_entry e;

    e = settings.find(ANDROID_CONTROL_AE_MODE);
    if (e.count == 0) {
        ALOGE("%s: No AE mode entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    uint8_t aeMode = e.data.u8[0];

    switch (aeMode) {
        case ANDROID_CONTROL_AE_MODE_OFF:
            // AE is OFF
            mAeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
            return OK;
        case ANDROID_CONTROL_AE_MODE_ON:
            // OK for AUTO modes
            break;
        default:
            ALOGE("%s: Emulator doesn't support AE mode %d",
                    __FUNCTION__, aeMode);
            return BAD_VALUE;
    }

    e = settings.find(ANDROID_CONTROL_AE_LOCK);
    if (e.count == 0) {
        ALOGE("%s: No AE lock entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    bool aeLocked = (e.data.u8[0] == ANDROID_CONTROL_AE_LOCK_ON);

    e = settings.find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER);
    bool precaptureTrigger = false;
    if (e.count != 0) {
        precaptureTrigger =
                (e.data.u8[0] == ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START);
    }

    if (precaptureTrigger) {
        ALOGV("%s: Pre capture trigger = %d", __FUNCTION__, precaptureTrigger);
    } else if (e.count > 0) {
        ALOGV("%s: Pre capture trigger was present? %d",
              __FUNCTION__,
              e.count);
    }

    // If we have an aePrecaptureTrigger, aePrecaptureId should be set too
    if (e.count != 0) {
        e = settings.find(ANDROID_CONTROL_AE_PRECAPTURE_ID);

        if (e.count == 0) {
            ALOGE("%s: When android.control.aePrecaptureTrigger is set "
                  " in the request, aePrecaptureId needs to be set as well",
                  __FUNCTION__);
            return BAD_VALUE;
        }

        mAeTriggerId = e.data.i32[0];
    }

    if (precaptureTrigger || mAeState == ANDROID_CONTROL_AE_STATE_PRECAPTURE) {
        // Run precapture sequence
        if (mAeState != ANDROID_CONTROL_AE_STATE_PRECAPTURE) {
            mAeCounter = 0;
        }

        if (mFacePriority) {
            mAeTargetExposureTime = kFacePriorityExposureTime;
        } else {
            mAeTargetExposureTime = kNormalExposureTime;
        }

        if (mAeCounter > kPrecaptureMinFrames &&
                (mAeTargetExposureTime - mAeCurrentExposureTime) <
                mAeTargetExposureTime / 10) {
            // Done with precapture
            mAeCounter = 0;
            mAeState = aeLocked ? ANDROID_CONTROL_AE_STATE_LOCKED :
                    ANDROID_CONTROL_AE_STATE_CONVERGED;
        } else {
            // Converge some more
            mAeCurrentExposureTime +=
                    (mAeTargetExposureTime - mAeCurrentExposureTime) *
                    kExposureTrackRate;
            mAeCounter++;
            mAeState = ANDROID_CONTROL_AE_STATE_PRECAPTURE;
        }

    } else if (!aeLocked) {
        // Run standard occasional AE scan
        switch (mAeState) {
            case ANDROID_CONTROL_AE_STATE_CONVERGED:
            case ANDROID_CONTROL_AE_STATE_INACTIVE:
                mAeCounter++;
                if (mAeCounter > kStableAeMaxFrames) {
                    mAeTargetExposureTime =
                            mFacePriority ? kFacePriorityExposureTime :
                            kNormalExposureTime;
                    float exposureStep = ((double)rand() / RAND_MAX) *
                            (kExposureWanderMax - kExposureWanderMin) +
                            kExposureWanderMin;
                    mAeTargetExposureTime *= std::pow(2, exposureStep);
                    mAeState = ANDROID_CONTROL_AE_STATE_SEARCHING;
                }
                break;
            case ANDROID_CONTROL_AE_STATE_SEARCHING:
                mAeCurrentExposureTime +=
                        (mAeTargetExposureTime - mAeCurrentExposureTime) *
                        kExposureTrackRate;
                if (abs(mAeTargetExposureTime - mAeCurrentExposureTime) <
                        mAeTargetExposureTime / 10) {
                    // Close enough
                    mAeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
                    mAeCounter = 0;
                }
                break;
            case ANDROID_CONTROL_AE_STATE_LOCKED:
                mAeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
                mAeCounter = 0;
                break;
            default:
                ALOGE("%s: Emulator in unexpected AE state %d",
                        __FUNCTION__, mAeState);
                return INVALID_OPERATION;
        }
    } else {
        // AE is locked
        mAeState = ANDROID_CONTROL_AE_STATE_LOCKED;
    }

    return OK;
}

status_t EmulatedFakeCamera3::doFakeAF(CameraMetadata &settings) {
    camera_metadata_entry e;

    e = settings.find(ANDROID_CONTROL_AF_MODE);
    if (e.count == 0) {
        ALOGE("%s: No AF mode entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    uint8_t afMode = e.data.u8[0];

    e = settings.find(ANDROID_CONTROL_AF_TRIGGER);
    typedef camera_metadata_enum_android_control_af_trigger af_trigger_t;
    af_trigger_t afTrigger;
    // If we have an afTrigger, afTriggerId should be set too
    if (e.count != 0) {
        afTrigger = static_cast<af_trigger_t>(e.data.u8[0]);

        e = settings.find(ANDROID_CONTROL_AF_TRIGGER_ID);

        if (e.count == 0) {
            ALOGE("%s: When android.control.afTrigger is set "
                  " in the request, afTriggerId needs to be set as well",
                  __FUNCTION__);
            return BAD_VALUE;
        }

        mAfTriggerId = e.data.i32[0];
    } else {
        afTrigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
    }

    // TODO: implement AF triggering semantic

    switch (afMode) {
        case ANDROID_CONTROL_AF_MODE_OFF:
            mAfState = ANDROID_CONTROL_AF_STATE_INACTIVE;
            return OK;
        case ANDROID_CONTROL_AF_MODE_AUTO:
        case ANDROID_CONTROL_AF_MODE_MACRO:
        case ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO:
        case ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE:
            if (!mFacingBack) {
                ALOGE("%s: Front camera doesn't support AF mode %d",
                        __FUNCTION__, afMode);
                return BAD_VALUE;
            }
            // OK
            break;
        default:
            ALOGE("%s: Emulator doesn't support AF mode %d",
                    __FUNCTION__, afMode);
            return BAD_VALUE;
    }


    return OK;
}

status_t EmulatedFakeCamera3::doFakeAWB(CameraMetadata &settings) {
    camera_metadata_entry e;

    e = settings.find(ANDROID_CONTROL_AWB_MODE);
    if (e.count == 0) {
        ALOGE("%s: No AWB mode entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    uint8_t awbMode = e.data.u8[0];

    // TODO: Add white balance simulation

    switch (awbMode) {
        case ANDROID_CONTROL_AWB_MODE_OFF:
            mAwbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
            return OK;
        case ANDROID_CONTROL_AWB_MODE_AUTO:
        case ANDROID_CONTROL_AWB_MODE_INCANDESCENT:
        case ANDROID_CONTROL_AWB_MODE_FLUORESCENT:
        case ANDROID_CONTROL_AWB_MODE_DAYLIGHT:
        case ANDROID_CONTROL_AWB_MODE_SHADE:
            // OK
            break;
        default:
            ALOGE("%s: Emulator doesn't support AWB mode %d",
                    __FUNCTION__, awbMode);
            return BAD_VALUE;
    }

    return OK;
}


void EmulatedFakeCamera3::update3A(CameraMetadata &settings) {
    if (mAeState != ANDROID_CONTROL_AE_STATE_INACTIVE) {
        settings.update(ANDROID_SENSOR_EXPOSURE_TIME,
                &mAeCurrentExposureTime, 1);
        settings.update(ANDROID_SENSOR_SENSITIVITY,
                &mAeCurrentSensitivity, 1);
    }

    settings.update(ANDROID_CONTROL_AE_STATE,
            &mAeState, 1);
    settings.update(ANDROID_CONTROL_AF_STATE,
            &mAfState, 1);
    settings.update(ANDROID_CONTROL_AWB_STATE,
            &mAwbState, 1);
    /**
     * TODO: Trigger IDs need a think-through
     */
    settings.update(ANDROID_CONTROL_AE_PRECAPTURE_ID,
            &mAeTriggerId, 1);
    settings.update(ANDROID_CONTROL_AF_TRIGGER_ID,
            &mAfTriggerId, 1);
}

void EmulatedFakeCamera3::signalReadoutIdle() {
    Mutex::Autolock l(mLock);
    // Need to chek isIdle again because waiting on mLock may have allowed
    // something to be placed in the in-flight queue.
    if (mStatus == STATUS_ACTIVE && mReadoutThread->isIdle()) {
        ALOGV("Now idle");
        mStatus = STATUS_READY;
    }
}

EmulatedFakeCamera3::ReadoutThread::ReadoutThread(EmulatedFakeCamera3 *parent) :
        mParent(parent) {
}

EmulatedFakeCamera3::ReadoutThread::~ReadoutThread() {
    for (List<Request>::iterator i = mInFlightQueue.begin();
         i != mInFlightQueue.end(); i++) {
        delete i->buffers;
        delete i->sensorBuffers;
    }
}

void EmulatedFakeCamera3::ReadoutThread::queueCaptureRequest(const Request &r) {
    Mutex::Autolock l(mLock);

    mInFlightQueue.push_back(r);
    mInFlightSignal.signal();
}

bool EmulatedFakeCamera3::ReadoutThread::isIdle() {
    Mutex::Autolock l(mLock);
    return mInFlightQueue.empty() && !mThreadActive;
}

status_t EmulatedFakeCamera3::ReadoutThread::waitForReadout() {
    status_t res;
    Mutex::Autolock l(mLock);
    int loopCount = 0;
    while (mInFlightQueue.size() >= kMaxQueueSize) {
        res = mInFlightSignal.waitRelative(mLock, kWaitPerLoop);
        if (res != OK && res != TIMED_OUT) {
            ALOGE("%s: Error waiting for in-flight queue to shrink",
                    __FUNCTION__);
            return INVALID_OPERATION;
        }
        if (loopCount == kMaxWaitLoops) {
            ALOGE("%s: Timed out waiting for in-flight queue to shrink",
                    __FUNCTION__);
            return TIMED_OUT;
        }
        loopCount++;
    }
    return OK;
}

bool EmulatedFakeCamera3::ReadoutThread::threadLoop() {
    status_t res;

    // First wait for a request from the in-flight queue

    if (mCurrentRequest.settings.isEmpty()) {
        Mutex::Autolock l(mLock);
        if (mInFlightQueue.empty()) {
            res = mInFlightSignal.waitRelative(mLock, kWaitPerLoop);
            if (res == TIMED_OUT) {
                return true;
            } else if (res != NO_ERROR) {
                ALOGE("%s: Error waiting for capture requests: %d",
                        __FUNCTION__, res);
                return false;
            }
        }
        mCurrentRequest.frameNumber = mInFlightQueue.begin()->frameNumber;
        mCurrentRequest.settings.acquire(mInFlightQueue.begin()->settings);
        mCurrentRequest.buffers = mInFlightQueue.begin()->buffers;
        mCurrentRequest.sensorBuffers = mInFlightQueue.begin()->sensorBuffers;
        mInFlightQueue.erase(mInFlightQueue.begin());
        mInFlightSignal.signal();
        mThreadActive = true;
    }

    // Then wait for it to be delivered from the sensor

    nsecs_t captureTime;
    bool gotFrame =
            mParent->mSensor->waitForNewFrame(kWaitPerLoop, &captureTime);
    if (!gotFrame) return true;

    // Check if we need to JPEG encode a buffer

    for (size_t i = 0; i < mCurrentRequest.buffers->size(); i++) {
        if ((*mCurrentRequest.buffers)[i].stream->format ==
                HAL_PIXEL_FORMAT_BLOB) {
            res = mParent->mJpegCompressor->
                compressSynchronous(mCurrentRequest.sensorBuffers);
            if (res != OK) {
                ALOGE("%s: Error compressing output buffer: %s (%d)",
                        __FUNCTION__, strerror(-res), res);
            }
        }
    }

    // Got everything, construct result

    camera3_capture_result result;

    mCurrentRequest.settings.update(ANDROID_SENSOR_TIMESTAMP,
            &captureTime, 1);

    for (size_t i = 0; i < mCurrentRequest.buffers->size(); i++) {
        camera3_stream_buffer &buf = mCurrentRequest.buffers->editItemAt(i);

        GraphicBufferMapper::get().unlock(*buf.buffer);

        buf.status = CAMERA3_BUFFER_STATUS_OK;
        buf.acquire_fence = -1;
        buf.release_fence = -1;
    }

    result.frame_number = mCurrentRequest.frameNumber;
    result.result = mCurrentRequest.settings.getAndLock();
    result.num_output_buffers = mCurrentRequest.buffers->size();
    result.output_buffers = mCurrentRequest.buffers->array();

    // Go idle if queue is empty, before sending result
    bool signalIdle = false;
    {
        Mutex::Autolock l(mLock);
        if (mInFlightQueue.empty()) {
            mThreadActive = false;
            signalIdle = true;
        }
    }
    if (signalIdle) mParent->signalReadoutIdle();

    // Send it off to the framework

    mParent->sendCaptureResult(&result);

    // Clean up
    mCurrentRequest.settings.unlock(result.result);

    delete mCurrentRequest.buffers;
    mCurrentRequest.buffers = NULL;
    delete mCurrentRequest.sensorBuffers;
    mCurrentRequest.sensorBuffers = NULL;
    mCurrentRequest.settings.clear();

    return true;
}


}; // namespace android
