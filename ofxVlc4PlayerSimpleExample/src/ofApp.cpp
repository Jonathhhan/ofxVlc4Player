#include "ofApp.h"

namespace {
const std::initializer_list<std::string> kSeedExtensions = {
	".mp4", ".mov", ".m4v", ".webm", ".avi", ".mkv",
	".jpg", ".jpeg", ".png", ".mp3", ".wav", ".aiff", ".h264",
	".flac", ".bmp"
};

std::string normalizeInputPath(std::string path) {
	path = ofTrim(path);
	if (path.size() >= 2) {
		const char first = path.front();
		const char last = path.back();
		if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
			path = path.substr(1, path.size() - 2);
			path = ofTrim(path);
		}
	}
	return path;
}

bool looksLikeUri(const std::string & path) {
	return path.find("://") != std::string::npos;
}

bool pathExists(const std::string & path) {
	return ofFile::doesFileExist(path, true) || ofDirectory::doesDirectoryExist(path, true);
}

bool isSupportedImagePath(const std::string & path) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(path));
	return extension == "png" || extension == "jpg" || extension == "jpeg" || extension == "bmp";
}

bool isSupportedVideoPath(const std::string & path) {
	const std::string extension = ofToLower(ofFilePath::getFileExt(path));
	return extension == "mp4" || extension == "mov" || extension == "m4v" || extension == "webm" ||
		extension == "avi" || extension == "mkv" || extension == "h264";
}

std::string resolveInputPath(const std::string & rawPath) {
	const std::string normalizedPath = normalizeInputPath(rawPath);
	if (normalizedPath.empty() || looksLikeUri(normalizedPath)) {
		return normalizedPath;
	}

	if (pathExists(normalizedPath)) {
		return normalizedPath;
	}

	const std::string dataPath = ofToDataPath(normalizedPath, true);
	if (pathExists(dataPath)) {
		return dataPath;
	}

	return normalizedPath;
}

void clearAllocatedFbo(ofFbo & fbo) {
	if (!fbo.isAllocated()) {
		return;
	}

	fbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.end();
}

void drawProjectMSourceImageToFbo(ofFbo & targetFbo, const ofImage & image) {
	if (!targetFbo.isAllocated()) {
		return;
	}

	targetFbo.begin();
	ofClear(0, 0, 0, 0);
	if (image.isAllocated()) {
		image.draw(0.0f, 0.0f, targetFbo.getWidth(), targetFbo.getHeight());
	}
	targetFbo.end();
}
}

namespace {
bool usesProjectMVideoSource(ofApp::ProjectMTextureSourceMode mode) {
	return mode == ofApp::ProjectMTextureSourceMode::MainPlayerVideo ||
		mode == ofApp::ProjectMTextureSourceMode::PlayerVideo ||
		mode == ofApp::ProjectMTextureSourceMode::CustomVideo;
}

void restartCurrentProjectMPreset(ofxProjectM & projectM) {
	const int currentPresetIndex = projectM.getPresetIndex();
	if (currentPresetIndex >= 0) {
		projectM.setPresetIndex(currentPresetIndex, true);
	}
}

bool ensureLoadedImage(ofImage & image, const std::string & path) {
	if (image.isAllocated()) {
		return true;
	}
	if (path.empty()) {
		return false;
	}

	image.clear();
	return image.load(path);
}
}

