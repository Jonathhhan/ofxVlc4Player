#include "ofApp.h"

namespace {
const std::initializer_list<std::string> kSeedExtensions = {
	".mp4", ".mov", ".m4v", ".webm", ".avi", ".mkv",
	".jpg", ".jpeg", ".png", ".mp3", ".wav", ".aiff", ".h264",
	".flac", ".bmp"
};
constexpr float kMaxVideoPreviewHeight = 4320.0f;

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

std::string resolveInputPath(const std::string & rawPath);

std::string decodeUrlComponent(const std::string & value) {
	std::string decoded;
	decoded.reserve(value.size());
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] == '%' && i + 2 < value.size()) {
			const char hi = value[i + 1];
			const char lo = value[i + 2];
			const auto hexToInt = [](char c) -> int {
				if (c >= '0' && c <= '9') {
					return c - '0';
				}
				if (c >= 'a' && c <= 'f') {
					return 10 + (c - 'a');
				}
				if (c >= 'A' && c <= 'F') {
					return 10 + (c - 'A');
				}
				return -1;
			};
			const int hiValue = hexToInt(hi);
			const int loValue = hexToInt(lo);
			if (hiValue >= 0 && loValue >= 0) {
				decoded.push_back(static_cast<char>((hiValue << 4) | loValue));
				i += 2;
				continue;
			}
		}
		decoded.push_back(value[i]);
	}
	return decoded;
}

std::string findMetadataValue(
	const std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & label) {
	for (const auto & [entryLabel, entryValue] : metadata) {
		if (entryLabel == label) {
			return entryValue;
		}
	}
	return "";
}

bool mediaHasVideoTrack(const std::vector<std::pair<std::string, std::string>> & metadata) {
	return !findMetadataValue(metadata, "Video Codec").empty() ||
		!findMetadataValue(metadata, "Video Resolution").empty();
}

bool hasUsableTextureSize(const ofTexture & texture) {
	return texture.isAllocated() &&
		texture.getWidth() > 1.0f &&
		texture.getHeight() > 1.0f;
}

void clampVideoPreviewDimensions(float & width, float & height) {
	if (width <= 0.0f || height <= 0.0f || height <= kMaxVideoPreviewHeight) {
		return;
	}

	const float scale = kMaxVideoPreviewHeight / height;
	width = std::max(1.0f, width * scale);
	height = kMaxVideoPreviewHeight;
}

void assignClampedPreviewDimensions(
	float sourceWidth,
	float sourceHeight,
	float & targetWidth,
	float & targetHeight) {
	targetWidth = sourceWidth;
	targetHeight = sourceHeight;
	clampVideoPreviewDimensions(targetWidth, targetHeight);
}

bool shouldUseAdjustedVideoPreview(
	bool previewHasContent,
	bool previewShowsVideo,
	bool videoAdjustmentsEnabled,
	bool shaderReady,
	const ofFbo & previewFbo) {
	return previewHasContent &&
		previewShowsVideo &&
		videoAdjustmentsEnabled &&
		shaderReady &&
		previewFbo.isAllocated();
}

bool shouldUseAnaglyphPreview(
	bool previewHasContent,
	bool previewShowsVideo,
	bool anaglyphEnabled,
	ofxVlc4Player::VideoStereoMode stereoMode,
	bool shaderReady,
	const ofFbo & previewFbo) {
	return previewHasContent &&
		previewShowsVideo &&
		anaglyphEnabled &&
		stereoMode == ofxVlc4Player::VideoStereoMode::SideBySide &&
		shaderReady &&
		previewFbo.isAllocated();
}

std::string resolveArtworkPath(const std::string & rawArtworkUrl) {
	std::string artworkPath = ofTrim(rawArtworkUrl);
	if (artworkPath.empty()) {
		return "";
	}

	const std::string lowerPath = ofToLower(artworkPath);
	if (ofIsStringInString(lowerPath, "attachment://")) {
		return "";
	}

	if (lowerPath.rfind("file:///", 0) == 0) {
		artworkPath = decodeUrlComponent(artworkPath.substr(8));
	} else if (lowerPath.rfind("file://", 0) == 0) {
		artworkPath = decodeUrlComponent(artworkPath.substr(7));
	} else {
		artworkPath = decodeUrlComponent(artworkPath);
	}

	return resolveInputPath(artworkPath);
}

