Inspired by: https://github.com/jnakanojp/ofxVLCVideoPlayer 

Binds libvlc 4. 


Windows:

Copy the plugins and lua folder, libvlc.dll, libvlccore.dll and axvlc.dll to the OF project folder:
https://artifacts.videolan.org/vlc/nightly-win64-llvm/

Linux (Ubuntu):

Install vlc 4 and libvlc:

sudo add-apt-repository ppa:videolan/master-daily

sudo apt-get update && sudo apt-get install vlc

sudo apt-get install libvlc-dev


For Linux and Nvidia GPU:

https://github.com/elFarto/nvidia-vaapi-driver

export LIBVA_DRIVER_NAME=nvidia


The example depends on the ofxProjectM addon: https://github.com/Jonathhhan/ofxProjectM
