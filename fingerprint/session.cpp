/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <fcntl.h>
#include <inttypes.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <chrono>
#include <limits>
#include <aidl/android/hardware/biometrics/common/BnCancellationSignal.h>
#include <android-base/unique_fd.h>
#include <debug.h>
#include <log/log.h>
#include <qemud.h>
#include <utils/Timers.h>

#include "session.h"
#include "storage.h"

#define SESSION_DEBUG(FMT, ...) \
    ALOGD("%p:%s:%d: " FMT, this, __func__, __LINE__, __VA_ARGS__)
#define SESSION_ERR(FMT, ...) \
    ALOGE("%p:%s:%d: " FMT, this, __func__, __LINE__, __VA_ARGS__)

#define SESSION_DEBUG0(STR) SESSION_DEBUG("%s", STR)

namespace aidl::android::hardware::biometrics::fingerprint {

using ::android::base::unique_fd;

namespace {
constexpr char kSensorServiceName[] = "fingerprintlisten";
constexpr char kSensorListenerQuitCmd = 'Q';

int64_t generateSeed(void* p) {
    auto now = std::chrono::high_resolution_clock::now();
    decltype(now) epoch;
    return (now - epoch).count() ^ reinterpret_cast<uintptr_t>(p);
}

int epollCtlAdd(int epollFd, int fd) {
    int ret;

    /* make the fd non-blocking */
    ret = TEMP_FAILURE_RETRY(fcntl(fd, F_GETFL));
    if (ret < 0) {
        return ret;
    }

    ret = TEMP_FAILURE_RETRY(fcntl(fd, F_SETFL, ret | O_NONBLOCK));
    if (ret < 0) {
        return ret;
    }

    struct epoll_event ev;
    ev.events  = EPOLLIN;
    ev.data.fd = fd;

    return TEMP_FAILURE_RETRY(epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev));
}

template <class T> std::string vec2str(const std::vector<T>& v) {
    if (v.empty()) {
        return "empty";
    } else {
        std::string result;

        for (const auto& x : v) {
            if (result.empty()) {
                result = std::to_string(x);
            } else {
                result += ",";
                result += std::to_string(x);
            }
        }

        return std::string("[") + result + std::string("]");
    }
}

std::string state2str(const Session::State s) {
    switch (s) {
    case Session::State::IDLE:                  return "IDLE";
    case Session::State::ENROLLING_START:       return "ENROLLING_START";
    case Session::State::ENROLLING_END:         return "ENROLLING_END";
    case Session::State::AUTHENTICATING:        return "AUTHENTICATING";
    case Session::State::DETECTING_INTERACTION: return "DETECTING_INTERACTION";
    default: return std::to_string(static_cast<int>(s));
    }
}

std::string errorCode2str(const Session::ErrorCode ec) {
    switch (ec) {
    case Session::ErrorCode::OK:                    return "OK";
    case Session::ErrorCode::E_HAT_MAC_EMPTY:       return "E_HAT_MAC_EMPTY";
    case Session::ErrorCode::E_HAT_WRONG_CHALLENGE: return "E_HAT_WRONG_CHALLENGE";
    case Session::ErrorCode::E_INCORRECT_STATE:     return "E_INCORRECT_STATE";
    case Session::ErrorCode::E_ENROLL_FAILED:       return "E_ENROLL_FAILED";
    default: return std::to_string(static_cast<int>(ec));
    }
}

}  // namespace

struct CancellationSignal : public common::BnCancellationSignal {
    CancellationSignal(std::function<void()> cb) : mCB(std::move(cb)) {}

    ndk::ScopedAStatus  cancel() override {
        mCB();
        return ndk::ScopedAStatus::ok();
    }

    const std::function<void()> mCB;
};

