#include "ofxVlc4Player.h"
#include <GLFW/glfw3.h>

ofxVlcPlayer::ofxVlcPlayer()
	: libvlc(NULL)
	, eventManager(NULL)
	, media(NULL)
	, mediaPlayer(NULL)
	, ringBuffer(static_cast<size_t>(2048 * 2048))
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

ofxVlcPlayer::~ofxVlcPlayer() {}

void ofxVlcPlayer::load(std::string name, int vlc_argc, char const* vlc_argv[]) {
    libvlc = libvlc_new(vlc_argc, vlc_argv);
    if (!libvlc) {
        const char* error = libvlc_errmsg();
        cout << error << endl;
        return;
    }

    if (ofStringTimesInString(name, "://") == 1) {
        media = libvlc_media_new_location(name.c_str());
    }
    else {
        media = libvlc_media_new_path(name.c_str());
    }

    libvlc_media_parse_request(libvlc, media, libvlc_media_parse_local, 0);
    mediaPlayer = libvlc_media_player_new_from_media(libvlc, media);
    std::cout << "media length in ms: " << libvlc_media_get_duration(media) << std::endl;

    libvlc_video_set_output_callbacks(mediaPlayer, libvlc_video_engine_opengl, videoSetup, videoCleanup, nullptr, videoResize, videoSwap, make_current, get_proc_address, nullptr, nullptr, this);

    libvlc_audio_set_callbacks(mediaPlayer, audioPlay, audioPause, audioResume, audioFlush, audioDrain, this);
    libvlc_audio_set_format_callbacks(mediaPlayer, audioSetup, audioCleanup);

    eventManager = libvlc_media_player_event_manager(mediaPlayer);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerStopping, vlcEventStatic, this);
}

void ofxVlcPlayer::audioPlay(void* data, const void* samples, unsigned int count, int64_t pts) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    that->buffer.copyFrom((float*)samples, count, that->channels, that->sampleRate);
    that->ringBuffer.writeFromBuffer(that->buffer);
    // std::cout << "sample size : " << count << ", pts: " << pts << std::endl;
    // std::cout << "readable samples: " << that->ringBuffer.getNumReadableSamples() << ", read position: " << that->ringBuffer.getReadPosition() << std::endl;
}

void ofxVlcPlayer::audioPause(void* data, int64_t pts) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    // std::cout << "audio pause" << std::endl;
}

void ofxVlcPlayer::audioResume(void* data, int64_t pts) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    // std::cout << "audio resume" << std::endl;
}

void ofxVlcPlayer::audioFlush(void* data, int64_t pts) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    // std::cout << "audio flush" << std::endl;
}

void ofxVlcPlayer::audioDrain(void* data) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    // std::cout << "audio drain " << std::endl;
}

int ofxVlcPlayer::audioSetup(void** data, char* format, unsigned int* rate, unsigned int* channels) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(*data);
    strncpy(format, "FL32", 4);
    that->sampleRate = rate[0];
    that->channels = channels[0];
    that->isAudioReady = true;
    std::cout << "audio format : " << format << ", rate: " << rate[0] << ", channels: " << channels[0] << std::endl;
    return 0;
}

void ofxVlcPlayer::audioCleanup(void* data) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    that->isAudioReady = false;
    std::cout << "audio cleanup" << std::endl;
}

// This callback is called during initialisation
bool ofxVlcPlayer::videoSetup(void** data, const libvlc_video_setup_device_cfg_t* cfg, libvlc_video_setup_device_info_t* out) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(*data);
    that->videoWidth = 0;
    that->videoHeight = 0;
    return true;
}

// This callback is called to release the texture and FBO created in resize
void ofxVlcPlayer::videoCleanup(void* data) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    if (that->videoWidth == 0 && that->videoHeight == 0)
        return;

    glDeleteTextures(3, that->tex);
    glDeleteFramebuffers(3, that->fbo);
}

