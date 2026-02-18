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
template<typename T> using C2P = C2BaseParams::C2P<T>;

constexpr uint32_t kDefaultOutputDelay = 8;
constexpr uint32_t kMaxOutputDelay = 16;
constexpr size_t kMinInputBufferSize = 6 * 1024 * 1024;

C2R SubscribedParamIndicesSetter(bool /*mayBlock*/,
                                 C2InterfaceHelper::C2P<C2SubscribedParamIndicesTuning>& /*me*/) {
    return C2R::Ok();
}

C2R SizeSetter(bool /*mayBlock*/,
               const C2BaseParams::C2P<C2StreamPictureSizeInfo::output>& oldMe,
               C2BaseParams::C2P<C2StreamPictureSizeInfo::output>& me) {
    C2R res = C2R::Ok();
    if (!me.F(me.v.width).supportsAtAll(me.v.width)) {
        ALOGW("w %d is not supported, using old one %d", me.v.width, oldMe.v.width);
        res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.width)));
        me.set().width = oldMe.v.width;
    }

    if (!me.F(me.v.height).supportsAtAll(me.v.height)) {
        ALOGW("h %d is not supported, using old one %d", me.v.height, oldMe.v.height);
        res = res.plus(C2SettingResultBuilder::BadValue(me.F(me.v.height)));
        me.set().height = oldMe.v.height;
    }

    return res;
}

C2R MaxPictureSizeSetter(bool /*mayBlock*/,
                         C2P<C2StreamMaxPictureSizeTuning::output> &me,
                         const C2P<C2StreamPictureSizeInfo::output> &size) {
    me.set().width = c2_min(c2_max(me.v.width, size.v.width), 4096u);
    me.set().height = c2_min(c2_max(me.v.height, size.v.height), 4096u);
    return C2R::Ok();
}

C2R MaxInputSizeSetter(bool /*mayBlock*/,
                       C2P<C2StreamMaxBufferSizeInfo::input> &me,
                       const C2P<C2StreamMaxPictureSizeTuning::output> &maxSize) {
    // assume compression ratio of 2
    me.set().value = c2_max((((maxSize.v.width + 15) / 16) *
                                ((maxSize.v.height + 15) / 16) * 192),
                            kMinInputBufferSize);
    return C2R::Ok();
}

C2R DefaultColorAspectsSetter(bool /*mayBlock*/,
                              C2P<C2StreamColorAspectsTuning::output> &me) {
    if (me.v.range > C2Color::RANGE_OTHER) {
        me.set().range = C2Color::RANGE_OTHER;
    }
    if (me.v.primaries > C2Color::PRIMARIES_OTHER) {
        me.set().primaries = C2Color::PRIMARIES_OTHER;
    }
    if (me.v.transfer > C2Color::TRANSFER_OTHER) {
        me.set().transfer = C2Color::TRANSFER_OTHER;
    }
    if (me.v.matrix > C2Color::MATRIX_OTHER) {
        me.set().matrix = C2Color::MATRIX_OTHER;
    }

    return C2R::Ok();
}

C2R CodedColorAspectsSetter(bool /*mayBlock*/,
                            C2P<C2StreamColorAspectsInfo::input> &me) {
    if (me.v.range > C2Color::RANGE_OTHER) {
        me.set().range = C2Color::RANGE_OTHER;
    }
    if (me.v.primaries > C2Color::PRIMARIES_OTHER) {
        me.set().primaries = C2Color::PRIMARIES_OTHER;
    }
    if (me.v.transfer > C2Color::TRANSFER_OTHER) {
        me.set().transfer = C2Color::TRANSFER_OTHER;
    }
    if (me.v.matrix > C2Color::MATRIX_OTHER) {
        me.set().matrix = C2Color::MATRIX_OTHER;
    }

    return C2R::Ok();
}