Session::Session(const int32_t sensorId, const int32_t userId,
                 std::shared_ptr<ISessionCallback> scb)
    : mSessionCb(std::move(scb))
    , mStorage(sensorId, userId)
    , mRandom(generateSeed(this))
 {
    SESSION_DEBUG("New session: sensorId=%d userId=%d", sensorId, userId);

    if (::android::base::Socketpair(AF_LOCAL, SOCK_STREAM, 0,
                                    &mCallerFd, &mSensorThreadFd)) {
        mSensorListener = std::thread(&Session::sensorListenerFunc, this);
    } else {
        mSensorListener = std::thread([](){});
        LOG_ALWAYS_FATAL("%p:%s:%d: Socketpair failed", this, __func__, __LINE__);
    }
}

Session::~Session() {
    SESSION_DEBUG0("Terminating session");

    TEMP_FAILURE_RETRY(write(mCallerFd.get(), &kSensorListenerQuitCmd, 1));
    mSensorListener.join();
}

ndk::ScopedAStatus Session::generateChallenge() {
    while (true) {
        int64_t challenge;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            challenge = generateInt64();
        }

        if (mChallenges.insert(challenge).second) {
            SESSION_DEBUG("onChallengeGenerated(challenge=%" PRId64 ")", challenge);
            mSessionCb->onChallengeGenerated(challenge);
            return ndk::ScopedAStatus::ok();
        }
    }
}

