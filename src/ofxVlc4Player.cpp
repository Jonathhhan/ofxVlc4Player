#include "ofxVlc4Player.h"
#include <GLFW/glfw3.h>

ofxVlc4Player::ofxVlc4Player()
	: libvlc(NULL)
	, eventManager(NULL)
	, media(NULL)
	, mediaPlayer(NULL)
	, ringBuffer(static_cast<size_t>(50000))
	, tex()
	, fbo() {
	ofGLFWWindowSettings settings;
	settings.shareContextWith = ofGetCurrentWindow();
	vlcWindow = std::make_shared<ofAppGLFWWindow>();
	vlcWindow->setup(settings);
	vlcWindow->setVerticalSync(true);
	texture.allocate(1, 1, GL_RGBA);
	buffer.allocate(1, 2);
}

ofxVlc4Player::~ofxVlc4Player() { }

void ofxVlc4Player::init(int vlc_argc, char const * vlc_argv[]) {
	libvlc = libvlc_new(vlc_argc, vlc_argv);
	if (!libvlc) {
		const char * error = libvlc_errmsg();
		cout << error << endl;
		return;
	}
	mediaPlayer = libvlc_media_player_new(libvlc);
	libvlc_video_set_output_callbacks(mediaPlayer, libvlc_video_engine_opengl, videoSetup, videoCleanup, nullptr, videoResize, videoSwap, make_current, get_proc_address, videoMetaData, nullptr, this);
	libvlc_audio_set_callbacks(mediaPlayer, audioPlay, audioPause, audioResume, audioFlush, audioDrain, this);
	libvlc_audio_set_format_callbacks(mediaPlayer, audioSetup, audioCleanup);
	eventManager = libvlc_media_player_event_manager(mediaPlayer);
	libvlc_event_attach(eventManager, libvlc_MediaPlayerLengthChanged, vlcEventStatic, this);
}

void ofxVlc4Player::load(std::string name) {
	if (!libvlc) {
		std::cout << "initialize libvlc first!" << std::endl;
	} else {
		if (mediaPlayer && libvlc_media_player_is_playing(mediaPlayer)) {
			stop();
		}
		if (ofStringTimesInString(name, "://") == 1) {
			media = libvlc_media_new_location(name.c_str());
		} else {
			media = libvlc_media_new_path(name.c_str());
		}
		libvlc_media_parse_request(libvlc, media, libvlc_media_parse_local, 0);
		libvlc_media_add_option(media, "demux=avformat");
		libvlc_media_player_set_media(mediaPlayer, media);
	}
}

void ofxVlc4Player::audioPlay(void * data, const void * samples, unsigned int count, int64_t pts) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	that->isAudioReady = true;
	that->buffer.copyFrom((float *)samples, count, that->channels, that->sampleRate);
	that->ringBuffer.writeFromBuffer(that->buffer);
	// std::cout << "sample size : " << count << ", pts: " << pts << std::endl;
	// std::cout << "readable samples: " << that->ringBuffer.getNumReadableSamples() << ", read position: " << that->ringBuffer.getReadPosition() << std::endl;
}

void ofxVlc4Player::audioPause(void * data, int64_t pts) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	that->isAudioReady = false;
	std::cout << "audio pause" << std::endl;
}

void ofxVlc4Player::audioResume(void * data, int64_t pts) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	that->isAudioReady = true;
	std::cout << "audio resume" << std::endl;
}

void ofxVlc4Player::audioFlush(void * data, int64_t pts) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	that->ringBuffer._readStart = 0;
	that->ringBuffer._writeStart = 0;
	std::cout << "audio flush" << std::endl;
}

void ofxVlc4Player::audioDrain(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	std::cout << "audio drain " << std::endl;
}

int ofxVlc4Player::audioSetup(void ** data, char * format, unsigned int * rate, unsigned int * channels) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(*data);
	strncpy(format, "FL32", 4);
	that->sampleRate = rate[0];
	that->channels = channels[0];
	that->ringBuffer._readStart = 0;
	that->ringBuffer._writeStart = 0;
	std::cout << "audio format : " << format << ", rate: " << rate[0] << ", channels: " << channels[0] << std::endl;
	return 0;
}

void ofxVlc4Player::audioCleanup(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	that->isAudioReady = false;
	std::cout << "audio cleanup" << std::endl;
}

// This callback is called during initialisation
bool ofxVlc4Player::videoSetup(void ** data, const libvlc_video_setup_device_cfg_t * cfg, libvlc_video_setup_device_info_t * out) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(*data);
	that->videoWidth = 0;
	that->videoHeight = 0;
	std::cout << "video setup" << std::endl;
	return true;
}

// This callback is called to release the texture and FBO created in resize
void ofxVlc4Player::videoCleanup(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (that->videoWidth == 0 && that->videoHeight == 0)
		return;

	glDeleteTextures(3, that->tex);
	glDeleteFramebuffers(3, that->fbo);
	std::cout << "video cleanup" << std::endl;
}