bool pathExists(const std::string & path) {
	return ofFile::doesFileExist(path, true) || ofDirectory::doesDirectoryExist(path, true);
}

bool isSupportedCustomImagePath(const std::string & path) {
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

	// Prefer explicit absolute/relative filesystem paths first, then fall back to OF's data folder lookup.
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

void resetPreviewFbo(ofFbo & fbo) {
	fbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(fbo);
}

void clearVideoPreviewState(
	ofFbo & fbo,
	ofImage & artworkImage,
	std::string & artworkPath,
	float & previewWidth,
	float & previewHeight) {
	resetPreviewFbo(fbo);
	artworkImage.clear();
	artworkPath.clear();
	previewWidth = 0.0f;
	previewHeight = 0.0f;
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

void ensureFboSize(ofFbo & fbo, float width, float height) {
	const int targetWidth = std::max(1, static_cast<int>(std::ceil(width)));
	const int targetHeight = std::max(1, static_cast<int>(std::ceil(height)));
	if (!fbo.isAllocated() ||
		static_cast<int>(fbo.getWidth()) != targetWidth ||
		static_cast<int>(fbo.getHeight()) != targetHeight) {
		fbo.allocate(targetWidth, targetHeight, GL_RGBA);
		clearAllocatedFbo(fbo);
	}
}

void restartCurrentProjectMPreset(ofxProjectM & projectM) {
	const int currentPresetIndex = projectM.getPresetIndex();
	if (currentPresetIndex >= 0) {
		projectM.setPresetIndex(currentPresetIndex, true);
	}
}

void restartCurrentProjectMPresetIfInitialized(ofxProjectM & projectM, bool projectMInitialized) {
	if (projectMInitialized) {
		restartCurrentProjectMPreset(projectM);
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
	if (!looksLikeUri(path) && ofFile::doesFileExist(path, true)) {
		ofBuffer buffer = ofBufferFromFile(path, true);
		return !buffer.size() ? false : image.load(buffer);
	}

	return image.load(path);
}

std::pair<glm::vec3, glm::vec3> getAnaglyphTints(AnaglyphColorMode mode) {
	switch (mode) {
	case AnaglyphColorMode::GreenMagenta:
		return {
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(1.0f, 0.0f, 1.0f)
		};
	case AnaglyphColorMode::AmberBlue:
		return {
			glm::vec3(1.0f, 0.75f, 0.0f),
			glm::vec3(0.0f, 0.5f, 1.0f)
		};
	case AnaglyphColorMode::RedCyan:
	default:
		return {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 1.0f)
		};
	}
}

bool loadShaderProgram(ofShader & shader, const std::string & fragmentBaseName) {
	const bool programmable = ofIsGLProgrammableRenderer();
	const std::string vertexPath = programmable ? "shaders/passthrough_gl3.vert" : "shaders/passthrough_gl2.vert";
	const std::string fragmentPath = programmable
		? ("shaders/" + fragmentBaseName + "_gl3.frag")
		: ("shaders/" + fragmentBaseName + "_gl2.frag");

	bool ready =
		shader.setupShaderFromFile(GL_VERTEX_SHADER, vertexPath) &&
		shader.setupShaderFromFile(GL_FRAGMENT_SHADER, fragmentPath);
	if (ready && programmable) {
		shader.bindDefaults();
	}
	if (ready) {
		ready = shader.linkProgram();
	}
	return ready;
}
}

void ofApp::setupVideoAdjustShader() {
	videoAdjustShaderReady = loadShaderProgram(videoAdjustShader, "videoAdjust");
}

void ofApp::setupAnaglyphShader() {
	// The example keeps anaglyph rendering local: VLC still produces the stereo frame,
	// and this shader only remaps an SBS preview into a simple red/cyan display.
	anaglyphShaderReady = loadShaderProgram(anaglyphShader, "anaglyph");
}

void ofApp::updateVideoAdjustPreview(const ofTexture & sourceTexture, float sourceWidth, float sourceHeight) {
	if (!videoAdjustShaderReady || !sourceTexture.isAllocated() || sourceWidth <= 1.0f || sourceHeight <= 1.0f) {
		return;
	}

	clampVideoPreviewDimensions(sourceWidth, sourceHeight);
	ensureFboSize(videoAdjustPreviewFbo, sourceWidth, sourceHeight);
	videoAdjustPreviewFbo.begin();
	ofClear(0, 0, 0, 0);
	videoAdjustShader.begin();
	videoAdjustShader.setUniformTexture("tex0", sourceTexture, 0);
	videoAdjustShader.setUniform1f("brightness", player.getVideoBrightness());
	videoAdjustShader.setUniform1f("contrast", player.getVideoContrast());
	videoAdjustShader.setUniform1f("saturation", player.getVideoSaturation());
	videoAdjustShader.setUniform1f("gammaValue", player.getVideoGamma());
	videoAdjustShader.setUniform1f("hueDegrees", player.getVideoHue());
	sourceTexture.draw(0.0f, 0.0f, sourceWidth, sourceHeight);
	videoAdjustShader.end();
	videoAdjustPreviewFbo.end();
}

void ofApp::updateAnaglyphPreview(const ofTexture & sourceTexture, float sourceWidth, float sourceHeight) {
	if (!anaglyphShaderReady || !sourceTexture.isAllocated() || sourceWidth <= 1.0f || sourceHeight <= 1.0f) {
		return;
	}

	clampVideoPreviewDimensions(sourceWidth, sourceHeight);
	// Anaglyph combines the left and right halves of the SBS preview into a single output,
	// so the preview width is halved while the original height stays untouched.
	const float targetWidth = std::max(1.0f, sourceWidth * 0.5f);
	ensureFboSize(anaglyphPreviewFbo, targetWidth, sourceHeight);
	const auto [leftTint, rightTint] = getAnaglyphTints(remoteGui.getAnaglyphColorMode());

	anaglyphPreviewFbo.begin();
	ofClear(0, 0, 0, 0);
	anaglyphShader.begin();
	anaglyphShader.setUniformTexture("tex0", sourceTexture, 0);
	anaglyphShader.setUniform3f("leftTint", leftTint.x, leftTint.y, leftTint.z);
	anaglyphShader.setUniform3f("rightTint", rightTint.x, rightTint.y, rightTint.z);
	anaglyphShader.setUniform1f("eyeSeparation", remoteGui.getAnaglyphEyeSeparation());
	anaglyphShader.setUniform1f("swapEyes", remoteGui.isAnaglyphSwapEyesEnabled() ? 1.0f : 0.0f);
	sourceTexture.draw(0.0f, 0.0f, targetWidth, sourceHeight);
	anaglyphShader.end();
	anaglyphPreviewFbo.end();
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
	setupVideoAdjustShader();
	setupAnaglyphShader();
	projectMSourceFbo.allocate(std::max(ofGetScreenWidth(), 1), std::max(ofGetScreenHeight(), 1), GL_RGBA);
	clearAllocatedFbo(projectMSourceFbo);
	resetPreviewFbo(videoPreviewFbo);

	const char * vlc_argv[] = {
		"--file-caching=10",
		"--network-caching=10",
		"--verbose=-1"
	};
	int vlc_argc = sizeof(vlc_argv) / sizeof(*vlc_argv);

	// -------- MAIN PLAYER (GUI controlled)
	player.setAudioCaptureEnabled(true);
	// Keep the demux workaround explicit in the example while we validate the GL stability issue.
	player.setForceAvformatDemuxEnabled(true);
	player.init(vlc_argc, vlc_argv);
	player.addPathToPlaylist(ofToDataPath("fingers.mp4", true), kSeedExtensions);

	player.setPlaybackMode(ofxVlc4Player::PlaybackMode::Default);
	player.setVolume(50);
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

void ofApp::update() {
	player.update();
	ensureProjectMInitialized();
	const bool renderProjectMPreview = remoteGui.shouldRenderProjectMPreview();
	const ofTexture & playerTexture = player.getTexture();
	const float currentVideoWidth = player.getWidth();
	const float currentVideoHeight = player.getHeight();
	const bool playerHasReportedVideoSize = currentVideoWidth > 1.0f && currentVideoHeight > 1.0f;
	const bool playerHasTextureSize = hasUsableTextureSize(playerTexture);
	const auto currentMetadata = player.getCurrentMetadata();
	const bool metadataReadyForPreviewDecision = !currentMetadata.empty();
	const bool playerHasVideoFrame =
		(playerHasReportedVideoSize || playerHasTextureSize) &&
		(!metadataReadyForPreviewDecision || mediaHasVideoTrack(currentMetadata));
	const bool playbackTransitioning = player.isPlaybackTransitioning();
	const bool playbackRestartPending = player.isPlaybackRestartPending();
	const bool holdPreviewState = playbackTransitioning || playbackRestartPending;
	const std::string artworkPath = resolveArtworkPath(findMetadataValue(currentMetadata, "Artwork URL"));
	const float previewSourceWidth = playerHasReportedVideoSize ? currentVideoWidth : (playerHasTextureSize ? playerTexture.getWidth() : 0.0f);
	const float previewSourceHeight = playerHasReportedVideoSize ? currentVideoHeight : (playerHasTextureSize ? playerTexture.getHeight() : 0.0f);
	const bool hadPreviewContent = videoPreviewHasContent;
	const bool hadVideoPreview = videoPreviewShowsVideo;
	videoPreviewHasContent = false;
	videoPreviewShowsVideo = false;
	// Prefer VLC's reported display geometry, but fall back to the exposed texture once it is larger than 1x1.
	if (playerHasReportedVideoSize) {
		assignClampedPreviewDimensions(currentVideoWidth, currentVideoHeight, videoPreviewWidth, videoPreviewHeight);
	} else if (playerHasTextureSize && videoPreviewWidth <= 1.0f && videoPreviewHeight <= 1.0f) {
		assignClampedPreviewDimensions(playerTexture.getWidth(), playerTexture.getHeight(), videoPreviewWidth, videoPreviewHeight);
	}
	if (playerHasVideoFrame && previewSourceWidth > 1.0f && previewSourceHeight > 1.0f) {
		assignClampedPreviewDimensions(previewSourceWidth, previewSourceHeight, videoPreviewWidth, videoPreviewHeight);
		ensureFboSize(videoPreviewFbo, videoPreviewWidth, videoPreviewHeight);
		videoPreviewFbo.begin();
		ofClear(0, 0, 0, 255);
		player.draw(0.0f, 0.0f, videoPreviewWidth, videoPreviewHeight);
		videoPreviewFbo.end();
		videoPreviewHasContent = true;
		videoPreviewShowsVideo = true;
	} else if (!player.isStopped() && !artworkPath.empty()) {
		if (videoPreviewArtworkPath != artworkPath) {
			videoPreviewArtworkPath = artworkPath;
			videoPreviewArtworkImage.clear();
		}
		if (ensureLoadedImage(videoPreviewArtworkImage, artworkPath) &&
			videoPreviewArtworkImage.isAllocated() &&
			videoPreviewArtworkImage.getWidth() > 1 &&
			videoPreviewArtworkImage.getHeight() > 1) {
			assignClampedPreviewDimensions(
				static_cast<float>(videoPreviewArtworkImage.getWidth()),
				static_cast<float>(videoPreviewArtworkImage.getHeight()),
				videoPreviewWidth,
				videoPreviewHeight);
			ensureFboSize(videoPreviewFbo, videoPreviewWidth, videoPreviewHeight);
			videoPreviewFbo.begin();
			ofClear(0, 0, 0, 255);
			videoPreviewArtworkImage.draw(0.0f, 0.0f, videoPreviewWidth, videoPreviewHeight);
			videoPreviewFbo.end();
			videoPreviewHasContent = true;
		}
		if (!videoPreviewHasContent && holdPreviewState && hadPreviewContent) {
			videoPreviewHasContent = true;
			videoPreviewShowsVideo = hadVideoPreview;
		}
	} else {
		if (holdPreviewState && hadPreviewContent) {
			videoPreviewHasContent = true;
			videoPreviewShowsVideo = hadVideoPreview;
		} else {
			clearVideoPreviewState(
				videoPreviewFbo,
				videoPreviewArtworkImage,
				videoPreviewArtworkPath,
				videoPreviewWidth,
				videoPreviewHeight);
		}
	}
	const bool useVideoAdjustPreview = shouldUseAdjustedVideoPreview(
		videoPreviewHasContent,
		videoPreviewShowsVideo,
		player.isVideoAdjustmentsEnabled(),
		videoAdjustShaderReady,
		videoPreviewFbo);
	if (useVideoAdjustPreview) {
		updateVideoAdjustPreview(videoPreviewFbo.getTexture(), videoPreviewWidth, videoPreviewHeight);
	}

	const ofFbo & previewEffectsSourceFbo = useVideoAdjustPreview
		? videoAdjustPreviewFbo
		: videoPreviewFbo;
	const ofTexture & previewEffectsSourceTexture = previewEffectsSourceFbo.getTexture();

	// Only real video frames are pushed through the anaglyph pass.
	// Cover art and empty placeholders stay on the normal preview path.
	if (shouldUseAnaglyphPreview(
			videoPreviewHasContent,
			videoPreviewShowsVideo,
			remoteGui.isAnaglyphEnabled(),
			player.getVideoStereoMode(),
			true,
			previewEffectsSourceFbo)) {
		updateAnaglyphPreview(previewEffectsSourceTexture, videoPreviewWidth, videoPreviewHeight);
	}
	if (projectMInitialized &&
		renderProjectMPreview &&
		projectMTextureSourceMode == ProjectMTextureSourceMode::MainPlayerVideo) {
		// projectM consumes a texture, so the player frame is copied into its dedicated source FBO here.
		drawPlayerToFbo(
			player,
			projectMSourceFbo,
			player.getWidth(),
			player.getHeight(),
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
	const ofTexture emptyTexture;
	const bool showActiveVideoPreview = videoPreviewHasContent && (!player.isStopped() || player.isPlaybackRestartPending());
	// Display switches to the derived anaglyph texture only when the preview currently
	// represents SBS video. All other states keep the standard preview texture.
	const bool useAnaglyphPreview = shouldUseAnaglyphPreview(
		showActiveVideoPreview,
		videoPreviewShowsVideo,
		remoteGui.isAnaglyphEnabled(),
		player.getVideoStereoMode(),
		anaglyphShaderReady,
		anaglyphPreviewFbo);
	const bool useAdjustedPreview = shouldUseAdjustedVideoPreview(
		showActiveVideoPreview,
		videoPreviewShowsVideo,
		player.isVideoAdjustmentsEnabled(),
		videoAdjustShaderReady,
		videoAdjustPreviewFbo);
	const ofTexture * videoPreviewTexturePtr = &emptyTexture;
	if (useAnaglyphPreview) {
		videoPreviewTexturePtr = &anaglyphPreviewFbo.getTexture();
	} else if (useAdjustedPreview) {
		videoPreviewTexturePtr = &videoAdjustPreviewFbo.getTexture();
	} else if (showActiveVideoPreview && videoPreviewFbo.isAllocated()) {
		videoPreviewTexturePtr = &videoPreviewFbo.getTexture();
	}
	const ofTexture & videoPreviewTexture = *videoPreviewTexturePtr;
	const float displayPreviewWidth = showActiveVideoPreview
		? (useAnaglyphPreview ? std::max(1.0f, videoPreviewWidth * 0.5f) : videoPreviewWidth)
		: 0.0f;
	const float displayPreviewHeight = showActiveVideoPreview ? videoPreviewHeight : 0.0f;

	remoteGui.draw(
		player,
		projectM,
		projectMInitialized,
		videoPreviewTexture,
		displayPreviewWidth,
		displayPreviewHeight,
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
void ofApp::exit() {
	player.close();
	soundStream.close();
}

int ofApp::addPathToPlaylist(const std::string & rawPath) {
	const std::string resolvedPath = resolveInputPath(rawPath);
	const bool isLocalPath = !looksLikeUri(resolvedPath);
	if (resolvedPath.empty()) {
		ofxVlc4Player::logWarning("Playlist path is empty.");
		return 0;
	}

	if (isLocalPath && !pathExists(resolvedPath)) {
		ofxVlc4Player::logWarning("Playlist path not found: " + normalizeInputPath(rawPath));
		return 0;
	}

	const int addedCount = player.addPathToPlaylist(resolvedPath, kSeedExtensions);
	return addedCount;
}

void ofApp::drawPlayerToFbo(ofxVlc4Player & sourcePlayer, ofFbo & targetFbo, float width, float height, bool preserveAspect) {
	if (!targetFbo.isAllocated()) {
		return;
	}

	targetFbo.begin();
	ofClear(0, 0, 0, 255);
	if (width > 0.0f && height > 0.0f) {
		ofPushStyle();
		ofEnableBlendMode(OF_BLENDMODE_DISABLED);
		ofSetColor(255, 255, 255, 255);
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
		ofPopStyle();
	}
	targetFbo.end();
}

void ofApp::refreshProjectMSourceTexture() {
	if (projectMTextureSourceMode != ProjectMTextureSourceMode::MainPlayerVideo) {
		return;
	}

	// Clear the source FBO when VLC has no valid frame so projectM shows a neutral texture instead of a stale frame.
	const float sourceWidth = player.getWidth();
	const float sourceHeight = player.getHeight();
	if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
		clearAllocatedFbo(projectMSourceFbo);
		return;
	}

	drawPlayerToFbo(
		player,
		projectMSourceFbo,
		sourceWidth,
		sourceHeight,
		false);
}

void ofApp::applyProjectMTexture() {
	switch (projectMTextureSourceMode) {
	case ProjectMTextureSourceMode::InternalTextures:
		ofxProjectM::logNotice("Texture source: internal textures.");
		projectM.useInternalTextureOnly();
		projectM.resetTextures();
		break;
	case ProjectMTextureSourceMode::CustomImage:
		ofxProjectM::logNotice("Texture source: image texture.");
		if (projectMCustomTextureImage.isAllocated()) {
			drawProjectMSourceImageToFbo(projectMSourceFbo, projectMCustomTextureImage);
		}
		projectM.setTexture(projectMSourceFbo.getTexture());
		projectM.resetTextures();
		break;
	case ProjectMTextureSourceMode::MainPlayerVideo:
		ofxProjectM::logNotice("Texture source: main player video.");
		refreshProjectMSourceTexture();
		projectM.setTexture(projectMSourceFbo.getTexture());
		projectM.resetTextures();
		break;
	}
}

bool ofApp::hasProjectMSourceSize() const {
	switch (projectMTextureSourceMode) {
	case ProjectMTextureSourceMode::MainPlayerVideo:
		return player.getWidth() > 0.0f && player.getHeight() > 0.0f;
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
		restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
		return;
	}

	if (projectMTextureSourceMode == ProjectMTextureSourceMode::MainPlayerVideo) {
		applyProjectMTexture();
		restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
		return;
	}

	if (!ensureLoadedImage(projectMCustomTextureImage, projectMCustomTexturePath)) {
		projectMTextureSourceMode = ProjectMTextureSourceMode::InternalTextures;
	}

	applyProjectMTexture();
	restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
}

void ofApp::loadPlayerProjectMTexture() {
	projectMTextureSourceMode = ProjectMTextureSourceMode::MainPlayerVideo;
	applyProjectMTexture();
	restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
}

bool ofApp::loadCustomProjectMTexture(const std::string & rawPath) {
	const std::string normalizedPath = normalizeInputPath(rawPath);
	const std::string requestedPath = normalizedPath.empty() ? projectMCustomTexturePath : normalizedPath;
	const std::string resolvedPath = resolveInputPath(requestedPath);
	if (resolvedPath.empty()) {
		ofxProjectM::logWarning("projectM image texture path is empty.");
		return false;
	}

	if (!pathExists(resolvedPath)) {
		ofxProjectM::logWarning("projectM image texture path not found: " + requestedPath);
		return false;
	}

	if (isSupportedVideoPath(resolvedPath)) {
		ofxProjectM::logWarning("Only image textures allowed.");
		return false;
	}

	if (!isSupportedCustomImagePath(resolvedPath)) {
		return false;
	}

	ofImage image;
	if (!image.load(resolvedPath)) {
		ofxProjectM::logError("Failed to load custom projectM texture image: " + resolvedPath);
		return false;
	}

	projectMCustomTextureImage = std::move(image);
	projectMCustomTexturePath = resolvedPath;
	projectMTextureSourceMode = ProjectMTextureSourceMode::CustomImage;
	applyProjectMTexture();
	restartCurrentProjectMPresetIfInitialized(projectM, projectMInitialized);
	return true;
}