ndk::ScopedAStatus Session::revokeChallenge(const int64_t challenge) {
    mChallenges.erase(challenge);
    SESSION_DEBUG("onChallengeRevoked(challenge=%" PRId64 ")", challenge);
    mSessionCb->onChallengeRevoked(challenge);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enroll(const keymaster::HardwareAuthToken& hat,
                                   std::shared_ptr<common::ICancellationSignal>* out) {
    const ErrorCode err = validateHat(hat);
    if (err == ErrorCode::OK) {
        State previousState;
        bool ok;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            previousState = mState;
            if (previousState == State::IDLE) {
                mEnrollingSecUserId = hat.userId;
                mState = State::ENROLLING_START;
                ok = true;
            } else {
                ok = false;
            }
        }

        if (ok) {
            SESSION_DEBUG("ENROLLING_START hat.userId=%" PRId64, hat.userId);
            *out = SharedRefBase::make<CancellationSignal>([this](){ cancellEnroll(); });
        } else {
            SESSION_ERR("onError(UNABLE_TO_PROCESS, %d): incorrect state, %s",
                  int(ErrorCode::E_INCORRECT_STATE), state2str(previousState).c_str());
            mSessionCb->onError(Error::UNABLE_TO_PROCESS,
                                int(ErrorCode::E_INCORRECT_STATE));
        }
    } else {
        SESSION_ERR("onError(UNABLE_TO_PROCESS, %d): `hat` is invalid: %s",
                    int(err), errorCode2str(err).c_str());
        mSessionCb->onError(Error::UNABLE_TO_PROCESS, int(err));
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::authenticate(const int64_t operationId,
                                         std::shared_ptr<common::ICancellationSignal>* out) {
    State previousState;
    bool ok;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        previousState = mState;
        if (previousState == State::IDLE) {
            mAuthChallenge = operationId;
            mState = State::AUTHENTICATING;
            ok = true;
        } else {
            ok = false;
        }
    }

    if (ok) {
        SESSION_DEBUG("AUTHENTICATING operationId=%" PRId64, operationId);
        *out = SharedRefBase::make<CancellationSignal>([this](){ cancellAuthenticate(); });
    } else {
        SESSION_ERR("onError(UNABLE_TO_PROCESS, %d): incorrect state, %s",
                    int(ErrorCode::E_INCORRECT_STATE),
                    state2str(previousState).c_str());
        mSessionCb->onError(Error::UNABLE_TO_PROCESS,
                            int(ErrorCode::E_INCORRECT_STATE));
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::detectInteraction(
        std::shared_ptr<common::ICancellationSignal>* out) {
    State previousState;
    bool ok;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        previousState = mState;
        if (previousState == State::IDLE) {
            mState = State::DETECTING_INTERACTION;
            ok = true;
        } else {
            ok = false;
        }
    }

    if (ok) {
        SESSION_DEBUG0("DETECTING_INTERACTION");
        *out = SharedRefBase::make<CancellationSignal>([this](){ cancellDetectInteraction(); });
    } else {
        SESSION_ERR("onError(UNABLE_TO_PROCESS, %d): incorrect state, %s",
                    int(ErrorCode::E_INCORRECT_STATE),
                    state2str(previousState).c_str());
        mSessionCb->onError(Error::UNABLE_TO_PROCESS,
                            int(ErrorCode::E_INCORRECT_STATE));
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::enumerateEnrollments() {
    std::vector<int32_t> enrollmentIds;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        enrollmentIds = mStorage.enumerateEnrollments();
    }

    SESSION_DEBUG("onEnrollmentsEnumerated(enrollmentIds=%s)",
                  vec2str(enrollmentIds).c_str());
    mSessionCb->onEnrollmentsEnumerated(enrollmentIds);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::removeEnrollments(const std::vector<int32_t>& enrollmentIds) {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStorage.removeEnrollments(enrollmentIds);
    }

    SESSION_DEBUG("onEnrollmentsRemoved(enrollmentIds=%s)",
                  vec2str(enrollmentIds).c_str());
    mSessionCb->onEnrollmentsRemoved(enrollmentIds);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::getAuthenticatorId() {
    int64_t authId;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        authId = mStorage.getAuthenticatorId();
    }

    SESSION_DEBUG("onAuthenticatorIdRetrieved(authId=%" PRId64 ")", authId);
    mSessionCb->onAuthenticatorIdRetrieved(authId);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::invalidateAuthenticatorId() {
    int64_t authId;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        authId = mStorage.invalidateAuthenticatorId(generateInt64());
    }

    SESSION_DEBUG("onAuthenticatorIdInvalidated(authId=%" PRId64 ")", authId);
    mSessionCb->onAuthenticatorIdInvalidated(authId);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::resetLockout(const keymaster::HardwareAuthToken& hat) {
    const ErrorCode err = validateHat(hat);
    if (err == ErrorCode::OK) {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mStorage.resetLockout();
        }

        SESSION_DEBUG0("onLockoutCleared");
        mSessionCb->onLockoutCleared();
    } else {
        SESSION_ERR("onError(UNABLE_TO_PROCESS, %d): `hat` is invalid: %s",
                    int(err), errorCode2str(err).c_str());
        mSessionCb->onError(Error::UNABLE_TO_PROCESS, int(err));
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Session::close() {
    mChallenges.clear();
    SESSION_DEBUG0("onSessionClosed");
    mSessionCb->onSessionClosed();
    return ndk::ScopedAStatus::ok();
}

Session::ErrorCode Session::validateHat(const keymaster::HardwareAuthToken& hat) const {
    if (hat.mac.empty()) {
        return FAILURE(ErrorCode::E_HAT_MAC_EMPTY);
    }

    if (!mChallenges.count(hat.challenge)) {
        return FAILURE_V(ErrorCode::E_HAT_WRONG_CHALLENGE,
                         "unexpected challenge: %" PRId64, hat.challenge);
    }

    return ErrorCode::OK;
}

int64_t Session::generateInt64() {
    std::uniform_int_distribution<int64_t> distrib(1, std::numeric_limits<int64_t>::max());
    return distrib(mRandom);
}

void Session::onSensorEventOn(const int32_t enrollmentId) {
    std::lock_guard<std::mutex> lock(mMutex);
    switch (mState) {
    case State::ENROLLING_START:
    case State::ENROLLING_END:
        {
            SESSION_DEBUG("onAcquired(GOOD, %d)", 0);
            mSessionCb->onAcquired(AcquiredInfo::GOOD, 0);

            const int left = int(State::ENROLLING_END) - int(mState);
            if (left > 0) {
                SESSION_DEBUG("onEnrollmentProgress(enrollmentId=%d, left=%d)",
                              enrollmentId, left);
                mSessionCb->onEnrollmentProgress(enrollmentId, left);
                mState = State(int(mState) + 1);
            } else if (mStorage.enroll(enrollmentId, mEnrollingSecUserId, generateInt64())) {
                SESSION_DEBUG("onEnrollmentProgress(enrollmentId=%d, left=%d)",
                              enrollmentId, left);
                mSessionCb->onEnrollmentProgress(enrollmentId, left);
                mState = State::IDLE;
            } else {
                SESSION_ERR("onError(UNABLE_TO_PROCESS, %d): enrollmentId=%d, "
                            "secureIserId=%" PRId64 ,
                            int(ErrorCode::E_ENROLL_FAILED),
                            enrollmentId, mEnrollingSecUserId);
                mSessionCb->onError(Error::UNABLE_TO_PROCESS,
                                    int(ErrorCode::E_ENROLL_FAILED));
                mState = State::IDLE;
            }
        }
        break;

    case State::AUTHENTICATING:
        {
            const auto [res, lockoutDurationMillis, tok] =
                mStorage.authenticate(enrollmentId);
            if (res != Storage::AuthResult::LOCKED_OUT_PERMANENT) {
                SESSION_DEBUG("onAcquired(GOOD, %d)", 0);
                mSessionCb->onAcquired(AcquiredInfo::GOOD, 0);
            }

            switch (res) {
            case Storage::AuthResult::OK: {
                    SESSION_DEBUG("onAuthenticationSucceeded(enrollmentId=%d, "
                                  "hat={ .challenge=%" PRId64 ", .userId=%" PRId64 ", "
                                  ".authenticatorId=%" PRId64 " })",
                                  enrollmentId, mAuthChallenge,
                                  tok.userId, tok.authenticatorId);

                    keymaster::HardwareAuthToken hat;
                    hat.challenge = mAuthChallenge;
                    hat.userId = tok.userId;
                    hat.authenticatorId = tok.authenticatorId;
                    hat.authenticatorType = keymaster::HardwareAuthenticatorType::FINGERPRINT;
                    hat.timestamp.milliSeconds = ns2ms(systemTime(SYSTEM_TIME_BOOTTIME));
                    mSessionCb->onAuthenticationSucceeded(enrollmentId, hat);
                    mState = State::IDLE;
                }
                break;

            case Storage::AuthResult::FAILED:
                SESSION_ERR("onAuthenticationFailed: enrollmentId=%d", enrollmentId);
                mSessionCb->onAuthenticationFailed();
                break;

            case Storage::AuthResult::LOCKED_OUT_TIMED:
                SESSION_ERR("onLockoutTimed(durationMillis=%d): enrollmentId=%d",
                            lockoutDurationMillis, enrollmentId);
                mSessionCb->onLockoutTimed(lockoutDurationMillis);
                mState = State::IDLE;
                break;

            case Storage::AuthResult::LOCKED_OUT_PERMANENT:
                SESSION_ERR("onLockoutPermanent: enrollmentId=%d", enrollmentId);
                mSessionCb->onLockoutPermanent();
                mState = State::IDLE;
                break;

            default:
                LOG_ALWAYS_FATAL("%p:%s:%d: Unexpected result from `mStorage.authenticate`",
                                 this, __func__, __LINE__);
                break;
            }
        }
        break;

    case State::DETECTING_INTERACTION:
        mSessionCb->onInteractionDetected();
        mState = State::IDLE;
        break;

    case State::IDLE:
        break;

    default:
        LOG_ALWAYS_FATAL("%p:%s:%d: Unexpected session state", this, __func__, __LINE__);
        break;
    }
}

void Session::onSensorEventOff() {}

void Session::cancellEnroll() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if ((mState >= State::ENROLLING_START) && (mState <= State::ENROLLING_END)) {
            mState = State::IDLE;
        }
    }

    SESSION_DEBUG("onError(CANCELED, %d)", 0);
    mSessionCb->onError(Error::CANCELED, 0);
}

void Session::cancellAuthenticate() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mState == State::AUTHENTICATING) {
            mState = State::IDLE;
        }
    }

    SESSION_DEBUG("onError(CANCELED, %d)", 0);
    mSessionCb->onError(Error::CANCELED, 0);
}

