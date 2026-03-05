# Fingerprint biometrics HAL implementation for goldfish #

The fingerprint HAL is lazily started one which implements the

`aidl::android::hardware::biometrics::fingerprint::BnFingerprint`

interface, see the `aidl::android::hardware::biometrics::fingerprint` namespace
(and comments there) for more details.

The `cc_binary` is called "android.hardware.biometrics.fingerprint-service.ranchu", and its
`LOG_TAG` is defined as "fingerprint-service.ranchu".

The HAL implementation has persisten storage, see the [Storage](#storage) section for
more details.

## Files ##

* `android.hardware.biometrics.fingerprint-service.ranchu.xml`: the service manifest for Android.
* `android.hardware.biometrics.fingerprint-service.ranchu.rc`: contains the service definition
    for Android.

## Talking to the hardware sensor ##

The hardware fingerprint sensor is emulated by the host side. To open a channel to the host side
you call `qemud_channel_open("fingerprintlisten")` from the `libqemud.ranchu` library. Use
the `qemud_channel_send` and `qemud_channel_recv` to send and receive data over the channel.

### The channel protocol ###

#### Requests ####

* `listen`: starts listening for sensor events.

#### Notifications ####

* `on:{fingerprint_hash}\0` (e.g. "on:1234\0"): a touch is detected with a fingerprint
    hash (`int32_t`) provided. Only positive hashes are valid: negative and the zero value
    should be ignored.
* `off\0`: the sensor is released.

## Interfaces ##

### BnFingerprint ###

The implementation (called `FingerprintHal`) of the `BnFingerprint` interface contains two methods:

* `getSensorProps`: replies with a set of predefined properties (`SensorProps`):

```cpp
constexpr char HW_COMPONENT_ID[] = "FingerprintSensor";
constexpr char XW_VERSION[] = "ranchu/fingerprint/aidl";
constexpr char FW_VERSION[] = "1";
constexpr char SERIAL_NUMBER[] = "00000001";
constexpr char SW_COMPONENT_ID[] = "matchingAlgorithm";

    std::vector<common::ComponentInfo> componentInfo = {
        {
            HW_COMPONENT_ID,
            XW_VERSION,
            FW_VERSION,
            SERIAL_NUMBER,
            "" /* softwareVersion */
        },
        {
            SW_COMPONENT_ID,
            "" /* hardwareVersion */,
            "" /* firmwareVersion */,
            "" /* serialNumber */,
            XW_VERSION
        }
    };

    SensorLocation sensorLocation;
    sensorLocation.sensorLocationX = 0;
    sensorLocation.sensorLocationY = 0;
    sensorLocation.sensorRadius = 0;
    sensorLocation.display = "";

    TouchDetectionParameters touchDetectionParameters;
    touchDetectionParameters.targetSize = 1.0;
    touchDetectionParameters.minOverlap = 0.2;

    SensorProps props;
    props.commonProps.sensorId = 0;
    props.commonProps.sensorStrength = common::SensorStrength::STRONG;
    props.commonProps.maxEnrollmentsPerUser = Storage::getMaxEnrollmentsPerUser();
    props.commonProps.componentInfo = std::move(componentInfo);
    props.sensorType = FingerprintSensorType::REAR;
    props.sensorLocations.push_back(std::move(sensorLocation));
    props.supportsNavigationGestures = false;
    props.supportsDetectInteraction = true;
    props.halHandlesDisplayTouches = false;
    props.halControlsIllumination = false;
    props.touchDetectionParameters = touchDetectionParameters;
```

* `createSession`: creates a `Session` instance for the provided `sensorId`, `userId` and
    the `ISessionCallback` instance.

### BnSession ###

The implementation (called `Session`) of the `BnSession` interface, it keeps the arguments passed to
`FingerprintHal::createSession` instance and implements the following methods:

* `generateChallenge`: generates challenge (`int64_t`) using `std::mt19937_64`, refer to the AIDL
    spec for the details.
* `revokeChallenge`: refer to the AIDL spec for the details.
* `enroll`: refer to the AIDL spec for the details.
* `authenticate`: refer to the AIDL spec for the details.
* `detectInteraction`: refer to the AIDL spec for the details.
* `enumerateEnrollments`: refer to the AIDL spec for the details.
* `removeEnrollments`: refer to the AIDL spec for the details.
* `getAuthenticatorId`: refer to the AIDL spec for the details.
* `invalidateAuthenticatorId`: refer to the AIDL spec for the details.
* `resetLockout`: refer to the AIDL spec for the details.
* `close`: clears the challenges added by `generateChallenge` and closes the `Session`.
* `onPointerDown`: simply returns `ScopedAStatus::ok()`.
* `onPointerUp`: simply returns `ScopedAStatus::ok()`.
* `onUiReady`: simply returns `ScopedAStatus::ok()`.
* `enrollWithContext`: ignores the `OperationContext` and calls `enroll`.
* `authenticateWithContext`: ignores the `OperationContext` and calls `authenticate`.
* `detectInteractionWithContext`: ignores the `OperationContext` and calls `detectInteraction`.
* `onPointerDownWithContext`: simply returns `ScopedAStatus::ok()`.
* `onPointerUpWithContext`: simply returns `ScopedAStatus::ok()`.
* `onContextChanged`: simply returns `ScopedAStatus::ok()`.
* `onPointerCancelWithContext`: simply returns `ScopedAStatus::ok()`.
* `setIgnoreDisplayTouches`: simply returns `ScopedAStatus::ok()`.

### Storage ###

The state specified below survives HAL and device restarts.

#### The state preserved ####

* `authenticatorId` (`int64_t`) which is updated to a new ramdom `int64_t` on
    a new succesfull enrollment.
* `secureUserId` (`int64_t`), provided by `BnSession::enroll` in
    `keymaster::HardwareAuthToken::userId`.
* a number of enrollments (`uint8_t`, from 0 to 5).
* a list of enrollments (`int32_t`).

The storage provides a lockout mechanism (a growing timeout on the first 10 attempts and then
permanent until `resetLockout` is called) on a wrong entry.

#### Filename ####

The state is stored in the

`/data/vendor_de/{user_id}/fpdata/sensor{sensor_id}.bin`

file with the `user_id` and `sensor_id` values provided in the `BnFingerprint::createSession` call.

The format is binary, with the `0x46507261` (`uint32_t`) marker.

