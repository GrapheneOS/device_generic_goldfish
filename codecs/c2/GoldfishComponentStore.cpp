/* Copyright (C) 2020 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include "GoldfishComponentStore.h"
#include "C2GoldfishAvcDecFactory.h"
#include "C2GoldfishHevcDecFactory.h"
#include "C2GoldfishVpxDecFactory.h"

#include <memory>
#include <mutex>

#include <C2.h>
#include <C2Config.h>
#include <cutils/properties.h>
#include <log/log.h>

namespace goldfish::media::c2 {
namespace {
bool useAndroidGoldfishComponentInstance(const char *libname) {
    using namespace std::literals::string_literals;

    // We have a property set indicating whether to use the host side codec
    // or not (ro.boot.qemu.hwcodec.<mLibNameSuffix>).
    const std::string propName = "ro.boot.qemu.hwcodec."s + libname;
    char propValue[PROP_VALUE_MAX];
    bool myret = property_get(propName.c_str(), propValue, "") > 0 &&
                 strcmp("2", propValue) == 0;
    if (myret) {
        ALOGD("%s %d found prop %s val %s", __func__, __LINE__, propName.c_str(),
              propValue);
    }
    return myret;
}
}  // namespace

GoldfishComponentStore::GoldfishComponentStore()
        : mReflector(std::make_shared<C2ReflectorHelper>()) {
    std::vector<std::shared_ptr<const IComponentFactory>> factories;

    if (useAndroidGoldfishComponentInstance("avcdec")) {
        factories.push_back(getC2GoldfishAvcDecFactory());
    }

    if (useAndroidGoldfishComponentInstance("hevcdec")) {
        factories.push_back(getC2GoldfishHevcDecFactory());
    }

    if (useAndroidGoldfishComponentInstance("vpxdec")) {
        factories.push_back(getC2GoldfishVpxDecFactory(false));
        factories.push_back(getC2GoldfishVpxDecFactory(true));
    }

    auto* componentFactories = &mComponentFactories;
    const auto insertFactory = [componentFactories](
                const C2String& name,
                const std::shared_ptr<const IComponentFactory>& factory){
        const auto [it, ok] = componentFactories->insert({name, factory});
        if (!ok) {
            const std::string_view insertedName = it->second->getName();
            ALOGE("Could not insert '%s' because it is "
                    "already occupied by '%*.*s'", name.c_str(),
                    int(insertedName.size()), int(insertedName.size()), insertedName.data());
        }
    };

    for (const auto& factory : factories) {
        const auto [res, interface] = factory->createInterface(mReflector);
        if ((res == C2_OK) && interface) {
            if (std::shared_ptr<const C2Component::Traits> traits = buildTraits(*interface)) {
                mAllComponentTraits.push_back(traits);

                insertFactory(traits->name, factory);
                for (const C2String &alias : traits->aliases) {
                    insertFactory(alias, factory);
                }
            } else {
                ALOGE("The '%s' interface does not have traits", interface->getName().c_str());
            }
        } else {
            const std::string_view name = factory->getName();

            ALOGE("Could not fetch the interface from '%*.*s'",
                  int(name.size()), int(name.size()), name.data());
        }
    }
}

C2String GoldfishComponentStore::getName() const {
    using namespace std::literals::string_literals;
    return "android.componentStore.goldfish"s;
}

c2_status_t GoldfishComponentStore::createComponent(const C2String name,
                                                    std::shared_ptr<C2Component> *const component) {
    const auto cf = findComponentFactory(name);
    if (!cf) {
        return C2_NOT_FOUND;
    }

    c2_status_t res;
    std::tie(res, *component) = cf->createComponent(mReflector);
    return res;
}

c2_status_t GoldfishComponentStore::createInterface(
        const C2String name,
        std::shared_ptr<C2ComponentInterface> *const interface) {
    const auto cf = findComponentFactory(name);
    if (!cf) {
        return C2_NOT_FOUND;
    }

    c2_status_t res;
    std::tie(res, *interface) = cf->createInterface(mReflector);
    return res;
}

std::vector<std::shared_ptr<const C2Component::Traits>> GoldfishComponentStore::listComponents() {
    return mAllComponentTraits;
}

c2_status_t GoldfishComponentStore::copyBuffer(std::shared_ptr<C2GraphicBuffer> /*src*/,
                                               std::shared_ptr<C2GraphicBuffer> /*dst*/) {
    return C2_OMITTED;
}

c2_status_t GoldfishComponentStore::query_sm(
        const std::vector<C2Param *> &stackParams,
        const std::vector<C2Param::Index> &heapParamIndices,
        std::vector<std::unique_ptr<C2Param>> * /*heapParams*/) const {
    return stackParams.empty() && heapParamIndices.empty() ? C2_OK
                                                           : C2_BAD_INDEX;
}

