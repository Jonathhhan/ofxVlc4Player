#pragma once

#include "ofxVlc4PlayerRecorder.h"
#include "ofxVlc4PlayerRingBuffer.h"
#include "ofMain.h"
#include "GLFW/glfw3.h"
#include "vlc/vlc.h"

#include <atomic>
#include <chrono>
#include <initializer_list>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

class ofxVlc4Player {
public:
	// PlaybackMode mirrors the small policy surface the example exposes in its transport UI.
	enum class PlaybackMode {
		Default,
		Repeat,
		Loop
	};

	enum class VideoProjectionMode : int {
		Auto = -1,
		Rectangular = 0,
		Equirectangular = 1,
		CubemapStandard = 2
	};

	enum class VideoStereoMode {
		Auto = 0,
		Stereo,
		LeftEye,
		RightEye,
		SideBySide
	};

private:
	libvlc_instance_t * libvlc = nullptr;
	libvlc_media_t * media = nullptr;
	libvlc_media_player_t * mediaPlayer = nullptr;
	libvlc_event_manager_t * mediaPlayerEventManager = nullptr;
	libvlc_event_manager_t * mediaEventManager = nullptr;

	std::shared_ptr<ofAppGLFWWindow> mainWindow;
	std::shared_ptr<ofAppGLFWWindow> vlcWindow;

	std::atomic<unsigned> renderWidth { 0 };
	std::atomic<unsigned> renderHeight { 0 };
	std::atomic<unsigned> videoWidth { 0 };
	std::atomic<unsigned> videoHeight { 0 };
	std::atomic<unsigned> pixelAspectNumerator { 1 };
	std::atomic<unsigned> pixelAspectDenominator { 1 };
	std::atomic<float> displayAspectRatio { 1.0f };
	unsigned allocatedVideoWidth = 1;
	unsigned allocatedVideoHeight = 1;
	std::atomic<int> channels { 0 };
	std::atomic<int> sampleRate { 0 };
	std::atomic<bool> isAudioReady { false };

	PlaybackMode playbackMode = PlaybackMode::Default;
	bool shuffleEnabled = false;
	bool forceAvformatDemuxEnabled = true;
	bool audioCaptureEnabled = true;
	bool equalizerEnabled = false;
	float equalizerPreamp = 0.0f;
	int equalizerPresetIndex = -1;
	bool videoAdjustmentsEnabled = false;
	float videoAdjustContrast = 1.0f;
	float videoAdjustBrightness = 1.0f;
	float videoAdjustHue = 0.0f;
	float videoAdjustSaturation = 1.0f;
	float videoAdjustGamma = 1.0f;
	VideoProjectionMode videoProjectionMode = VideoProjectionMode::Auto;
	VideoStereoMode videoStereoMode = VideoStereoMode::Auto;
	float videoViewYaw = 0.0f;
	float videoViewPitch = 0.0f;
	float videoViewRoll = 0.0f;
	float videoViewFov = 80.0f;
	std::string lastStatusMessage;
	std::string lastErrorMessage;
	std::atomic<bool> playbackWanted { false };
	std::atomic<bool> pauseRequested { false };
	std::atomic<bool> audioPausedSignal { false };

	ofFbo fbo;
	ofFbo exposedTextureFbo;

	mutable std::mutex videoMutex;
	mutable std::mutex audioMutex;
	mutable std::mutex metadataCacheMutex;
	std::atomic<bool> shuttingDown { false };
	std::atomic<int> pendingManualStopEvents { 0 };
	std::atomic<bool> playNextRequested { false };
	std::atomic<int> pendingActivateIndex { -1 };
	std::atomic<bool> pendingActivateShouldPlay { false };
	std::atomic<bool> pendingActivateReady { false };

	std::atomic<unsigned> pendingRenderWidth { 0 };
	std::atomic<unsigned> pendingRenderHeight { 0 };
	std::atomic<bool> pendingResize { false };

	std::vector<std::string> playlist;
	mutable std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> metadataCache;
	std::vector<float> equalizerBandAmps;
	mutable std::vector<float> smoothedSpectrumLevels;
	int currentIndex = -1;

