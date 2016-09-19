/*
 * Copyright (C) 2011 The Android Open Source Project
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
 * Contains implementation of an abstract class EmulatedCameraDevice that defines
 * functionality expected from an emulated physical camera device:
 *  - Obtaining and setting camera parameters
 *  - Capturing frames
 *  - Streaming video
 *  - etc.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_Device"
#include <cutils/log.h>
#include <sys/select.h>
#include <cmath>
#include "Alignment.h"
#include "EmulatedCamera.h"
#include "EmulatedCameraDevice.h"

#undef min
#undef max
#include <algorithm>

namespace android {

const float GAMMA_CORRECTION = 2.2f;
EmulatedCameraDevice::EmulatedCameraDevice(EmulatedCamera* camera_hal)
    : mObjectLock(),
      mCameraHAL(camera_hal),
      mExposureCompensation(1.0f),
      mWhiteBalanceScale(NULL),
      mSupportedWhiteBalanceScale(),
      mState(ECDS_CONSTRUCTED),
      mTriggerAutoFocus(false)
{
}

EmulatedCameraDevice::~EmulatedCameraDevice()
{
    ALOGV("EmulatedCameraDevice destructor");
    for (size_t i = 0; i < mSupportedWhiteBalanceScale.size(); ++i) {
        if (mSupportedWhiteBalanceScale.valueAt(i) != NULL) {
            delete[] mSupportedWhiteBalanceScale.valueAt(i);
        }
    }
}

/****************************************************************************
 * Emulated camera device public API
 ***************************************************************************/

status_t EmulatedCameraDevice::Initialize()
{
    if (isInitialized()) {
        ALOGW("%s: Emulated camera device is already initialized: mState = %d",
             __FUNCTION__, mState);
        return NO_ERROR;
    }

    mState = ECDS_INITIALIZED;

    return NO_ERROR;
}

status_t EmulatedCameraDevice::startDeliveringFrames(bool one_burst)
{
    ALOGV("%s", __FUNCTION__);

    if (!isStarted()) {
        ALOGE("%s: Device is not started", __FUNCTION__);
        return EINVAL;
    }

    /* Frames will be delivered from the thread routine. */
    const status_t res = startWorkerThreads(one_burst);
    ALOGE_IF(res != NO_ERROR, "%s: startWorkerThreads failed", __FUNCTION__);
    return res;
}

status_t EmulatedCameraDevice::stopDeliveringFrames()
{
    ALOGV("%s", __FUNCTION__);

    if (!isStarted()) {
        ALOGW("%s: Device is not started", __FUNCTION__);
        return NO_ERROR;
    }

    const status_t res = stopWorkerThreads();
    ALOGE_IF(res != NO_ERROR, "%s: stopWorkerThreads failed", __FUNCTION__);
    return res;
}

status_t EmulatedCameraDevice::setPreviewFrameRate(int framesPerSecond) {
    if (framesPerSecond <= 0) {
        return EINVAL;
    }
    mFramesPerSecond = framesPerSecond;
    return NO_ERROR;
}

void EmulatedCameraDevice::setExposureCompensation(const float ev) {
    ALOGV("%s", __FUNCTION__);

    if (!isStarted()) {
        ALOGW("%s: Fake camera device is not started.", __FUNCTION__);
    }

    mExposureCompensation = std::pow(2.0f, ev / GAMMA_CORRECTION);
    ALOGV("New exposure compensation is %f", mExposureCompensation);
}

void EmulatedCameraDevice::initializeWhiteBalanceModes(const char* mode,
                                                       const float r_scale,
                                                       const float b_scale) {
    ALOGV("%s with %s, %f, %f", __FUNCTION__, mode, r_scale, b_scale);
    float* value = new float[3];
    value[0] = r_scale; value[1] = 1.0f; value[2] = b_scale;
    mSupportedWhiteBalanceScale.add(String8(mode), value);
}

void EmulatedCameraDevice::setWhiteBalanceMode(const char* mode) {
    ALOGV("%s with white balance %s", __FUNCTION__, mode);
    mWhiteBalanceScale =
            mSupportedWhiteBalanceScale.valueFor(String8(mode));
}

/* Computes the pixel value after adjusting the white balance to the current
 * one. The input the y, u, v channel of the pixel and the adjusted value will
 * be stored in place. The adjustment is done in RGB space.
 */
