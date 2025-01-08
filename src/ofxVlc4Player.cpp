#include "ofxVlc4Player.h"
#include <GLFW/glfw3.h>

ofxVlc4Player::ofxVlc4Player()
	: libvlc(NULL)
	, mediaPlayerEventManager(NULL)
	, mediaEventManager(NULL)
	, media(NULL)
	, mediaPlayer(NULL)
	, ringBuffer(static_cast<size_t>(50000))
	, fbo() {
	ofGLFWWindowSettings settings;
	settings.shareContextWith = ofGetCurrentWindow();
	vlcWindow = std::make_shared<ofAppGLFWWindow>();
	vlcWindow->setup(settings);
	vlcWindow->setVerticalSync(true);
	fbo.allocate(1, 1, GL_RGBA);
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
	libvlc_video_set_output_callbacks(mediaPlayer, libvlc_video_engine_opengl, nullptr, nullptr, nullptr, videoResize, videoSwap, make_current, get_proc_address, nullptr, nullptr, this);
	libvlc_audio_set_callbacks(mediaPlayer, audioPlay, audioPause, audioResume, audioFlush, audioDrain, this);
	libvlc_audio_set_format_callbacks(mediaPlayer, audioSetup, audioCleanup);
	mediaPlayerEventManager = libvlc_media_player_event_manager(mediaPlayer);
	libvlc_event_attach(mediaPlayerEventManager, libvlc_MediaPlayerLengthChanged, vlcMediaPlayerEventStatic, this);
}

void ofxVlc4Player::load(std::string name) {
	if (!libvlc) {
		std::cout << "initialize libvlc first!" << std::endl;
	} else {
		if (ofStringTimesInString(name, "://") == 1) {
			media = libvlc_media_new_location(name.c_str());
		} else {
			media = libvlc_media_new_path(name.c_str());
		}
		libvlc_media_add_option(media, "demux=avcodec");
		mediaEventManager = libvlc_media_event_manager(media);
		libvlc_event_attach(mediaEventManager, libvlc_MediaParsedChanged, vlcMediaEventStatic, this);
		libvlc_media_parse_request(libvlc, media, libvlc_media_parse_local, 0);
		libvlc_media_player_set_media(mediaPlayer, media);
		// libvlc_media_player_record(mediaPlayer, true, &ofToDataPath("")[0]);
	}
}

void ofxVlc4Player::record(std::string name, ofTexture texture) {
	if (!libvlc) {
		std::cout << "initialize libvlc first!" << std::endl;
	} else {
		pix.allocate(texture.getWidth(), texture.getHeight(), OF_IMAGE_COLOR);
		tex.allocate(texture.getWidth(), texture.getHeight(), GL_RGB);
		tex.setUseExternalTextureID(texture.getTextureData().textureID);
		media = libvlc_media_new_callbacks(customOpen, customRead, customSeek, customClose, this);
		std::string width = "rawvid-width=" + ofToString(texture.getWidth());
		std::string height = "rawvid-height=" + ofToString(texture.getHeight());
		std::string stream = "sout=#transcode{vcodec=h264,vb=10000}:standard{access=file,dst=" + name + ofGetTimestampString("-%Y-%m-%d-%H-%M-%S") + ".mp4}";
		libvlc_media_add_option(media, "demux=rawvid");
		libvlc_media_add_option(media, &width[0]);
		libvlc_media_add_option(media, &height[0]);
		libvlc_media_add_option(media, "rawvid-chroma=RV24");
		libvlc_media_add_option(media, "rawvid-fps=60");
		libvlc_media_add_option(media, &stream[0]);
		libvlc_media_player_set_media(mediaPlayer, media);
		libvlc_media_player_play();
	}
}

int ofxVlc4Player::customOpen(void * data, void ** datap, uint64_t * sizep) {
	*sizep = static_cast<uint64_t>(1920 * 1080) * 3;
	*datap = data;
	std::cout << "open " << std::endl;
	return 0;
}

long long ofxVlc4Player::customRead(void * data, unsigned char * buffer, size_t size) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	memcpy(buffer, that->pix.getData(), size);
	return size;
}

int ofxVlc4Player::customSeek(void * data, uint64_t offset) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	std::cout << "seek" << std::endl;
	return 0;
}

