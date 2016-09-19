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

#ifndef HW_EMULATOR_CAMERA_WORKER_THREAD_H
#define HW_EMULATOR_CAMERA_WORKER_THREAD_H

#include <utils/Thread.h>

namespace android {

class EmulatedCameraDevice;

class WorkerThread : public Thread {
public:
    WorkerThread(const char* threadName,
                 EmulatedCameraDevice* camera_dev,
                 Mutex& cameraMutex);
    virtual ~WorkerThread();

    /* Starts the thread
     * Param:
     *  one_burst - Controls how many times thread loop should run. If
     *      this parameter is 'true', thread routine will run only once
     *      If this parameter is 'false', thread routine will run until
     *      stopThread method is called. See startWorkerThread for more
     *      info.
     * Return:
     *  NO_ERROR on success, or an appropriate error status.
     */
    status_t startThread(bool one_burst);

    /* Overriden base class method.
     * It is overriden in order to provide one-time initialization just
     * prior to starting the thread routine.
     */
    status_t readyToRun() override;

    /* Stops the thread. */
    status_t stopThread();

    /* Wake a thread that's currently waiting to timeout or to be awoken */
    status_t wakeThread();

    /* Values returned from the Select method of this class. */
    enum SelectRes {
        TIMEOUT,      /* A timeout has occurred. */
        READY,        /* Data are available for read on the provided FD. */
        EXIT_THREAD,  /* Thread exit request has been received. */
        WAKE_THREAD,  /* Thread wake request has been received */
        ERROR         /* An error has occurred. */
    };

    /* Select on an FD event, keeping in mind thread exit message.
     * Param:
     *  fd - File descriptor on which to wait for an event. This
     *      parameter may be negative. If it is negative this method will
     *      only wait on a control message to the thread.
     *  timeoutMicroSec - Timeout in microseconds. A negative value indicates
     *            no timeout (wait forever). A timeout of zero indicates
     *            an immediate return after polling the FD's; this can
     *            be used to check if a thread exit has been requested
     *            without having to wait for a timeout.
     * Return:
     *  See SelectRes enum comments.
     */
    SelectRes Select(int fd, int timeoutMicroSec);

protected:
    /* Perform whatever work should be done in the worker thread. A subclass
     * needs to implement this.
     * Return:
     *  true To continue thread loop, or false to exit the thread loop and
     *  terminate the thread.
     */
    virtual bool inWorkerThread() = 0;

    /* Containing camera device object. */
    EmulatedCameraDevice* mCameraDevice;

    /* Controls number of times the thread loop runs.
     * See startThread for more information. */
    bool mOneBurst;

private:
    /* Enumerates control messages that can be sent into the thread. */
    enum ControlMessage {
        THREAD_STOP,  /* Stop the thread. */
        THREAD_WAKE   /* Wake the thread if it's waiting for something */
    };

    /* Implements abstract method of the base Thread class. */
    bool threadLoop() override;

    /* Send a control message to the thread */
    status_t sendControlMessage(ControlMessage message);

    const char* mThreadName;

    /* FD that is used to send control messages into the thread. */
    int mThreadControl;

    /* FD that thread uses to receive control messages. */
    int mControlFD;

    Mutex& mCameraMutex;
    Condition mSetup;
};

}  // namespace android

#endif  // HW_EMULATOR_CAMERA_WORKER_THREAD_H
