#pragma once

#include "ofMain.h"
#include "ofxVlc4PlayerRingBuffer.h"
#include "vlc/vlc.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

class ofxVlc4PlayerRecorder {
public:
	static constexpr double kBufferedAudioSeconds = 4.0;

	static std::string buildTimestampedOutputPath(const std::string & baseName, const std::string & extension);
	static std::string buildTimestampedOutputBasePath(const std::string & baseName);
	static void writeWavHeader(std::ofstream & stream, int sampleRate, int channels, uint32_t dataBytes);

	bool start(const std::string & baseName, int sampleRate, int channels);
	bool startAtPath(const std::string & outputPath, int sampleRate, int channels);
	std::string stop();
	void clearLastError();

	void writeInterleaved(const float * samples, size_t sampleCount);
	void writeBuffer(const ofSoundBuffer & buffer);
	void write(const float * samples, size_t sampleCount);
	void write(const ofSoundBuffer & buffer);

	bool isRecording() const;
	const std::string & getOutputPath() const;
	const std::string & getLastError() const;
	int getSampleRate() const;
	int getChannelCount() const;
	void setVideoFrameRate(int fps);
	int getVideoFrameRate() const;
	void setVideoCodec(const std::string & codec);
	const std::string & getVideoCodec() const;

	void clearVideoRecording();
	void clearAudioRecording();
	void flushAudioRecording();
	bool startAudioRecordingToPath(const std::string & outputPath, int sampleRate, int channels);
	bool startVideoRecordingToPath(libvlc_media_t *& mediaOut, const std::string & outputPath, const ofTexture & texture);
	void updateVideoFrame();
	void captureAudioSamples(const float * samples, size_t sampleCount);
	void resetCapturedAudio();
	void prepareAudioRecordingBuffer(int sampleRate, int channelCount);

	bool isVideoRecording() const;
	bool isAudioRecording() const;
	uint64_t getRecordedAudioOverrunCount() const;
	uint64_t getRecordedAudioUnderrunCount() const;

private:
	static constexpr uint64_t kMaxWavDataBytes = 0xFFFFFFFFull;
	static std::string trimRecordingWhitespace(const std::string & value);
	void finalize(bool clearError);
	static int textureOpen(void * data, void ** datap, uint64_t * sizep);
	static long long textureRead(void * data, unsigned char * buffer, size_t size);
	static int textureSeek(void * data, uint64_t offset);
	static void textureClose(void * data);

	int sampleRate = 0;
	int channelCount = 0;
	uint64_t dataBytes = 0;
	std::string outputPath;
	std::string lastError;
	std::ofstream stream;
	int videoFrameRate = 30;
	std::string videoCodec = "MJPG";

	std::atomic<bool> videoRecordingActive { false };
	std::atomic<bool> audioRecordingActive { false };
	ofTexture recordingTexture;
	ofPixels recordingPixels;
	mutable std::mutex recordingMutex;
	mutable std::mutex audioRecordingMutex;
	size_t recordingFrameSize = 0;
	uint64_t recordingReadOffset = 0;
	std::vector<float> audioTransferScratch;
	ofxVlc4PlayerRingBuffer audioRingBuffer;
};
