/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <algorithm>
#include <charconv>
#include <numeric>
#include <string_view>

#include <log/log.h>

#include "debug.h"
#include "list_qemu_cameras.h"
#include "QemuCamera.h"
#include "qemu_channel.h"

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace hw {
namespace {
bool findToken(const std::string_view str, const std::string_view key, std::string_view* value) {
    size_t pos = 0;
    while (true) {
        pos = str.find(key, pos);
        if (pos == std::string_view::npos) {
            return FAILURE(false);
        } else if ((!pos || str[pos - 1] == ' ') &&
                (str.size() >= (pos + key.size() + 1)) &&
                (str[pos + key.size()] == '=')) {
            const size_t vbegin = pos + key.size() + 1;
            const size_t vend = str.find(' ', vbegin);
            if (vend == std::string_view::npos) {
                *value = str.substr(vbegin, str.size() - vbegin);
            } else {
                *value = str.substr(vbegin, vend - vbegin);
            }
            return true;
        } else {
            ++pos;
        }
    }
}

bool parseResolutions(const std::string_view str, std::vector<Rect<uint16_t>>* supportedResolutions) {
    const char* i = &*str.begin();
    const char* const end = &*str.end();
    if (i == end) {
        return FAILURE(false);
    }

    while (true) {
        Rect<uint16_t> resolution;
        std::from_chars_result r =  std::from_chars(i, end, resolution.width, 10);
        if (r.ec != std::errc()) {
            return FAILURE(false);
        }
        i = r.ptr;
        if (i == end) {
            return FAILURE(false);
        }
        if (*i != 'x') {
            return FAILURE(false);
        }
        r =  std::from_chars(i + 1, end, resolution.height, 10);
        if (r.ec != std::errc()) {
            return FAILURE(false);
        }
        i = r.ptr;

        supportedResolutions->push_back(resolution);

        if (i == end) {
            break;
        } else {
            if (*i == ',') {
                ++i;
            } else {
                return FAILURE(false);
            }
        }
    }

    return true;
}

struct RectAreaComparator {
    bool operator()(Rect<uint16_t> lhs, Rect<uint16_t> rhs) const {
        const size_t lArea = size_t(lhs.width) * lhs.height;
        const size_t rArea = size_t(rhs.width) * rhs.height;

        if (lArea < rArea) {
            return true;
        } else if (lArea > rArea) {
            return false;
        } else {
            return lhs.width < rhs.width;
        }
    }
};

} // namespace

std::vector<HwCameraFactory> listQemuCameras() {
    using ResultT = std::vector<HwCameraFactory>;

    using namespace std::literals;
    static const char kListQuery[] = "list";

    const auto fd = qemuOpenChannel();
    if (!fd.ok()) { return FAILURE(ResultT()); }

    std::vector<uint8_t> data;
    if (qemuRunQuery(fd.get(), kListQuery, sizeof(kListQuery), &data) < 0) {
        return FAILURE(ResultT());
    }

    const char* i = reinterpret_cast<const char*>(&*data.begin());
    const char* const end = reinterpret_cast<const char*>(&*data.end());

    ResultT cameras;
    while (i < end) {
        const char* const lf = std::find(i, end, '\n');
        if (lf == end) {
            if (*i) {
                return FAILURE(ResultT());
            } else {
                break;
            }
        }

        // line='name=virtualscene channel=0 pix=876758866 dir=back framedims=640x480,352x288,320x240,176x144,1280x720,1280x960'
        const std::string_view line(i, lf - i);

        std::string_view name;
        if (!findToken(line, "name"sv, &name)) { return FAILURE(ResultT()); }

        std::string_view dir;
        if (!findToken(line, "dir"sv, &dir)) { return FAILURE(ResultT()); }

        std::string_view framedims;
        if (!findToken(line, "framedims"sv, &framedims)) { return FAILURE(ResultT()); }

        QemuCamera::Parameters params;
        if (!parseResolutions(framedims, &params.supportedResolutions)) {
            return FAILURE(ResultT());
        }

        if (params.supportedResolutions.empty()) {
            return FAILURE(ResultT());
        } else {
            std::sort(params.supportedResolutions.begin(),
                      params.supportedResolutions.end(),
                      RectAreaComparator());
        }

        ALOGD("%s:%d found a '%.*s' QEMU camera, dir=%.*s framedims=%.*s",
              __func__, __LINE__,
              int(name.size()), name.data(),
              int(dir.size()), dir.data(),
              int(framedims.size()), framedims.data());

        params.name = std::string(name.begin(), name.end());

        params.sensorSize = std::accumulate(
            std::next(params.supportedResolutions.begin()),
            params.supportedResolutions.end(),
            params.supportedResolutions.front(),
            [](Rect<uint16_t> z, Rect<uint16_t> x) -> Rect<uint16_t> {
                return {std::max(z.width, x.width),
                        std::max(z.height, x.height)};
        });

        params.isBackFacing = (dir == "back"sv);

        cameras.push_back([params = std::move(params)]() {
            return std::make_unique<QemuCamera>(params);
        });

        i = lf + 1;
    }

    return cameras;
}

}  // namespace hw
}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
