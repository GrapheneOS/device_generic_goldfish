/*
 * Copyright (C) 2017 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "SimpleC2Interface"
#include <utils/Log.h>

// use MediaDefs here vs. MediaCodecConstants as this is not MediaCodec
// specific/dependent
#include <media/stagefright/foundation/MediaDefs.h>

#include <C2PlatformSupport.h>
#include <SimpleC2Interface.h>

namespace goldfish::media::c2 {

using ::android::C2PlatformAllocatorStore;
using ::android::GetCodec2PoolMask;
using ::android::GetPreferredLinearAllocatorId;

namespace {
C2R SubscribedParamIndicesSetter(
        bool mayBlock, C2InterfaceHelper::C2P<C2SubscribedParamIndicesTuning> &me) {
    (void)mayBlock;
    (void)me;

    return C2R::Ok();
}
}  // namespace

C2BaseParams::C2BaseParams(const std::shared_ptr<C2ReflectorHelper> &reflector,
                           const C2String& name,
                           C2Component::kind_t kind,
                           C2Component::domain_t domain,
                           const C2String& mediaType,
                           const std::vector<C2String>& aliases)
        : C2InterfaceHelper(reflector) {
    setDerivedInstance(this);

    addParameter(DefineParam(mName, C2_PARAMKEY_COMPONENT_NAME)
                     .withConstValue(AllocSharedString<C2ComponentNameSetting>(name))
                     .build());

    if (aliases.size()) {
        C2String joined;
        for (const C2String &alias : aliases) {
            if (joined.length()) {
                joined += ",";
            }
            joined += alias;
        }
        addParameter(
            DefineParam(mAliases, C2_PARAMKEY_COMPONENT_ALIASES)
                .withConstValue(AllocSharedString<C2ComponentAliasesSetting>(joined))
                .build());
    }

    addParameter(DefineParam(mKind, C2_PARAMKEY_COMPONENT_KIND)
                     .withConstValue(std::make_shared<C2ComponentKindSetting>(kind))
                     .build());

    addParameter(DefineParam(mDomain, C2_PARAMKEY_COMPONENT_DOMAIN)
                     .withConstValue(std::make_shared<C2ComponentDomainSetting>(domain))
                     .build());

    // simple interfaces have single streams
    addParameter(DefineParam(mInputStreamCount, C2_PARAMKEY_INPUT_STREAM_COUNT)
                     .withConstValue(std::make_shared<C2PortStreamCountTuning::input>(1))
                     .build());

    addParameter(
        DefineParam(mOutputStreamCount, C2_PARAMKEY_OUTPUT_STREAM_COUNT)
            .withConstValue(std::make_shared<C2PortStreamCountTuning::output>(1))
            .build());

    addParameter(
        DefineParam(mRequestedInputDelay, C2_PARAMKEY_INPUT_DELAY_REQUEST)
            .withConstValue(std::make_shared<C2PortRequestedDelayTuning::input>(0u))
            .build());

    addParameter(
        DefineParam(mActualInputDelay, C2_PARAMKEY_INPUT_DELAY)
            .withConstValue(std::make_shared<C2PortActualDelayTuning::input>(0u))
            .build());

    addParameter(
        DefineParam(mMaxInputReferenceAge, C2_PARAMKEY_INPUT_MAX_REFERENCE_AGE)
            .withConstValue(std::make_shared<C2StreamMaxReferenceAgeTuning::input>(0u))
            .build());

    addParameter(
        DefineParam(mMaxInputReferenceCount, C2_PARAMKEY_INPUT_MAX_REFERENCE_COUNT)
            .withConstValue(std::make_shared<C2StreamMaxReferenceCountTuning::input>(0u))
            .build());

    addParameter(
        DefineParam(mMaxOutputReferenceAge, C2_PARAMKEY_OUTPUT_MAX_REFERENCE_AGE)
            .withConstValue(std::make_shared<C2StreamMaxReferenceAgeTuning::output>(0u))
            .build());

    addParameter(
        DefineParam(mMaxOutputReferenceCount, C2_PARAMKEY_OUTPUT_MAX_REFERENCE_COUNT)
            .withConstValue(std::make_shared<C2StreamMaxReferenceCountTuning::output>(0u))
            .build());

    addParameter(
        DefineParam(mPrivateAllocators, C2_PARAMKEY_PRIVATE_ALLOCATORS)
            .withConstValue(C2PrivateAllocatorsTuning::AllocShared(0u))
            .build());

    addParameter(
        DefineParam(mMaxPrivateBufferCount, C2_PARAMKEY_MAX_PRIVATE_BUFFER_COUNT)
            .withConstValue(C2MaxPrivateBufferCountTuning::AllocShared(0u))
            .build());

    addParameter(
        DefineParam(mPrivatePoolIds, C2_PARAMKEY_PRIVATE_BLOCK_POOLS)
            .withConstValue(C2PrivateBlockPoolsTuning::AllocShared(0u))
            .build());

    addParameter(
        DefineParam(mTimeStretch, C2_PARAMKEY_TIME_STRETCH)
            .withConstValue(std::make_shared<C2ComponentTimeStretchTuning>(1.f))
            .build());

    // default to linear buffers and no media type
    C2BufferData::type_t rawBufferType = C2BufferData::LINEAR;
    C2String rawMediaType;
    C2Allocator::id_t rawAllocator = C2AllocatorStore::DEFAULT_LINEAR;
    C2BlockPool::local_id_t rawPoolId = C2BlockPool::BASIC_LINEAR;
    C2BufferData::type_t codedBufferType = C2BufferData::LINEAR;
    int poolMask = GetCodec2PoolMask();
    C2Allocator::id_t preferredLinearId =
        GetPreferredLinearAllocatorId(poolMask);
    C2Allocator::id_t codedAllocator = preferredLinearId;
    C2BlockPool::local_id_t codedPoolId = C2BlockPool::BASIC_LINEAR;

    switch (domain) {
    case C2Component::DOMAIN_IMAGE:
        [[fallthrough]];
    case C2Component::DOMAIN_VIDEO:
        // TODO: should we define raw image? The only difference is timestamp
        // handling
        rawBufferType = C2BufferData::GRAPHIC;
        rawMediaType = ::android::MEDIA_MIMETYPE_VIDEO_RAW;
        rawAllocator = C2PlatformAllocatorStore::GRALLOC;
        rawPoolId = C2BlockPool::BASIC_GRAPHIC;
        break;
    case C2Component::DOMAIN_AUDIO:
        rawBufferType = C2BufferData::LINEAR;
        rawMediaType = ::android::MEDIA_MIMETYPE_AUDIO_RAW;
        rawAllocator = preferredLinearId;
        rawPoolId = C2BlockPool::BASIC_LINEAR;
        break;
    default:
        break;
    }
    bool isEncoder = kind == C2Component::KIND_ENCODER;

    // handle raw decoders
    if (mediaType == rawMediaType) {
        codedBufferType = rawBufferType;
        codedAllocator = rawAllocator;
        codedPoolId = rawPoolId;
    }

    addParameter(DefineParam(mInputFormat, C2_PARAMKEY_INPUT_STREAM_BUFFER_TYPE)
                     .withConstValue(std::make_shared<C2StreamBufferTypeSetting::input>(
                         0u, isEncoder ? rawBufferType : codedBufferType))
                     .build());

    addParameter(
        DefineParam(mInputMediaType, C2_PARAMKEY_INPUT_MEDIA_TYPE)
            .withConstValue(AllocSharedString<C2PortMediaTypeSetting::input>(
                isEncoder ? rawMediaType : mediaType))
            .build());

    addParameter(
        DefineParam(mOutputFormat, C2_PARAMKEY_OUTPUT_STREAM_BUFFER_TYPE)
            .withConstValue(std::make_shared<C2StreamBufferTypeSetting::output>(
                0u, isEncoder ? codedBufferType : rawBufferType))
            .build());

    addParameter(
        DefineParam(mOutputMediaType, C2_PARAMKEY_OUTPUT_MEDIA_TYPE)
            .withConstValue(AllocSharedString<C2PortMediaTypeSetting::output>(
                isEncoder ? mediaType : rawMediaType))
            .build());

    C2Allocator::id_t inputAllocators[1] = {isEncoder ? rawAllocator
                                                      : codedAllocator};
    C2Allocator::id_t outputAllocators[1] = {isEncoder ? codedAllocator
                                                       : rawAllocator};
    C2BlockPool::local_id_t outputPoolIds[1] = {isEncoder ? codedPoolId
                                                          : rawPoolId};

    addParameter(
        DefineParam(mInputAllocators, C2_PARAMKEY_INPUT_ALLOCATORS)
            .withDefault(
                C2PortAllocatorsTuning::input::AllocShared(inputAllocators))
            .withFields({C2F(mInputAllocators, m.values[0]).any(),
                         C2F(mInputAllocators, m.values).inRange(0, 1)})
            .withSetter(
                Setter<
                    C2PortAllocatorsTuning::input>::NonStrictValuesWithNoDeps)
            .build());

    addParameter(
        DefineParam(mOutputAllocators, C2_PARAMKEY_OUTPUT_ALLOCATORS)
            .withDefault(
                C2PortAllocatorsTuning::output::AllocShared(outputAllocators))
            .withFields({C2F(mOutputAllocators, m.values[0]).any(),
                         C2F(mOutputAllocators, m.values).inRange(0, 1)})
            .withSetter(
                Setter<
                    C2PortAllocatorsTuning::output>::NonStrictValuesWithNoDeps)
            .build());

    addParameter(
        DefineParam(mOutputPoolIds, C2_PARAMKEY_OUTPUT_BLOCK_POOLS)
            .withDefault(
                C2PortBlockPoolsTuning::output::AllocShared(outputPoolIds))
            .withFields({C2F(mOutputPoolIds, m.values[0]).any(),
                         C2F(mOutputPoolIds, m.values).inRange(0, 1)})
            .withSetter(
                Setter<
                    C2PortBlockPoolsTuning::output>::NonStrictValuesWithNoDeps)
            .build());

    // add stateless params
    addParameter(
        DefineParam(mSubscribedParamIndices,
                    C2_PARAMKEY_SUBSCRIBED_PARAM_INDICES)
            .withDefault(C2SubscribedParamIndicesTuning::AllocShared(0u))
            .withFields({C2F(mSubscribedParamIndices, m.values[0]).any(),
                         C2F(mSubscribedParamIndices, m.values).any()})
            .withSetter(SubscribedParamIndicesSetter)
            .build());
}

}  // namespace goldfish::media::c2
