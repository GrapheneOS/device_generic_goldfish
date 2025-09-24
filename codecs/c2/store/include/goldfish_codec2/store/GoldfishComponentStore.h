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

#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include <C2Component.h>
#include <C2ComponentFactory.h>
#include <android-base/thread_annotations.h>
#include <util/C2InterfaceHelper.h>

namespace android {

class GoldfishComponentStore : public C2ComponentStore {
  public:
    static std::shared_ptr<C2ComponentStore> Create();

    virtual std::vector<std::shared_ptr<const C2Component::Traits>>
    listComponents() override;
    virtual std::shared_ptr<C2ParamReflector>
    getParamReflector() const override;
    virtual C2String getName() const override;
    virtual c2_status_t querySupportedValues_sm(
        std::vector<C2FieldSupportedValuesQuery> &fields) const override;
    virtual c2_status_t querySupportedParams_nb(
        std::vector<std::shared_ptr<C2ParamDescriptor>> *const params)
        const override;
    virtual c2_status_t query_sm(
        const std::vector<C2Param *> &stackParams,
        const std::vector<C2Param::Index> &heapParamIndices,
        std::vector<std::unique_ptr<C2Param>> *const heapParams) const override;
    virtual c2_status_t createInterface(
        C2String name,
        std::shared_ptr<C2ComponentInterface> *const interface) override;
    virtual c2_status_t
    createComponent(C2String name,
                    std::shared_ptr<C2Component> *const component) override;
    virtual c2_status_t
    copyBuffer(std::shared_ptr<C2GraphicBuffer> src,
               std::shared_ptr<C2GraphicBuffer> dst) override;
    virtual c2_status_t config_sm(
        const std::vector<C2Param *> &params,
        std::vector<std::unique_ptr<C2SettingResult>> *const failures) override;
    GoldfishComponentStore();

    virtual ~GoldfishComponentStore() override = default;

  private:
    struct ComponentModule
            : public C2ComponentFactory,
              public std::enable_shared_from_this<ComponentModule> {
        virtual ~ComponentModule() override;

        virtual c2_status_t
        createComponent(c2_node_id_t id,
                        std::shared_ptr<C2Component> *component,
                        ComponentDeleter deleter =
                            std::default_delete<C2Component>()) override;
        virtual c2_status_t createInterface(
            c2_node_id_t id, std::shared_ptr<C2ComponentInterface> *interface,
            InterfaceDeleter deleter =
                std::default_delete<C2ComponentInterface>()) override;

        c2_status_t init(const char* libPath);

        std::shared_ptr<const C2Component::Traits> getTraits() const;

      protected:
        void *mLibHandle = nullptr;
        C2ComponentFactory* mComponentFactory = nullptr;
        C2ComponentFactory::DestroyCodec2FactoryFunc mDestroyFactory = nullptr;
        std::shared_ptr<C2Component::Traits> mTraits;
    };

    struct ComponentLoader {
        explicit ComponentLoader(std::string libPath) : mLibPath(std::move(libPath)) {}

        std::pair<c2_status_t, std::shared_ptr<ComponentModule>> fetch();
        const std::string& getLibPath() const { return mLibPath; }

      private:
        const std::string mLibPath;
        std::weak_ptr<ComponentModule> mModuleCache;
    };

    /**
     * Retrieves the component module for a component by its name.
     *
     * \retval C2_OK        the component loader has been successfully retrieved
     * \retval C2_NO_MEMORY not enough memory to locate the component loader
     * \retval C2_NOT_FOUND could not locate the component to be loaded
     * \retval C2_CORRUPTED the component loader could not be identified due to
     * some modules being corrupted (this can happen if the name does not refer
     * to an already identified component but some components could not be
     * loaded due to bad library) \retval C2_REFUSED   permission denied to find
     * the component loader for the named component (this can happen if the name
     * does not refer to an already identified component but some components
     * could not be loaded due to lack of permissions)
     */
    std::pair<c2_status_t, std::shared_ptr<ComponentModule>> findComponent(const C2String& name);

    /**
     * Loads each component module and discover its contents. This cannot be
     * done in the ctor because the components need an instance of
     * GoldfishComponentStore.
     */
    void visitComponents();

    std::vector<ComponentLoader> mComponentLoaders;  // mMutex
    std::unordered_map<C2String, unsigned> mComponentLoaderIndex; // name->index
    std::vector<std::shared_ptr<const C2Component::Traits>> mComponentList;
    const std::shared_ptr<C2ReflectorHelper> mReflector;
    std::mutex mMutex;
};
} // namespace android