void EmulatedCameraDevice::changeWhiteBalance(uint8_t& y,
                                              uint8_t& u,
                                              uint8_t& v) const {
    float r_scale = mWhiteBalanceScale[0];
    float b_scale = mWhiteBalanceScale[2];
    int r = static_cast<float>(YUV2R(y, u, v)) / r_scale;
    int g = YUV2G(y, u, v);
    int b = static_cast<float>(YUV2B(y, u, v)) / b_scale;

    y = RGB2Y(r, g, b);
    u = RGB2U(r, g, b);
    v = RGB2V(r, g, b);
}

void EmulatedCameraDevice::checkAutoFocusTrigger() {
    // The expected value is a reference so we need it to be a variable
    bool expectedTrigger = true;
    if (mTriggerAutoFocus.compare_exchange_strong(expectedTrigger, false)) {
        // If the compare exchange returns true then the value was the expected
        // 'true' and was successfully set to 'false'. So that means it's time
        // to trigger an auto-focus event and that we have disabled that trigger
        // so it won't happen until another request is received.
        mCameraHAL->autoFocusComplete();
    }
}

status_t EmulatedCameraDevice::getCurrentFrame(void* buffer)
{
    if (!isStarted()) {
        ALOGE("%s: Device is not started", __FUNCTION__);
        return EINVAL;
    }
    if (buffer == nullptr) {
        ALOGE("%s: Invalid buffer provided", __FUNCTION__);
        return EINVAL;
    }

    FrameLock lock(*this);
    const void* source = mFrameProducer->getPrimaryBuffer();
    if (source == nullptr) {
        ALOGE("%s: No framebuffer", __FUNCTION__);
        return EINVAL;
    }
    memcpy(buffer, source, mFrameBufferSize);
    return NO_ERROR;
}

status_t EmulatedCameraDevice::getCurrentPreviewFrame(void* buffer)
{
    if (!isStarted()) {
        ALOGE("%s: Device is not started", __FUNCTION__);
        return EINVAL;
    }
    if (buffer == nullptr) {
        ALOGE("%s: Invalid buffer provided", __FUNCTION__);
        return EINVAL;
    }

    FrameLock lock(*this);
    const void* currentFrame = mFrameProducer->getPrimaryBuffer();
    if (currentFrame == nullptr) {
        ALOGE("%s: No framebuffer", __FUNCTION__);
        return EINVAL;
    }

    /* In emulation the framebuffer is never RGB. */
    switch (mPixelFormat) {
        case V4L2_PIX_FMT_YVU420:
            YV12ToRGB32(currentFrame, buffer, mFrameWidth, mFrameHeight);
            return NO_ERROR;
        case V4L2_PIX_FMT_YUV420:
            YU12ToRGB32(currentFrame, buffer, mFrameWidth, mFrameHeight);
            return NO_ERROR;
        case V4L2_PIX_FMT_NV21:
            NV21ToRGB32(currentFrame, buffer, mFrameWidth, mFrameHeight);
            return NO_ERROR;
        case V4L2_PIX_FMT_NV12:
            NV12ToRGB32(currentFrame, buffer, mFrameWidth, mFrameHeight);
            return NO_ERROR;

        default:
            ALOGE("%s: Unknown pixel format %.4s",
                 __FUNCTION__, reinterpret_cast<const char*>(&mPixelFormat));
            return EINVAL;
    }
}

const void* EmulatedCameraDevice::getCurrentFrame() {
    if (mFrameProducer.get()) {
        return mFrameProducer->getPrimaryBuffer();
    }
    return nullptr;
}

EmulatedCameraDevice::FrameLock::FrameLock(EmulatedCameraDevice& cameraDevice)
    : mCameraDevice(cameraDevice) {
        mCameraDevice.lockCurrentFrame();
}

EmulatedCameraDevice::FrameLock::~FrameLock() {
    mCameraDevice.unlockCurrentFrame();
}

status_t EmulatedCameraDevice::setAutoFocus() {
    mTriggerAutoFocus = true;
    return NO_ERROR;
}

status_t EmulatedCameraDevice::cancelAutoFocus() {
    mTriggerAutoFocus = false;
    return NO_ERROR;
}

/****************************************************************************
 * Emulated camera device private API
 ***************************************************************************/

