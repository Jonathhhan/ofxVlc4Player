#pragma once

#include "LockFreeRingBuffer.h"
#include "ofMain.h"
#include "vlc/vlc.h"

class ofxVlc4Player {
	libvlc_instance_t * libvlc;
	libvlc_media_t * media;
	libvlc_media_player_t * mediaPlayer;
	libvlc_event_manager_t * mediaPlayerEventManager;
	libvlc_event_manager_t * mediaEventManager;

	ofTexture texture;
	shared_ptr<ofAppBaseWindow> vlcWindow;
	unsigned videoWidth = 0;
	unsigned videoHeight = 0;
	bool updated = false;
	std::mutex texLock;
	GLuint tex[3];
	GLuint fbo[3];
	size_t idxRender = 0;
	size_t idxSwap = 1;
	size_t idxDisplay = 2;
	int channels = 0;
	int sampleRate = 0;
	int ringBufferSize = 0;
	bool isAudioReady = false;
	ofSoundBuffer buffer;

	// VLC Video callbaks
	static bool videoSetup(void ** data, const libvlc_video_setup_device_cfg_t * cfg, libvlc_video_setup_device_info_t * out);
	static void videoCleanup(void * data);
	static bool videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg);
	static void videoSwap(void * data);
	static bool make_current(void * data, bool current);
	static void * get_proc_address(void * data, const char * current);

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
	void update();
	ofTexture & getTexture();
	void draw(float x, float y, float w, float h);
	void draw(float x, float y);
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
