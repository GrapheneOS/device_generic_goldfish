#include <log/log.h>

#include "goldfish_media_utils.h"
#include "goldfish_vpx_defs.h"
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <mutex>
#include <vector>

namespace {
uint64_t s_CtxId = 0;
std::mutex sCtxidMutex;

uint64_t applyForOneId() {
    std::lock_guard<std::mutex> g{sCtxidMutex};
    ++s_CtxId;
    return s_CtxId;
}

void getVpxFrame(const uint8_t *ptr, vpx_image_t &myImg) {
    myImg.fmt = *reinterpret_cast<const vpx_img_fmt_t *>(ptr + 8);
    myImg.d_w = *reinterpret_cast<const uint32_t *>(ptr + 16);
    myImg.d_h = *reinterpret_cast<const uint32_t *>(ptr + 24);
    myImg.user_priv = reinterpret_cast<void*>(
            *reinterpret_cast<const uint64_t *>(ptr + 32));
}

}  // namespace

VpxCodecCtx::VpxCodecCtx(uint8_t mVpVersion, int version)
        : mVersion(version), mVpVersion(mVpVersion) {}

VpxCodecCtx::~VpxCodecCtx() {
    if (mMemorySlot >= 0) {
        auto transport = GoldfishMediaTransport::getInstance();

        transport->writeParam(mId, 0, mAddressOffset);
        sendOperation(MediaOperation::DestroyContext);

        transport->returnMemorySlot(mMemorySlot);
    }
}

int VpxCodecCtx::init() {
    auto transport = GoldfishMediaTransport::getInstance();
    int slot = transport->getMemorySlot();
    if (slot < 0) {
        ALOGE("ERROR: Failed %s:%d: cannot get memory slot", __func__, __LINE__);
        return -1;
    }

    mMemorySlot = slot;
    mId = applyForOneId();
    mAddressOffset = static_cast<uint64_t>(mMemorySlot) << 20;
    // data and dst are on the host side actually
    mData = transport->getInputAddr(mAddressOffset);
    mDst = mData; // re-use input address

    transport->writeParam(mId, 0, mAddressOffset);
    transport->writeParam(mVersion, 1, mAddressOffset);
    sendOperation(MediaOperation::InitContext);
    return 0;
}

void VpxCodecCtx::setupParameters(const uint32_t width, const uint32_t height,
                                  const int hostColorBufferId,
                                  const uint32_t outputBufferWidth,
                                  const uint32_t outputBufferHeight,
                                  const uint8_t bpp) {
    mWidth = width;
    mHeight = height;
    mHostColorBufferId = hostColorBufferId;
    mOutputBufferWidth = outputBufferWidth;
    mOutputBufferHeight = outputBufferHeight;
    mBpp = bpp;
}

const vpx_image_t* VpxCodecCtx::getFrame(const int hostColorBufferId) {
    auto transport = GoldfishMediaTransport::getInstance();

    transport->writeParam(mId, 0, mAddressOffset);
    transport->writeParam(mOutputBufferWidth, 1, mAddressOffset);
    transport->writeParam(mOutputBufferHeight, 2, mAddressOffset);
    transport->writeParam(mWidth, 3, mAddressOffset);
    transport->writeParam(mHeight, 4, mAddressOffset);
    transport->writeParam(mBpp, 5, mAddressOffset);
    transport->writeParam(hostColorBufferId, 6, mAddressOffset);
    transport->writeParam(transport->offsetOf((uint64_t)(mDst)) - mAddressOffset,
                          7, mAddressOffset);

    sendOperation(MediaOperation::GetImage);

    const uint8_t* retptr = transport->getReturnAddr(mAddressOffset);
    int ret = *reinterpret_cast<const int*>(retptr);
    if (ret) {
        return nullptr;
    }

    getVpxFrame(retptr, mImg);
    return &mImg;
}

const uint8_t* VpxCodecCtx::getDst() const {
    return mDst;
}

void VpxCodecCtx::sendMetadata(const MetaDataColorAspects& meta) const {
    auto transport = GoldfishMediaTransport::getInstance();

    transport->writeParam(mId, 0, mAddressOffset);
    transport->writeParam(meta.type, 1, mAddressOffset);
    transport->writeParam(meta.primaries, 2, mAddressOffset);
    transport->writeParam(meta.range, 3, mAddressOffset);
    transport->writeParam(meta.transfer, 4, mAddressOffset);

    sendOperation(MediaOperation::SendMetadata);
}

int VpxCodecCtx::decode(const uint8_t *data, const size_t dataSz,
                        void *userPriv, long /*deadline*/) {
    auto transport = GoldfishMediaTransport::getInstance();
    memcpy(mData, data, dataSz);

    transport->writeParam(mId, 0, mAddressOffset);
    transport->writeParam(transport->offsetOf((uint64_t)(mData)) - mAddressOffset,
                          1, mAddressOffset);
    transport->writeParam(uint64_t(dataSz), 2, mAddressOffset);
    transport->writeParam(reinterpret_cast<uint64_t>(userPriv), 3, mAddressOffset);

    sendOperation(MediaOperation::DecodeImage);
    return 0;
}

int VpxCodecCtx::flush() {
    auto transport = GoldfishMediaTransport::getInstance();
    transport->writeParam(mId, 0, mAddressOffset);
    sendOperation(MediaOperation::Flush);
    return 0;
}

void VpxCodecCtx::sendOperation(const MediaOperation op) const {
    auto transport = GoldfishMediaTransport::getInstance();

    transport->sendOperation(((mVpVersion == 9) ? MediaCodecType::VP9Codec
                                                : MediaCodecType::VP8Codec),
                             op, mAddressOffset);
}