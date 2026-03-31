#include "ofxVlc4Player.h"
#include "ofxVlc4PlayerRecorder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <sstream>

#include "vlc/libvlc_picture.h"

namespace {
constexpr double kBufferedAudioSeconds = 0.75;
constexpr const char * kLogChannel = "ofxVlc4Player";
std::atomic<int> gLogLevel { static_cast<int>(OF_LOG_NOTICE) };

std::string trimWhitespace(const std::string & value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return "";
	}

	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

bool isUri(const std::string & value) {
	return ofStringTimesInString(value, "://") == 1;
}

std::string artworkFileExtension(libvlc_picture_type_t pictureType) {
	switch (pictureType) {
	case libvlc_picture_Png:
		return ".png";
	case libvlc_picture_Jpg:
		return ".jpg";
	case libvlc_picture_WebP:
		return ".webp";
	default:
		return ".img";
	}
}

bool isStoppedOrIdleState(libvlc_state_t state) {
	return state == libvlc_Stopped || state == libvlc_NothingSpecial;
}

bool isTransientPlaybackState(libvlc_state_t state) {
	return state == libvlc_Opening || state == libvlc_Buffering || state == libvlc_Stopping;
}

libvlc_video_projection_t toLibvlcProjectionMode(ofxVlc4Player::VideoProjectionMode mode) {
	switch (mode) {
	case ofxVlc4Player::VideoProjectionMode::Rectangular:
		return libvlc_video_projection_rectangular;
	case ofxVlc4Player::VideoProjectionMode::Equirectangular:
		return libvlc_video_projection_equirectangular;
	case ofxVlc4Player::VideoProjectionMode::CubemapStandard:
		return libvlc_video_projection_cubemap_layout_standard;
	case ofxVlc4Player::VideoProjectionMode::Auto:
	default:
		return libvlc_video_projection_rectangular;
	}
}

libvlc_video_stereo_mode_t toLibvlcStereoMode(ofxVlc4Player::VideoStereoMode mode) {
	switch (mode) {
	case ofxVlc4Player::VideoStereoMode::Stereo:
		return libvlc_VideoStereoStereo;
	case ofxVlc4Player::VideoStereoMode::LeftEye:
		return libvlc_VideoStereoLeftEye;
	case ofxVlc4Player::VideoStereoMode::RightEye:
		return libvlc_VideoStereoRightEye;
	case ofxVlc4Player::VideoStereoMode::SideBySide:
		return libvlc_VideoStereoSideBySide;
	case ofxVlc4Player::VideoStereoMode::Auto:
	default:
		return libvlc_VideoStereoAuto;
	}
}

std::string fileNameFromUri(const std::string & uri) {
	const size_t queryPos = uri.find_first_of("?#");
	const std::string withoutQuery = uri.substr(0, queryPos);
	const size_t slashPos = withoutQuery.find_last_of('/');
	if (slashPos == std::string::npos || slashPos + 1 >= withoutQuery.size()) {
		return withoutQuery;
	}
	return withoutQuery.substr(slashPos + 1);
}

std::string mediaLabelForPath(const std::string & path) {
	if (path.empty()) {
		return "";
	}

	if (isUri(path)) {
		const std::string label = fileNameFromUri(path);
		return label.empty() ? path : label;
	}

	return ofFilePath::getFileName(path);
}

std::string readMediaMeta(libvlc_media_t * media, libvlc_meta_t metaType) {
	if (!media) {
		return "";
	}

	char * rawValue = libvlc_media_get_meta(media, metaType);
	if (!rawValue) {
		return "";
	}

	std::string value = trimWhitespace(rawValue);
	libvlc_free(rawValue);
	return value;
}

const std::set<std::string> & defaultMediaExtensions() {
	static const std::set<std::string> extensions = {
		".wav", ".mp3", ".flac", ".ogg",
		".m4a", ".aac", ".aiff", ".wma",
		".mp4", ".mov", ".mkv", ".avi",
		".webm", ".m4v", ".mpg", ".mpeg",
		".jpg", ".jpeg", ".png", ".bmp"
	};
	return extensions;
}

std::set<std::string> normalizeExtensions(std::initializer_list<std::string> extensions) {
	std::set<std::string> out;
	for (const auto & extension : extensions) {
		std::string value = ofToLower(trimWhitespace(extension));
		if (value.empty()) {
			continue;
		}
		if (!value.empty() && value[0] != '.') {
			value = "." + value;
		}
		out.insert(value);
	}
	return out;
}

void clearAllocatedFbo(ofFbo & fbo) {
	if (!fbo.isAllocated()) {
		return;
	}

	fbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.end();
}

bool shouldLog(ofLogLevel level) {
	const ofLogLevel configuredLevel = static_cast<ofLogLevel>(gLogLevel.load());
	return configuredLevel != OF_LOG_SILENT && level >= configuredLevel;
}

bool nearlyEqual(float a, float b, float epsilon = 0.0001f) {
	return std::abs(a - b) <= epsilon;
}

std::string formatAdjustmentValue(float value, int precision = 1, const char * suffix = nullptr) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(precision) << value;
	if (suffix && *suffix) {
		stream << suffix;
	}
	return stream.str();
}

void appendMetadataValue(
	std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & label,
	const std::string & value) {
	const std::string trimmedValue = trimWhitespace(value);
	if (!trimmedValue.empty()) {
		metadata.emplace_back(label, trimmedValue);
	}
}

std::string codecFourccToString(uint32_t codec) {
	std::string out(4, ' ');
	out[0] = static_cast<char>(codec & 0xFF);
	out[1] = static_cast<char>((codec >> 8) & 0xFF);
	out[2] = static_cast<char>((codec >> 16) & 0xFF);
	out[3] = static_cast<char>((codec >> 24) & 0xFF);

	for (char & ch : out) {
		const unsigned char uchar = static_cast<unsigned char>(ch);
		if (!std::isprint(uchar) || std::isspace(uchar)) {
			ch = '.';
		}
	}

	return out;
}

std::string describeCodec(libvlc_track_type_t trackType, uint32_t codec) {
	if (codec == 0) {
		return "";
	}

	const char * description = libvlc_media_get_codec_description(trackType, codec);
	const std::string fourcc = codecFourccToString(codec);
	if (description && *description) {
		return std::string(description) + " (" + fourcc + ")";
	}

	return fourcc;
}

std::string formatBitrate(unsigned int bitsPerSecond) {
	if (bitsPerSecond == 0) {
		return "";
	}

	std::ostringstream stream;
	if (bitsPerSecond >= 1000000) {
		stream << std::fixed << std::setprecision(1)
			   << (static_cast<double>(bitsPerSecond) / 1000000.0) << " Mbps";
	} else {
		stream << std::fixed << std::setprecision(0)
			   << (static_cast<double>(bitsPerSecond) / 1000.0) << " kbps";
	}
	return stream.str();
}

std::string formatFrameRate(unsigned numerator, unsigned denominator) {
	if (numerator == 0 || denominator == 0) {
		return "";
	}

	std::ostringstream stream;
	stream << std::fixed << std::setprecision(2)
		   << (static_cast<double>(numerator) / static_cast<double>(denominator)) << " fps";
	return stream.str();
}

void appendTrackMetadata(
	std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & prefix,
	libvlc_track_type_t trackType,
	const libvlc_media_track_t * track) {
	if (!track || track->i_type != trackType) {
		return;
	}

	appendMetadataValue(metadata, prefix + " Codec", describeCodec(trackType, track->i_codec));
	appendMetadataValue(metadata, prefix + " Bitrate", formatBitrate(track->i_bitrate));
	appendMetadataValue(
		metadata,
		prefix + " Language",
		track->psz_language ? track->psz_language : "");
	appendMetadataValue(
		metadata,
		prefix + " Track",
		track->psz_name ? track->psz_name : "");

	if (trackType == libvlc_track_video && track->video) {
		const auto * video = track->video;
		if (video->i_width > 0 && video->i_height > 0) {
			appendMetadataValue(
				metadata,
				"Video Resolution",
				ofToString(video->i_width) + " x " + ofToString(video->i_height));
		}
		appendMetadataValue(metadata, "Frame Rate", formatFrameRate(video->i_frame_rate_num, video->i_frame_rate_den));
		if (video->i_sar_num > 0 && video->i_sar_den > 0 &&
			(video->i_sar_num != 1 || video->i_sar_den != 1)) {
			appendMetadataValue(
				metadata,
				"Pixel Aspect",
				ofToString(video->i_sar_num) + ":" + ofToString(video->i_sar_den));
		}
	} else if (trackType == libvlc_track_audio && track->audio) {
		const auto * audio = track->audio;
		if (audio->i_channels > 0) {
			appendMetadataValue(metadata, "Audio Channels", ofToString(audio->i_channels));
		}
		if (audio->i_rate > 0) {
			appendMetadataValue(metadata, "Audio Rate", ofToString(audio->i_rate) + " Hz");
		}
	} else if (trackType == libvlc_track_text && track->subtitle) {
		appendMetadataValue(
			metadata,
			"Subtitle Encoding",
			track->subtitle->psz_encoding ? track->subtitle->psz_encoding : "");
	}
}

void appendTrackMetadataFromMediaTracklist(
	std::vector<std::pair<std::string, std::string>> & metadata,
	libvlc_media_t * media,
	libvlc_track_type_t trackType,
	const std::string & prefix) {
	if (!media) {
		return;
	}

	libvlc_media_tracklist_t * tracklist = libvlc_media_get_tracklist(media, trackType);
	if (!tracklist) {
		return;
	}

	const size_t trackCount = libvlc_media_tracklist_count(tracklist);
	if (trackCount > 0) {
		appendTrackMetadata(metadata, prefix, trackType, libvlc_media_tracklist_at(tracklist, 0));
	}

	libvlc_media_tracklist_delete(tracklist);
}