void ofxVlc4Player::customClose(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	std::cout << "close" << std::endl;
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

// this callback will create the surfaces and FBO used by VLC to perform its rendering
bool ofxVlc4Player::videoResize(void * data, const libvlc_video_render_cfg_t * cfg, libvlc_video_output_cfg_t * render_cfg) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
	if (cfg->width != that->videoWidth || cfg->height != that->videoHeight) {
		render_cfg->opengl_format = GL_RGBA;
		render_cfg->full_range = true;
		render_cfg->colorspace = libvlc_video_colorspace_BT709;
		render_cfg->primaries = libvlc_video_primaries_BT709;
		render_cfg->transfer = libvlc_video_transfer_func_SRGB;
		render_cfg->orientation = libvlc_video_orient_top_left;
		that->videoWidth = cfg->width;
		that->videoHeight = cfg->height;
		that->fbo.allocate(that->videoWidth, that->videoHeight, GL_RGBA);
		that->fbo.getTexture().getTextureData().bFlipTexture = true;
		that->fbo.bind();
	}
	std::cout << "video size: " << that->videoWidth << " * " << that->videoHeight << std::endl;
	return true;
}

// This callback is called after VLC performs drawing calls
void ofxVlc4Player::videoSwap(void * data) {
	ofxVlc4Player * that = static_cast<ofxVlc4Player *>(data);
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

void ofxVlc4Player::draw(float x, float y, float w, float h) {
	tex.readToPixels(pix);
	fbo.getTexture().draw(x, y, w, h);
}

void ofxVlc4Player::draw(float x, float y) {
	fbo.getTexture().draw(x, y);
}

void ofxVlc4Player::updateRecorder() {
	tex.readToPixels(pix);
}

ofTexture & ofxVlc4Player::getTexture() {
	return fbo.getTexture();
}

void ofxVlc4Player::play() {
	libvlc_media_player_play(mediaPlayer);
}

void ofxVlc4Player::pause() {
	libvlc_media_player_pause(mediaPlayer);
}

void ofxVlc4Player::stop() {
	libvlc_media_player_stop_async(mediaPlayer);
}

void ofxVlc4Player::setPosition(float pct) {
	libvlc_media_player_set_position(mediaPlayer, pct, true);
}

float ofxVlc4Player::getHeight() const {
	return videoHeight;
}

float ofxVlc4Player::getWidth() const {
	return videoWidth;
}

bool ofxVlc4Player::isPlaying() {
	return libvlc_media_player_is_playing(mediaPlayer);
}

bool ofxVlc4Player::isSeekable() {
	return libvlc_media_player_is_seekable(mediaPlayer);
}

float ofxVlc4Player::getPosition() {
	return libvlc_media_player_get_position(mediaPlayer);
}

int ofxVlc4Player::getTime() {
	return libvlc_media_player_get_time(mediaPlayer);
}

void ofxVlc4Player::setTime(int ms) {
	libvlc_media_player_set_time(mediaPlayer, ms, true);
}

float ofxVlc4Player::getLength() {
	return libvlc_media_player_get_length(mediaPlayer);
}

void ofxVlc4Player::setVolume(int volume) {
	libvlc_audio_set_volume(mediaPlayer, volume);
}

void ofxVlc4Player::toggleMute() {
	libvlc_audio_toggle_mute(mediaPlayer);
}

void ofxVlc4Player::vlcMediaPlayerEventStatic(const libvlc_event_t * event, void * data) {
	((ofxVlc4Player *)data)->vlcMediaPlayerEvent(event);
}

void ofxVlc4Player::vlcMediaPlayerEvent(const libvlc_event_t * event) {
	if (event->type == libvlc_MediaPlayerLengthChanged) {
		// std::cout << "media length in ms: " << libvlc_media_get_duration(media) << std::endl;
	}
}

void ofxVlc4Player::vlcMediaEventStatic(const libvlc_event_t * event, void * data) {
	((ofxVlc4Player *)data)->vlcMediaEvent(event);
}

void ofxVlc4Player::vlcMediaEvent(const libvlc_event_t * event) {
	if (event->type == libvlc_MediaParsedChanged) {
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
