#include <iostream>

#include <camera_player/CameraPlayer.hpp>

int main(int argc, char **argv)
{
	CameraPlayer player;
	player.openCamera();
	player.capture();
  return 0;
}