status_t EmulatedCameraDevice::commonStartDevice(int width,
                                                 int height,
                                                 uint32_t pix_fmt)
{
    /* Validate pixel format, and calculate framebuffer size at the same time. */
    switch (pix_fmt) {
        case V4L2_PIX_FMT_YVU420:
        case V4L2_PIX_FMT_YUV420:
            // For these pixel formats the strides have to be aligned to 16 byte
            // boundaries as per the format specification
            // https://developer.android.com/reference/android/graphics/ImageFormat.html#YV12
            mYStride = align(width, 16);
            mUVStride = align(mYStride / 2, 16);
            // The second term should use half the height but since there are
            // two planes the multiplication with two cancels that out
            mFrameBufferSize = mYStride * height + mUVStride * height;
            break;
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_NV12:
            mYStride = width;
            // Because of interleaving the UV stride is the same as the Y stride
            // since it covers two pixels, one U and one V.
            mUVStride = mYStride;
            // Since the U/V stride covers both U and V we don't multiply by two
            mFrameBufferSize = mYStride * height + mUVStride * (height / 2);
            break;
        default:
            ALOGE("%s: Unknown pixel format %.4s",
                 __FUNCTION__, reinterpret_cast<const char*>(&pix_fmt));
            return EINVAL;
    }

    /* Cache framebuffer info. */
    mFrameWidth = width;
    mFrameHeight = height;
    mPixelFormat = pix_fmt;
    mTotalPixels = width * height;

    /* Allocate framebuffer. */
    mFrameBuffers[0].resize(mFrameBufferSize);
    mFrameBuffers[1].resize(mFrameBufferSize);
    ALOGV("%s: Allocated %zu bytes for %d pixels in %.4s[%dx%d] frame",
         __FUNCTION__, mFrameBufferSize, mTotalPixels,
         reinterpret_cast<const char*>(&mPixelFormat), mFrameWidth, mFrameHeight);
    return NO_ERROR;
}

void EmulatedCameraDevice::commonStopDevice()
{
    mFrameWidth = mFrameHeight = mTotalPixels = 0;
    mPixelFormat = 0;

    mFrameBuffers[0].clear();
    mFrameBuffers[1].clear();
    // No need to keep all that memory allocated if the camera isn't running
    mFrameBuffers[0].shrink_to_fit();
    mFrameBuffers[1].shrink_to_fit();
}

/****************************************************************************
 * Worker thread management.
 ***************************************************************************/

status_t EmulatedCameraDevice::startWorkerThreads(bool one_burst)
{
    ALOGV("%s", __FUNCTION__);

    if (!isInitialized()) {
        ALOGE("%s: Emulated camera device is not initialized", __FUNCTION__);
        return EINVAL;
    }

    // First create and start a frame producer, without a producer there are no
    // frames to deliver and the deliverer will not deliver frames until one has
    // been produced.
    void* primaryBuffer = getPrimaryBuffer();
    void* secondaryBuffer = getSecondaryBuffer();
    mFrameProducer = new FrameProducer(this, mObjectLock,
                                       staticProduceFrame, this,
                                       primaryBuffer, secondaryBuffer);
    if (mFrameProducer == NULL) {
        ALOGE("%s: Unable to instantiate FrameProducer object", __FUNCTION__);
        return ENOMEM;
    }
    status_t res = mFrameProducer->startThread(one_burst);
    if (res != NO_ERROR) {
        ALOGE("%s: Unable to start frame producer thread: %s",
              __FUNCTION__, strerror(res));
        return res;
    }

    // Then create a frame deliverer, this takes the producer as a reference to
    // be able to check if a frame has been produced yet.
    mFrameDeliverer = new FrameDeliverer(this, mObjectLock,
                                         mFrameProducer.get());
    if (mFrameDeliverer == NULL) {
        ALOGE("%s: Unable to instantiate FrameDeliverer object", __FUNCTION__);
        mFrameProducer->stopThread();
        return ENOMEM;
    }
    res = mFrameDeliverer->startThread(one_burst);
    if (res != NO_ERROR) {
        ALOGE("%s: Unable to start frame deliverer: %s",
              __FUNCTION__, strerror(res));
        mFrameProducer->stopThread();
        return res;
    }

    return res;
}

status_t EmulatedCameraDevice::stopWorkerThreads()
{
    ALOGV("%s", __FUNCTION__);

    if (!isInitialized()) {
        ALOGE("%s: Emulated camera device is not initialized", __FUNCTION__);
        return EINVAL;
    }

    // Since the deliverer holds a reference to the producer make sure we shut
    // down the deliverer first so that it won't use an invalid reference.
    status_t res = mFrameDeliverer->stopThread();
    ALOGE_IF(res != NO_ERROR, "%s: Unable to stop FrameDeliverer", __FUNCTION__);

    res = mFrameProducer->stopThread();
    ALOGE_IF(res != NO_ERROR, "%s: Unable to stop FrameProducer", __FUNCTION__);

    // Destroy the threads as well
    mFrameDeliverer.clear();
    mFrameProducer.clear();
    return res;
}

