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

#define LOG_TAG "GoldfishComponentStore"

#include <goldfish_codec2/store/GoldfishComponentStore.h>

#include <dlfcn.h>
#include <stdint.h>

#include <memory>
#include <mutex>

#include <C2.h>
#include <C2Config.h>
#include <cutils/properties.h>
#include <log/log.h>

namespace android {
namespace {
template <class FP> bool lookupLibFunc(FP& fp, void* lib, const char* name, const char* libPath) {
    void* sym = ::dlsym(lib, name);
    if (sym) {
        fp = reinterpret_cast<FP>(sym);
        return true;
    } else {
        ALOGE("Could not find '%s' in '%s'", name, libPath);
        return false;
    }
}

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

// static
std::shared_ptr<C2ComponentStore> GoldfishComponentStore::Create() {
    static std::mutex mutex;
    static std::weak_ptr<C2ComponentStore> cachedStore;

    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<C2ComponentStore> store = cachedStore.lock();
    if (store) {
        return store;
    }

    store = std::make_shared<GoldfishComponentStore>(Private());
    cachedStore = store;
    return store;
}

GoldfishComponentStore::GoldfishComponentStore(Private)
        : mReflector(std::make_shared<C2ReflectorHelper>()) {
    if (useAndroidGoldfishComponentInstance("vpxdec")) {
        mComponentLoaders.emplace_back("libcodec2_goldfish_vp8dec.so");
        mComponentLoaders.emplace_back("libcodec2_goldfish_vp9dec.so");
    }
    if (useAndroidGoldfishComponentInstance("avcdec")) {
        mComponentLoaders.emplace_back("libcodec2_goldfish_avcdec.so");
    }
    if (useAndroidGoldfishComponentInstance("hevcdec")) {
        mComponentLoaders.emplace_back("libcodec2_goldfish_hevcdec.so");
    }
}

C2String GoldfishComponentStore::getName() const {
    using namespace std::literals::string_literals;
    return "android.componentStore.goldfish"s;
}

c2_status_t GoldfishComponentStore::createComponent(const C2String name,
                                                    std::shared_ptr<C2Component> *const component) {
    auto [res, module] = findComponent(name);
    if (res == C2_OK) {
        // TODO: get a unique node ID
        res = module->createComponent(0, component, std::default_delete<C2Component>());
    }

    return res;
}

c2_status_t GoldfishComponentStore::createInterface(
        const C2String name,
        std::shared_ptr<C2ComponentInterface> *const interface) {
    auto [res, module] = findComponent(name);
    if (res == C2_OK) {
        // TODO: get a unique node ID
        res = module->createInterface(0, interface, std::default_delete<C2ComponentInterface>());
    }
    return res;
}

std::vector<std::shared_ptr<const C2Component::Traits>> GoldfishComponentStore::listComponents() {
    visitComponents();
    return mComponentList;
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

std::pair<c2_status_t, std::shared_ptr<GoldfishComponentStore::ComponentModule>>
GoldfishComponentStore::findComponent(const C2String& name) {
    visitComponents();

    auto i = mComponentLoaderIndex.find(name);
    if (i != mComponentLoaderIndex.end()) {
        std::lock_guard<std::mutex> lock(mMutex);
        return mComponentLoaders[i->second].fetch();
    }
    return {C2_NOT_FOUND, {}};
}

void GoldfishComponentStore::visitComponents() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mComponentList.empty()) {
        return;
    }

