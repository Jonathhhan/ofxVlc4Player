#include "ofApp.h"

//--------------------------------------------------------------
void ofApp::setup() {
	ofBackground(30, 100, 145);
	ofDisableArbTex();
	cam.setPosition(0, 0, 200);
	projectM.load();
	projectM.setWindowSize(1920, 1080);
	ofSetWindowTitle("ofxVlc4PlayerRecorderExample");
	ofSetFrameRate(60);
	mediaPath = ofToDataPath("recording");
	char const * vlc_argv[] = { "" };
	int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);
	player.init(vlc_argc, vlc_argv);
}

//--------------------------------------------------------------
void ofApp::update() {
	projectM.update();
	player.updateRecorder();
}

//--------------------------------------------------------------
void ofApp::draw() {
	player.draw(0, 0, 1280, 720);
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
	ofDrawBitmapString("Press b to start recording, e to stop recording and m to switch the projectM preset!", 20, 60);
	ofDrawBitmapString(projectM.getPresetName(), 32, 700);
}

//--------------------------------------------------------------
void ofApp::exit() {
	player.close();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	if (key == 98) {
		player.record(mediaPath, projectM.getTexture());
	}
	else if (key == 101) {
		player.stop();
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