void waitForMediaParse(libvlc_media_t * media, int timeoutMs) {
	if (!media || timeoutMs <= 0) {
		return;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (std::chrono::steady_clock::now() < deadline) {
		const libvlc_media_parsed_status_t status = libvlc_media_get_parsed_status(media);
		if (status == libvlc_media_parsed_status_done ||
			status == libvlc_media_parsed_status_failed ||
			status == libvlc_media_parsed_status_timeout ||
			status == libvlc_media_parsed_status_skipped ||
			status == libvlc_media_parsed_status_cancelled) {
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
}

bool hasDetailedTrackMetadata(const std::vector<std::pair<std::string, std::string>> & metadata) {
	for (const auto & [label, value] : metadata) {
		if (value.empty()) {
			continue;
		}

		if (label == "Video Codec" ||
			label == "Audio Codec" ||
			label == "Subtitle Codec" ||
			label == "Video Resolution" ||
			label == "Frame Rate" ||
			label == "Audio Channels" ||
			label == "Audio Rate") {
			return true;
		}
	}

	return false;
}
}

ofxVlc4Player::ofxVlc4Player() {
	ofGLFWWindowSettings settings;
	mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
	if (mainWindow) {
		settings = mainWindow->getSettings();
	}
	settings.setSize(1, 1);
	settings.setPosition(glm::vec2(-32000, -32000));
	settings.visible = false;
	settings.decorated = false;
	settings.resizable = false;
	settings.shareContextWith = mainWindow;
	vlcWindow = std::make_shared<ofAppGLFWWindow>();
	vlcWindow->setup(settings);
	vlcWindow->setVerticalSync(true);

	fbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(fbo);
	exposedTextureFbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(exposedTextureFbo);
	allocatedVideoWidth = 1;
	allocatedVideoHeight = 1;
	equalizerBandAmps.assign(libvlc_audio_equalizer_get_band_count(), 0.0f);
}

ofxVlc4Player::~ofxVlc4Player() {
	close();
}

void ofxVlc4Player::setLogLevel(ofLogLevel level) {
	gLogLevel.store(static_cast<int>(level));
}

ofLogLevel ofxVlc4Player::getLogLevel() {
	return static_cast<ofLogLevel>(gLogLevel.load());
}

void ofxVlc4Player::logVerbose(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_VERBOSE)) {
		ofLogVerbose(kLogChannel) << message;
	}
}

void ofxVlc4Player::logError(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_ERROR)) {
		ofLogError(kLogChannel) << message;
	}
}

void ofxVlc4Player::logWarning(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_WARNING)) {
		ofLogWarning(kLogChannel) << message;
	}
}

void ofxVlc4Player::logNotice(const std::string & message) {
	if (!message.empty() && shouldLog(OF_LOG_NOTICE)) {
		ofLogNotice(kLogChannel) << message;
	}
}

void ofxVlc4Player::update() {
	refreshDisplayAspectRatio();
	refreshExposedTexture();
	updateRecorder();

	if (pendingActivateReady.exchange(false)) {
		const int pendingIndex = pendingActivateIndex.exchange(-1);
		const bool shouldPlay = pendingActivateShouldPlay.exchange(false);
		if (pendingIndex >= 0) {
			activatePlaylistIndexImmediate(pendingIndex, shouldPlay);
		}
	}

	if (playNextRequested.exchange(false)) {
		handlePlaybackEnded();
	}
}

void ofxVlc4Player::init(int vlc_argc, char const * vlc_argv[]) {
	// Re-init starts from a clean VLC state so partial previous setup cannot leak across sessions.
	releaseVlcResources();
	mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(ofGetCurrentWindow());
	shuttingDown.store(false);
	pendingManualStopEvents.store(0);
	playNextRequested.store(false);
	playbackWanted.store(false);
	pauseRequested.store(false);
	audioPausedSignal.store(false);
	clearPendingActivationRequest();
	recorder.clearVideoRecording();
	recorder.clearAudioRecording();
	clearLastMessages();

	libvlc = libvlc_new(vlc_argc, vlc_argv);
	if (!libvlc) {
		const char * error = libvlc_errmsg();
		setError(error ? error : "libvlc_new failed");
		return;
	}

	mediaPlayer = libvlc_media_player_new(libvlc);
	if (!mediaPlayer) {
		const char * error = libvlc_errmsg();
		setError(error ? error : "libvlc_media_player_new failed");
		releaseVlcResources();
		return;
	}

	libvlc_video_set_output_callbacks(
		mediaPlayer,
		libvlc_video_engine_opengl,
		nullptr, nullptr, nullptr,
		videoResize,
		videoSwap,
		make_current,
		get_proc_address,
		nullptr, nullptr,
		this);

	if (audioCaptureEnabled) {
		libvlc_audio_set_callbacks(mediaPlayer, audioPlay, audioPause, audioResume, audioFlush, audioDrain, this);
		libvlc_audio_set_format_callbacks(mediaPlayer, audioSetup, audioCleanup);
	} else {
		isAudioReady.store(false);
		resetAudioBuffer();
	}

	mediaPlayerEventManager = libvlc_media_player_event_manager(mediaPlayer);
	if (mediaPlayerEventManager) {
		libvlc_event_attach(mediaPlayerEventManager, libvlc_MediaPlayerLengthChanged, vlcMediaPlayerEventStatic, this);
		libvlc_event_attach(mediaPlayerEventManager, libvlc_MediaPlayerStopped, vlcMediaPlayerEventStatic, this);
		libvlc_event_attach(mediaPlayerEventManager, libvlc_MediaPlayerPlaying, vlcMediaPlayerEventStatic, this);
	}

	applyEqualizerSettings();
	applyVideoAdjustments();
	applyVideoProjectionMode();
	applyVideoStereoMode();
	applyVideoViewpoint();
	logNotice("Player initialized.");
}

void ofxVlc4Player::setError(const std::string & message) {
	lastErrorMessage = message;
	lastStatusMessage.clear();
	logError(message);
}

void ofxVlc4Player::setStatus(const std::string & message) {
	lastStatusMessage = message;
	lastErrorMessage.clear();
}

void ofxVlc4Player::clearPendingActivationRequest() {
	pendingActivateIndex.store(-1);
	pendingActivateShouldPlay.store(false);
	pendingActivateReady.store(false);
}

void ofxVlc4Player::clearMetadataCache() {
	std::lock_guard<std::mutex> lock(metadataCacheMutex);
	metadataCache.clear();
}

