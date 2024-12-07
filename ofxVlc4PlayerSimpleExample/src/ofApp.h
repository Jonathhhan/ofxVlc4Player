#pragma once

#include "ofMain.h"
#include "ofxVlc3Player.h"
#include "ofxProjectM.h"

class ofApp : public ofBaseApp {
	int bufferSize;
	int outChannels;
public:
    void setup();
    void update();
    void draw();
    void exit();
    void keyPressed(int key);
    void keyReleased(int key);
    void mouseMoved(int x, int y);
    void mouseDragged(int x, int y, int button);
    void mousePressed(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void windowResized(int w, int h);
    void dragEvent(ofDragInfo dragInfo);
    void gotMessage(ofMessage msg);
    void audioOut(ofSoundBuffer& buffer);
    ofSoundStream soundStream;
	ofSoundBuffer bufferCopy;
    ofxVlcPlayer player;
    ofxProjectM projectM;
    ofBoxPrimitive box;
    ofEasyCam cam;
};
