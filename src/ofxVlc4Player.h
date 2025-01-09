#pragma once

#include "ofMain.h"
#include "LockFreeRingBuffer.h"
#include "vlc/vlc.h"
#include "GLFW/glfw3.h"

class ofxVlc4Player {
	libvlc_instance_t * libvlc;
	libvlc_media_t * media;
	libvlc_media_player_t * mediaPlayer;
	libvlc_event_manager_t * mediaPlayerEventManager;
	libvlc_event_manager_t * mediaEventManager;

	shared_ptr<ofAppBaseWindow> vlcWindow;
	unsigned videoWidth = 0;
	unsigned videoHeight = 0;
	int channels = 0;
	int sampleRate = 0;
	int ringBufferSize = 0;
	bool isAudioReady = false;
	bool isRecording = false;
	ofSoundBuffer buffer;
	ofFbo fbo;
	ofPixels pix;
	ofTexture tex;

	// VLC Video callbaks
	static bool videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg);
	static void videoSwap(void * data);
	static bool make_current(void * data, bool current);
	static void * get_proc_address(void * data, const char * current);

	static int textureOpen(void * data, void ** datap, uint64_t * sizep);
	static long long textureRead(void * data, unsigned char * buffer, size_t size);
	static int textureSeek(void * data, uint64_t offset);
	static void textureClose(void * data);

	static int audioOpen(void * data, void ** datap, uint64_t * sizep);
	static long long audioRead(void * data, unsigned char * buffer, size_t size);
	static int audioSeek(void * data, uint64_t offset);
	static void audioClose(void * data);

	static void audioPlay(void * data, const void * samples, unsigned int count, int64_t pts);
	static void audioPause(void * data, int64_t pts);
	static void audioResume(void * data, int64_t pts);
	static void audioFlush(void * data, int64_t pts);
	static void audioDrain(void * data);

	static int audioSetup(void ** data, char * format, unsigned int * rate, unsigned int * channels);
	static void audioCleanup(void * data);

	// VLC Event callbacks
	static void vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data);
	void vlcMediaPlayerEvent(const libvlc_event_t * event);
	static void vlcMediaEventStatic(const libvlc_event_t * event, void * data);
	void vlcMediaEvent(const libvlc_event_t * event);

public:
	ofxVlc4Player();
	virtual ~ofxVlc4Player();
	void init(int vlc_argc, char const * vlc_argv[]);
	void load(std::string name);
	void recordVideo(std::string name, ofTexture texture);
	void recordAudio(std::string name);
	void draw(float x, float y, float w, float h);
	void draw(float x, float y);
	void updateRecorder();
	ofTexture & getTexture();
	void play();
	void pause();
	void stop();
	void setPosition(float pct);
	float getHeight() const;
	float getWidth() const;
	bool isPlaying();
	bool isSeekable();
	float getPosition();
	int getTime();
	void setTime(int ms);
	float getLength();
	void setVolume(int volume);
	void toggleMute();
	void close();
	bool audioIsReady() const;
	LockFreeRingBuffer ringBuffer;
};