void ofxVlc4Player::cacheArtworkPathForCurrentMedia(const std::string & artworkPath) {
	if (artworkPath.empty() || currentIndex < 0 || currentIndex >= static_cast<int>(playlist.size())) {
		return;
	}

	const std::string & currentPath = playlist[currentIndex];
	if (currentPath.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(metadataCacheMutex);
	auto & metadata = metadataCache[currentPath];
	auto existing = std::find_if(
		metadata.begin(),
		metadata.end(),
		[](const auto & entry) { return entry.first == "Artwork URL"; });
	if (existing != metadata.end()) {
		existing->second = artworkPath;
	} else {
		metadata.emplace_back("Artwork URL", artworkPath);
	}
}

ofxVlc4Player::PlaybackMode ofxVlc4Player::playbackModeFromString(const std::string & mode) {
	const std::string normalized = ofToLower(trimWhitespace(mode));
	if (normalized == "repeat") {
		return PlaybackMode::Repeat;
	}
	if (normalized == "loop") {
		return PlaybackMode::Loop;
	}
	return PlaybackMode::Default;
}

std::string ofxVlc4Player::playbackModeToString(PlaybackMode mode) {
	switch (mode) {
	case PlaybackMode::Repeat:
		return "repeat";
	case PlaybackMode::Loop:
		return "loop";
	case PlaybackMode::Default:
	default:
		return "default";
	}
}

void ofxVlc4Player::setVideoRecordingFrameRate(int fps) {
	recorder.setVideoFrameRate(fps);
	if (!recorder.getLastError().empty()) {
		setError(recorder.getLastError());
		recorder.clearLastError();
	}
}

void ofxVlc4Player::setVideoRecordingCodec(const std::string & codec) {
	recorder.setVideoCodec(codec);
	if (!recorder.getLastError().empty()) {
		setError(recorder.getLastError());
		recorder.clearLastError();
	}
}

void ofxVlc4Player::detachEvents() {
	if (mediaPlayerEventManager) {
		libvlc_event_detach(mediaPlayerEventManager, libvlc_MediaPlayerLengthChanged, vlcMediaPlayerEventStatic, this);
		libvlc_event_detach(mediaPlayerEventManager, libvlc_MediaPlayerStopped, vlcMediaPlayerEventStatic, this);
		libvlc_event_detach(mediaPlayerEventManager, libvlc_MediaPlayerPlaying, vlcMediaPlayerEventStatic, this);
		mediaPlayerEventManager = nullptr;
	}

	if (mediaEventManager) {
		libvlc_event_detach(mediaEventManager, libvlc_MediaParsedChanged, vlcMediaEventStatic, this);
		libvlc_event_detach(mediaEventManager, libvlc_MediaAttachedThumbnailsFound, vlcMediaEventStatic, this);
		mediaEventManager = nullptr;
	}
}

void ofxVlc4Player::releaseVlcResources() {
	detachEvents();

	if (mediaPlayer) {
		libvlc_media_player_release(mediaPlayer);
		mediaPlayer = nullptr;
	}

	clearCurrentMedia();
	recorder.clearVideoRecording();
	recorder.clearAudioRecording();

	if (libvlc) {
		libvlc_release(libvlc);
		libvlc = nullptr;
	}
}

bool ofxVlc4Player::isSupportedMediaFile(const ofFile & file, const std::set<std::string> * extensions) const {
	if (!file.exists() || !file.isFile()) {
		return false;
	}

	const std::set<std::string> & activeExtensions = extensions ? *extensions : defaultMediaExtensions();
	const std::string extension = "." + ofToLower(ofFilePath::getFileExt(file.getAbsolutePath()));
	return !extension.empty() && activeExtensions.count(extension) > 0;
}

void ofxVlc4Player::prepareAudioRingBuffer() {
	const int rate = std::max(sampleRate.load(), 44100);
	const int channelCount = std::max(channels.load(), 2);
	sampleRate.store(rate);
	channels.store(channelCount);

	const size_t requestedSamples =
		static_cast<size_t>(rate) *
		static_cast<size_t>(channelCount) *
		kBufferedAudioSeconds;

	std::lock_guard<std::mutex> lock(audioMutex);
	ringBuffer.allocate(requestedSamples);
}

void ofxVlc4Player::resetAudioBuffer() {
	std::lock_guard<std::mutex> lock(audioMutex);
	ringBuffer.reset();
}

void ofxVlc4Player::ensureVideoFboCapacity(unsigned requiredWidth, unsigned requiredHeight) {
	if (requiredWidth == 0 || requiredHeight == 0 || shuttingDown.load()) {
		return;
	}

	if (!fbo.isAllocated() || requiredWidth > allocatedVideoWidth || requiredHeight > allocatedVideoHeight) {
		allocatedVideoWidth = std::max(allocatedVideoWidth, requiredWidth);
		allocatedVideoHeight = std::max(allocatedVideoHeight, requiredHeight);
		fbo.allocate(allocatedVideoWidth, allocatedVideoHeight, GL_RGBA);
		fbo.getTexture().getTextureData().bFlipTexture = true;
		clearAllocatedFbo(fbo);
	}
}

void ofxVlc4Player::ensureExposedTextureFboCapacity(unsigned requiredWidth, unsigned requiredHeight) {
	if (requiredWidth == 0 || requiredHeight == 0 || shuttingDown.load()) {
		return;
	}

	const unsigned currentWidth =
		exposedTextureFbo.isAllocated() ? static_cast<unsigned>(exposedTextureFbo.getWidth()) : 0u;
	const unsigned currentHeight =
		exposedTextureFbo.isAllocated() ? static_cast<unsigned>(exposedTextureFbo.getHeight()) : 0u;
	const unsigned targetWidth = std::max(currentWidth, requiredWidth);
	const unsigned targetHeight = std::max(currentHeight, requiredHeight);

	if (!exposedTextureFbo.isAllocated() ||
		targetWidth != currentWidth ||
		targetHeight != currentHeight) {
		exposedTextureFbo.allocate(targetWidth, targetHeight, GL_RGBA);
		exposedTextureFbo.getTexture().getTextureData().bFlipTexture = true;
		clearAllocatedFbo(exposedTextureFbo);
	}
}

bool ofxVlc4Player::applyPendingVideoResize() {
	if (!pendingResize.exchange(false)) {
		return false;
	}

	const unsigned newRenderWidth = pendingRenderWidth.load();
	const unsigned newRenderHeight = pendingRenderHeight.load();
	if (newRenderWidth == 0 || newRenderHeight == 0) {
		return false;
	}

	unsigned visibleWidth = newRenderWidth;
	unsigned visibleHeight = newRenderHeight;
	if (mediaPlayer) {
		unsigned queriedWidth = 0;
		unsigned queriedHeight = 0;
		if (libvlc_video_get_size(mediaPlayer, 0, &queriedWidth, &queriedHeight) == 0 &&
			queriedWidth > 0 &&
			queriedHeight > 0) {
			visibleWidth = queriedWidth;
			visibleHeight = queriedHeight;
		}
	}

	refreshPixelAspectRatio();
	renderWidth.store(newRenderWidth);
	renderHeight.store(newRenderHeight);
	videoWidth.store(visibleWidth);
	videoHeight.store(visibleHeight);
	ensureVideoFboCapacity(newRenderWidth, newRenderHeight);
	isVideoLoaded.store(true);
	return true;
}

void ofxVlc4Player::recordVideo(std::string name, const ofTexture & texture) {
	if (!libvlc) {
		setError("Initialize libvlc first.");
		return;
	}
	if (recorder.isVideoRecording()) {
		setError("Stop video recording first.");
		return;
	}
	if (!mediaPlayer) {
		setError("Initialize the media player first.");
		return;
	}
	if (texture.isAllocated() &&
		exposedTextureFbo.isAllocated() &&
		texture.getTextureData().textureID == exposedTextureFbo.getTexture().getTextureData().textureID) {
		setError("Recording from the player's own output texture is not supported.");
		return;
	}

	libvlc_media_t * loadedMedia = libvlc_media_player_get_media(mediaPlayer);
	const bool hasLoadedMedia = (loadedMedia != nullptr);
	if (loadedMedia) {
		libvlc_media_release(loadedMedia);
	}

	if (hasLoadedMedia && !isStopped()) {
		setError("Stop current playback before recording.");
		return;
	}

	const std::string outputPath = ofxVlc4PlayerRecorder::buildTimestampedOutputPath(name, ".mp4");
	libvlc_media_t * recordingMedia = nullptr;
	if (!recorder.startVideoRecordingToPath(recordingMedia, outputPath, texture)) {
		setError(recorder.getLastError());
		return;
	}

	clearCurrentMedia();

	media = recordingMedia;
	libvlc_media_player_set_media(mediaPlayer, media);
	libvlc_media_player_play(mediaPlayer);
	setStatus("Video recording started: " + outputPath);
}

void ofxVlc4Player::recordAudio(std::string name) {
	if (!audioCaptureEnabled) {
		setError("Enable audio capture before recording audio.");
		return;
	}

	if (recorder.isAudioRecording()) {
		const std::string finishedPath = recorder.getOutputPath();
		recorder.clearAudioRecording();
		setStatus("Audio recording saved to " + finishedPath + ".");
		return;
	}

	const std::string outputPath = ofxVlc4PlayerRecorder::buildTimestampedOutputPath(name, ".wav");
	const int recordingChannelCount = std::max(channels.load(), 2);
	const int recordingSampleRate = std::max(sampleRate.load(), 44100);
	if (!recorder.startAudioRecordingToPath(outputPath, recordingSampleRate, recordingChannelCount)) {
		setError(recorder.getLastError());
		return;
	}

	setStatus("Audio recording started: " + outputPath);
}

void ofxVlc4Player::recordAudioVideo(std::string name, const ofTexture & texture) {
	if (recorder.isVideoRecording() || recorder.isAudioRecording()) {
		setError("Stop the current recording session first.");
		return;
	}
	if (!audioCaptureEnabled) {
		setError("Enable audio capture before recording audio.");
		return;
	}
	if (!libvlc) {
		setError("Initialize libvlc first.");
		return;
	}
	if (!mediaPlayer) {
		setError("Initialize the media player first.");
		return;
	}
	if (texture.isAllocated() &&
		exposedTextureFbo.isAllocated() &&
		texture.getTextureData().textureID == exposedTextureFbo.getTexture().getTextureData().textureID) {
		setError("Recording from the player's own output texture is not supported.");
		return;
	}

	libvlc_media_t * loadedMedia = libvlc_media_player_get_media(mediaPlayer);
	const bool hasLoadedMedia = (loadedMedia != nullptr);
	if (loadedMedia) {
		libvlc_media_release(loadedMedia);
	}

	if (hasLoadedMedia && !isStopped()) {
		setError("Stop current playback before recording.");
		return;
	}

	const std::string outputBase = ofxVlc4PlayerRecorder::buildTimestampedOutputBasePath(name);
	const std::string audioPath = outputBase + ".wav";
	const std::string videoPath = outputBase + ".mp4";
	const int recordingChannelCount = std::max(channels.load(), 2);
	const int recordingSampleRate = std::max(sampleRate.load(), 44100);
	if (!recorder.startAudioRecordingToPath(audioPath, recordingSampleRate, recordingChannelCount)) {
		setError(recorder.getLastError());
		return;
	}

	libvlc_media_t * recordingMedia = nullptr;
	if (!recorder.startVideoRecordingToPath(recordingMedia, videoPath, texture)) {
		recorder.clearAudioRecording();
		setError(recorder.getLastError());
		return;
	}

	clearCurrentMedia();

	media = recordingMedia;
	libvlc_media_player_set_media(mediaPlayer, media);
	libvlc_media_player_play(mediaPlayer);
	setStatus("Audio/video recording started: " + videoPath + " and " + audioPath);
}

bool ofxVlc4Player::loadMediaAtIndex(int index) {
	if (!mediaPlayer) return false;
	if (index < 0 || index >= static_cast<int>(playlist.size())) return false;

	clearCurrentMedia();

	const std::string & path = playlist[index];

	if (ofStringTimesInString(path, "://") == 1) {
		media = libvlc_media_new_location(path.c_str());
	} else {
		media = libvlc_media_new_path(path.c_str());
	}

	if (!media) {
		setError("Failed to create media for playlist item.");
		return false;
	}

	mediaEventManager = libvlc_media_event_manager(media);
	if (mediaEventManager) {
		libvlc_event_attach(mediaEventManager, libvlc_MediaParsedChanged, vlcMediaEventStatic, this);
		libvlc_event_attach(mediaEventManager, libvlc_MediaAttachedThumbnailsFound, vlcMediaEventStatic, this);
	}

	if (libvlc) {
		libvlc_media_parse_flag_t parseFlags = libvlc_media_parse_forced;
		if (isUri(path)) {
			parseFlags = static_cast<libvlc_media_parse_flag_t>(parseFlags | libvlc_media_parse_network);
		} else {
			parseFlags = static_cast<libvlc_media_parse_flag_t>(parseFlags | libvlc_media_parse_local);
		}
		if (libvlc_media_parse_request(libvlc, media, parseFlags, 1000) != 0) {
			logNotice("Media parse request failed: " + path);
		}
	}

	libvlc_media_player_set_media(mediaPlayer, media);
	applyVideoProjectionMode();
	applyVideoStereoMode();
	applyVideoViewpoint();
	return true;
}

void ofxVlc4Player::clearCurrentMedia() {
	if (mediaPlayer) {
		libvlc_media_player_set_media(mediaPlayer, nullptr);
	}

	if (mediaEventManager) {
		libvlc_event_detach(mediaEventManager, libvlc_MediaParsedChanged, vlcMediaEventStatic, this);
		libvlc_event_detach(mediaEventManager, libvlc_MediaAttachedThumbnailsFound, vlcMediaEventStatic, this);
		mediaEventManager = nullptr;
	}

	if (media) {
		libvlc_media_release(media);
		media = nullptr;
	}

	{
		std::lock_guard<std::mutex> lock(videoMutex);
		renderWidth.store(0);
		renderHeight.store(0);
		videoWidth.store(0);
		videoHeight.store(0);
		pixelAspectNumerator.store(1);
		pixelAspectDenominator.store(1);
		pendingRenderWidth.store(0);
		pendingRenderHeight.store(0);
		pendingResize.store(false);
		displayAspectRatio.store(1.0f);
		isVideoLoaded.store(false);
		clearAllocatedFbo(fbo);
		clearAllocatedFbo(exposedTextureFbo);
	}

	audioPausedSignal.store(false);
}

void ofxVlc4Player::addToPlaylistInternal(const std::string & path, bool preloadMetadata) {
	if (!libvlc) {
		setError("Initialize libvlc first.");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(metadataCacheMutex);
		metadataCache.erase(path);
	}

	playlist.push_back(path);
	setStatus("Added media to playlist.");
	logNotice("Playlist item added: " + mediaLabelForPath(path) + ".");

	if (currentIndex < 0 && !playlist.empty()) {
		currentIndex = 0;
	}

	if (preloadMetadata) {
		// Warm the metadata cache as soon as a manually added item enters the playlist so
		// the UI does not have to trigger the first parse during initial display.
		getMetadataAtIndex(static_cast<int>(playlist.size()) - 1);
	}
}

void ofxVlc4Player::addToPlaylist(const std::string & path) {
	addToPlaylistInternal(path, true);
}

int ofxVlc4Player::addPathToPlaylist(const std::string & path) {
	return addPathToPlaylist(path, {});
}

int ofxVlc4Player::addPathToPlaylist(const std::string & path, std::initializer_list<std::string> extensions) {
	if (!libvlc) {
		setError("Initialize libvlc first.");
		return 0;
	}

	const std::set<std::string> requestedExtensions = extensions.size() > 0 ? normalizeExtensions(extensions) : std::set<std::string>();
	const std::string trimmed = trimWhitespace(path);
	if (trimmed.empty()) {
		setError("Path is empty.");
		return 0;
	}

	if (isUri(trimmed)) {
		addToPlaylist(trimmed);
		setStatus("Added URI to playlist.");
		return 1;
	}

	const std::string resolvedPath = ofFilePath::getAbsolutePath(trimmed);
	ofFile file(resolvedPath);
	if (!file.exists()) {
		setError("Path not found: " + trimmed);
		return 0;
	}

	if (file.isDirectory()) {
		// Folder imports are filtered up front so the example UI only receives playable media entries.
		ofDirectory dir(resolvedPath);
		dir.listDir();

		int added = 0;
		for (int i = 0; i < dir.size(); ++i) {
			if (isSupportedMediaFile(dir.getFile(i), requestedExtensions.empty() ? nullptr : &requestedExtensions)) {
				addToPlaylistInternal(dir.getPath(i), false);
				++added;
			}
		}
		if (added == 0) {
			setError("No supported media files found in folder.");
		} else {
			setStatus("Added " + ofToString(added) + " media item(s) from folder.");
			if (currentIndex >= 0 && currentIndex < static_cast<int>(playlist.size())) {
				getMetadataAtIndex(currentIndex);
			}
		}
		return added;
	}

	if (!isSupportedMediaFile(file, requestedExtensions.empty() ? nullptr : &requestedExtensions)) {
		setError("Unsupported media file type: " + resolvedPath);
		return 0;
	}

	addToPlaylistInternal(resolvedPath, true);
	setStatus("Added media file to playlist.");
	return 1;
}

void ofxVlc4Player::clearPlaylist() {
	stop();
	playlist.clear();
	clearMetadataCache();
	currentIndex = -1;
	clearCurrentMedia();
	setStatus("Playlist cleared.");
	logNotice("Playlist cleared.");
}

void ofxVlc4Player::playIndex(int index) {
	if (!mediaPlayer) return;
	if (index < 0 || index >= static_cast<int>(playlist.size())) return;

	activatePlaylistIndex(index, true);
}

void ofxVlc4Player::activatePlaylistIndex(int index, bool shouldPlay) {
	if (!mediaPlayer) return;
	if (index < 0 || index >= static_cast<int>(playlist.size())) return;

	if (pendingManualStopEvents.load() > 0) {
		const libvlc_state_t state = libvlc_media_player_get_state(mediaPlayer);
		if (isStoppedOrIdleState(state)) {
			pendingManualStopEvents.store(0);
			clearPendingActivationRequest();
		} else {
			pendingActivateIndex.store(index);
			pendingActivateShouldPlay.store(shouldPlay);
			pendingActivateReady.store(false);
			playNextRequested.store(false);
			resetAudioBuffer();
			return;
		}
	}

	libvlc_media_t * loadedMedia = libvlc_media_player_get_media(mediaPlayer);
	const bool hasLoadedMedia = (loadedMedia != nullptr);
	if (loadedMedia) {
		libvlc_media_release(loadedMedia);
	}

	const libvlc_state_t state = libvlc_media_player_get_state(mediaPlayer);
	const bool needsAsyncStop =
		hasLoadedMedia &&
		!isStoppedOrIdleState(state);

	if (needsAsyncStop) {
		// Queue the new item until VLC confirms the previous input has fully stopped.
		pendingActivateIndex.store(index);
		pendingActivateShouldPlay.store(shouldPlay);
		pendingActivateReady.store(false);
		pendingManualStopEvents.store(1);
		playNextRequested.store(false);
		resetAudioBuffer();
		libvlc_media_player_stop_async(mediaPlayer);
		return;
	}

	clearPendingActivationRequest();
	activatePlaylistIndexImmediate(index, shouldPlay);
}

void ofxVlc4Player::activatePlaylistIndexImmediate(int index, bool shouldPlay) {
	if (!mediaPlayer) return;
	if (index < 0 || index >= static_cast<int>(playlist.size())) return;

	currentIndex = index;
	playNextRequested.store(false);
	resetAudioBuffer();
	playbackWanted.store(shouldPlay);

	if (!loadMediaAtIndex(index)) {
		return;
	}

	if (shouldPlay) {
		libvlc_media_player_play(mediaPlayer);
	}
}

void ofxVlc4Player::play() {
	if (!mediaPlayer) return;
	if (playlist.empty()) return;

	if (currentIndex < 0 || currentIndex >= static_cast<int>(playlist.size())) {
		currentIndex = 0;
	}

	playNextRequested.store(false);
	playbackWanted.store(true);
	pauseRequested.store(false);
	audioPausedSignal.store(false);
	const libvlc_state_t state = libvlc_media_player_get_state(mediaPlayer);
	if (pendingManualStopEvents.load() > 0 || pendingActivateIndex.load() >= 0) {
		pendingActivateShouldPlay.store(true);
		return;
	}
	if (isTransientPlaybackState(state)) {
		return;
	}

	libvlc_media_t * currentMedia = libvlc_media_player_get_media(mediaPlayer);
	bool hasLoadedMedia = (currentMedia != nullptr);
	if (currentMedia) {
		libvlc_media_release(currentMedia);
	}

	if (!hasLoadedMedia) {
		if (!loadMediaAtIndex(currentIndex)) {
			return;
		}
	}

	libvlc_media_player_play(mediaPlayer);
	logNotice("Playback started.");
}

void ofxVlc4Player::pause() {
	if (mediaPlayer) {
		const libvlc_state_t state = libvlc_media_player_get_state(mediaPlayer);
		const bool hasQueuedActivation = pendingManualStopEvents.load() > 0 || pendingActivateIndex.load() >= 0;
		const bool pauseSignaled = audioPausedSignal.load();
		const bool shouldResume = pauseRequested.load() || (pauseSignaled && (state == libvlc_Paused || hasQueuedActivation));
		if (shouldResume) {
			play();
			return;
		}
		if (isStoppedOrIdleState(state) && !hasQueuedActivation) {
			pauseRequested.store(false);
			playbackWanted.store(false);
			audioPausedSignal.store(false);
			return;
		}

		pauseRequested.store(true);
		playbackWanted.store(false);
		if (hasQueuedActivation) {
			pendingActivateShouldPlay.store(false);
			return;
		}
		if (isTransientPlaybackState(state)) {
			return;
		}
		if (state == libvlc_Playing) {
			libvlc_media_player_pause(mediaPlayer);
			logNotice("Playback paused.");
		}
	}
}

void ofxVlc4Player::stop() {
	recorder.clearVideoRecording();
	recorder.clearAudioRecording();
	clearPendingActivationRequest();
	pendingManualStopEvents.fetch_add(1);
	playNextRequested.store(false);
	playbackWanted.store(false);
	pauseRequested.store(false);
	audioPausedSignal.store(false);
	resetAudioBuffer();

	if (mediaPlayer) {
		// The VLC 4 API exposes async stop for media players, so shutdown completion comes back via events.
		libvlc_media_player_stop_async(mediaPlayer);
	}
	logNotice("Playback stopped.");
}

int ofxVlc4Player::getNextShuffleIndex() const {
	if (playlist.empty()) return -1;
	if (playlist.size() == 1) return 0;

	int next = currentIndex;
	while (next == currentIndex) {
		next = static_cast<int>(ofRandom(static_cast<float>(playlist.size())));
	}
	return next;
}

void ofxVlc4Player::nextMediaListItem() {
	if (playlist.empty()) return;
	const bool shouldPlay = playbackWanted.load();

	if (shuffleEnabled) {
		int next = getNextShuffleIndex();
		if (next >= 0) {
			activatePlaylistIndex(next, shouldPlay);
		}
		return;
	}

	if (currentIndex + 1 < static_cast<int>(playlist.size())) {
		activatePlaylistIndex(currentIndex + 1, shouldPlay);
	} else {
		activatePlaylistIndex(0, shouldPlay);
	}
	logNotice("Next playlist item selected.");
}

void ofxVlc4Player::previousMediaListItem() {
	if (playlist.empty()) return;
	const bool shouldPlay = playbackWanted.load();

	if (shuffleEnabled) {
		int next = getNextShuffleIndex();
		if (next >= 0) {
			activatePlaylistIndex(next, shouldPlay);
		}
		return;
	}

	if (currentIndex > 0) {
		activatePlaylistIndex(currentIndex - 1, shouldPlay);
	} else {
		activatePlaylistIndex(static_cast<int>(playlist.size()) - 1, shouldPlay);
	}
	logNotice("Previous playlist item selected.");
}

void ofxVlc4Player::removeFromPlaylist(int index) {
	if (index < 0 || index >= static_cast<int>(playlist.size())) {
		return;
	}

	bool wasCurrent = (index == currentIndex);
	const bool shouldPlayReplacement = wasCurrent && playbackWanted.load();

	if (wasCurrent) {
		stop();
		resetAudioBuffer();
	}

	playlist.erase(playlist.begin() + index);
	clearMetadataCache();

	if (playlist.empty()) {
		currentIndex = -1;
		clearCurrentMedia();
		return;
	}

	if (index < currentIndex) {
		currentIndex--;
	} else if (index == currentIndex) {
		if (currentIndex >= static_cast<int>(playlist.size())) {
			currentIndex = static_cast<int>(playlist.size()) - 1;
		}
	}

	if (wasCurrent && currentIndex >= 0) {
		activatePlaylistIndex(currentIndex, shouldPlayReplacement);
	}

	logNotice("Playlist item removed.");
}

void ofxVlc4Player::movePlaylistItem(int fromIndex, int toIndex) {
	if (fromIndex < 0 || fromIndex >= static_cast<int>(playlist.size())) return;
	if (toIndex < 0) return;
	if (toIndex > static_cast<int>(playlist.size())) {
		toIndex = static_cast<int>(playlist.size());
	}

	if (toIndex == fromIndex || toIndex == fromIndex + 1) return;

	const int originalCurrent = currentIndex;
	const std::string moved = playlist[fromIndex];
	// Inserting after erasing shifts the destination left when dragging an item downward.
	const int insertIndex = (fromIndex < toIndex) ? (toIndex - 1) : toIndex;

	playlist.erase(playlist.begin() + fromIndex);
	playlist.insert(playlist.begin() + insertIndex, moved);

	if (originalCurrent == fromIndex) {
		currentIndex = insertIndex;
	} else if (originalCurrent > fromIndex && originalCurrent <= insertIndex) {
		currentIndex = originalCurrent - 1;
	} else if (originalCurrent < fromIndex && originalCurrent >= insertIndex) {
		currentIndex = originalCurrent + 1;
	} else {
		currentIndex = originalCurrent;
	}

	logNotice("Playlist item moved.");
}

void ofxVlc4Player::movePlaylistItems(const std::vector<int> & fromIndices, int toIndex) {
	if (fromIndices.empty() || playlist.empty()) {
		return;
	}

	std::vector<int> indices = fromIndices;
	std::sort(indices.begin(), indices.end());
	indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

	indices.erase(
		std::remove_if(
			indices.begin(),
			indices.end(),
			[this](int index) { return index < 0 || index >= static_cast<int>(playlist.size()); }),
		indices.end());

	if (indices.empty()) {
		return;
	}

	if (toIndex < 0) {
		toIndex = 0;
	} else if (toIndex > static_cast<int>(playlist.size())) {
		toIndex = static_cast<int>(playlist.size());
	}

	std::vector<std::string> movedItems;
	movedItems.reserve(indices.size());
	std::vector<std::string> remaining;
	remaining.reserve(playlist.size() - indices.size());

	size_t selectedCursor = 0;
	for (int i = 0; i < static_cast<int>(playlist.size()); ++i) {
		if (selectedCursor < indices.size() && indices[selectedCursor] == i) {
			movedItems.push_back(playlist[i]);
			++selectedCursor;
		} else {
			remaining.push_back(playlist[i]);
		}
	}

	const int removedBeforeInsert =
		static_cast<int>(std::count_if(indices.begin(), indices.end(), [toIndex](int index) { return index < toIndex; }));
	int adjustedInsertIndex = toIndex - removedBeforeInsert;
	adjustedInsertIndex = ofClamp(adjustedInsertIndex, 0, static_cast<int>(remaining.size()));

	if (currentIndex >= 0) {
		const auto currentIt = std::find(indices.begin(), indices.end(), currentIndex);
		if (currentIt != indices.end()) {
			currentIndex = adjustedInsertIndex + static_cast<int>(std::distance(indices.begin(), currentIt));
		} else {
			const int removedBeforeCurrent =
				static_cast<int>(std::count_if(indices.begin(), indices.end(), [this](int index) { return index < currentIndex; }));
			int remainingIndex = currentIndex - removedBeforeCurrent;
			if (remainingIndex >= adjustedInsertIndex) {
				remainingIndex += static_cast<int>(movedItems.size());
			}
			currentIndex = remainingIndex;
		}
	}

	playlist = std::move(remaining);
	playlist.insert(
		playlist.begin() + adjustedInsertIndex,
		std::make_move_iterator(movedItems.begin()),
		std::make_move_iterator(movedItems.end()));
	clearMetadataCache();

	logNotice("Playlist items moved.");
}

void ofxVlc4Player::handlePlaybackEnded() {
	if (playlist.empty()) return;
	if (currentIndex < 0 || currentIndex >= static_cast<int>(playlist.size())) return;

	// End-of-track policy is centralized here so manual next/prev and VLC stop events stay consistent.
	if (playbackMode == PlaybackMode::Repeat) {
		playNextRequested.store(false);
		resetAudioBuffer();

		if (mediaPlayer) {
			libvlc_media_player_set_time(mediaPlayer, 0, true);
			libvlc_media_player_play(mediaPlayer);
			return;
		}

		playIndex(currentIndex);
		return;
	}

	if (shuffleEnabled) {
		int next = getNextShuffleIndex();
		if (next >= 0) {
			playIndex(next);
		}
		return;
	}

	if (currentIndex + 1 < static_cast<int>(playlist.size())) {
		playIndex(currentIndex + 1);
		return;
	}

	if (playbackMode == PlaybackMode::Loop) {
		playIndex(0);
		return;
	}

	playNextRequested.store(false);
	playbackWanted.store(false);
	pauseRequested.store(false);
	audioPausedSignal.store(false);
	resetAudioBuffer();
}

void ofxVlc4Player::applyEqualizerSettings() {
	if (!mediaPlayer) {
		return;
	}

	if (!equalizerEnabled) {
		libvlc_media_player_set_equalizer(mediaPlayer, nullptr);
		return;
	}

	libvlc_equalizer_t * equalizer = libvlc_audio_equalizer_new();
	if (!equalizer) {
		logWarning("Equalizer could not be created.");
		return;
	}

	libvlc_audio_equalizer_set_preamp(equalizer, equalizerPreamp);
	for (unsigned i = 0; i < equalizerBandAmps.size(); ++i) {
		libvlc_audio_equalizer_set_amp_at_index(equalizer, equalizerBandAmps[i], i);
	}

	if (libvlc_media_player_set_equalizer(mediaPlayer, equalizer) != 0) {
		logWarning("Equalizer settings could not be applied.");
	}

	libvlc_audio_equalizer_release(equalizer);
}

void ofxVlc4Player::applyVideoAdjustments() {
	// The example applies video adjustments in a post shader so the custom OpenGL
	// output path behaves consistently during playback. The player still owns the
	// adjustment state and exposes it to consumers through the getters/setters.
}

void ofxVlc4Player::applyVideoProjectionMode() {
	if (!mediaPlayer) {
		return;
	}

	if (videoProjectionMode == VideoProjectionMode::Auto) {
		libvlc_video_unset_projection_mode(mediaPlayer);
		return;
	}

	libvlc_video_set_projection_mode(mediaPlayer, toLibvlcProjectionMode(videoProjectionMode));
}

void ofxVlc4Player::applyVideoStereoMode() {
	if (!mediaPlayer) {
		return;
	}

	libvlc_video_set_video_stereo_mode(mediaPlayer, toLibvlcStereoMode(videoStereoMode));
}

void ofxVlc4Player::applyVideoViewpoint(bool absolute) {
	if (!mediaPlayer) {
		return;
	}

	libvlc_video_viewpoint_t viewpoint {};
	viewpoint.f_yaw = videoViewYaw;
	viewpoint.f_pitch = videoViewPitch;
	viewpoint.f_roll = videoViewRoll;
	viewpoint.f_field_of_view = videoViewFov;
	libvlc_video_update_viewpoint(mediaPlayer, &viewpoint, absolute);
}

void ofxVlc4Player::setPlaybackMode(PlaybackMode mode) {
	playbackMode = mode;
	setStatus("Playback mode set to " + playbackModeToString(mode) + ".");
	logNotice("Playback mode: " + playbackModeToString(mode));
}

void ofxVlc4Player::setPlaybackMode(const std::string & mode) {
	setPlaybackMode(playbackModeFromString(mode));
}

std::string ofxVlc4Player::getPathAtIndex(int index) const {
	if (index < 0 || index >= static_cast<int>(playlist.size())) {
		return "";
	}

	return playlist[index];
}

std::string ofxVlc4Player::getFileNameAtIndex(int index) const {
	const std::string currentPath = getPathAtIndex(index);
	if (currentPath.empty()) {
		return "";
	}

	if (isUri(currentPath)) {
		const std::string uriFileName = fileNameFromUri(currentPath);
		return uriFileName.empty() ? currentPath : uriFileName;
	}

	return ofFilePath::getFileName(currentPath);
}

std::string ofxVlc4Player::getCurrentPath() const {
	return getPathAtIndex(currentIndex);
}

std::string ofxVlc4Player::getCurrentFileName() const {
	return getFileNameAtIndex(currentIndex);
}

std::vector<std::pair<std::string, std::string>> ofxVlc4Player::buildMetadataForMedia(libvlc_media_t * sourceMedia) const {
	std::vector<std::pair<std::string, std::string>> metadata;
	if (!sourceMedia) {
		return metadata;
	}

	const std::pair<const char *, libvlc_meta_t> knownMetadata[] = {
		{ "Title", libvlc_meta_Title },
		{ "Artist", libvlc_meta_Artist },
		{ "Album", libvlc_meta_Album },
		{ "Artwork URL", libvlc_meta_ArtworkURL },
		{ "Album Artist", libvlc_meta_AlbumArtist },
		{ "Genre", libvlc_meta_Genre },
		{ "Date", libvlc_meta_Date },
		{ "Track", libvlc_meta_TrackNumber },
		{ "Track Total", libvlc_meta_TrackTotal },
		{ "Disc", libvlc_meta_DiscNumber },
		{ "Disc Total", libvlc_meta_DiscTotal },
		{ "Show", libvlc_meta_ShowName },
		{ "Season", libvlc_meta_Season },
		{ "Episode", libvlc_meta_Episode },
		{ "Director", libvlc_meta_Director },
		{ "Actors", libvlc_meta_Actors },
		{ "Publisher", libvlc_meta_Publisher },
		{ "Language", libvlc_meta_Language },
		{ "Now Playing", libvlc_meta_NowPlaying },
		{ "Description", libvlc_meta_Description },
		{ "URL", libvlc_meta_URL }
	};

	for (const auto & [label, metaType] : knownMetadata) {
		const std::string value = readMediaMeta(sourceMedia, metaType);
		if (!value.empty()) {
			metadata.emplace_back(label, value);
		}
	}

	const libvlc_time_t duration = libvlc_media_get_duration(sourceMedia);
	if (duration > 0) {
		appendMetadataValue(metadata, "Duration", ofToString(static_cast<int64_t>(duration / 1000)));
	}

	appendTrackMetadataFromMediaTracklist(metadata, sourceMedia, libvlc_track_video, "Video");
	appendTrackMetadataFromMediaTracklist(metadata, sourceMedia, libvlc_track_audio, "Audio");
	appendTrackMetadataFromMediaTracklist(metadata, sourceMedia, libvlc_track_text, "Subtitle");

	return metadata;
}

std::vector<std::pair<std::string, std::string>> ofxVlc4Player::getMetadataAtIndex(int index) const {
	const std::string path = getPathAtIndex(index);
	if (path.empty()) {
		return {};
	}

	std::vector<std::pair<std::string, std::string>> currentMediaMetadata;
	if (index == currentIndex && media) {
		currentMediaMetadata = buildMetadataForMedia(media);
		if (!currentMediaMetadata.empty()) {
			std::lock_guard<std::mutex> lock(metadataCacheMutex);
			const auto cached = metadataCache.find(path);
			if (cached != metadataCache.end() &&
				(cached->second.size() > currentMediaMetadata.size() ||
					(hasDetailedTrackMetadata(cached->second) && !hasDetailedTrackMetadata(currentMediaMetadata)))) {
				return cached->second;
			}
			metadataCache[path] = currentMediaMetadata;
			return currentMediaMetadata;
		}
	}

	std::vector<std::pair<std::string, std::string>> cachedMetadata;
	{
		std::lock_guard<std::mutex> lock(metadataCacheMutex);
		const auto cached = metadataCache.find(path);
		if (cached != metadataCache.end()) {
			cachedMetadata = cached->second;
			if (hasDetailedTrackMetadata(cachedMetadata)) {
				return cachedMetadata;
			}
		}
	}

	if (!libvlc) {
		return cachedMetadata;
	}

	libvlc_media_t * inspectMedia = isUri(path)
		? libvlc_media_new_location(path.c_str())
		: libvlc_media_new_path(path.c_str());
	if (!inspectMedia) {
		return cachedMetadata.empty() ? currentMediaMetadata : cachedMetadata;
	}

	libvlc_media_parse_flag_t parseFlags = libvlc_media_parse_forced;
	if (isUri(path)) {
		parseFlags = static_cast<libvlc_media_parse_flag_t>(
			parseFlags | libvlc_media_parse_network | libvlc_media_fetch_network);
	} else {
		parseFlags = static_cast<libvlc_media_parse_flag_t>(
			parseFlags | libvlc_media_parse_local | libvlc_media_fetch_local);
	}
	if (libvlc_media_parse_request(libvlc, inspectMedia, parseFlags, 1500) == 0) {
		waitForMediaParse(inspectMedia, 1500);
	}

	std::vector<std::pair<std::string, std::string>> metadata = buildMetadataForMedia(inspectMedia);
	libvlc_media_release(inspectMedia);

	if (metadata.empty()) {
		if (!cachedMetadata.empty()) {
			return cachedMetadata;
		}
		return currentMediaMetadata;
	}

	{
		std::lock_guard<std::mutex> lock(metadataCacheMutex);
		const auto cached = metadataCache.find(path);
		if (cached == metadataCache.end() ||
			metadata.size() >= cached->second.size() ||
			hasDetailedTrackMetadata(metadata)) {
			metadataCache[path] = metadata;
		}
	}

	return metadata;
}

std::vector<std::pair<std::string, std::string>> ofxVlc4Player::getCurrentMetadata() const {
	return getMetadataAtIndex(currentIndex);
}

bool ofxVlc4Player::isInitialized() const {
	return libvlc != nullptr && mediaPlayer != nullptr;
}

bool ofxVlc4Player::hasPlaylist() const {
	return !playlist.empty();
}

void ofxVlc4Player::clearLastMessages() {
	lastStatusMessage.clear();
	lastErrorMessage.clear();
}

std::string ofxVlc4Player::getPlaybackModeString() const {
	return playbackModeToString(playbackMode);
}

void ofxVlc4Player::setShuffleEnabled(bool enabled) {
	shuffleEnabled = enabled;
	setStatus(std::string("Shuffle ") + (enabled ? "enabled." : "disabled."));
	logNotice(std::string("Shuffle ") + (enabled ? "enabled." : "disabled."));
}

bool ofxVlc4Player::isShuffleEnabled() const {
	return shuffleEnabled;
}

void ofxVlc4Player::setAudioCaptureEnabled(bool enabled) {
	if (audioCaptureEnabled == enabled) {
		return;
	}

	if (libvlc || mediaPlayer) {
		setError("setAudioCaptureEnabled() must be called before init(); reinitialize the player to change audio capture.");
		return;
	}

	audioCaptureEnabled = enabled;
	if (!enabled) {
		isAudioReady.store(false);
		resetAudioBuffer();
	}
}

bool ofxVlc4Player::isAudioCaptureEnabled() const {
	return audioCaptureEnabled;
}

bool ofxVlc4Player::isEqualizerEnabled() const {
	return equalizerEnabled;
}

void ofxVlc4Player::setEqualizerEnabled(bool enabled) {
	equalizerEnabled = enabled;
	applyEqualizerSettings();
	setStatus(std::string("Equalizer ") + (enabled ? "enabled." : "disabled."));
	logNotice(std::string("Equalizer ") + (enabled ? "enabled." : "disabled."));
}

float ofxVlc4Player::getEqualizerPreamp() const {
	return equalizerPreamp;
}

void ofxVlc4Player::setEqualizerPreamp(float preamp) {
	equalizerEnabled = true;
	equalizerPreamp = ofClamp(preamp, -20.0f, 20.0f);
	equalizerPresetIndex = -1;
	applyEqualizerSettings();
}

int ofxVlc4Player::getEqualizerBandCount() const {
	return static_cast<int>(equalizerBandAmps.size());
}

float ofxVlc4Player::getEqualizerBandFrequency(int index) const {
	if (index < 0 || index >= static_cast<int>(equalizerBandAmps.size())) {
		return -1.0f;
	}

	return libvlc_audio_equalizer_get_band_frequency(static_cast<unsigned>(index));
}

float ofxVlc4Player::getEqualizerBandAmp(int index) const {
	if (index < 0 || index >= static_cast<int>(equalizerBandAmps.size())) {
		return 0.0f;
	}

	return equalizerBandAmps[index];
}

void ofxVlc4Player::setEqualizerBandAmp(int index, float amp) {
	if (index < 0 || index >= static_cast<int>(equalizerBandAmps.size())) {
		return;
	}

	equalizerEnabled = true;
	equalizerBandAmps[index] = ofClamp(amp, -20.0f, 20.0f);
	equalizerPresetIndex = -1;
	applyEqualizerSettings();
}

int ofxVlc4Player::getEqualizerPresetCount() const {
	return static_cast<int>(libvlc_audio_equalizer_get_preset_count());
}

std::string ofxVlc4Player::getEqualizerPresetName(int index) const {
	if (index < 0 || index >= getEqualizerPresetCount()) {
		return "";
	}

	const char * name = libvlc_audio_equalizer_get_preset_name(static_cast<unsigned>(index));
	return name ? name : "";
}

int ofxVlc4Player::getEqualizerPresetIndex() const {
	return equalizerPresetIndex;
}

void ofxVlc4Player::applyEqualizerPreset(int index) {
	if (index < 0 || index >= getEqualizerPresetCount()) {
		return;
	}

	libvlc_equalizer_t * equalizer = libvlc_audio_equalizer_new_from_preset(static_cast<unsigned>(index));
	if (!equalizer) {
		logWarning("Equalizer preset could not be loaded.");
		return;
	}

	equalizerEnabled = true;
	equalizerPresetIndex = index;
	equalizerPreamp = libvlc_audio_equalizer_get_preamp(equalizer);
	for (unsigned i = 0; i < equalizerBandAmps.size(); ++i) {
		equalizerBandAmps[i] = libvlc_audio_equalizer_get_amp_at_index(equalizer, i);
	}

	if (mediaPlayer && libvlc_media_player_set_equalizer(mediaPlayer, equalizer) != 0) {
		logWarning("Equalizer preset could not be applied.");
	}

	libvlc_audio_equalizer_release(equalizer);
	setStatus("Equalizer preset applied.");
	const std::string presetName = getEqualizerPresetName(index);
	if (!presetName.empty()) {
		logNotice("Equalizer preset applied: " + presetName + ".");
	}
}

void ofxVlc4Player::resetEqualizer() {
	equalizerEnabled = true;
	equalizerPresetIndex = -1;
	equalizerPreamp = 0.0f;
	std::fill(equalizerBandAmps.begin(), equalizerBandAmps.end(), 0.0f);
	applyEqualizerSettings();
	setStatus("Equalizer reset.");
	logNotice("Equalizer reset.");
}

bool ofxVlc4Player::isVideoAdjustmentsEnabled() const {
	return videoAdjustmentsEnabled;
}

void ofxVlc4Player::setVideoAdjustmentsEnabled(bool enabled) {
	if (videoAdjustmentsEnabled == enabled) {
		return;
	}

	videoAdjustmentsEnabled = enabled;
	applyVideoAdjustments();
	setStatus(std::string("Video adjustments ") + (enabled ? "enabled." : "disabled."));
	logNotice(std::string("Video adjustments ") + (enabled ? "enabled." : "disabled."));
}

float ofxVlc4Player::getVideoContrast() const {
	return videoAdjustContrast;
}

void ofxVlc4Player::setVideoContrast(float contrast) {
	const float clampedContrast = ofClamp(contrast, 0.0f, 2.0f);
	const bool wasEnabled = videoAdjustmentsEnabled;
	if (wasEnabled && nearlyEqual(videoAdjustContrast, clampedContrast)) {
		return;
	}

	videoAdjustmentsEnabled = true;
	videoAdjustContrast = clampedContrast;
	applyVideoAdjustments();
	setStatus("Video contrast set.");
	logVerbose("Video contrast: " + formatAdjustmentValue(videoAdjustContrast) + ".");
}

float ofxVlc4Player::getVideoBrightness() const {
	return videoAdjustBrightness;
}

void ofxVlc4Player::setVideoBrightness(float brightness) {
	const float clampedBrightness = ofClamp(brightness, 0.0f, 2.0f);
	const bool wasEnabled = videoAdjustmentsEnabled;
	if (wasEnabled && nearlyEqual(videoAdjustBrightness, clampedBrightness)) {
		return;
	}

	videoAdjustmentsEnabled = true;
	videoAdjustBrightness = clampedBrightness;
	applyVideoAdjustments();
	setStatus("Video brightness set.");
	logVerbose("Video brightness: " + formatAdjustmentValue(videoAdjustBrightness) + ".");
}

float ofxVlc4Player::getVideoHue() const {
	return videoAdjustHue;
}

void ofxVlc4Player::setVideoHue(float hue) {
	float clampedHue = ofClamp(hue, -180.0f, 180.0f);
	if (clampedHue < 0.0f) {
		clampedHue += 360.0f;
	}
	const bool wasEnabled = videoAdjustmentsEnabled;
	if (wasEnabled && nearlyEqual(videoAdjustHue, clampedHue)) {
		return;
	}

	videoAdjustmentsEnabled = true;
	videoAdjustHue = clampedHue;
	applyVideoAdjustments();
	setStatus("Video hue set.");
	float displayHue = videoAdjustHue;
	if (displayHue > 180.0f) {
		displayHue -= 360.0f;
	}
	logVerbose("Video hue: " + formatAdjustmentValue(displayHue, 0, " deg") + ".");
}

float ofxVlc4Player::getVideoSaturation() const {
	return videoAdjustSaturation;
}

void ofxVlc4Player::setVideoSaturation(float saturation) {
	const float clampedSaturation = ofClamp(saturation, 0.0f, 3.0f);
	const bool wasEnabled = videoAdjustmentsEnabled;
	if (wasEnabled && nearlyEqual(videoAdjustSaturation, clampedSaturation)) {
		return;
	}

	videoAdjustmentsEnabled = true;
	videoAdjustSaturation = clampedSaturation;
	applyVideoAdjustments();
	setStatus("Video saturation set.");
	logVerbose("Video saturation: " + formatAdjustmentValue(videoAdjustSaturation) + ".");
}

float ofxVlc4Player::getVideoGamma() const {
	return videoAdjustGamma;
}

void ofxVlc4Player::setVideoGamma(float gamma) {
	const float clampedGamma = ofClamp(gamma, 0.5f, 2.5f);
	const bool wasEnabled = videoAdjustmentsEnabled;
	if (wasEnabled && nearlyEqual(videoAdjustGamma, clampedGamma)) {
		return;
	}

	videoAdjustmentsEnabled = true;
	videoAdjustGamma = clampedGamma;
	applyVideoAdjustments();
	setStatus("Video gamma set.");
	logVerbose("Video gamma: " + formatAdjustmentValue(videoAdjustGamma) + ".");
}

void ofxVlc4Player::resetVideoAdjustments() {
	videoAdjustmentsEnabled = true;
	videoAdjustContrast = 1.0f;
	videoAdjustBrightness = 1.0f;
	videoAdjustHue = 0.0f;
	videoAdjustSaturation = 1.0f;
	videoAdjustGamma = 1.0f;
	applyVideoAdjustments();
	setStatus("Video adjustments reset.");
	logNotice("Video adjustments reset.");
}

ofxVlc4Player::VideoProjectionMode ofxVlc4Player::getVideoProjectionMode() const {
	return videoProjectionMode;
}

void ofxVlc4Player::setVideoProjectionMode(VideoProjectionMode mode) {
	if (videoProjectionMode == mode) {
		return;
	}

	videoProjectionMode = mode;
	applyVideoProjectionMode();
	std::string modeLabel = "Auto";
	switch (mode) {
	case VideoProjectionMode::Rectangular:
		modeLabel = "Rectangular";
		break;
	case VideoProjectionMode::Equirectangular:
		modeLabel = "360 Equirectangular";
		break;
	case VideoProjectionMode::CubemapStandard:
		modeLabel = "Cubemap";
		break;
	case VideoProjectionMode::Auto:
	default:
		break;
	}
	setStatus("3D projection set.");
	logNotice("3D projection set: " + modeLabel + ".");
}

ofxVlc4Player::VideoStereoMode ofxVlc4Player::getVideoStereoMode() const {
	return videoStereoMode;
}

void ofxVlc4Player::setVideoStereoMode(VideoStereoMode mode) {
	if (videoStereoMode == mode) {
		return;
	}

	videoStereoMode = mode;
	applyVideoStereoMode();
	std::string modeLabel = "Auto";
	switch (mode) {
	case VideoStereoMode::Stereo:
		modeLabel = "Stereo";
		break;
	case VideoStereoMode::LeftEye:
		modeLabel = "Left eye";
		break;
	case VideoStereoMode::RightEye:
		modeLabel = "Right eye";
		break;
	case VideoStereoMode::SideBySide:
		modeLabel = "Side by side";
		break;
	case VideoStereoMode::Auto:
	default:
		break;
	}
	setStatus("3D stereo mode set.");
	logNotice("3D stereo mode set: " + modeLabel + ".");
}

float ofxVlc4Player::getVideoYaw() const {
	return videoViewYaw;
}

float ofxVlc4Player::getVideoPitch() const {
	return videoViewPitch;
}

float ofxVlc4Player::getVideoRoll() const {
	return videoViewRoll;
}

float ofxVlc4Player::getVideoFov() const {
	return videoViewFov;
}

void ofxVlc4Player::setVideoViewpoint(float yaw, float pitch, float roll, float fov, bool absolute) {
	videoViewYaw = ofClamp(yaw, -180.0f, 180.0f);
	videoViewPitch = ofClamp(pitch, -90.0f, 90.0f);
	videoViewRoll = ofClamp(roll, -180.0f, 180.0f);
	videoViewFov = ofClamp(fov, 1.0f, 179.0f);
	applyVideoViewpoint(absolute);
}

void ofxVlc4Player::resetVideoViewpoint() {
	videoViewYaw = 0.0f;
	videoViewPitch = 0.0f;
	videoViewRoll = 0.0f;
	videoViewFov = 80.0f;
	applyVideoViewpoint();
	setStatus("3D view reset.");
	logNotice("3D view reset.");
}

void ofxVlc4Player::setPosition(float pct) {
	if (!mediaPlayer) return;
	if (!libvlc_media_player_is_seekable(mediaPlayer)) return;

	resetAudioBuffer();
	libvlc_media_player_set_position(mediaPlayer, pct, true);
}

float ofxVlc4Player::getHeight() const {
	return static_cast<float>(videoHeight.load());
}

float ofxVlc4Player::getWidth() const {
	const float rawWidth = static_cast<float>(videoWidth.load());
	const float rawHeight = static_cast<float>(videoHeight.load());
	if (rawWidth <= 0.0f || rawHeight <= 0.0f) {
		return rawWidth;
	}

	const float aspect = std::max(displayAspectRatio.load(), 0.0001f);
	return rawHeight * aspect;
}

bool ofxVlc4Player::isPlaying() {
	return mediaPlayer ? libvlc_media_player_is_playing(mediaPlayer) : false;
}

bool ofxVlc4Player::isStopped() {
	return !mediaPlayer || isStoppedOrIdleState(libvlc_media_player_get_state(mediaPlayer));
}

bool ofxVlc4Player::isPlaybackTransitioning() const {
	return mediaPlayer && isTransientPlaybackState(libvlc_media_player_get_state(mediaPlayer));
}

bool ofxVlc4Player::isPlaybackRestartPending() const {
	if (!playbackWanted.load()) {
		return false;
	}

	// Repeat/queued activation briefly moves through stopped/transient VLC states even though
	// the app should keep showing the previous preview until the replacement frame arrives.
	if (pendingManualStopEvents.load() > 0 ||
		pendingActivateIndex.load() >= 0 ||
		pendingActivateReady.load() ||
		playNextRequested.load()) {
		return true;
	}

	if (!mediaPlayer) {
		return false;
	}

	const libvlc_state_t state = libvlc_media_player_get_state(mediaPlayer);
	return isStoppedOrIdleState(state) || isTransientPlaybackState(state);
}

bool ofxVlc4Player::isSeekable() {
	return mediaPlayer ? libvlc_media_player_is_seekable(mediaPlayer) : false;
}

float ofxVlc4Player::getPosition() {
	return mediaPlayer ? libvlc_media_player_get_position(mediaPlayer) : 0.f;
}

int ofxVlc4Player::getTime() {
	return mediaPlayer ? static_cast<int>(libvlc_media_player_get_time(mediaPlayer)) : 0;
}

void ofxVlc4Player::setTime(int ms) {
	if (mediaPlayer) {
		resetAudioBuffer();
		libvlc_media_player_set_time(mediaPlayer, ms, true);
	}
}

float ofxVlc4Player::getLength() {
	return mediaPlayer ? static_cast<float>(libvlc_media_player_get_length(mediaPlayer)) : 0.f;
}

void ofxVlc4Player::setVolume(int volume) {
	currentVolume.store(ofClamp(volume, 0, 100));
}

void ofxVlc4Player::toggleMute() {
	if (currentVolume.load() > 0) {
		currentVolume.store(0);
		logNotice("Mute enabled.");
	} else {
		currentVolume.store(100);
		logNotice("Mute disabled.");
	}
}

bool ofxVlc4Player::videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (!that || !cfg || !render_cfg || that->shuttingDown.load()) {
		return false;
	}

	render_cfg->opengl_format = GL_RGBA;
	render_cfg->full_range = true;
	render_cfg->colorspace = libvlc_video_colorspace_BT709;
	render_cfg->primaries = libvlc_video_primaries_BT709;
	render_cfg->transfer = libvlc_video_transfer_func_SRGB;
	render_cfg->orientation = libvlc_video_orient_top_left;

	if (cfg->width != that->renderWidth.load() || cfg->height != that->renderHeight.load()) {
		that->pendingRenderWidth.store(cfg->width);
		that->pendingRenderHeight.store(cfg->height);
		that->pendingResize.store(true);
	}

	return true;
}