// this callback will create the surfaces and FBO used by VLC to perform its rendering
bool ofxVlc4Player::videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (cfg->width != that->videoWidth || cfg->height != that->videoHeight)
		videoCleanup(data);

	glGenTextures(3, that->tex);
	glGenFramebuffers(3, that->fbo);

	for (int i = 0; i < 3; i++) {
		glBindTexture(GL_TEXTURE_2D, that->tex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cfg->width, cfg->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[i]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, that->tex[i], 0);
	}
	glBindTexture(GL_TEXTURE_2D, 0);

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[that->idxRender]);

	render_cfg->opengl_format = GL_RGBA;
	render_cfg->full_range = true;
	render_cfg->colorspace = libvlc_video_colorspace_BT709;
	render_cfg->primaries = libvlc_video_primaries_BT709;
	render_cfg->transfer = libvlc_video_transfer_func_SRGB;
	render_cfg->orientation = libvlc_video_orient_top_left;

	that->videoWidth = cfg->width;
	that->videoHeight = cfg->height;
	that->texture.allocate(that->videoWidth, that->videoHeight, GL_RGBA);
	that->texture.getTextureData().bFlipTexture = true;
	std::cout << "video size: " << that->videoWidth << " * " << that->videoHeight << std::endl;

	return true;
}

// This callback is called after VLC performs drawing calls
void ofxVlc4Player::videoSwap(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	std::lock_guard<std::mutex> lock(that->texLock);
	that->updated = true;
	std::swap(that->idxSwap, that->idxRender);
	glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[that->idxRender]);
}

// This callback is called to set the OpenGL context
bool ofxVlc4Player::make_current(void * data, bool current) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (current) {
		ofAppGLFWWindow * win = dynamic_cast<ofAppGLFWWindow *>(that->vlcWindow.get());
		glfwMakeContextCurrent(win->getGLFWWindow());
		return true;
	} else {
		glfwMakeContextCurrent(NULL);
		return false;
	}
}

// This callback is called by VLC to get OpenGL functions
void * ofxVlc4Player::get_proc_address(void * data, const char * current) {
	// std::cout << current << std::endl;
	return (void *)glfwGetProcAddress(current);
}

void ofxVlc4Player::update() {
	if (updated) {
		std::swap(idxSwap, idxDisplay);
		updated = false;
	}
	texture.setUseExternalTextureID(tex[idxDisplay]);
}

ofTexture & ofxVlc4Player::getTexture() {
	return texture;
}

void ofxVlc4Player::draw(float x, float y, float w, float h) {
	ofSetColor(255);
	texture.draw(x, y, w, h);
}

void ofxVlc4Player::draw(float x, float y) {
	ofSetColor(255);
	texture.draw(x, y);
}

void ofxVlc4Player::play() {
	if (mediaPlayer) {
		libvlc_media_player_play(mediaPlayer);
	}
}

void ofxVlc4Player::pause() {
	if (mediaPlayer) {
		libvlc_media_player_pause(mediaPlayer);
	}
}

void ofxVlc4Player::stop() {
	if (mediaPlayer) {
		libvlc_media_player_stop_async(mediaPlayer);
	}
}

void ofxVlc4Player::setPosition(float pct) {
	if (mediaPlayer) {
		libvlc_media_player_set_position(mediaPlayer, pct, true);
	}
}

float ofxVlc4Player::getHeight() const {
	return videoHeight;
}

float ofxVlc4Player::getWidth() const {
	return videoWidth;
}

bool ofxVlc4Player::isPlaying() {
	if (mediaPlayer) {
		return libvlc_media_player_is_playing(mediaPlayer);
	} else {
		return false;
	}
}

bool ofxVlc4Player::isSeekable() {
	if (mediaPlayer) {
		return libvlc_media_player_is_seekable(mediaPlayer);
	} else {
		return false;
	}
}

float ofxVlc4Player::getPosition() {
	if (mediaPlayer) {
		return libvlc_media_player_get_position(mediaPlayer);
	} else {
		return 0;
	}
}

int ofxVlc4Player::getTime() {
	if (mediaPlayer) {
		return libvlc_media_player_get_time(mediaPlayer);
	} else {
		return 0;
	}
}

void ofxVlc4Player::setTime(int ms) {
	if (mediaPlayer) {
		libvlc_media_player_set_time(mediaPlayer, ms, true);
	}
}

float ofxVlc4Player::getLength() {
	if (mediaPlayer) {
		return libvlc_media_player_get_length(mediaPlayer);
	} else {
		return 0;
	}
}

void ofxVlc4Player::setVolume(int volume) {
	if (mediaPlayer) {
		libvlc_audio_set_volume(mediaPlayer, volume);
	}
}

void ofxVlc4Player::toggleMute() {
	if (mediaPlayer) {
		libvlc_audio_toggle_mute(mediaPlayer);
	}
}

void ofxVlc4Player::vlcEventStatic(const libvlc_event_t * event, void * data) {
	((ofxVlc4Player *)data)->vlcEvent(event);
}

void ofxVlc4Player::vlcEvent(const libvlc_event_t * event) {
	if (event->type == libvlc_MediaPlayerLengthChanged) {
		std::cout << "media length in ms: " << libvlc_media_get_duration(media) << std::endl;
	}
}

void ofxVlc4Player::close() {
	libvlc_media_player_release(mediaPlayer);
	libvlc_media_release(media);
	libvlc_free(libvlc);
}

bool ofxVlc4Player::audioIsReady() const {
	return isAudioReady;
}
