#pragma once

#include "ofMain.h"
#include "ofVlcPlayer4Gui.h"
#include "ofxProjectM.h"
#include "ofxVlc4Player.h"

class ofApp : public ofBaseApp {
public:
	enum class ProjectMTextureSourceMode {
		MainPlayerVideo,
		CustomImage,
		InternalTextures
	};

	void setup();
	void update();
	void draw();
	void exit();

	void dragEvent(ofDragInfo dragInfo);
	int addPathToPlaylist(const std::string & rawPath);
	void reloadProjectMTextures(bool useStandardTextures = false);
	bool loadCustomProjectMTexture(const std::string & rawPath);
	void loadPlayerProjectMTexture();
	void drawPlayerToFbo(ofxVlc4Player & sourcePlayer, ofFbo & targetFbo, float width, float height, bool preserveAspect);
	void refreshProjectMSourceTexture();
	void applyProjectMTexture();
	bool hasProjectMSourceSize() const;
	void ensureProjectMInitialized();
	void setupAnaglyphShader();
	void setupVideoAdjustShader();
	void updateVideoAdjustPreview(const ofTexture & sourceTexture, float sourceWidth, float sourceHeight);
	void updateAnaglyphPreview(const ofTexture & sourceTexture, float sourceWidth, float sourceHeight);

	void audioOut(ofSoundBuffer & buffer);

	ofVlcPlayer4Gui remoteGui;

	ofxVlc4Player player; // GUI-controlled

	ofSoundStream soundStream;
	ofxProjectM projectM;
	ofImage projectMCustomTextureImage;
	ofImage videoPreviewArtworkImage;
	ofFbo projectMSourceFbo;
	ofFbo videoPreviewFbo;
	ofFbo videoAdjustPreviewFbo;
	ofFbo anaglyphPreviewFbo;
	ofShader videoAdjustShader;
	ofShader anaglyphShader;
	std::string projectMCustomTexturePath;
	std::string videoPreviewArtworkPath;
	float videoPreviewWidth = 0.0f;
	float videoPreviewHeight = 0.0f;
	bool videoPreviewHasContent = false;
	bool videoPreviewShowsVideo = false;
	bool videoAdjustShaderReady = false;
	bool anaglyphShaderReady = false;

	int bufferSize = 128;
	int outChannels = 2;
	bool projectMInitialized = false;
	ProjectMTextureSourceMode projectMTextureSourceMode = ProjectMTextureSourceMode::InternalTextures;
};