void ofxVlc4Player::videoSwap(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (!that || that->shuttingDown.load()) {
		return;
	}

	// Publish the VLC callback thread's GL work before the main OF thread samples the shared texture.
	glFlush();
}

bool ofxVlc4Player::make_current(void * data, bool current) {
	auto * that = static_cast<ofxVlc4Player *>(data);
	if (!that || !that->vlcWindow || that->shuttingDown.load()) {
		return false;
	}

	std::lock_guard<std::mutex> lock(that->videoMutex);

	if (current) {
		that->vlcWindow->makeCurrent();
		that->applyPendingVideoResize();
		that->bindVlcRenderTarget();
	} else {
		that->unbindVlcRenderTarget();
		glfwMakeContextCurrent(nullptr);
	}

	return true;
}

void * ofxVlc4Player::get_proc_address(void * data, const char * name) {
	(void)data;
	return name ? (void *)glfwGetProcAddress(name) : nullptr;
}

void ofxVlc4Player::bindVlcRenderTarget() {
	if (!fbo.isAllocated()) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fbo.getId());
	const unsigned currentRenderWidth = renderWidth.load();
	const unsigned currentRenderHeight = renderHeight.load();
	if (currentRenderWidth > 0 && currentRenderHeight > 0) {
		ofViewport(0, 0, static_cast<float>(currentRenderWidth), static_cast<float>(currentRenderHeight), false);
	}
	vlcFboBound = true;
}

