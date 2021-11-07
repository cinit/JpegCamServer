#include <iostream>
#include <mutex>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <opencv2/opencv.hpp>
#include <condition_variable>

#include "mmtcp/TcpServerSocket.h"
#include "utils/Time.h"
#include "mmtcp/MmTcpV2.h"

using namespace std;
using cv::VideoCapture;

std::condition_variable gFrameReadyStatus;
std::mutex globalMutex;
cv::Mat globalSharedMat;
volatile uint64_t gFrameTime = 0;
volatile uint32_t gFrameCost = 0;

void sleepms(int ms) {
    usleep(1000 * ms);
}

cv::Mat fix_distortion(const cv::Mat &img, int index) {
    return img;
}

void sendImageThread() {
    TcpServerSocket serverSocket;
    if (int err; (err = serverSocket.setListenPort(8003)) != 0) {
        cout << "unable to listen port: " << strerror(-err) << endl;
        abort();
    }
    for (int fd; (fd = serverSocket.accept()) > 0;) {
        MmTcpV2 mmTcpV2;
        mmTcpV2.setSocket(fd);
        while (true) {
            cv::Mat targetImg;
            uint32_t deltaTime;
            uint64_t startTime;
            {
                std::unique_lock lk(globalMutex);
                gFrameReadyStatus.wait(lk);
                targetImg = fix_distortion(globalSharedMat, 2 - 1);
                deltaTime = gFrameCost;
                startTime = gFrameTime;
            }
//            cout << deltaTime << endl;
            if (int err; (err = mmTcpV2.writeImage(targetImg, startTime, deltaTime)) != 0) {
                cout << "send error: " << err << strerror(-err) << endl;
                break;
            }
            sleepms(10);
        }
    }
    cout << "sendImageThread exited unexpectedly" << endl;
}

int main() {
    cv::VideoCapture capture = VideoCapture(0);
    if (!capture.isOpened()) {
        cout << "unable to open camera" << endl;
        return 1;
    }
    std::thread localSendImageThread(&sendImageThread);
    cv::namedWindow("MainWindow", cv::WINDOW_AUTOSIZE);
    cv::moveWindow("MainWindow", 0, 0);
    uint64_t lastUpdate = 0;
    while (true) {
        cv::Mat rawImg;
        uint64_t st = getRelativeTimeMs();
        capture >> rawImg;
        uint32_t cost = uint32_t(getRelativeTimeMs() - st);
        {
            std::scoped_lock lk(globalMutex);
            rawImg.copyTo(globalSharedMat);
            gFrameCost = cost;
            gFrameTime = st;
        }
        gFrameReadyStatus.notify_all();
        if (st - lastUpdate > 100) {
            cv::imshow("MainWindow", rawImg);
            if (cv::waitKey(1) == '1') {
                break;
            }
            lastUpdate = st;
        }
    }
    return 3;
}
