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
 * Contains implementation of a class EmulatedFakeCameraDevice that encapsulates
 * fake camera device.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_FakeDevice"
#include <cutils/log.h>
#include "EmulatedFakeCamera.h"
#include "EmulatedFakeCameraDevice.h"

namespace android {

EmulatedFakeCameraDevice::EmulatedFakeCameraDevice(EmulatedFakeCamera* camera_hal)
    : EmulatedCameraDevice(camera_hal),
      mBlackYUV(kBlack32),
      mWhiteYUV(kWhite32),
      mRedYUV(kRed8),
      mGreenYUV(kGreen8),
      mBlueYUV(kBlue8),
      mLastRedrawn(0),
      mCheckX(0),
      mCheckY(0),
      mCcounter(0)
#if EFCD_ROTATE_FRAME
      , mLastRotatedAt(0),
        mCurrentFrameType(0),
        mCurrentColor(&mWhiteYUV)
#endif  // EFCD_ROTATE_FRAME
{
    // Makes the image with the original exposure compensation darker.
    // So the effects of changing the exposure compensation can be seen.
    mBlackYUV.Y = mBlackYUV.Y / 2;
    mWhiteYUV.Y = mWhiteYUV.Y / 2;
    mRedYUV.Y = mRedYUV.Y / 2;
    mGreenYUV.Y = mGreenYUV.Y / 2;
    mBlueYUV.Y = mBlueYUV.Y / 2;
}

EmulatedFakeCameraDevice::~EmulatedFakeCameraDevice()
{
}

/****************************************************************************
 * Emulated camera device abstract interface implementation.
 ***************************************************************************/

status_t EmulatedFakeCameraDevice::connectDevice()
{
    ALOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    if (!isInitialized()) {
        ALOGE("%s: Fake camera device is not initialized.", __FUNCTION__);
        return EINVAL;
    }
    if (isConnected()) {
        ALOGW("%s: Fake camera device is already connected.", __FUNCTION__);
        return NO_ERROR;
    }

    /* There is no device to connect to. */
    mState = ECDS_CONNECTED;

    return NO_ERROR;
}

status_t EmulatedFakeCameraDevice::disconnectDevice()
{
    ALOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    if (!isConnected()) {
        ALOGW("%s: Fake camera device is already disconnected.", __FUNCTION__);
        return NO_ERROR;
    }
    if (isStarted()) {
        ALOGE("%s: Cannot disconnect from the started device.", __FUNCTION__);
        return EINVAL;
    }

    /* There is no device to disconnect from. */
    mState = ECDS_INITIALIZED;

    return NO_ERROR;
}

status_t EmulatedFakeCameraDevice::startDevice(int width,
                                               int height,
                                               uint32_t pix_fmt)
{
    ALOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    if (!isConnected()) {
        ALOGE("%s: Fake camera device is not connected.", __FUNCTION__);
        return EINVAL;
    }
    if (isStarted()) {
        ALOGE("%s: Fake camera device is already started.", __FUNCTION__);
        return EINVAL;
    }

    /* Initialize the base class. */
    const status_t res =
        EmulatedCameraDevice::commonStartDevice(width, height, pix_fmt);
    if (res == NO_ERROR) {
        /* Calculate U/V panes inside the framebuffer. */
        switch (mPixelFormat) {
            case V4L2_PIX_FMT_YVU420:
                mFrameV = mCurrentFrame + mYStride * mFrameHeight;
                mFrameU = mFrameV + mUVStride * (mFrameHeight / 2);
                mUVStep = 1;
                break;

            case V4L2_PIX_FMT_YUV420:
                mFrameU = mCurrentFrame + mYStride * mFrameHeight;
                mFrameV = mFrameU + mUVStride * (mFrameHeight / 2);
                mUVStep = 1;
                break;

            case V4L2_PIX_FMT_NV21:
                /* Interleaved UV pane, V first. */
                mFrameV = mCurrentFrame + mYStride * mFrameHeight;
                mFrameU = mFrameV + 1;
                mUVStep = 2;
                break;

            case V4L2_PIX_FMT_NV12:
                /* Interleaved UV pane, U first. */
                mFrameU = mCurrentFrame + mYStride * mFrameHeight;
                mFrameV = mFrameU + 1;
                mUVStep = 2;
                break;

            default:
                ALOGE("%s: Unknown pixel format %.4s", __FUNCTION__,
                     reinterpret_cast<const char*>(&mPixelFormat));
                return EINVAL;
        }
        /* Number of items in a single row inside U/V panes. */
        mUVInRow = (width / 2) * mUVStep;
        mState = ECDS_STARTED;
    } else {
        ALOGE("%s: commonStartDevice failed", __FUNCTION__);
    }

    return res;
}

status_t EmulatedFakeCameraDevice::stopDevice()
{
    ALOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    if (!isStarted()) {
        ALOGW("%s: Fake camera device is not started.", __FUNCTION__);
        return NO_ERROR;
    }

    mFrameU = mFrameV = NULL;
    EmulatedCameraDevice::commonStopDevice();
    mState = ECDS_CONNECTED;

    return NO_ERROR;
}

/****************************************************************************
 * Worker thread management overrides.
 ***************************************************************************/

bool EmulatedFakeCameraDevice::inWorkerThread()
{
    /* Wait till FPS timeout expires, or thread exit message is received. */
    WorkerThread::SelectRes res =
        getWorkerThread()->Select(-1, 1000000 / mEmulatedFPS);
    if (res == WorkerThread::EXIT_THREAD) {
        ALOGV("%s: Worker thread has been terminated.", __FUNCTION__);
        return false;
    }

    /* Lets see if we need to generate a new frame. */
    if ((systemTime(SYSTEM_TIME_MONOTONIC) - mLastRedrawn) >= mRedrawAfter) {
        /*
         * Time to generate a new frame.
         */

#if EFCD_ROTATE_FRAME
        const int frame_type = rotateFrame();
        switch (frame_type) {
            case 0:
                drawCheckerboard();
                break;
            case 1:
                drawStripes();
                break;
            case 2:
                drawSolid(mCurrentColor);
                break;
        }
#else
        /* Draw the checker board. */
        drawCheckerboard();

#endif  // EFCD_ROTATE_FRAME

        mLastRedrawn = systemTime(SYSTEM_TIME_MONOTONIC);
    }

    /* Timestamp the current frame, and notify the camera HAL about new frame. */
    mCurFrameTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);
    mCameraHAL->onNextFrameAvailable(mCurrentFrame, mCurFrameTimestamp, this);