void ofxVlc4Player::unbindVlcRenderTarget() {
	if (!vlcFboBound) {
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	vlcFboBound = false;
}

bool ofxVlc4Player::drawCurrentFrame(float x, float y, float width, float height) {
	const unsigned currentVideoWidth = videoWidth.load();
	const unsigned currentVideoHeight = videoHeight.load();
	const unsigned currentRenderWidth = renderWidth.load();
	const unsigned currentRenderHeight = renderHeight.load();
	const float sourceWidth = static_cast<float>(std::min(currentVideoWidth, currentRenderWidth));
	const float sourceHeight = static_cast<float>(std::min(currentVideoHeight, currentRenderHeight));
	if (!fbo.isAllocated() || sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
		return false;
	}

	fbo.getTexture().drawSubsection(x, y, width, height, 0, 0, sourceWidth, sourceHeight);
	return true;
}

void ofxVlc4Player::draw(float x, float y, float w, float h) {
	std::lock_guard<std::mutex> lock(videoMutex);
	drawCurrentFrame(x, y, w, h);
}

void ofxVlc4Player::draw(float x, float y) {
	std::lock_guard<std::mutex> lock(videoMutex);
	const float displayWidth = getWidth();
	const float displayHeight = getHeight();
	drawCurrentFrame(x, y, displayWidth, displayHeight);
}

void ofxVlc4Player::updateRecorder() {
	recorder.updateVideoFrame();
	recorder.flushAudioRecording();
	if (!recorder.getLastError().empty()) {
		setError(recorder.getLastError());
		recorder.clearLastError();
	}
}

void ofxVlc4Player::refreshExposedTexture() {
	std::lock_guard<std::mutex> lock(videoMutex);
	const unsigned currentVideoWidth = videoWidth.load();
	const unsigned currentVideoHeight = videoHeight.load();
	const unsigned currentRenderWidth = renderWidth.load();
	const unsigned currentRenderHeight = renderHeight.load();
	const unsigned sourceWidth = std::min(currentVideoWidth, currentRenderWidth);
	const unsigned sourceHeight = std::min(currentVideoHeight, currentRenderHeight);
	if (!fbo.isAllocated() || sourceWidth == 0 || sourceHeight == 0) {
		return;
	}

	ensureExposedTextureFboCapacity(sourceWidth, sourceHeight);
	exposedTextureFbo.begin();
	ofClear(0, 0, 0, 255);
	ofPushStyle();
	ofEnableBlendMode(OF_BLENDMODE_DISABLED);
	ofSetColor(255, 255, 255, 255);
	fbo.getTexture().drawSubsection(
		0.0f,
		0.0f,
		static_cast<float>(sourceWidth),
		static_cast<float>(sourceHeight),
		0.0f,
		0.0f,
		static_cast<float>(sourceWidth),
		static_cast<float>(sourceHeight));
	ofPopStyle();
	exposedTextureFbo.end();
}

void ofxVlc4Player::readAudioIntoBuffer(ofSoundBuffer & buffer, float gain) {
	std::lock_guard<std::mutex> lock(audioMutex);
	ringBuffer.readIntoBuffer(buffer, gain);
}

ofTexture & ofxVlc4Player::getTexture() {
	return exposedTextureFbo.getTexture();
}

void ofxVlc4Player::audioPlay(void * data, const void * samples, unsigned int count, int64_t pts) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	(void)pts;

	if (!that || !that->audioCaptureEnabled || !samples || count == 0) return;

	that->isAudioReady.store(true);

	size_t sampleCount = (size_t)count * (size_t)std::max(that->channels.load(), 1);
	{
		std::lock_guard<std::mutex> lock(that->audioMutex);
		that->ringBuffer.write(static_cast<const float *>(samples), sampleCount);
	}
	that->recorder.captureAudioSamples(static_cast<const float *>(samples), sampleCount);
}

