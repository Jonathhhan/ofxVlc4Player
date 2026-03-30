#include "ofxVlc4Player.h"
#include "ofxVlc4PlayerRecorder.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {
constexpr double kBufferedAudioSeconds = 0.75;

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

bool isStoppedOrIdleState(libvlc_state_t state) {
	return state == libvlc_Stopped || state == libvlc_NothingSpecial;
}

bool isTransientPlaybackState(libvlc_state_t state) {
	return state == libvlc_Opening || state == libvlc_Buffering || state == libvlc_Stopping;
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
}

ofxVlc4Player::ofxVlc4Player() {
	ofGLFWWindowSettings settings;
	settings.shareContextWith = ofGetCurrentWindow();
	vlcWindow = std::make_shared<ofAppGLFWWindow>();
	vlcWindow->setup(settings);
	vlcWindow->setVerticalSync(true);

	fbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(fbo);
	exposedTextureFbo.allocate(1, 1, GL_RGBA);
	clearAllocatedFbo(exposedTextureFbo);
	allocatedVideoWidth = 1;
	allocatedVideoHeight = 1;
}

ofxVlc4Player::~ofxVlc4Player() {
	close();
}

void ofxVlc4Player::update() {
	updateVideoResources();
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
}

void ofxVlc4Player::setError(const std::string & message) {
	lastErrorMessage = message;
	lastStatusMessage.clear();
	if (!message.empty()) {
		ofLogWarning("ofxVlc4Player") << message;
	}
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
	ringBufferSize.store(static_cast<int>(ringBuffer.size()));
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

	if (!exposedTextureFbo.isAllocated() ||
		requiredWidth > static_cast<unsigned>(exposedTextureFbo.getWidth()) ||
		requiredHeight > static_cast<unsigned>(exposedTextureFbo.getHeight())) {
		exposedTextureFbo.allocate(requiredWidth, requiredHeight, GL_RGBA);
		exposedTextureFbo.getTexture().getTextureData().bFlipTexture = true;
		clearAllocatedFbo(exposedTextureFbo);
	}
}

bool ofxVlc4Player::applyPendingVideoResize() {
	if (!pendingResize.exchange(false)) {
		return false;
	}

	const unsigned newW = pendingVideoWidth.load();
	const unsigned newH = pendingVideoHeight.load();
	if (newW == 0 || newH == 0) {
		return false;
	}

	videoWidth.store(newW);
	videoHeight.store(newH);
	ensureVideoFboCapacity(newW, newH);
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

	libvlc_media_t * loadedMedia = libvlc_media_player_get_media(mediaPlayer);
	const bool hasLoadedMedia = (loadedMedia != nullptr);
	if (loadedMedia) {
		libvlc_media_release(loadedMedia);
	}

	if (hasLoadedMedia && !isStopped()) {
		setError("Stop current playback before recording.");
		return;
	}

	clearCurrentMedia();

	const std::string outputPath = ofxVlc4PlayerRecorder::buildTimestampedOutputPath(name, ".mp4");
	libvlc_media_t * recordingMedia = nullptr;
	if (!recorder.startVideoRecordingToPath(recordingMedia, outputPath, texture)) {
		setError(recorder.getLastError());
		return;
	}

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

	clearCurrentMedia();

	libvlc_media_t * recordingMedia = nullptr;
	if (!recorder.startVideoRecordingToPath(recordingMedia, videoPath, texture)) {
		recorder.clearAudioRecording();
		setError(recorder.getLastError());
		return;
	}

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
	}

	libvlc_media_player_set_media(mediaPlayer, media);
	return true;
}

void ofxVlc4Player::clearCurrentMedia() {
	if (mediaPlayer) {
		libvlc_media_player_set_media(mediaPlayer, nullptr);
	}

	if (mediaEventManager) {
		libvlc_event_detach(mediaEventManager, libvlc_MediaParsedChanged, vlcMediaEventStatic, this);
		mediaEventManager = nullptr;
	}

	if (media) {
		libvlc_media_release(media);
		media = nullptr;
	}

	{
		std::lock_guard<std::mutex> lock(videoMutex);
		videoWidth.store(0);
		videoHeight.store(0);
		pendingVideoWidth.store(0);
		pendingVideoHeight.store(0);
		pendingResize.store(false);
		displayAspectRatio.store(1.0f);
		isVideoLoaded.store(false);
		clearAllocatedFbo(fbo);
		clearAllocatedFbo(exposedTextureFbo);
	}

	audioPausedSignal.store(false);
}

