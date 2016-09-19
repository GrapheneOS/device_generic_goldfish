/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "WorkerThread.h"

#define LOG_NDEBUG 0
#define LOG_TAG "EmulatedCamera_WorkerThread"
#include <cutils/log.h>

#include <algorithm>

namespace android {

WorkerThread::WorkerThread(const char* threadName,
                           EmulatedCameraDevice* camera_dev,
                           Mutex& cameraMutex)
    : Thread(true),   // Callbacks may involve Java calls.
      mCameraDevice(camera_dev),
      mThreadName(threadName),
      mThreadControl(-1),
      mControlFD(-1),
      mCameraMutex(cameraMutex) {
}

WorkerThread::~WorkerThread() {
    ALOGW_IF(mThreadControl >= 0 || mControlFD >= 0,
            "%s: Control FDs are opened in the destructor",
            __FUNCTION__);
    if (mThreadControl >= 0) {
        close(mThreadControl);
    }
    if (mControlFD >= 0) {
        close(mControlFD);
    }
}

status_t WorkerThread::startThread(bool one_burst) {
    ALOGV("Starting worker thread, one_burst=%s", one_burst ? "true" : "false");
    mOneBurst = one_burst;
    return run(mThreadName, ANDROID_PRIORITY_URGENT_DISPLAY, 0);
}

status_t WorkerThread::readyToRun()
{
    ALOGV("Starting emulated camera device worker thread...");

    ALOGW_IF(mThreadControl >= 0 || mControlFD >= 0,
            "%s: Thread control FDs are opened", __FUNCTION__);
    /* Create a pair of FDs that would be used to control the thread. */
    int thread_fds[2];
    status_t ret;
    Mutex::Autolock lock(mCameraMutex);
    if (pipe(thread_fds) == 0) {
        mThreadControl = thread_fds[1];
        mControlFD = thread_fds[0];
        ALOGV("Emulated device's worker thread has been started.");
        ret = NO_ERROR;
    } else {
        ALOGE("%s: Unable to create thread control FDs: %d -> %s",
             __FUNCTION__, errno, strerror(errno));
        ret = errno;
    }

    mSetup.broadcast();
    return ret;
}

status_t WorkerThread::sendControlMessage(ControlMessage msg) {
    status_t res = EINVAL;

    // Limit the scope of the Autolock
    {
      // If thread is running and readyToRun() has not finished running,
      //    then wait until it is done.
      Mutex::Autolock lock(mCameraMutex);
      ALOGV("%s: Acquired camera mutex lock", __FUNCTION__);
      if (isRunning() && (mThreadControl < 0 || mControlFD < 0)) {
          ALOGV("%s: Waiting for setup condition", __FUNCTION__);
          mSetup.wait(mCameraMutex);
      }
    }
    ALOGV("%s: Waited for setup complete", __FUNCTION__);

    if (mThreadControl >= 0) {
        /* Send "stop" message to the thread loop. */
        const int wres =
            TEMP_FAILURE_RETRY(write(mThreadControl, &msg, sizeof(msg)));
        if (wres == sizeof(msg)) {
            res = NO_ERROR;
        } else {
            res = errno ? errno : EINVAL;
        }
        ALOGV("%s: Sent control message, result: %d", __FUNCTION__, (int)res);
        return res;
    } else {
        ALOGE("%s: Thread control FDs are not opened", __FUNCTION__);
    }

    return res;
}

status_t WorkerThread::stopThread() {
    ALOGV("%s: Stopping worker thread...",
          __FUNCTION__);

    status_t res = sendControlMessage(THREAD_STOP);
    if (res == NO_ERROR) {
        /* Stop the thread, and wait till it's terminated. */
        ALOGV("%s: Requesting exit and waiting", __FUNCTION__);
        res = requestExitAndWait();
        ALOGV("%s: requestExitAndWait returned", __FUNCTION__);
        if (res == NO_ERROR) {
            /* Close control FDs. */
            if (mThreadControl >= 0) {
                close(mThreadControl);
                mThreadControl = -1;
            }
            if (mControlFD >= 0) {
                close(mControlFD);
                mControlFD = -1;
            }
            ALOGV("%s: Worker thread has been stopped.", __FUNCTION__);
        } else {
            ALOGE("%s: requestExitAndWait failed: %d -> %s",
                 __FUNCTION__, res, strerror(-res));
        }
    } else {
        ALOGE("%s: Unable to send THREAD_STOP message: %d -> %s",
             __FUNCTION__, errno, strerror(errno));
    }
    return res;
}

status_t WorkerThread::wakeThread() {
    ALOGV("%s: Waking emulated camera device's worker thread...",
          __FUNCTION__);

    status_t res = sendControlMessage(THREAD_WAKE);
    if (res != NO_ERROR) {
        ALOGE("%s: Unable to send THREAD_WAKE message: %d -> %s",
             __FUNCTION__, errno, strerror(errno));
    }
    return res;
}

WorkerThread::SelectRes WorkerThread::Select(int fd, int timeoutMicroSec)
{
    fd_set fds[1];
    struct timeval tv, *tvp = NULL;

    const int fd_num = std::max(fd, mControlFD) + 1;
    FD_ZERO(fds);
    FD_SET(mControlFD, fds);
    if (fd >= 0) {
        FD_SET(fd, fds);
    }
    if (timeoutMicroSec >= 0) {
        tv.tv_sec = timeoutMicroSec / 1000000;
        tv.tv_usec = timeoutMicroSec % 1000000;
        tvp = &tv;
    }
    int res = TEMP_FAILURE_RETRY(select(fd_num, fds, NULL, NULL, tvp));
    if (res < 0) {
        ALOGE("%s: select returned %d and failed: %d -> %s",
             __FUNCTION__, res, errno, strerror(errno));
        return ERROR;
    } else if (res == 0) {
        /* Timeout. */
        return TIMEOUT;
    } else if (FD_ISSET(mControlFD, fds)) {
        /* A control event. Lets read the message. */
        ControlMessage msg;
        res = TEMP_FAILURE_RETRY(read(mControlFD, &msg, sizeof(msg)));
        if (res != sizeof(msg)) {
            ALOGE("%s: Unexpected message size %d, or an error %d -> %s",
                 __FUNCTION__, res, errno, strerror(errno));
            return ERROR;
        }
        if (msg == THREAD_STOP) {
            ALOGV("%s: THREAD_STOP message is received", __FUNCTION__);
            return EXIT_THREAD;
        } else if (msg == THREAD_WAKE) {
            ALOGV("%s: THREAD_WAKE message is received", __FUNCTION__);
            return WAKE_THREAD;
        } else {
            ALOGE("Unknown worker thread message %d", msg);
            return ERROR;
        }
    } else {
        /* Must be an FD. */
        ALOGW_IF(fd < 0 || !FD_ISSET(fd, fds), "%s: Undefined 'select' result",
                __FUNCTION__);
        return READY;
    }
}

bool WorkerThread::threadLoop() {
    /* Simply dispatch the call to the containing camera device. */
    if (inWorkerThread()) {
        /* Respect "one burst" parameter (see startThread). */
        return !mOneBurst;
    } else {
        return false;
    }
}

}  // namespace android