void ofxVlc4Player::audioPause(void * data, int64_t pts) {
	(void)pts;
	auto * that = static_cast<ofxVlc4Player *>(data);
	if (!that) return;
	that->audioPausedSignal.store(true);
}

void ofxVlc4Player::audioResume(void * data, int64_t pts) {
	(void)pts;
	auto * that = static_cast<ofxVlc4Player *>(data);
	if (!that) return;
	that->audioPausedSignal.store(false);
}

void ofxVlc4Player::audioFlush(void * data, int64_t pts) {
	(void)pts;
	auto * that = static_cast<ofxVlc4Player *>(data);
	if (!that) return;
	that->audioPausedSignal.store(false);
	that->resetAudioBuffer();
	that->recorder.resetCapturedAudio();
}

void ofxVlc4Player::audioDrain(void * data) {
	auto * that = static_cast<ofxVlc4Player *>(data);
	if (!that) return;
	that->audioPausedSignal.store(false);
}

int ofxVlc4Player::audioSetup(void ** data, char * format, unsigned int * rate, unsigned int * channelsPtr) {
	if (!data || !format || !rate || !channelsPtr) return 1;

	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(*data);
	if (!that) return 1;
	if (!that->audioCaptureEnabled) return 1;

	std::memcpy(format, "FL32", 4);
	*rate = 44100;
	*channelsPtr = 2;
	that->sampleRate.store(static_cast<int>(*rate));
	that->channels.store(static_cast<int>(*channelsPtr));
	that->prepareAudioRingBuffer();
	{
		std::lock_guard<std::mutex> lock(that->audioMutex);
		that->ringBuffer.clear();
	}
	that->recorder.prepareAudioRecordingBuffer(static_cast<int>(*rate), static_cast<int>(*channelsPtr));
	that->isAudioReady.store(true);

	return 0;
}

