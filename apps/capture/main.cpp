#include <iostream>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <camera_player/CameraPlayer.hpp>

CameraPlayer player;

void my_handler(int s) {
  printf("Caught signal %d\n", s);
  player.stop = true;
}

int main(int argc, char **argv) {
	struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = my_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, NULL);

  player.OpenCamera();
  player.Capture();
  return 0;
}
