#include <mvIMPACT_CPP/mvIMPACT_acquire.h>

class CameraPlayer {
public:
	void openCamera();
	void capture();
private:
	mvIMPACT::acquire::DeviceManager manager_;
	mvIMPACT::acquire::Device* device_;
};
