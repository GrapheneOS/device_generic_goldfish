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
#include <string_view>
#include <unordered_map>
#include <vector>

#include <C2Component.h>

#include <goldfish/media/c2/IComponentFactory.h>

namespace goldfish::media::c2 {

class GoldfishComponentStore : public C2ComponentStore {
  public:
    GoldfishComponentStore();

    virtual C2String getName() const override;

    virtual c2_status_t createComponent(C2String name,
                                        std::shared_ptr<C2Component> *const component) override;

    virtual c2_status_t createInterface(C2String name,
                                        std::shared_ptr<C2ComponentInterface> *const interface) override;

    virtual std::vector<std::shared_ptr<const C2Component::Traits>> listComponents() override;

    virtual c2_status_t copyBuffer(std::shared_ptr<C2GraphicBuffer> src,
                                   std::shared_ptr<C2GraphicBuffer> dst) override;


    virtual c2_status_t query_sm(const std::vector<C2Param *> &stackParams,
                                 const std::vector<C2Param::Index> &heapParamIndices,
                                 std::vector<std::unique_ptr<C2Param>> *const heapParams) const override;

    virtual c2_status_t config_sm(const std::vector<C2Param *> &params,
                                  std::vector<std::unique_ptr<C2SettingResult>> *const failures) override;

    virtual std::shared_ptr<C2ParamReflector> getParamReflector() const override;

    virtual c2_status_t querySupportedParams_nb(std::vector<std::shared_ptr<C2ParamDescriptor>> *const params) const override;

    virtual c2_status_t querySupportedValues_sm(std::vector<C2FieldSupportedValuesQuery> &fields) const override;

  private:
    std::shared_ptr<const IComponentFactory> findComponentFactory(const C2String& name) const;
    static std::shared_ptr<C2Component::Traits> buildTraits(const C2ComponentInterface& intf);

    const std::shared_ptr<C2ReflectorHelper> mReflector;
    std::unordered_map<C2String, std::shared_ptr<const IComponentFactory>> mComponentFactories;
    std::vector<std::shared_ptr<const C2Component::Traits>> mAllComponentTraits;
};

} // namespace goldfish::media::c2