EmulatedCameraDevice::FrameDeliverer::FrameDeliverer(EmulatedCameraDevice* dev,
                                                     Mutex& cameraMutex,
                                                     FrameProducer* producer)
    : WorkerThread("Camera_FrameDeliverer", dev, cameraMutex),
      mCurFrameTimestamp(0),
      mFrameProducer(producer) {

}

bool EmulatedCameraDevice::FrameDeliverer::inWorkerThread() {
    /* Wait till FPS timeout expires, or thread exit message is received. */
    nsecs_t wakeAt =
        mCurFrameTimestamp + 1000000000.0 / mCameraDevice->mFramesPerSecond;
    nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
    nsecs_t timeout = std::max<nsecs_t>(0, wakeAt - now);

    // Keep waiting until the frame producer indicates that a frame is available
    // This does introduce some unnecessary latency to the first frame delivery
    // but avoids a lot of thread synchronization.
    do {
        // We don't have any specific fd we want to select so we pass in -1
        // timeout is in nanoseconds but Select expects microseconds
        SelectRes res = Select(-1, timeout / 1000);
        if (res == EXIT_THREAD) {
            ALOGV("%s: FrameDeliverer thread has been terminated.",
                  __FUNCTION__);
            // Reset this to true, the next time the thread is started it will
            // be considred as the first loop
            return false;
        }
        // Set a short timeout in case there is no frame available and we are
        // going to loop. This way we ensure a sleep but keep a decent latency
        timeout = milliseconds(5);
    } while (!mFrameProducer->hasFrame());

    /* Check if an auto-focus event needs to be triggered */
    mCameraDevice->checkAutoFocusTrigger();

    mCurFrameTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);
    mCameraDevice->mCameraHAL->onNextFrameAvailable(mCurFrameTimestamp,
                                                    mCameraDevice);

    return true;
}

EmulatedCameraDevice::FrameProducer::FrameProducer(EmulatedCameraDevice* dev,
                                                   Mutex& cameraMutex,
                                                   ProduceFrameFunc producer,
                                                   void* opaque,
                                                   void* primaryBuffer,
                                                   void* secondaryBuffer)
    : WorkerThread("Camera_FrameProducer", dev, cameraMutex),
      mProducer(producer),
      mOpaque(opaque),
      mPrimaryBuffer(primaryBuffer),
      mSecondaryBuffer(secondaryBuffer),
      mLastFrame(0),
      mHasFrame(false) {

}

const void* EmulatedCameraDevice::FrameProducer::getPrimaryBuffer() const {
    return mPrimaryBuffer;
}

void EmulatedCameraDevice::FrameProducer::lockPrimaryBuffer() {
    mBufferMutex.lock();
}
void EmulatedCameraDevice::FrameProducer::unlockPrimaryBuffer() {
    mBufferMutex.unlock();
}

bool EmulatedCameraDevice::FrameProducer::hasFrame() const {
    return mHasFrame;
}

bool EmulatedCameraDevice::FrameProducer::inWorkerThread() {
    nsecs_t nextFrame =
        mLastFrame + 1000000000 / mCameraDevice->mFramesPerSecond;
    nsecs_t now = systemTime(SYSTEM_TIME_MONOTONIC);
    nsecs_t timeout = std::max<nsecs_t>(0, nextFrame - now);

    SelectRes res = Select(-1, timeout / 1000);
    if (res == EXIT_THREAD) {
        ALOGV("%s: FrameProducer thread has been terminated.", __FUNCTION__);
        return false;
    }

    // Produce one frame and place it in the secondary buffer
    mLastFrame = systemTime(SYSTEM_TIME_MONOTONIC);
    if (!mProducer(mOpaque, mSecondaryBuffer)) {
        ALOGE("FrameProducer could not produce frame, exiting thread");
        return false;
    }

    {
        // Switch buffers now that the secondary buffer is ready
        Mutex::Autolock lock(mBufferMutex);
        std::swap(mPrimaryBuffer, mSecondaryBuffer);
    }
    mHasFrame = true;
    return true;
}

void EmulatedCameraDevice::lockCurrentFrame() {
    if (mFrameProducer.get()) {
        mFrameProducer->lockPrimaryBuffer();
    }
}

void EmulatedCameraDevice::unlockCurrentFrame() {
    if (mFrameProducer.get()) {
        mFrameProducer->unlockPrimaryBuffer();
    }
}

};  /* namespace android */