void ofxVlc4Player::addToPlaylist(const std::string & path) {
	if (!libvlc) {
		setError("Initialize libvlc first.");
		return;
	}

	playlist.push_back(path);
	setStatus("Added media to playlist.");

	if (currentIndex < 0 && !playlist.empty()) {
		currentIndex = 0;
	}
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
				addToPlaylist(dir.getPath(i));
				++added;
			}
		}
		if (added == 0) {
			setError("No supported media files found in folder.");
		} else {
			setStatus("Added " + ofToString(added) + " media item(s) from folder.");
		}
		return added;
	}

	if (!isSupportedMediaFile(file, requestedExtensions.empty() ? nullptr : &requestedExtensions)) {
		setError("Unsupported media file type: " + resolvedPath);
		return 0;
	}

	addToPlaylist(resolvedPath);
	setStatus("Added media file to playlist.");
	return 1;
}

void ofxVlc4Player::clearPlaylist() {
	stop();
	playlist.clear();
	currentIndex = -1;
	clearCurrentMedia();
	setStatus("Playlist cleared.");
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
	}
}

void ofxVlc4Player::setPlaybackMode(PlaybackMode mode) {
	playbackMode = mode;
	setStatus("Playback mode set to " + playbackModeToString(mode) + ".");
}

void ofxVlc4Player::setPlaybackMode(const std::string & mode) {
	setPlaybackMode(playbackModeFromString(mode));
}

std::string ofxVlc4Player::getCurrentPath() const {
	if (currentIndex < 0 || currentIndex >= static_cast<int>(playlist.size())) {
		return "";
	}

	return playlist[currentIndex];
}

std::string ofxVlc4Player::getCurrentFileName() const {
	const std::string currentPath = getCurrentPath();
	if (currentPath.empty()) {
		return "";
	}

	if (isUri(currentPath)) {
		const std::string uriFileName = fileNameFromUri(currentPath);
		return uriFileName.empty() ? currentPath : uriFileName;
	}

	return ofFilePath::getFileName(currentPath);
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
	return !mediaPlayer || libvlc_media_player_get_state(mediaPlayer) == libvlc_Stopped;
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
	} else {
		currentVolume.store(100);
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

	if (cfg->width != that->videoWidth.load() || cfg->height != that->videoHeight.load()) {
		that->pendingVideoWidth.store(cfg->width);
		that->pendingVideoHeight.store(cfg->height);
		that->pendingResize.store(true);
	}

	return true;
}

void ofxVlc4Player::updateVideoResources() {
	if (!pendingResize.load()) {
		return;
	}

	std::lock_guard<std::mutex> lock(videoMutex);
	applyPendingVideoResize();
}

void ofxVlc4Player::videoSwap(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (!that || that->shuttingDown.load()) {
		return;
	}
}

bool ofxVlc4Player::make_current(void * data, bool current) {
	auto * that = static_cast<ofxVlc4Player *>(data);
	if (!that || !that->vlcWindow || that->shuttingDown.load()) {
		return false;
	}

	auto * win = dynamic_cast<ofAppGLFWWindow *>(that->vlcWindow.get());
	if (!win) {
		return false;
	}

	std::lock_guard<std::mutex> lock(that->videoMutex);

	if (current) {
		glfwMakeContextCurrent(win->getGLFWWindow());
		that->applyPendingVideoResize();

		GLint prev = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
		that->previousFramebuffer = static_cast<GLuint>(prev);

		if (that->fbo.isAllocated()) {
			glBindFramebuffer(GL_FRAMEBUFFER, that->fbo.getId());
			const unsigned currentVideoWidth = that->videoWidth.load();
			const unsigned currentVideoHeight = that->videoHeight.load();
			if (currentVideoWidth > 0 && currentVideoHeight > 0) {
				glViewport(0, 0, currentVideoWidth, currentVideoHeight);
			}
			that->vlcFboBound = true;
		}
	} else {
		if (that->vlcFboBound) {
			glBindFramebuffer(GL_FRAMEBUFFER, that->previousFramebuffer);
			that->vlcFboBound = false;
		}
		glfwMakeContextCurrent(nullptr);
	}

	return true;
}

