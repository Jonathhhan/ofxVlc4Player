#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
	ofBackground(30, 100, 145);
	ofDisableArbTex();
	cam.setPosition(0, 0, 200);
	projectM.load();
	projectM.setWindowSize(1024, 1024);
	ofSetWindowTitle("ofxVlc4PlayerSimpleExample");
	ofSetFrameRate(60);

	soundStream.printDeviceList();
	
	ofSoundStreamSettings settings;
	auto devices = soundStream.getDeviceList();
	settings.setOutDevice(devices[0]);
	settings.setOutListener(this);
	// Change the sample rate to the rate and output channels to the channel number of the file that you want to play!
	settings.sampleRate = 48000;
	settings.numOutputChannels = 2;
	settings.numInputChannels = 0;
	settings.bufferSize = 128;
	soundStream.setup(settings);

	char const* vlc_argv[] = { "", "--input-repeat=100" };
	int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);
	player.load(ofToDataPath("FC Shuttle 1303.mp3"), vlc_argc, vlc_argv);
	player.setLoop(false);
	player.play();
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer& buffer) {
	player.ringBuffer.readIntoBuffer(buffer);
}

//--------------------------------------------------------------
void ofApp::update() {
	player.update();
	projectM.audio(&player.buffer.getBuffer()[0]);
	projectM.update();
}

//--------------------------------------------------------------
void ofApp::draw() {
	// player.draw(0, 0, 1280, 720);
	cam.begin();
	projectM.bind();
	ofEnableDepthTest();
	box.draw();
	ofDisableDepthTest();
	projectM.unbind();
	cam.end();
	ofSetColor(200);
	ofDrawBitmapString("FPS: " + ofToString(ofGetFrameRate()), 20, 20);
	ofDrawBitmapString("Second: " + ofToString(player.getTime() / 1000), 20, 40);
	ofDrawBitmapString("Press space for play, p for pause and m for switching the projectM preset!", 20, 60);
	ofDrawBitmapString(projectM.getPresetName(), 32, 700);
}

//--------------------------------------------------------------
void ofApp::exit() {
	soundStream.close();
	player.close();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	if (key == 32) {
		player.play();
	}
	else if (key == 112) {
		player.pause();
	}
	else if (key == 109) {
		projectM.randomPreset();
	}
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key) {
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y) {
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button) {
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h) {
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg) {
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
}