//--------------------------------------------------------------
void ofApp::setup() {
	ofSetWindowTitle("VLC Playlist GUI");
	ofSetFrameRate(60);
	ofDisableArbTex();

	ofSoundStreamSettings settings;
	settings.setOutListener(this);

	settings.sampleRate = 44100;
	settings.numOutputChannels = outChannels;
	settings.numInputChannels = 0;
	settings.bufferSize = bufferSize;
	soundStream.setup(settings);
	soundStream.start();

	remoteGui.setup();
	videoPreviewFbo.allocate(std::max(ofGetScreenWidth(), 1), std::max(ofGetScreenHeight(), 1), GL_RGBA);
	projectMSourceFbo.allocate(std::max(ofGetScreenWidth(), 1), std::max(ofGetScreenHeight(), 1), GL_RGBA);
	clearAllocatedFbo(videoPreviewFbo);
	clearAllocatedFbo(projectMSourceFbo);

	const char * vlc_argv[] = {
		"--file-caching=10",
		"--network-caching=10",
		"--verbose=-1"
	};
	int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);

	const char * hidden_vlc_argv[] = {
		"--file-caching=10",
		"--network-caching=10",
		"--verbose=-1",
		"--no-audio"
	};
	int hidden_vlc_argc = sizeof(hidden_vlc_argv) / sizeof(*hidden_vlc_argv);

	// -------- MAIN PLAYER (GUI controlled)
	player.setAudioCaptureEnabled(true);
	player.init(vlc_argc, vlc_argv);
	player.addPathToPlaylist(ofToDataPath("fingers.mp4", true), kSeedExtensions);

	player.setPlaybackMode(ofxVlc4Player::PlaybackMode::Default);
	player.setVolume(50);

	// -------- VIDEO PLAYER (hidden, for projectM)
	videoPlayer.setAudioCaptureEnabled(false);
	videoPlayer.init(hidden_vlc_argc, hidden_vlc_argv);
	videoPlayer.addPathToPlaylist(ofToDataPath("fingers.mp4", true), kSeedExtensions);
	videoPlayer.setPlaybackMode(ofxVlc4Player::PlaybackMode::Repeat);

	if (!videoPlayer.getPlaylist().empty()) {
		videoPlayer.play();
	}
}

//--------------------------------------------------------------
void ofApp::audioOut(ofSoundBuffer & buffer) {
	buffer.set(0);

	if (player.audioIsReady()) {
		const float gain = player.currentVolume.load() / 100.0f;
		player.readAudioIntoBuffer(buffer, gain);

		if (projectMInitialized && !buffer.getBuffer().empty()) {
			projectM.audio(
				buffer.getBuffer().data(),
				static_cast<int>(buffer.getNumFrames()),
				static_cast<int>(buffer.getNumChannels()));
		}
	}
}

//--------------------------------------------------------------
void ofApp::update() {
	player.update();
	videoPlayer.update();
	ensureProjectMInitialized();
	const bool renderProjectMPreview = remoteGui.shouldRenderProjectMPreview();

	videoPreviewWidth = player.getWidth();
	videoPreviewHeight = player.getHeight();
	const bool renderVideoPreview = remoteGui.shouldRenderVideoPreview();
	if (renderVideoPreview) {
		drawPlayerToFbo(player, videoPreviewFbo, videoPreviewWidth, videoPreviewHeight, true);
	}

	if (projectMInitialized &&
		renderProjectMPreview &&
		projectMTextureSourceMode != ProjectMTextureSourceMode::InternalTextures &&
		usesProjectMVideoSource(projectMTextureSourceMode)) {
		ofxVlc4Player & projectMSourcePlayer =
			projectMTextureSourceMode == ProjectMTextureSourceMode::MainPlayerVideo ? player : videoPlayer;
		drawPlayerToFbo(
			projectMSourcePlayer,
			projectMSourceFbo,
			projectMSourcePlayer.getWidth(),
			projectMSourcePlayer.getHeight(),
			false);
	}

	remoteGui.updateSelection(player);

	if (projectMInitialized && renderProjectMPreview) {
		projectM.update();
	}
}