    const unsigned n = mComponentLoaders.size();
    for (unsigned i = 0; i < n; ++i) {
        ComponentLoader& loader = mComponentLoaders[i];
        const auto [res, module] = loader.fetch();
        if (res == C2_OK) {
            std::shared_ptr<const C2Component::Traits> traits = module->getTraits();
            if (traits) {
                mComponentList.push_back(traits);
                for (const C2String &alias : traits->aliases) {
                    const auto [it, ok] = mComponentLoaderIndex.insert({alias, i});
                    if (!ok) {
                        ALOGE("Could not insert '%s' alias because it is "
                              "already occupied by '%s'", alias.c_str(),
                              mComponentLoaders[it->second].getLibPath().c_str());
                    }
                }
            } else {
                ALOGE("The module from '%s' does not have traits",
                      loader.getLibPath().c_str());
            }
        } else {
            ALOGE("Could not fetch the module from '%s'", loader.getLibPath().c_str());
        }
    }
}

/****************************** GoldfishComponentStore::ComponentModule ***************************/

void GoldfishComponentStore::ComponentModule::LibraryDeleter::operator()(void* h) const {
    ::dlclose(h);
}

c2_status_t GoldfishComponentStore::ComponentModule::init(const char* libPath) {
    ALOGI("loading dll of path %s", libPath);

    std::unique_ptr<void, LibraryDeleter> lib(::dlopen(libPath, RTLD_NOW | RTLD_NODELETE));
    if (!lib) {
        ALOGE("Could not dlopen '%s': %s", libPath, dlerror());
        return C2_NO_INIT;
    }

    C2ComponentFactory::DestroyCodec2FactoryFunc destroyFactory;
    if (!lookupLibFunc(destroyFactory, lib.get(), "DestroyCodec2Factory", libPath)) {
        return C2_NO_INIT;
    }

    C2ComponentFactory::CreateCodec2FactoryFunc createFactory;
    if (!lookupLibFunc(createFactory, lib.get(), "CreateCodec2Factory", libPath)) {
        return C2_NO_INIT;
    }

    C2ComponentFactoryHandle componentFactory(createFactory(), destroyFactory);
    if (!componentFactory) {
        ALOGD("could not create factory in '%s'", libPath);
        return C2_NO_MEMORY;
    }

    std::shared_ptr<C2ComponentInterface> intf;
    c2_status_t res = createInterfaceImpl(0, &intf,
                                          std::default_delete<C2ComponentInterface>(),
                                          *componentFactory);
    if (res != C2_OK) {
        ALOGD("failed to create interface: %d", res);
        return res;
    }

    std::tie(res, mTraits) = buildTraits(*intf);
    if (res != C2_OK) {
        return res;
    }

    mLib = std::move(lib);
    mComponentFactory = std::move(componentFactory);
    return C2_OK;
}

std::pair<c2_status_t, std::shared_ptr<C2Component::Traits>>
GoldfishComponentStore::ComponentModule::buildTraits(const C2ComponentInterface& intf) {
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
        return {res, {}};
    }
    if (params.size() != 1u) {
        ALOGD("failed to query interface: unexpected vector size: %zu",
              params.size());
        return {C2_NO_INIT, {}};
    }

    C2PortMediaTypeSetting *mediaTypeConfig =
        C2PortMediaTypeSetting::From(params[0].get());
    if (mediaTypeConfig == nullptr) {
        ALOGD("failed to query media type");
        return {C2_NO_INIT, {}};
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

    return {C2_OK, std::move(traits)};
}

c2_status_t GoldfishComponentStore::ComponentModule::createInterfaceImpl(
        const c2_node_id_t id,
        std::shared_ptr<C2ComponentInterface> *interface,
        std::function<void(::C2ComponentInterface *)> deleter,
        C2ComponentFactory& factory) const {
    return factory.createInterface(
        id, interface, [module = shared_from_this(), deleter = std::move(deleter)](C2ComponentInterface *p) {
            // capture module so that we ensure we still have it while deleting
            // interface
            deleter(p);     // delete interface first
        });
}

c2_status_t GoldfishComponentStore::ComponentModule::createInterface(
        const c2_node_id_t id,
        std::shared_ptr<C2ComponentInterface> *interface,
        std::function<void(::C2ComponentInterface *)> deleter) {
    return createInterfaceImpl(id, interface, std::move(deleter), *mComponentFactory);
}

c2_status_t GoldfishComponentStore::ComponentModule::createComponent(
        const c2_node_id_t id,
        std::shared_ptr<C2Component> *component,
        std::function<void(::C2Component *)> deleter) {
    return mComponentFactory->createComponent(
        id, component, [module = shared_from_this(), deleter = std::move(deleter)](C2Component *p) {
            // capture module so that we ensure we still have it while deleting component
            deleter(p);
        });
}

std::shared_ptr<const C2Component::Traits> GoldfishComponentStore::ComponentModule::getTraits() const {
    return mTraits;
}

/****************************** GoldfishComponentStore::ComponentLoader ***************************/

std::pair<c2_status_t, std::shared_ptr<GoldfishComponentStore::ComponentModule>>
GoldfishComponentStore::ComponentLoader::fetch() {
    std::shared_ptr<ComponentModule> localModule = mModuleCache.lock();
    if (localModule) {
        return {C2_OK, std::move(localModule)};
    }

    localModule = std::make_shared<ComponentModule>();
    const c2_status_t res = localModule->init(mLibPath.c_str());
    if (res != C2_OK) {
        return {res, {}};
    }

    mModuleCache = localModule;
    return {C2_OK, std::move(localModule)};
}

} // namespace android