c2_status_t GoldfishComponentStore::config_sm(
        const std::vector<C2Param *> &params,
        std::vector<std::unique_ptr<C2SettingResult>> * /*failures*/) {
    return params.empty() ? C2_OK : C2_BAD_INDEX;
}

std::shared_ptr<C2ParamReflector> GoldfishComponentStore::getParamReflector() const {
    return mReflector;
}

c2_status_t GoldfishComponentStore::querySupportedParams_nb(
        std::vector<std::shared_ptr<C2ParamDescriptor>> * /*params*/) const {
    return C2_OK;
}

c2_status_t GoldfishComponentStore::querySupportedValues_sm(
        std::vector<C2FieldSupportedValuesQuery> &fields) const {
    return fields.empty() ? C2_OK : C2_BAD_INDEX;
}

std::shared_ptr<const IComponentFactory>
GoldfishComponentStore::findComponentFactory(const C2String& name) const {
    auto i = mComponentFactories.find(name);
    if (i != mComponentFactories.end()) {
        return i->second;
    } else {
        return {};
    }
}

std::shared_ptr<C2Component::Traits> GoldfishComponentStore::buildTraits(const C2ComponentInterface& intf) {
    const auto traits = std::make_shared<C2Component::Traits>();
    traits->name = intf.getName();

    C2ComponentKindSetting kind;
    C2ComponentDomainSetting domain;
    c2_status_t res = intf.query_vb({&kind, &domain}, {}, C2_MAY_BLOCK, nullptr);
    const bool fixDomain = res != C2_OK;
    if (res == C2_OK) {
        traits->kind = kind.value;
        traits->domain = domain.value;
    } else {
        // TODO: remove this fall-back
        ALOGD("failed to query interface for kind and domain: %d", res);

        traits->kind = (traits->name.find("encoder") != std::string::npos)
                           ? C2Component::KIND_ENCODER
                       : (traits->name.find("decoder") != std::string::npos)
                           ? C2Component::KIND_DECODER
                           : C2Component::KIND_OTHER;
    }

    const uint32_t mediaTypeIndex =
        (traits->kind == C2Component::KIND_ENCODER)
            ? C2PortMediaTypeSetting::output::PARAM_TYPE
            : C2PortMediaTypeSetting::input::PARAM_TYPE;

    std::vector<std::unique_ptr<C2Param>> params;
    res = intf.query_vb({}, {mediaTypeIndex}, C2_MAY_BLOCK, &params);
    if (res != C2_OK) {
        ALOGD("failed to query interface: %d", res);
        return {};
    }
    if (params.size() != 1u) {
        ALOGD("failed to query interface: unexpected vector size: %zu",
              params.size());
        return {};
    }

    C2PortMediaTypeSetting *mediaTypeConfig =
        C2PortMediaTypeSetting::From(params[0].get());
    if (mediaTypeConfig == nullptr) {
        ALOGD("failed to query media type");
        return {};
    }
    traits->mediaType = std::string(
        mediaTypeConfig->m.value,
        strnlen(mediaTypeConfig->m.value, mediaTypeConfig->flexCount()));

    if (fixDomain) {
        if (strncmp(traits->mediaType.c_str(), "audio/", 6) == 0) {
            traits->domain = C2Component::DOMAIN_AUDIO;
        } else if (strncmp(traits->mediaType.c_str(), "video/", 6) == 0) {
            traits->domain = C2Component::DOMAIN_VIDEO;
        } else if (strncmp(traits->mediaType.c_str(), "image/", 6) == 0) {
            traits->domain = C2Component::DOMAIN_IMAGE;
        } else {
            traits->domain = C2Component::DOMAIN_OTHER;
        }
    }

    // TODO: get this properly from the store during emplace
    switch (traits->domain) {
    case C2Component::DOMAIN_AUDIO:
        traits->rank = 8;
        break;
    default:
        traits->rank = 512;
    }

    params.clear();
    res = intf.query_vb({}, {C2ComponentAliasesSetting::PARAM_TYPE},
                         C2_MAY_BLOCK, &params);
    if (res == C2_OK && params.size() == 1u) {
        C2ComponentAliasesSetting *aliasesSetting =
            C2ComponentAliasesSetting::From(params[0].get());
        if (aliasesSetting) {
            // Split aliases on ','
            // This looks simpler in plain C and even std::string would
            // still make a copy.
            char *aliases = ::strndup(aliasesSetting->m.value,
                                      aliasesSetting->flexCount());
            ALOGD("'%s' has aliases: '%s'", intf.getName().c_str(), aliases);

            for (char *tok, *ptr, *str = aliases;
                 (tok = ::strtok_r(str, ",", &ptr)); str = nullptr) {
                traits->aliases.push_back(tok);
                ALOGD("adding alias: '%s'", tok);
            }
            free(aliases);
        }
    }

    return traits;
}

} // namespace goldfish::media::c2