C2R ColorAspectsSetter(bool /*mayBlock*/,
                       C2P<C2StreamColorAspectsInfo::output> &me,
                       const C2P<C2StreamColorAspectsTuning::output> &def,
                       const C2P<C2StreamColorAspectsInfo::input> &coded) {
    // take default values for all unspecified fields, and coded values for
    // specified ones

    me.set().range = coded.v.range == RANGE_UNSPECIFIED ? def.v.range : coded.v.range;
    me.set().primaries = coded.v.primaries == PRIMARIES_UNSPECIFIED
                                ? def.v.primaries
                                : coded.v.primaries;
    me.set().transfer = coded.v.transfer == TRANSFER_UNSPECIFIED
                            ? def.v.transfer
                            : coded.v.transfer;
    me.set().matrix = coded.v.matrix == MATRIX_UNSPECIFIED ? def.v.matrix
                                                            : coded.v.matrix;
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

    addParameter(
        DefineParam(mActualOutputDelay, C2_PARAMKEY_OUTPUT_DELAY)
            .withDefault(std::make_shared<C2PortActualDelayTuning::output>(kDefaultOutputDelay))
            .withFields({C2F(mActualOutputDelay, value).inRange(0, kMaxOutputDelay)})
            .withSetter(Setter<decltype(*mActualOutputDelay)>::StrictValueWithNoDeps)
            .build());

    addParameter(
        DefineParam(mAttrib, C2_PARAMKEY_COMPONENT_ATTRIBUTES)
            .withConstValue(
                std::make_shared<C2ComponentAttributesSetting>(C2Component::ATTRIB_IS_TEMPORAL))
            .build());

    addParameter(
        DefineParam(mSize, C2_PARAMKEY_PICTURE_SIZE)
            .withDefault(std::make_shared<C2StreamPictureSizeInfo::output>(0u, 320, 240))
            .withFields({
                C2F(mSize, width).inRange(2, 4096, 2),
                C2F(mSize, height).inRange(2, 4096, 2),
            })
            .withSetter(SizeSetter)
            .build());

    addParameter(
        DefineParam(mMaxSize, C2_PARAMKEY_MAX_PICTURE_SIZE)
            .withDefault(std::make_shared<C2StreamMaxPictureSizeTuning::output>(0u, 320, 240))
            .withFields({
                C2F(mSize, width).inRange(2, 4096, 2),
                C2F(mSize, height).inRange(2, 4096, 2),
            })
            .withSetter(MaxPictureSizeSetter, mSize)
            .build());

    addParameter(
        DefineParam(mMaxInputSize, C2_PARAMKEY_INPUT_MAX_BUFFER_SIZE)
            .withDefault(std::make_shared<C2StreamMaxBufferSizeInfo::input>(0u, kMinInputBufferSize))
            .withFields({ C2F(mMaxInputSize, value).any(), })
            .calculatedAs(MaxInputSizeSetter, mMaxSize)
            .build());

    addParameter(
        DefineParam(mDefaultColorAspects, C2_PARAMKEY_DEFAULT_COLOR_ASPECTS)
            .withDefault(std::make_shared<C2StreamColorAspectsTuning::output>(
                0u,
                C2Color::RANGE_UNSPECIFIED,
                C2Color::PRIMARIES_UNSPECIFIED,
                C2Color::TRANSFER_UNSPECIFIED,
                C2Color::MATRIX_UNSPECIFIED))
            .withFields({
                C2F(mDefaultColorAspects, range)
                    .inRange(C2Color::RANGE_UNSPECIFIED, C2Color::RANGE_OTHER),
                C2F(mDefaultColorAspects, primaries)
                    .inRange(C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                C2F(mDefaultColorAspects, transfer)
                    .inRange(C2Color::TRANSFER_UNSPECIFIED, C2Color::TRANSFER_OTHER),
                C2F(mDefaultColorAspects, matrix)
                    .inRange(C2Color::MATRIX_UNSPECIFIED, C2Color::MATRIX_OTHER)
            })
            .withSetter(DefaultColorAspectsSetter)
            .build());

    addParameter(
        DefineParam(mCodedColorAspects, C2_PARAMKEY_VUI_COLOR_ASPECTS)
            .withDefault(std::make_shared<C2StreamColorAspectsInfo::input>(
                0u,
                C2Color::RANGE_LIMITED,
                C2Color::PRIMARIES_UNSPECIFIED,
                C2Color::TRANSFER_UNSPECIFIED,
                C2Color::MATRIX_UNSPECIFIED))
            .withFields({
                C2F(mCodedColorAspects, range)
                    .inRange(C2Color::RANGE_UNSPECIFIED, C2Color::RANGE_OTHER),
                C2F(mCodedColorAspects, primaries)
                    .inRange(C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                C2F(mCodedColorAspects, transfer)
                    .inRange(C2Color::TRANSFER_UNSPECIFIED, C2Color::TRANSFER_OTHER),
                C2F(mCodedColorAspects, matrix)
                    .inRange(C2Color::MATRIX_UNSPECIFIED, C2Color::MATRIX_OTHER),
            })
            .withSetter(CodedColorAspectsSetter)
            .build());

    addParameter(
        DefineParam(mColorAspects, C2_PARAMKEY_COLOR_ASPECTS)
            .withDefault(std::make_shared<C2StreamColorAspectsInfo::output>(
                0u, C2Color::RANGE_UNSPECIFIED,
                C2Color::PRIMARIES_UNSPECIFIED,
                C2Color::TRANSFER_UNSPECIFIED, C2Color::MATRIX_UNSPECIFIED))
            .withFields({
                C2F(mColorAspects, range)
                    .inRange(C2Color::RANGE_UNSPECIFIED, C2Color::RANGE_OTHER),
                C2F(mColorAspects, primaries)
                    .inRange(C2Color::PRIMARIES_UNSPECIFIED, C2Color::PRIMARIES_OTHER),
                C2F(mColorAspects, transfer)
                    .inRange(C2Color::TRANSFER_UNSPECIFIED, C2Color::TRANSFER_OTHER),
                C2F(mColorAspects, matrix)
                    .inRange(C2Color::MATRIX_UNSPECIFIED, C2Color::MATRIX_OTHER)
            })
            .withSetter(ColorAspectsSetter, mDefaultColorAspects, mCodedColorAspects)
            .build());

    addParameter(
        DefineParam(mPixelFormat, C2_PARAMKEY_PIXEL_FORMAT)
            .withConstValue(std::make_shared<C2StreamPixelFormatInfo::output>(
                0u,
                HAL_PIXEL_FORMAT_YCBCR_420_888))
            .build());

    reflector->addStructDescriptors<C2ChromaOffsetStruct>();

    addParameter(
        DefineParam(mColorInfo, C2_PARAMKEY_CODED_COLOR_INFO)
            .withConstValue(
                C2StreamColorInfo::output::AllocShared(
                    {C2ChromaOffsetStruct::ITU_YUV_420_0()},
                    0u,
                    8u /* bitDepth */,
                    C2Color::YUV_420))
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
