#include <iostream>
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/logging.h"
// #include "rtc_base/thread.h"

// #include "api/peer_connection_interface.h"
// #include "api/create_peerconnection_factory.h"
// #include "api/audio_codecs/builtin_audio_encoder_factory.h"
// #include "api/audio_codecs/builtin_audio_decoder_factory.h"
// #include "modules/audio_processing/include/audio_processing.h"
// #include "modules/audio_device/include/audio_device.h"

#include "ui/widg.h"
#include <QApplication>
#include <QUrl>
#include <X11/Xlib.h>
#include <thread>
#include <fstream>

#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/desktop_capture_options.h"
#include "modules/desktop_capture/desktop_frame.h"
#include "modules/desktop_capture/screen_capturer_helper.h"

#include "module/pushclient.h"

class MyDesktopCapturerCallback : public webrtc::DesktopCapturer::Callback
{
public:
  void OnFrameCaptureStart() override
  {
    std::cout << "[Callback] Frame capture started" << std::endl;
  }

  void OnCaptureResult(webrtc::DesktopCapturer::Result result,
                       std::unique_ptr<webrtc::DesktopFrame> frame) override
  {
    std::ofstream yuvfile("frame.yuv", std::ios::binary);
    if (result == webrtc::DesktopCapturer::Result::SUCCESS)
    {
      std::cout << "[Callback] Frame captured: "
                << frame->size().width() << "x" << frame->size().height() << "format" << frame->pixel_format() << std::endl;
      // 保存为 YUV 文件
      int width = frame->size().width();
      int height = frame->size().height();
      const uint8_t *data = frame->data();
      int stride = frame->stride();
      yuvfile.write(reinterpret_cast<const char *>(data), stride * height);
      yuvfile.close();
    }
    else
    {
      std::cout << "[Callback] Capture failed with result: " << static_cast<int>(result) << std::endl;
    }
  }
};

int main(int argc, char *argv[])
{
#if 0
    std::cout << "[webrtc-smoke] SSL initialized" << std::endl;

    //webrtc连接到一个信令服务器

#endif

  webrtc::LogMessage::SetLogToStderr(true);
  webrtc::LogMessage::LogToDebug(webrtc::LS_INFO); // 或者 rtc::LS_INFO
  std::cout << "[webrtc-smoke] Start" << std::endl;

  if (!webrtc::InitializeSSL())
  {
    RTC_LOG(LS_ERROR) << "Failed to initialize SSL";
    return 2;
  }
  if (!XInitThreads())
  {
    RTC_LOG(LS_ERROR) << "Failed to initialize XInitThreads";
    return 3;
  }

  QApplication a(argc, argv);

  widg window;
  window.show();
  auto capturer = DesktopCapturerSource::Create(true);
  capturer->Start();
#if 0
  std::thread tmp([]()
                  {
                    MyDesktopCapturerCallback callback;
                    auto screen_capture_ = webrtc::DesktopCapturer::CreateScreenCapturer(webrtc::DesktopCaptureOptions::CreateDefault());
                    if (!screen_capture_)
                    {
                      RTC_LOG(LS_ERROR) << "Failed to create CreateScreenCapturer";
                      return;
                    }
                    webrtc::DesktopCapturer::SourceList sources;
                    screen_capture_->GetSourceList(&sources);
                    int id = 0;
                    for (const auto &src : sources)
                    {
                      std::cout << "id: " << src.id << ", title: " << src.title << std::endl;
                      id = src.id;
                    }
                    screen_capture_->SelectSource(id);  // 选择屏幕ID
                    screen_capture_->Start(&callback); // 设置回调函数
                    int cnt=0;
while(true)
{
if(++cnt>50) break;
screen_capture_->CaptureFrame(); // 捕获一帧
std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 等待回调完成
}
std::this_thread::sleep_for(std::chrono::seconds(1));
                    screen_capture_.reset(); });
  tmp.detach();
#endif
  std::cout << "Hello, World!" << std::endl;
  a.exec();
  webrtc::CleanupSSL();
  return 0;
}