void Session::cancellDetectInteraction() {
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mState == State::DETECTING_INTERACTION) {
            mState = State::IDLE;
        }
    }

    SESSION_DEBUG("onError(CANCELED, %d)", 0);
    mSessionCb->onError(Error::CANCELED, 0);
}

bool Session::sensorListenerFuncImpl() {
    unique_fd sensorFd(qemud_channel_open(kSensorServiceName));
    LOG_ALWAYS_FATAL_IF(!sensorFd.ok(),
                        "%p:%s:%d: Could not open the sensor service: '%s'",
                        this, __func__, __LINE__, kSensorServiceName);

    const unique_fd epollFd(epoll_create1(EPOLL_CLOEXEC));
    epollCtlAdd(epollFd.get(), sensorFd.get());
    epollCtlAdd(epollFd.get(), mSensorThreadFd.get());

    qemud_channel_send(sensorFd.get(), "listen", 6);

    while (true) {
        const int kTimeoutMs = 250;
        struct epoll_event event;
        const int n = TEMP_FAILURE_RETRY(epoll_wait(epollFd.get(),
                                                    &event, 1,
                                                    kTimeoutMs));
        if (n <= 0) {
            bool lockoutCleared;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                lockoutCleared = mStorage.checkIfLockoutCleared();
            }

            if (lockoutCleared) {
                SESSION_DEBUG0("onLockoutCleared");
                mSessionCb->onLockoutCleared();
            }
            continue;
        }

        const int fd = event.data.fd;
        const int ev_events = event.events;
        if (fd == sensorFd.get()) {
            if (ev_events & (EPOLLERR | EPOLLHUP)) {
                SESSION_ERR("epoll_wait: devFd has an error, ev_events=%x", ev_events);
                return true;
            } else if (ev_events & EPOLLIN) {
                char buf[64];
                int n = qemud_channel_recv(fd, buf, sizeof(buf) - 1);
                if (n > 0) {
                    buf[n] = 0;
                    int32_t fid;
                    if (sscanf(buf, "on:%d", &fid) == 1) {
                        if (fid > 0) {
                            onSensorEventOn(fid);
                        } else {
                            SESSION_ERR("incorrect fingerprint: %d", fid);
                        }
                    } else if (!strcmp(buf, "off")) {
                        onSensorEventOff();
                    } else {
                        SESSION_ERR("unexpected hw message: '%s'", buf);
                        return true;
                    }
                } else {
                    SESSION_ERR("hw read error, n=%d, errno=%d", n, errno);
                    return true;
                }
            }
        } else if (fd == mSensorThreadFd.get()) {
            if (ev_events & (EPOLLERR | EPOLLHUP)) {
                LOG_ALWAYS_FATAL("%p:%s:%d: epoll_wait: threadsFd has an error, ev_events=%x",
                                 this, __func__, __LINE__, ev_events);
            } else if (ev_events & EPOLLIN) {
                char cmd;
                int n = TEMP_FAILURE_RETRY(read(fd, &cmd, sizeof(cmd)));
                if (n == 1) {
                    switch (cmd) {
                    case kSensorListenerQuitCmd:
                        return false;  // quit

                    default:
                        LOG_ALWAYS_FATAL("%p:%s:%d: unexpected command, cmd=%c",
                                         this, __func__, __LINE__, cmd);
                        break;
                    }
                } else {
                    LOG_ALWAYS_FATAL("%p:%s:%d: error readind from mThreadsFd, errno=%d",
                                     this, __func__, __LINE__, errno);
                }
            }
        } else {
            SESSION_ERR("%s", "epoll_wait() returned unexpected fd");
        }
    }
}

}  // namespace aidl::android::hardware::biometrics::fingerprint
