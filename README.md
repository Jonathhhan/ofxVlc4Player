Inspired by: https://github.com/jnakanojp/ofxVLCVideoPlayer 

Binds libvlc 4. 


Windows:

Copy the plugins and lua folder to the OF project folder:
https://artifacts.videolan.org/vlc/nightly-win64-llvm/


Linux (Ubuntu):

Install vlc 4 and libvlc:

sudo add-apt-repository ppa:videolan/master-daily

sudo apt-get update && sudo apt-get install vlc

sudo apt-get install libvlc-dev


For Linux and Nvidia GPU:

https://github.com/elFarto/nvidia-vaapi-driver


The example depends on the ofxProjectM addon: https://github.com/Jonathhhan/ofxProjectM