    return true;
}

/****************************************************************************
 * Fake camera device private API
 ***************************************************************************/

void EmulatedFakeCameraDevice::drawCheckerboard()
{
    const int size = mFrameWidth / 10;
    bool black = true;

    if (size == 0) {
        // When this happens, it happens at a very high rate,
        //     so don't log any messages and just return.
        return;
    }


    if((mCheckX / size) & 1)
        black = false;
    if((mCheckY / size) & 1)
        black = !black;

    int county = mCheckY % size;
    int checkxremainder = mCheckX % size;

    YUVPixel adjustedWhite = YUVPixel(mWhiteYUV);
    changeWhiteBalance(adjustedWhite.Y, adjustedWhite.U, adjustedWhite.V);

    for(int y = 0; y < mFrameHeight; y++) {
        int countx = checkxremainder;
        bool current = black;
        uint8_t* Y = mCurrentFrame + mYStride * y;
        uint8_t* U = mFrameU + mUVStride * (y / 2);
        uint8_t* V = mFrameV + mUVStride * (y / 2);
        for(int x = 0; x < mFrameWidth; x += 2) {
            if (current) {
                mBlackYUV.get(Y, U, V);
            } else {
                adjustedWhite.get(Y, U, V);
            }
            *Y = changeExposure(*Y);
            Y[1] = *Y;
            Y += 2; U += mUVStep; V += mUVStep;
            countx += 2;
            if(countx >= size) {
                countx = 0;
                current = !current;
            }
        }
        if(county++ >= size) {
            county = 0;
            black = !black;
        }
    }
    mCheckX += 3;
    mCheckY++;

    /* Run the square. */
    int sqx = ((mCcounter * 3) & 255);
    if(sqx > 128) sqx = 255 - sqx;
    int sqy = ((mCcounter * 5) & 255);
    if(sqy > 128) sqy = 255 - sqy;
    const int sqsize = mFrameWidth / 10;
    drawSquare(sqx * sqsize / 32, sqy * sqsize / 32, (sqsize * 5) >> 1,
               (mCcounter & 0x100) ? &mRedYUV : &mGreenYUV);
    mCcounter++;
}

