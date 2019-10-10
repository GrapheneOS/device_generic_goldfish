#include <iostream>
#include <string>
#include <vector>

#include "fake-pipeline2/Base.h"
#include "QemuClient.h"

#include <linux/videodev2.h>
#include <utils/Timers.h>

using namespace android;

// Test the capture speed of qemu camera, e.g., webcam and virtual scene
int main(int argc, char* argv[]) {
    uint32_t pixFmt;
    if (!strncmp(argv[1], "RGB", 3)) {
        pixFmt = V4L2_PIX_FMT_RGB32;
    } else if (!strncmp(argv[1], "NV21", 3)) {
        pixFmt = V4L2_PIX_FMT_NV21;
    } else if (!strncmp(argv[1], "YV12", 3)) {
        pixFmt = V4L2_PIX_FMT_YVU420;
    } else {
        printf("format error, use RGB, NV21 or YV12");
        return -1;
    }
    uint32_t width = atoi(argv[2]);
    uint32_t height = atoi(argv[3]);
    std::string deviceName;
    if (!strncmp(argv[4], "web", 3)) {
        deviceName = "name=/dev/video0";
    } else if (!strncmp(argv[4], "vir", 3)) {
        deviceName = "name=virtualscene";
    } else {
        printf("device error, use web or virtual");
        return -1;
    }

    // Open qemu pipe
    CameraQemuClient client;
    int ret = client.connectClient(deviceName.c_str());
    if (ret != NO_ERROR) {
        printf("Failed to connect device\n");
        return -1;
    }
    ret = client.queryConnect();
    if (ret == NO_ERROR) {
        printf("Connected to device\n");
    } else {
        printf("Failed to connect device\n");
        return -1;
    }

    // Caputre ASAP
    ret = client.queryStart(pixFmt, width, height);
    if (ret != NO_ERROR) {
        printf("Failed to configure device for query\n");
        return -1;
    }
    size_t bufferSize;
    if (pixFmt == V4L2_PIX_FMT_RGB32) {
        bufferSize = width * height * 4;
    } else {
        bufferSize = width * height * 12 / 8;
    }
    std::vector<char> buffer(bufferSize, 0);
    float whiteBalance[] = {1.0f, 1.0f, 1.0f};
    float exposureCompensation = 1.0f;
    std::vector<nsecs_t> report;
    size_t repeated = 100;
    for (int i = 0 ; i < repeated; i++) {
        nsecs_t start = systemTime();
        client.queryFrame(buffer.data(), nullptr, 0, bufferSize,
                          whiteBalance[0], whiteBalance[1], whiteBalance[2],
                          exposureCompensation, nullptr);
        nsecs_t end = systemTime();
        report.push_back(end - start);
    }

    // Report
    nsecs_t average, sum = 0;
    for (int i = 0; i < repeated; i++) {
        sum += report[i];
    }
    average = sum / repeated;
    printf("Report for reading %d frames\n", repeated);
    printf("\ttime total: %lld\n", sum);
    printf("\tframe average: %lld\n", average);

    return 0;
}

