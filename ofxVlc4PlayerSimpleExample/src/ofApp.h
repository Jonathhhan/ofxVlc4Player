#pragma once

#include "ofMain.h"
#include "ofVlcPlayer4Gui.h"
#include "ofxProjectM.h"
#include "ofxVlc4Player.h"

class ofApp : public ofBaseApp {
public:
	enum class ProjectMTextureSourceMode {
		MainPlayerVideo,
		PlayerVideo,
		CustomImage,
		CustomVideo,
		InternalTextures
	};

	void setup();
	void update();
	void draw();
	void exit();

	void keyPressed(int key);
	void dragEvent(ofDragInfo dragInfo);
	int addPathToPlaylist(const std::string & rawPath);
	void reloadProjectMTextures(bool useStandardTextures = false);
	bool loadCustomProjectMTexture(const std::string & rawPath);
	void loadPlayerProjectMTexture();
	void ensureFboSize(ofFbo & fbo, int width, int height);
	void drawPlayerToFbo(ofxVlc4Player & sourcePlayer, ofFbo & targetFbo, float width, float height, bool preserveAspect);
	void refreshProjectMSourceTexture();
	void applyProjectMTexture();
	bool hasProjectMSourceSize() const;
	void ensureProjectMInitialized();
	void syncHiddenProjectMVideoPlayer();

	void audioOut(ofSoundBuffer & buffer);

	ofVlcPlayer4Gui remoteGui;

	ofxVlc4Player player; // GUI-controlled
	ofxVlc4Player videoPlayer; // hidden (projectM)

	ofSoundStream soundStream;
	ofxProjectM projectM;
	ofImage projectMCustomTextureImage;
	ofFbo videoPreviewFbo;
	ofFbo projectMSourceFbo;
	std::string projectMCustomTexturePath;
	float videoPreviewWidth = 0.0f;
	float videoPreviewHeight = 0.0f;

	int bufferSize = 128;
	int outChannels = 2;
	bool projectMInitialized = false;
	bool hiddenProjectMVideoSourceWasActive = false;
	ProjectMTextureSourceMode projectMTextureSourceMode = ProjectMTextureSourceMode::PlayerVideo;
};