void ofxVlc4Player::audioCleanup(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (!that) return;
	that->isAudioReady.store(false);
	that->channels.store(0);
	that->sampleRate.store(0);
	that->audioPausedSignal.store(false);
}

void ofxVlc4Player::vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data) {
	((ofxVlc4Player *)data)->vlcMediaPlayerEvent(event);
}

void ofxVlc4Player::vlcMediaPlayerEvent(const libvlc_event_t * event) {
	if (!event) return;

	if (event->type == libvlc_MediaPlayerPlaying) {
		audioPausedSignal.store(false);
		pauseRequested.store(false);
		return;
	}

	if (event->type == libvlc_MediaPlayerStopped) {
		audioPausedSignal.store(false);
		const int pendingManualStops = pendingManualStopEvents.fetch_sub(1);
		if (pendingManualStops > 0) {
			if (pendingActivateIndex.load() >= 0) {
				pendingActivateReady.store(true);
			}
		} else {
			pendingManualStopEvents.store(0);
			const libvlc_state_t state = mediaPlayer ? libvlc_media_player_get_state(mediaPlayer) : libvlc_Stopped;
			const bool stillStopped =
				!mediaPlayer ||
				isStoppedOrIdleState(state);
			if (stillStopped) {
				playNextRequested.store(true);
			}
		}
	}
}

