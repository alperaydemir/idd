add_library(camera_player CameraPlayer.cpp)

target_link_libraries(camera_player ${mvBlueFox} ${mvDeviceManager} ${mvPropHandling})