void * ofxVlc4Player::get_proc_address(void * data, const char * name) {
	(void)data;
	return name ? (void *)glfwGetProcAddress(name) : nullptr;
}

void ofxVlc4Player::draw(float x, float y, float w, float h) {
	std::lock_guard<std::mutex> lock(videoMutex);
	const libvlc_state_t state = mediaPlayer ? libvlc_media_player_get_state(mediaPlayer) : libvlc_Stopped;
	if (isStoppedOrIdleState(state)) {
		return;
	}

	const unsigned currentVideoWidth = videoWidth.load();
	const unsigned currentVideoHeight = videoHeight.load();
	if (fbo.isAllocated() && currentVideoWidth > 0 && currentVideoHeight > 0) {
		fbo.getTexture().drawSubsection(x, y, w, h, 0, 0, currentVideoWidth, currentVideoHeight);
	}
}

void ofxVlc4Player::draw(float x, float y) {
	std::lock_guard<std::mutex> lock(videoMutex);
	const libvlc_state_t state = mediaPlayer ? libvlc_media_player_get_state(mediaPlayer) : libvlc_Stopped;
	if (isStoppedOrIdleState(state)) {
		return;
	}

	const unsigned currentVideoWidth = videoWidth.load();
	const unsigned currentVideoHeight = videoHeight.load();
	if (fbo.isAllocated() && currentVideoWidth > 0 && currentVideoHeight > 0) {
		fbo.getTexture().drawSubsection(x, y, currentVideoWidth, currentVideoHeight, 0, 0, currentVideoWidth, currentVideoHeight);
	}
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
	if (!fbo.isAllocated() || currentVideoWidth == 0 || currentVideoHeight == 0) {
		return;
	}

	ensureExposedTextureFboCapacity(currentVideoWidth, currentVideoHeight);
	exposedTextureFbo.begin();
	ofClear(0, 0, 0, 0);
	fbo.getTexture().drawSubsection(
		0.0f,
		0.0f,
		static_cast<float>(currentVideoWidth),
		static_cast<float>(currentVideoHeight),
		0.0f,
		0.0f,
		static_cast<float>(currentVideoWidth),
		static_cast<float>(currentVideoHeight));
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
		libvlc_media_tracklist_t * tracklist = libvlc_media_get_tracklist(media, libvlc_track_video);
		if (tracklist) {
			size_t count = libvlc_media_tracklist_count(tracklist);
			for (size_t i = 0; i < count; ++i) {
				const libvlc_media_track_t * track = libvlc_media_tracklist_at(tracklist, i);
				if (track && track->i_type == libvlc_track_video && track->video) {
				}
			}
			libvlc_media_tracklist_delete(tracklist);
		}
	}
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
	previousFramebuffer = 0;
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
	if (!mediaPlayer) {
		displayAspectRatio.store(1.0f);
		return;
	}

	const unsigned currentVideoWidth = videoWidth.load();
	const unsigned currentVideoHeight = videoHeight.load();
	if (currentVideoWidth == 0 || currentVideoHeight == 0) {
		displayAspectRatio.store(1.0f);
		return;
	}

	float aspect = static_cast<float>(currentVideoWidth) / static_cast<float>(currentVideoHeight);
	char * aspectText = libvlc_video_get_aspect_ratio(mediaPlayer);
	if (aspectText) {
		std::string value(aspectText);
		libvlc_free(aspectText);

		const size_t separator = value.find(':');
		if (separator != std::string::npos) {
			const float left = static_cast<float>(std::atof(value.substr(0, separator).c_str()));
			const float right = static_cast<float>(std::atof(value.substr(separator + 1).c_str()));
			if (left > 0.0f && right > 0.0f) {
				aspect = left / right;
			}
		} else {
			const float parsed = static_cast<float>(std::atof(value.c_str()));
			if (parsed > 0.0f) {
				aspect = parsed;
			}
		}
	}

	displayAspectRatio.store(std::max(aspect, 0.0001f));
}

bool ofxVlc4Player::videoReadyEvent() {
	return isVideoLoaded.exchange(false);
}