// this callback will create the surfaces and FBO used by VLC to perform its rendering
bool ofxVlcPlayer::videoResize(void* data, const libvlc_video_render_cfg_t* cfg, libvlc_video_output_cfg_t* render_cfg) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
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
void ofxVlcPlayer::videoSwap(void* data) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    std::lock_guard<std::mutex> lock(that->texLock);
    that->updated = true;
    std::swap(that->idxSwap, that->idxRender);
    glBindFramebuffer(GL_FRAMEBUFFER, that->fbo[that->idxRender]);
}

// This callback is called to set the OpenGL context
bool ofxVlcPlayer::make_current(void* data, bool current) {
    ofxVlcPlayer* that = static_cast<ofxVlcPlayer*>(data);
    if (current) {
        ofAppGLFWWindow* win = dynamic_cast<ofAppGLFWWindow*>(that->vlcWindow.get());
        glfwMakeContextCurrent(win->getGLFWWindow());
        return true;
    } else {
        glfwMakeContextCurrent(NULL);
        return false;
    }
}

// This callback is called by VLC to get OpenGL functions
void* ofxVlcPlayer::get_proc_address(void* data, const char* current) {
    // std::cout << current << std::endl;
    return glfwGetProcAddress(current);
}

void ofxVlcPlayer::update() {
    if (updated) {
        std::swap(idxSwap, idxDisplay);
        updated = false;
    }
    texture.setUseExternalTextureID(tex[idxDisplay]);
}

ofTexture& ofxVlcPlayer::getTexture() {
    return texture;
}

void ofxVlcPlayer::draw(float x, float y, float w, float h) {
    ofSetColor(255);
    texture.draw(x, y, w, h);
}

void ofxVlcPlayer::draw(float x, float y) {
    ofSetColor(255);
    texture.draw(x, y);
}

void ofxVlcPlayer::play() {
    libvlc_media_player_play(mediaPlayer);
}

void ofxVlcPlayer::pause() {
    libvlc_media_player_pause(mediaPlayer);
}

void ofxVlcPlayer::stop() {
    libvlc_media_player_stop_async(mediaPlayer);
}

void ofxVlcPlayer::setPosition(float pct) {
    libvlc_media_player_set_position(mediaPlayer, pct, true);
}

void ofxVlcPlayer::setLoop(bool loop) {
    isLooping = loop;
}

bool ofxVlcPlayer::getLoop() const {
    return isLooping;
}

float ofxVlcPlayer::getHeight() const {
    return videoHeight;
}

float ofxVlcPlayer::getWidth() const {
    return videoWidth;
}

bool ofxVlcPlayer::isPlaying() {
    return libvlc_media_player_is_playing(mediaPlayer);
}

bool ofxVlcPlayer::isSeekable() {
    return libvlc_media_player_is_seekable(mediaPlayer);
}

float ofxVlcPlayer::getPosition() {
    return libvlc_media_player_get_position(mediaPlayer);
}

int ofxVlcPlayer::getTime() {
    return libvlc_media_player_get_time(mediaPlayer);
}

void ofxVlcPlayer::setTime(int ms) {
    libvlc_media_player_set_time(mediaPlayer, ms, true);
}

float ofxVlcPlayer::getLength() {
    return libvlc_media_player_get_length(mediaPlayer);
}

void ofxVlcPlayer::setVolume(int volume) {
    libvlc_audio_set_volume(mediaPlayer, volume);
}

void ofxVlcPlayer::toggleMute() {
    libvlc_audio_toggle_mute(mediaPlayer);
}

void ofxVlcPlayer::vlcEventStatic(const libvlc_event_t* event, void* data) {
    ((ofxVlcPlayer*)data)->vlcEvent(event);
}

void ofxVlcPlayer::vlcEvent(const libvlc_event_t* event) {
    if (event->type == libvlc_MediaPlayerStopping) {
        if (isLooping) {
            // play();
        }
    }
}

void ofxVlcPlayer::close() {
    libvlc_media_player_release(mediaPlayer);
    libvlc_media_release(media);
}

bool ofxVlcPlayer::audioIsReady() const {
    return isAudioReady;
}
