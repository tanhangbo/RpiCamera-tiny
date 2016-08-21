# RpiCamera-tiny

### Purpose

* A simple camera to detect strangers and record video when motion detected
* Higher FPS on low speed device

### Platform

* Logitech C270
* Raspberry Pi B+

### Setup

```
sudo apt-get update
sudo apt-get install libcv-dev
sudo apt-get install libopencv-dev
```

on ubuntu, you may install `libhighgui-dev`

### Test result

Result shows it is bad performance on Rpi, but it still works about 5FPS,
which is enough.

video recording can also be achieved by ffmpeg（avconv）:
```
sudo avconv -f video4linux2 -i /dev/video0 -r 5  -t 30  1.avi
```

### View the result

The result video lies in the record/avi dir, you may adjust the video path
in function `create_video` if it does not work.

to split the mjpeg video in jpeg files, you may do:

```
sudo apt-get install ffmpeg
ffmpeg -i video.avi -vcodec copy frame%d.jpg
```