void ofxVlc4Player::vlcMediaEventStatic(const libvlc_event_t * event, void * data) {
	((ofxVlc4Player *)data)->vlcMediaEvent(event);
}

void ofxVlc4Player::vlcMediaEvent(const libvlc_event_t * event) {
	if (event->type == libvlc_MediaParsedChanged && media) {
		refreshPixelAspectRatio();
		refreshDisplayAspectRatio();
		return;
	}

	if (event->type == libvlc_MediaAttachedThumbnailsFound) {
		libvlc_picture_list_t * thumbnails = event->u.media_attached_thumbnails_found.thumbnails;
		if (!thumbnails || libvlc_picture_list_count(thumbnails) == 0) {
			return;
		}

		libvlc_picture_t * picture = libvlc_picture_list_at(thumbnails, 0);
		if (!picture) {
			return;
		}

		const std::string currentPath = getCurrentPath();
		const std::string artworkStem = currentPath.empty()
			? "current_media"
			: mediaLabelForPath(currentPath);
		const std::string tempFileName =
			"ofxvlc4player_artwork_" + ofToString(std::hash<std::string>{}(artworkStem)) +
			artworkFileExtension(libvlc_picture_type(picture));
		const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / tempFileName;
		if (libvlc_picture_save(picture, tempPath.string().c_str()) == 0) {
			cacheArtworkPathForCurrentMedia(tempPath.string());
		}
	}
}

bool ofxVlc4Player::queryVideoTrackGeometry(unsigned & width, unsigned & height, unsigned & sarNum, unsigned & sarDen) const {
	width = 0;
	height = 0;
	sarNum = 1;
	sarDen = 1;

	if (!mediaPlayer) {
		return false;
	}

	auto applyVideoTrackGeometry = [&](const libvlc_media_track_t * track) {
		if (!track || track->i_type != libvlc_track_video || !track->video) {
			return false;
		}

		if (track->video->i_width == 0 || track->video->i_height == 0) {
			return false;
		}

		width = track->video->i_width;
		height = track->video->i_height;
		sarNum = track->video->i_sar_num > 0 ? track->video->i_sar_num : 1u;
		sarDen = track->video->i_sar_den > 0 ? track->video->i_sar_den : 1u;
		return true;
	};

	libvlc_media_track_t * selectedTrack =
		libvlc_media_player_get_selected_track(mediaPlayer, libvlc_track_video);
	if (selectedTrack) {
		const bool foundSelectedTrack = applyVideoTrackGeometry(selectedTrack);
		libvlc_media_track_release(selectedTrack);
		if (foundSelectedTrack) {
			return true;
		}
	}

	auto readTracklistGeometry = [&](bool selectedOnly) {
		libvlc_media_tracklist_t * tracklist =
			libvlc_media_player_get_tracklist(mediaPlayer, libvlc_track_video, selectedOnly);
		if (!tracklist) {
			return false;
		}

		const size_t trackCount = libvlc_media_tracklist_count(tracklist);
		for (size_t i = 0; i < trackCount; ++i) {
			const libvlc_media_track_t * track = libvlc_media_tracklist_at(tracklist, i);
			if (applyVideoTrackGeometry(track)) {
				libvlc_media_tracklist_delete(tracklist);
				return true;
			}
		}

		libvlc_media_tracklist_delete(tracklist);
		return false;
	};

	if (readTracklistGeometry(true) || readTracklistGeometry(false)) {
		return true;
	}

	if (media) {
		libvlc_media_tracklist_t * mediaTracklist = libvlc_media_get_tracklist(media, libvlc_track_video);
		if (mediaTracklist) {
			const size_t trackCount = libvlc_media_tracklist_count(mediaTracklist);
			for (size_t i = 0; i < trackCount; ++i) {
				const libvlc_media_track_t * track = libvlc_media_tracklist_at(mediaTracklist, i);
				if (applyVideoTrackGeometry(track)) {
					libvlc_media_tracklist_delete(mediaTracklist);
					return true;
				}
			}
			libvlc_media_tracklist_delete(mediaTracklist);
		}
	}

	return false;
}

void ofxVlc4Player::refreshPixelAspectRatio() {
	unsigned trackWidth = 0;
	unsigned trackHeight = 0;
	unsigned sarNum = 1;
	unsigned sarDen = 1;
	if (queryVideoTrackGeometry(trackWidth, trackHeight, sarNum, sarDen)) {
		pixelAspectNumerator.store(sarNum);
		pixelAspectDenominator.store(sarDen);
		return;
	}

	pixelAspectNumerator.store(1);
	pixelAspectDenominator.store(1);
}

void ofxVlc4Player::close() {
	bool expected = false;
	if (!shuttingDown.compare_exchange_strong(expected, true)) {
		return;
	}

	// Callback threads may still be unwinding, so shutdown flips the guard first and then releases resources.
	clearPendingActivationRequest();
	pauseRequested.store(false);
	audioPausedSignal.store(false);
	recorder.clearVideoRecording();
	recorder.clearAudioRecording();
	releaseVlcResources();
	isAudioReady.store(false);
	isVideoLoaded.store(false);
	vlcFboBound = false;
	setStatus("Player closed.");
}

bool ofxVlc4Player::audioIsReady() const {
	return isAudioReady.load();
}

uint64_t ofxVlc4Player::getAudioOverrunCount() const {
	return ringBuffer.getOverrunCount();
}

uint64_t ofxVlc4Player::getAudioUnderrunCount() const {
	return ringBuffer.getUnderrunCount();
}

void ofxVlc4Player::refreshDisplayAspectRatio() {
	const unsigned currentVideoWidth = videoWidth.load();
	const unsigned currentVideoHeight = videoHeight.load();
	if (currentVideoWidth == 0 || currentVideoHeight == 0) {
		displayAspectRatio.store(1.0f);
		return;
	}

	refreshPixelAspectRatio();

	float aspect = static_cast<float>(currentVideoWidth) / static_cast<float>(currentVideoHeight);
	unsigned trackWidth = 0;
	unsigned trackHeight = 0;
	unsigned ignoredSarNum = 1;
	unsigned ignoredSarDen = 1;
	if (queryVideoTrackGeometry(trackWidth, trackHeight, ignoredSarNum, ignoredSarDen)) {
		if (trackWidth > 0 && trackHeight > 0) {
			aspect = static_cast<float>(trackWidth) / static_cast<float>(trackHeight);
		}
	}

	const unsigned sarNum = pixelAspectNumerator.load();
	const unsigned sarDen = pixelAspectDenominator.load();
	if (sarNum > 0 && sarDen > 0) {
		aspect *= static_cast<float>(sarNum) / static_cast<float>(sarDen);
	}

	displayAspectRatio.store(std::max(aspect, 0.0001f));
}