	// VLC Video callbacks
	static bool videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg);
	static void videoSwap(void * data);
	static bool make_current(void * data, bool current);
	static void * get_proc_address(void * data, const char * name);

	static void audioPlay(void * data, const void * samples, unsigned int count, int64_t pts);
	static void audioPause(void * data, int64_t pts);
	static void audioResume(void * data, int64_t pts);
	static void audioFlush(void * data, int64_t pts);
	static void audioDrain(void * data);

	static int audioSetup(void ** data, char * format, unsigned int * rate, unsigned int * channels);
	static void audioCleanup(void * data);

	static void vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data);
	void vlcMediaPlayerEvent(const libvlc_event_t * event);
	static void vlcMediaEventStatic(const libvlc_event_t * event, void * data);
	void vlcMediaEvent(const libvlc_event_t * event);

	void detachEvents();
	void releaseVlcResources();
	void setError(const std::string & message);
	void setStatus(const std::string & message);
	static PlaybackMode playbackModeFromString(const std::string & mode);
	static std::string playbackModeToString(PlaybackMode mode);
	bool isSupportedMediaFile(const ofFile & file, const std::set<std::string> * extensions = nullptr) const;
	void clearPendingActivationRequest();
	void prepareAudioRingBuffer();
	void resetAudioBuffer();
	bool applyPendingVideoResize();
	void ensureVideoFboCapacity(unsigned requiredWidth, unsigned requiredHeight);
	void ensureExposedTextureFboCapacity(unsigned requiredWidth, unsigned requiredHeight);
	bool loadMediaAtIndex(int index);
	void activatePlaylistIndex(int index, bool shouldPlay);
	void activatePlaylistIndexImmediate(int index, bool shouldPlay);
	void addToPlaylistInternal(const std::string & path, bool preloadMetadata);
	void clearCurrentMedia();
	void handlePlaybackEnded();
	void applyEqualizerSettings();
	void applyVideoAdjustments();
	void applyVideoProjectionMode();
	void applyVideoStereoMode();
	void applyVideoViewpoint(bool absolute = true);
	void refreshExposedTexture();
	void refreshDisplayAspectRatio();
	void refreshPixelAspectRatio();
	void clearMetadataCache();
	void cacheArtworkPathForCurrentMedia(const std::string & artworkPath);
	void bindVlcRenderTarget();
	void unbindVlcRenderTarget();
	bool drawCurrentFrame(float x, float y, float width, float height);
	std::vector<std::pair<std::string, std::string>> buildMetadataForMedia(libvlc_media_t * sourceMedia) const;
	bool queryVideoTrackGeometry(unsigned & width, unsigned & height, unsigned & sarNum, unsigned & sarDen) const;
	int getNextShuffleIndex() const;

