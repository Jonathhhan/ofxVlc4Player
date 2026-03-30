#pragma once

#include "ofxVlc4PlayerRecorder.h"
#include "ofxVlc4PlayerRingBuffer.h"
#include "ofMain.h"
#include "GLFW/glfw3.h"
#include "vlc.h"

#include <atomic>
#include <initializer_list>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class ofxVlc4Player {
public:
	// PlaybackMode mirrors the small policy surface the example exposes in its transport UI.
	enum class PlaybackMode {
		Default,
		Repeat,
		Loop
	};

private:
	libvlc_instance_t * libvlc = nullptr;
	libvlc_media_t * media = nullptr;
	libvlc_media_player_t * mediaPlayer = nullptr;
	libvlc_event_manager_t * mediaPlayerEventManager = nullptr;
	libvlc_event_manager_t * mediaEventManager = nullptr;

	std::shared_ptr<ofAppBaseWindow> vlcWindow;

	std::atomic<unsigned> videoWidth { 0 };
	std::atomic<unsigned> videoHeight { 0 };
	unsigned allocatedVideoWidth = 1;
	unsigned allocatedVideoHeight = 1;
	std::atomic<int> channels { 0 };
	std::atomic<int> sampleRate { 0 };
	std::atomic<int> ringBufferSize { 0 };
	std::atomic<bool> isAudioReady { false };

	PlaybackMode playbackMode = PlaybackMode::Default;
	bool shuffleEnabled = false;
	bool audioCaptureEnabled = true;
	std::string lastStatusMessage;
	std::string lastErrorMessage;
	std::atomic<bool> playbackWanted { false };
	std::atomic<bool> pauseRequested { false };
	std::atomic<bool> audioPausedSignal { false };

	ofFbo fbo;
	ofFbo exposedTextureFbo;

	mutable std::mutex videoMutex;
	mutable std::mutex audioMutex;
	std::atomic<bool> shuttingDown { false };
	std::atomic<int> pendingManualStopEvents { 0 };
	std::atomic<bool> playNextRequested { false };
	std::atomic<int> pendingActivateIndex { -1 };
	std::atomic<bool> pendingActivateShouldPlay { false };
	std::atomic<bool> pendingActivateReady { false };

	std::atomic<unsigned> pendingVideoWidth { 0 };
	std::atomic<unsigned> pendingVideoHeight { 0 };
	std::atomic<bool> pendingResize { false };

	std::vector<std::string> playlist;
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
	void clearCurrentMedia();
	void handlePlaybackEnded();
	int getNextShuffleIndex() const;
	void refreshExposedTexture();

public:
	ofxVlc4Player();
	virtual ~ofxVlc4Player();

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

	void updateVideoResources();
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
	std::string getCurrentPath() const;
	std::string getCurrentFileName() const;
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
	void setAudioCaptureEnabled(bool enabled);
	bool isAudioCaptureEnabled() const;

	void setPosition(float pct);
	float getHeight() const;
	float getWidth() const;
	bool isPlaying();
	bool isStopped();
	bool isSeekable();
	bool videoReadyEvent();
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

	GLuint previousFramebuffer = 0;
	bool vlcFboBound = false;
};
