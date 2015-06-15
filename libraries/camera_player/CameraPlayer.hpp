#include <mvIMPACT_CPP/mvIMPACT_acquire.h>

class CameraPlayer {
public:
	~CameraPlayer();
	void OpenCamera();
	void Capture(bool continous = true, bool save = true);

	bool stop = false;
private:
	mvIMPACT::acquire::DeviceManager manager_;
	mvIMPACT::acquire::Device* device_;
	FunctionInterface* fi_;
};
