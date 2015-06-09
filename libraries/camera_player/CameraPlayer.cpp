#include "CameraPlayer.hpp"

#include <iostream>

#include "SaveBMP.hpp"

using namespace mvIMPACT::acquire;

const int kMaxCaptureWaitTime = 500;

void CameraPlayer::openCamera() {
	device_ = manager_[0]; // Assume single camera.
	if(device_) {
		device_->open();
		device_->acquisitionStartStopBehaviour.read();
	}
}

void CameraPlayer::capture() {
	FunctionInterface fi(device_);
	fi.imageRequestSingle();
	int request_nr = fi.imageRequestWaitFor(kMaxCaptureWaitTime);
	if(fi.isRequestNrValid(request_nr)) {
		const Request* req = fi.getRequest(request_nr);
		std::string filename = "image.bmp";
    if(req->isOK()) {
    	SaveBMP(filename, reinterpret_cast<char*>(req->imageData.read()),
    	req->imageWidth.read(), req->imageHeight.read(), req->imageLinePitch.read(),
    	req->imagePixelPitch.read() * 8);
    }
	}

}