void EmulatedFakeCameraDevice::drawSquare(int x,
                                          int y,
                                          int size,
                                          const YUVPixel* color)
{
    const int square_xstop = min(mFrameWidth, x + size);
    const int square_ystop = min(mFrameHeight, y + size);
    uint8_t* Y_pos = mCurrentFrame + y * mYStride + x;

    YUVPixel adjustedColor = *color;
    changeWhiteBalance(adjustedColor.Y, adjustedColor.U, adjustedColor.V);

    // Draw the square.
    for (; y < square_ystop; y++) {
        const int iUV = (y / 2) * mUVStride + (x / 2) * mUVStep;
        uint8_t* sqU = mFrameU + iUV;
        uint8_t* sqV = mFrameV + iUV;
        uint8_t* sqY = Y_pos;
        for (int i = x; i < square_xstop; i += 2) {
            adjustedColor.get(sqY, sqU, sqV);
            *sqY = changeExposure(*sqY);
            sqY[1] = *sqY;
            sqY += 2; sqU += mUVStep; sqV += mUVStep;
        }
        Y_pos += mYStride;
    }
}

#if EFCD_ROTATE_FRAME

void EmulatedFakeCameraDevice::drawSolid(YUVPixel* color)
{
    YUVPixel adjustedColor = *color;
    changeWhiteBalance(adjustedColor.Y, adjustedColor.U, adjustedColor.V);

    /* All Ys are the same, will fill any alignment padding but that's OK */
    memset(mCurrentFrame, changeExposure(adjustedColor.Y),
           mFrameHeight * mYStride);

    /* Fill U, and V panes. */
    for (int y = 0; y < mFrameHeight / 2; ++y) {
        uint8_t* U = mFrameU + y * mUVStride;
        uint8_t* V = mFrameV + y * mUVStride;

        for (int x = 0; x < mFrameWidth / 2; ++x, U += mUVStep, V += mUVStep) {
            *U = color->U;
            *V = color->V;
        }
    }
}

void EmulatedFakeCameraDevice::drawStripes()
{
    /* Divide frame into 4 stripes. */
    const int change_color_at = mFrameHeight / 4;
    const int each_in_row = mUVInRow / mUVStep;
    uint8_t* pY = mCurrentFrame;
    for (int y = 0; y < mFrameHeight; y++, pY += mYStride) {
        /* Select the color. */
        YUVPixel* color;
        const int color_index = y / change_color_at;
        if (color_index == 0) {
            /* White stripe on top. */
            color = &mWhiteYUV;
        } else if (color_index == 1) {
            /* Then the red stripe. */
            color = &mRedYUV;
        } else if (color_index == 2) {
            /* Then the green stripe. */
            color = &mGreenYUV;
        } else {
            /* And the blue stripe at the bottom. */
            color = &mBlueYUV;
        }
        changeWhiteBalance(color->Y, color->U, color->V);

        /* All Ys at the row are the same. */
        memset(pY, changeExposure(color->Y), mFrameWidth);

        /* Offset of the current row inside U/V panes. */
        const int uv_off = (y / 2) * mUVStride;
        /* Fill U, and V panes. */
        uint8_t* U = mFrameU + uv_off;
        uint8_t* V = mFrameV + uv_off;
        for (int k = 0; k < each_in_row; k++, U += mUVStep, V += mUVStep) {
            *U = color->U;
            *V = color->V;
        }
    }
}

int EmulatedFakeCameraDevice::rotateFrame()
{
    if ((systemTime(SYSTEM_TIME_MONOTONIC) - mLastRotatedAt) >= mRotateFreq) {
        mLastRotatedAt = systemTime(SYSTEM_TIME_MONOTONIC);
        mCurrentFrameType++;
        if (mCurrentFrameType > 2) {
            mCurrentFrameType = 0;
        }
        if (mCurrentFrameType == 2) {
            ALOGD("********** Rotated to the SOLID COLOR frame **********");
            /* Solid color: lets rotate color too. */
            if (mCurrentColor == &mWhiteYUV) {
                ALOGD("----- Painting a solid RED frame -----");
                mCurrentColor = &mRedYUV;
            } else if (mCurrentColor == &mRedYUV) {
                ALOGD("----- Painting a solid GREEN frame -----");
                mCurrentColor = &mGreenYUV;
            } else if (mCurrentColor == &mGreenYUV) {
                ALOGD("----- Painting a solid BLUE frame -----");
                mCurrentColor = &mBlueYUV;
            } else {
                /* Back to white. */
                ALOGD("----- Painting a solid WHITE frame -----");
                mCurrentColor = &mWhiteYUV;
            }
        } else if (mCurrentFrameType == 0) {
            ALOGD("********** Rotated to the CHECKERBOARD frame **********");
        } else if (mCurrentFrameType == 1) {
            ALOGD("********** Rotated to the STRIPED frame **********");
        }
    }

    return mCurrentFrameType;
}

#endif  // EFCD_ROTATE_FRAME

}; /* namespace android */