//--------------------------------------------------------------
void ofApp::draw() {
	ofClear(0, 0, 0, 255);
	remoteGui.draw(
		player,
		projectM,
		projectMInitialized,
		videoPreviewFbo.getTexture(),
		videoPreviewWidth,
		videoPreviewHeight,
		[this](const std::string & rawPath) {
			return addPathToPlaylist(rawPath);
		},
		[this]() {
			if (projectMInitialized) {
				projectM.randomPreset();
			}
		},
		[this]() {
			if (projectMInitialized) {
				projectM.reloadPresets();
			}
		},
		[this]() {
			reloadProjectMTextures(true);
		},
		[this]() {
			loadPlayerProjectMTexture();
		},
		[this](const std::string & rawPath) {
			return loadCustomProjectMTexture(rawPath);
		});
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo) {
	remoteGui.handleDragEvent(
		dragInfo,
		player,
		[this](const std::string & rawPath) {
			return addPathToPlaylist(rawPath);
		});
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key) {
	remoteGui.handleKeyPressed(key, player, projectM, projectMInitialized);
}

//--------------------------------------------------------------
void ofApp::exit() {
	player.close();
	videoPlayer.close();
	soundStream.close();
}

int ofApp::addPathToPlaylist(const std::string & rawPath) {
	const std::string resolvedPath = resolveInputPath(rawPath);
	if (resolvedPath.empty()) {
		return 0;
	}

	if (!looksLikeUri(resolvedPath) && !pathExists(resolvedPath)) {
		ofLogError("ofApp") << "Playlist path not found: " << normalizeInputPath(rawPath);
		return 0;
	}

	return player.addPathToPlaylist(resolvedPath, kSeedExtensions);
}

void ofApp::drawPlayerToFbo(ofxVlc4Player & sourcePlayer, ofFbo & targetFbo, float width, float height, bool preserveAspect) {
	if (!targetFbo.isAllocated()) {
		return;
	}

	targetFbo.begin();
	ofClear(0, 0, 0, 0);
	if (width > 0.0f && height > 0.0f) {
		if (preserveAspect) {
			const float targetWidth = targetFbo.getWidth();
			const float targetHeight = targetFbo.getHeight();
			const float sourceAspect = width / height;
			const float targetAspect = targetWidth / targetHeight;

			float drawWidth = targetWidth;
			float drawHeight = targetHeight;
			float drawX = 0.0f;
			float drawY = 0.0f;

			if (sourceAspect > targetAspect) {
				drawHeight = drawWidth / sourceAspect;
				drawY = (targetHeight - drawHeight) * 0.5f;
			} else {
				drawWidth = drawHeight * sourceAspect;
				drawX = (targetWidth - drawWidth) * 0.5f;
			}

			sourcePlayer.draw(drawX, drawY, drawWidth, drawHeight);
		} else {
			sourcePlayer.draw(0.0f, 0.0f, targetFbo.getWidth(), targetFbo.getHeight());
		}
	}
	targetFbo.end();
}

void ofApp::applyProjectMTexture() {
	switch (projectMTextureSourceMode) {
	case ProjectMTextureSourceMode::InternalTextures:
		projectM.useInternalTextureOnly();
		break;
	case ProjectMTextureSourceMode::CustomImage:
		if (projectMCustomTextureImage.isAllocated()) {
			drawProjectMSourceImageToFbo(projectMSourceFbo, projectMCustomTextureImage);
		}
		projectM.setTexture(projectMSourceFbo.getTexture());
		break;
	case ProjectMTextureSourceMode::MainPlayerVideo:
	case ProjectMTextureSourceMode::PlayerVideo:
	case ProjectMTextureSourceMode::CustomVideo:
		projectM.setTexture(projectMSourceFbo.getTexture());
		break;
	}

	if (projectMInitialized) {
		restartCurrentProjectMPreset(projectM);
	}
}

bool ofApp::hasProjectMSourceSize() const {
	switch (projectMTextureSourceMode) {
	case ProjectMTextureSourceMode::MainPlayerVideo:
		return player.getWidth() > 0.0f && player.getHeight() > 0.0f;
	case ProjectMTextureSourceMode::PlayerVideo:
	case ProjectMTextureSourceMode::CustomVideo:
		return videoPlayer.getWidth() > 0.0f && videoPlayer.getHeight() > 0.0f;
	case ProjectMTextureSourceMode::CustomImage:
		return projectMCustomTextureImage.isAllocated() &&
			projectMCustomTextureImage.getWidth() > 0 &&
			projectMCustomTextureImage.getHeight() > 0;
	case ProjectMTextureSourceMode::InternalTextures:
		return ofGetScreenWidth() > 0 && ofGetScreenHeight() > 0;
	}

	return false;
}

