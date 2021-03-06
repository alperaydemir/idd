#include "CameraPlayer.hpp"

#include <iostream>

#include "SaveBMP.hpp"

using namespace mvIMPACT::acquire;

const int kMaxCaptureWaitTime = 500;
static int frame_nr = 0;

void CameraPlayer::OpenCamera() {
  device_ = manager_[0];  // Assume single camera.
  if (device_) {
    device_->open();
    fi_ = new FunctionInterface(device_);
  }
}

void CameraPlayer::Capture(bool continuos, bool save) {
  SystemSettings ss(device_);
  const int REQUEST_COUNT = ss.requestCount.read();
  for(int i=0; i<REQUEST_COUNT; i++)
  {
    int result = fi_->imageRequestSingle();
    if( result != DMR_NO_ERROR )
    {
      std::cout << "Error while filling the request queue: " << ImpactAcquireException::getErrorCodeAsString( result ) << std::endl;
    }
  }

  do {
    int request_nr = fi_->imageRequestWaitFor(kMaxCaptureWaitTime);
    if (fi_->isRequestNrValid(request_nr)) {
      const Request* req = fi_->getRequest(request_nr);

      if (req->isOK() && save) {
        char buf[256];
        sprintf(buf, "image_%05d.bmp", frame_nr);
        std::string filename(buf);
        SaveBMP(filename, reinterpret_cast<char*>(req->imageData.read()),
                req->imageWidth.read(), req->imageHeight.read(),
                req->imageLinePitch.read(), req->imagePixelPitch.read() * 8);
        frame_nr++;
        // this image has been displayed thus the buffer is no longer needed...
      }
      fi_->imageRequestUnlock(request_nr);
      fi_->imageRequestSingle();
    }
  } while (continuos && !stop);
}

CameraPlayer::~CameraPlayer() { device_->close(); }