public:
	ofxVlc4Player();
	virtual ~ofxVlc4Player();

	static void setLogLevel(ofLogLevel level);
	static ofLogLevel getLogLevel();
	static void logVerbose(const std::string & message);
	static void logError(const std::string & message);
	static void logWarning(const std::string & message);
	static void logNotice(const std::string & message);

	// init() owns the VLC instance/player lifetime for this wrapper and can safely be called again.
	void update();
	void init(int vlc_argc, char const * vlc_argv[]);
	void recordVideo(std::string name, const ofTexture & texture);
	void recordAudio(std::string name);
	void recordAudioVideo(std::string name, const ofTexture & texture);
	void setVideoRecordingFrameRate(int fps);
	int getVideoRecordingFrameRate() const { return recorder.getVideoFrameRate(); }
	void setVideoRecordingCodec(const std::string & codec);
	const std::string & getVideoRecordingCodec() const { return recorder.getVideoCodec(); }

	void draw(float x, float y, float w, float h);
	void draw(float x, float y);
	void updateRecorder();
	void readAudioIntoBuffer(ofSoundBuffer & buffer, float gain);

	ofTexture & getTexture();

	void play();
	void pause();
	void stop();

	void addToPlaylist(const std::string & path);
	// Folder imports can be filtered either by the player's default extensions or a per-call override.
	int addPathToPlaylist(const std::string & path);
	int addPathToPlaylist(const std::string & path, std::initializer_list<std::string> extensions);
	void clearPlaylist();
	void playIndex(int index);
	void nextMediaListItem();
	void previousMediaListItem();
	void removeFromPlaylist(int index);
	void movePlaylistItem(int fromIndex, int toIndex);
	void movePlaylistItems(const std::vector<int> & fromIndices, int toIndex);

	const std::vector<std::string> & getPlaylist() const { return playlist; }
	std::string getPathAtIndex(int index) const;
	std::string getFileNameAtIndex(int index) const;
	std::vector<std::pair<std::string, std::string>> getMetadataAtIndex(int index) const;
	std::string getCurrentPath() const;
	std::string getCurrentFileName() const;
	std::vector<std::pair<std::string, std::string>> getCurrentMetadata() const;
	int getCurrentIndex() const { return currentIndex; }
	bool isInitialized() const;
	bool hasPlaylist() const;
	// Status/error messages let lightweight UIs surface addon feedback without duplicating validation logic.
	const std::string & getLastStatusMessage() const { return lastStatusMessage; }
	const std::string & getLastErrorMessage() const { return lastErrorMessage; }
	void clearLastMessages();

	// The enum overload is the canonical API; the string overload remains for compatibility.
	void setPlaybackMode(PlaybackMode mode);
	void setPlaybackMode(const std::string & mode);
	PlaybackMode getPlaybackMode() const { return playbackMode; }
	std::string getPlaybackModeString() const;

	void setShuffleEnabled(bool enabled);
	bool isShuffleEnabled() const;
	// Applies to media created after the call. Existing playback is left untouched.
	void setForceAvformatDemuxEnabled(bool enabled);
	bool isForceAvformatDemuxEnabled() const;
	void setAudioCaptureEnabled(bool enabled);
	bool isAudioCaptureEnabled() const;
	bool isEqualizerEnabled() const;
	void setEqualizerEnabled(bool enabled);
	float getEqualizerPreamp() const;
	void setEqualizerPreamp(float preamp);
	int getEqualizerBandCount() const;
	float getEqualizerBandFrequency(int index) const;
	float getEqualizerBandAmp(int index) const;
	void setEqualizerBandAmp(int index, float amp);
	std::vector<float> getEqualizerSpectrumLevels(size_t pointCount = 512) const;
	int getEqualizerPresetCount() const;
	std::string getEqualizerPresetName(int index) const;
	int getEqualizerPresetIndex() const;
	void applyEqualizerPreset(int index);
	void resetEqualizer();
	bool isVideoAdjustmentsEnabled() const;
	void setVideoAdjustmentsEnabled(bool enabled);
	float getVideoContrast() const;
	void setVideoContrast(float contrast);
	float getVideoBrightness() const;
	void setVideoBrightness(float brightness);
	float getVideoHue() const;
	void setVideoHue(float hue);
	float getVideoSaturation() const;
	void setVideoSaturation(float saturation);
	float getVideoGamma() const;
	void setVideoGamma(float gamma);
	void resetVideoAdjustments();
	VideoProjectionMode getVideoProjectionMode() const;
	void setVideoProjectionMode(VideoProjectionMode mode);
	VideoStereoMode getVideoStereoMode() const;
	void setVideoStereoMode(VideoStereoMode mode);
	float getVideoYaw() const;
	float getVideoPitch() const;
	float getVideoRoll() const;
	float getVideoFov() const;
	void setVideoViewpoint(float yaw, float pitch, float roll, float fov, bool absolute = true);
	void resetVideoViewpoint();

	void setPosition(float pct);
	float getHeight() const;
	float getWidth() const;
	bool isPlaying();
	bool isStopped();
	bool isPlaybackTransitioning() const;
	bool isPlaybackRestartPending() const;
	bool isSeekable();
	float getPosition();
	int getTime();
	void setTime(int ms);
	float getLength();
	void setVolume(int volume);
	void toggleMute();
	void close();

	bool audioIsReady() const;
	uint64_t getAudioOverrunCount() const;
	uint64_t getAudioUnderrunCount() const;

	std::atomic<bool> isVideoLoaded { false };
	std::atomic<int> currentVolume { 50 };

	ofxVlc4PlayerRingBuffer ringBuffer;
	ofxVlc4PlayerRecorder recorder;
	bool vlcFboBound = false;
};