void ofApp::ensureProjectMInitialized() {
	if (projectMInitialized || !hasProjectMSourceSize()) {
		return;
	}

	projectM.setWindowSize(ofGetScreenWidth(), ofGetScreenHeight());
	projectM.init();
	projectMInitialized = true;
	reloadProjectMTextures(projectMTextureSourceMode == ProjectMTextureSourceMode::InternalTextures);
}

void ofApp::reloadProjectMTextures(bool useStandardTextures) {
	if (useStandardTextures) {
		projectMTextureSourceMode = ProjectMTextureSourceMode::InternalTextures;
		applyProjectMTexture();
		return;
	}

	if (projectMTextureSourceMode == ProjectMTextureSourceMode::MainPlayerVideo ||
		projectMTextureSourceMode == ProjectMTextureSourceMode::PlayerVideo ||
		projectMTextureSourceMode == ProjectMTextureSourceMode::CustomVideo) {
		applyProjectMTexture();
		return;
	}

	if (!ensureLoadedImage(projectMCustomTextureImage, projectMCustomTexturePath)) {
		projectMTextureSourceMode = ProjectMTextureSourceMode::PlayerVideo;
	}

	applyProjectMTexture();
}

void ofApp::loadPlayerProjectMTexture() {
	projectMTextureSourceMode = ProjectMTextureSourceMode::MainPlayerVideo;
	applyProjectMTexture();
}

bool ofApp::loadCustomProjectMTexture(const std::string & rawPath) {
	const std::string normalizedPath = normalizeInputPath(rawPath);
	const std::string requestedPath = normalizedPath.empty() ? projectMCustomTexturePath : normalizedPath;
	const std::string resolvedPath = resolveInputPath(requestedPath);
	if (resolvedPath.empty()) {
		ofLogError("ofApp") << "No custom projectM texture path set.";
		return false;
	}

	if (!pathExists(resolvedPath)) {
		ofLogError("ofApp") << "Custom projectM texture file not found: " << requestedPath;
		return false;
	}

	if (isSupportedVideoPath(resolvedPath)) {
		videoPlayer.stop();
		videoPlayer.clearPlaylist();
		const int addedCount = videoPlayer.addPathToPlaylist(resolvedPath, kSeedExtensions);
		if (addedCount <= 0 || videoPlayer.getPlaylist().empty()) {
			ofLogError("ofApp") << "Failed to load custom projectM texture video: " << resolvedPath;
			return false;
		}
		videoPlayer.setPlaybackMode(ofxVlc4Player::PlaybackMode::Repeat);
		videoPlayer.playIndex(0);

		projectMCustomTextureImage.clear();
		projectMCustomTexturePath = resolvedPath;
		projectMTextureSourceMode = ProjectMTextureSourceMode::CustomVideo;
		applyProjectMTexture();
		return true;
	}

	if (!isSupportedImagePath(resolvedPath)) {
		ofLogError("ofApp") << "Custom projectM texture must be an image or video file (.png, .jpg, .jpeg, .bmp, .mp4, .mov, .m4v, .webm, .avi, .mkv, .h264): " << requestedPath;
		return false;
	}

	ofImage image;
	if (!image.load(resolvedPath)) {
		ofLogError("ofApp") << "Failed to load custom projectM texture image: " << resolvedPath;
		return false;
	}

	projectMCustomTextureImage = std::move(image);
	projectMCustomTexturePath = resolvedPath;
	projectMTextureSourceMode = ProjectMTextureSourceMode::CustomImage;
	applyProjectMTexture();
	return true;
}
