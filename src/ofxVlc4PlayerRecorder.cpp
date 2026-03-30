#include "ofxVlc4Player.h"
#include "ofxVlc4PlayerRecorder.h"

#include <algorithm>
#include <cstring>
#include <limits>

std::string ofxVlc4PlayerRecorder::trimRecordingWhitespace(const std::string & value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return "";
	}

	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

bool ofxVlc4PlayerRecorder::start(const std::string & baseName, int newSampleRate, int newChannels) {
	return startAtPath(
		buildTimestampedOutputPath(baseName, ".wav"),
		newSampleRate,
		newChannels);
}

bool ofxVlc4PlayerRecorder::startAtPath(const std::string & newOutputPath, int newSampleRate, int newChannels) {
	stop();
	clearLastError();

	if (newSampleRate <= 0 || newChannels <= 0) {
		lastError = "Invalid audio recorder format.";
		return false;
	}

	sampleRate = newSampleRate;
	channelCount = newChannels;
	dataBytes = 0;
	outputPath = newOutputPath;

	ofFilePath::createEnclosingDirectory(outputPath, false, true);
	stream.open(outputPath, std::ios::binary | std::ios::trunc);
	if (!stream.is_open()) {
		lastError = "Failed to open audio recording file.";
		outputPath.clear();
		sampleRate = 0;
		channelCount = 0;
		return false;
	}

	writeWavHeader(stream, sampleRate, channelCount, 0);
	return true;
}

std::string ofxVlc4PlayerRecorder::stop() {
	const std::string finishedPath = outputPath;
	finalize(lastError.empty());
	return finishedPath;
}

void ofxVlc4PlayerRecorder::clearLastError() {
	lastError.clear();
}

void ofxVlc4PlayerRecorder::finalize(bool clearError) {
	if (stream.is_open()) {
		writeWavHeader(stream, sampleRate, channelCount, static_cast<uint32_t>(std::min<uint64_t>(dataBytes, kMaxWavDataBytes)));
		stream.flush();
		stream.close();
	}

	sampleRate = 0;
	channelCount = 0;
	dataBytes = 0;
	outputPath.clear();
	if (clearError) {
		clearLastError();
	}
}

void ofxVlc4PlayerRecorder::writeInterleaved(const float * samples, size_t sampleCount) {
	if (!stream.is_open() || !samples || sampleCount == 0) {
		return;
	}

	const uint64_t remainingBytes = (dataBytes < kMaxWavDataBytes) ? (kMaxWavDataBytes - dataBytes) : 0;
	const size_t writableSamples = static_cast<size_t>(std::min<uint64_t>(sampleCount, remainingBytes / sizeof(float)));
	if (writableSamples > 0) {
		const size_t byteCount = writableSamples * sizeof(float);
		stream.write(reinterpret_cast<const char *>(samples), static_cast<std::streamsize>(byteCount));
		if (!stream.good()) {
			lastError = "Failed to write audio recording file.";
			finalize(false);
			return;
		}

		dataBytes += static_cast<uint64_t>(byteCount);
	}

	if (writableSamples < sampleCount) {
		lastError = "Audio recording reached the WAV size limit.";
		finalize(false);
	}
}

void ofxVlc4PlayerRecorder::writeBuffer(const ofSoundBuffer & buffer) {
	const auto & samples = buffer.getBuffer();
	if (!samples.empty()) {
		writeInterleaved(samples.data(), samples.size());
	}
}

void ofxVlc4PlayerRecorder::write(const float * samples, size_t sampleCount) {
	writeInterleaved(samples, sampleCount);
}

void ofxVlc4PlayerRecorder::write(const ofSoundBuffer & buffer) {
	writeBuffer(buffer);
}

bool ofxVlc4PlayerRecorder::isRecording() const {
	return stream.is_open();
}

const std::string & ofxVlc4PlayerRecorder::getOutputPath() const {
	return outputPath;
}

const std::string & ofxVlc4PlayerRecorder::getLastError() const {
	return lastError;
}

int ofxVlc4PlayerRecorder::getSampleRate() const {
	return sampleRate;
}

int ofxVlc4PlayerRecorder::getChannelCount() const {
	return channelCount;
}

void ofxVlc4PlayerRecorder::setVideoFrameRate(int fps) {
	clearLastError();
	if (fps <= 0) {
		lastError = "Video recording frame rate must be positive.";
		return;
	}

	videoFrameRate = fps;
}

int ofxVlc4PlayerRecorder::getVideoFrameRate() const {
	return videoFrameRate;
}

void ofxVlc4PlayerRecorder::setVideoCodec(const std::string & codec) {
	clearLastError();
	const std::string normalizedCodec = ofToUpper(trimRecordingWhitespace(codec));
	if (normalizedCodec.empty()) {
		lastError = "Video recording codec is empty.";
		return;
	}

	videoCodec = normalizedCodec;
}

const std::string & ofxVlc4PlayerRecorder::getVideoCodec() const {
	return videoCodec;
}

void ofxVlc4PlayerRecorder::writeWavHeader(std::ofstream & stream, int sampleRate, int channels, uint32_t dataBytes) {
	if (!stream.is_open()) {
		return;
	}

	const uint16_t channelCount = static_cast<uint16_t>(channels);
	const uint16_t blockAlign = static_cast<uint16_t>(channels * sizeof(float));
	const uint32_t byteRate = static_cast<uint32_t>(sampleRate * blockAlign);
	const uint32_t riffSize = 36u + dataBytes;
	const uint16_t audioFormat = 3; // IEEE float
	const uint16_t bitsPerSample = static_cast<uint16_t>(sizeof(float) * 8);
	const uint32_t fmtChunkSize = 16;

	stream.seekp(0, std::ios::beg);
	stream.write("RIFF", 4);
	stream.write(reinterpret_cast<const char *>(&riffSize), sizeof(riffSize));
	stream.write("WAVE", 4);
	stream.write("fmt ", 4);
	stream.write(reinterpret_cast<const char *>(&fmtChunkSize), sizeof(fmtChunkSize));
	stream.write(reinterpret_cast<const char *>(&audioFormat), sizeof(audioFormat));
	stream.write(reinterpret_cast<const char *>(&channelCount), sizeof(channelCount));
	stream.write(reinterpret_cast<const char *>(&sampleRate), sizeof(sampleRate));
	stream.write(reinterpret_cast<const char *>(&byteRate), sizeof(byteRate));
	stream.write(reinterpret_cast<const char *>(&blockAlign), sizeof(blockAlign));
	stream.write(reinterpret_cast<const char *>(&bitsPerSample), sizeof(bitsPerSample));
	stream.write("data", 4);
	stream.write(reinterpret_cast<const char *>(&dataBytes), sizeof(dataBytes));
}

std::string ofxVlc4PlayerRecorder::buildTimestampedOutputPath(const std::string & baseName, const std::string & extension) {
	std::string trimmed = trimRecordingWhitespace(baseName);
	if (trimmed.empty()) {
		trimmed = "recording";
	}

	std::string pathWithoutExtension = trimmed;
	std::string finalExtension = extension;

	const std::string providedExtension = ofFilePath::getFileExt(trimmed);
	if (!providedExtension.empty()) {
		pathWithoutExtension = ofFilePath::removeExt(trimmed);
		finalExtension = "." + providedExtension;
	}

	return pathWithoutExtension + ofGetTimestampString("-%Y-%m-%d-%H-%M-%S") + finalExtension;
}

std::string ofxVlc4PlayerRecorder::buildTimestampedOutputBasePath(const std::string & baseName) {
	std::string trimmed = trimRecordingWhitespace(baseName);
	if (trimmed.empty()) {
		trimmed = "recording";
	}

	if (!ofFilePath::getFileExt(trimmed).empty()) {
		trimmed = ofFilePath::removeExt(trimmed);
	}

	return trimmed + ofGetTimestampString("-%Y-%m-%d-%H-%M-%S");
}

void ofxVlc4PlayerRecorder::clearVideoRecording() {
	videoRecordingActive.store(false);

	std::lock_guard<std::mutex> lock(recordingMutex);
	recordingReadOffset = 0;
	recordingFrameSize = 0;
	recordingPixels.clear();
	recordingTexture.clear();
}

void ofxVlc4PlayerRecorder::clearAudioRecording() {
	flushAudioRecording();
	audioRecordingActive.store(false);
	stop();
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	audioTransferScratch.clear();
	audioRingBuffer.reset();
}

void ofxVlc4PlayerRecorder::flushAudioRecording() {
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	if (!isRecording()) {
		return;
	}

	const size_t readableSamples = audioRingBuffer.getNumReadableSamples();
	if (readableSamples == 0) {
		return;
	}

	audioTransferScratch.resize(readableSamples);
	audioRingBuffer.read(audioTransferScratch.data(), readableSamples);
	writeInterleaved(audioTransferScratch.data(), readableSamples);
	if (!isRecording()) {
		audioRecordingActive.store(false);
		audioTransferScratch.clear();
		audioRingBuffer.reset();
	}
}

bool ofxVlc4PlayerRecorder::startAudioRecordingToPath(const std::string & newOutputPath, int newSampleRate, int newChannels) {
	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	audioTransferScratch.clear();
	audioRingBuffer.allocate(
		static_cast<size_t>(newSampleRate) *
		static_cast<size_t>(newChannels) *
		kBufferedAudioSeconds);
	audioRingBuffer.clear();

	if (!startAtPath(newOutputPath, newSampleRate, newChannels)) {
		audioRecordingActive.store(false);
		return false;
	}

	audioRecordingActive.store(true);
	return true;
}

bool ofxVlc4PlayerRecorder::startVideoRecordingToPath(
	libvlc_media_t *& mediaOut,
	const std::string & outputPath,
	const ofTexture & texture) {
	clearVideoRecording();
	clearLastError();
	mediaOut = nullptr;

	if (!texture.isAllocated() || texture.getWidth() <= 0 || texture.getHeight() <= 0) {
		lastError = "Texture is not allocated.";
		return false;
	}

	if (videoFrameRate <= 0) {
		lastError = "Video recording frame rate must be positive.";
		return false;
	}

	if (videoCodec.empty()) {
		lastError = "Video recording codec is empty.";
		return false;
	}

	const int textureWidth = static_cast<int>(texture.getWidth());
	const int textureHeight = static_cast<int>(texture.getHeight());

	{
		std::lock_guard<std::mutex> lock(recordingMutex);
		recordingTexture.allocate(textureWidth, textureHeight, GL_RGB);
		recordingTexture.setUseExternalTextureID(texture.getTextureData().textureID);
		recordingPixels.allocate(textureWidth, textureHeight, OF_PIXELS_RGB);
		recordingTexture.readToPixels(recordingPixels);
		recordingFrameSize = recordingPixels.size();
		recordingReadOffset = 0;
	}

	mediaOut = libvlc_media_new_callbacks(textureOpen, textureRead, textureSeek, textureClose, this);
	if (!mediaOut) {
		lastError = "Failed to create recording media.";
		clearVideoRecording();
		return false;
	}

	std::string width = "rawvid-width=" + ofToString(textureWidth);
	std::string height = "rawvid-height=" + ofToString(textureHeight);
	std::string bufferSize = "prefetch-buffer-size=" + ofToString(textureWidth * textureHeight * 3);
	std::string rawFrameRate = "rawvid-fps=" + ofToString(videoFrameRate);
	std::string streamSpec = "sout=#transcode{vcodec=" + videoCodec + "}:standard{access=file,dst=" + outputPath + "}";

	libvlc_media_add_option(mediaOut, "demux=rawvid");
	libvlc_media_add_option(mediaOut, width.c_str());
	libvlc_media_add_option(mediaOut, height.c_str());
	libvlc_media_add_option(mediaOut, "rawvid-chroma=RV24");
	libvlc_media_add_option(mediaOut, rawFrameRate.c_str());
	libvlc_media_add_option(mediaOut, bufferSize.c_str());
	libvlc_media_add_option(mediaOut, streamSpec.c_str());

	videoRecordingActive.store(true);
	return true;
}

void ofxVlc4PlayerRecorder::updateVideoFrame() {
	if (!videoRecordingActive.load()) {
		return;
	}

	std::lock_guard<std::mutex> lock(recordingMutex);
	if (!recordingTexture.isAllocated() || !recordingPixels.isAllocated()) {
		return;
	}

	recordingTexture.readToPixels(recordingPixels);
	recordingFrameSize = recordingPixels.size();
}

void ofxVlc4PlayerRecorder::captureAudioSamples(const float * samples, size_t sampleCount) {
	if (audioRecordingActive.load() && samples && sampleCount > 0) {
		std::lock_guard<std::mutex> lock(audioRecordingMutex);
		audioRingBuffer.write(samples, sampleCount);
	}
}

void ofxVlc4PlayerRecorder::resetCapturedAudio() {
	if (audioRecordingActive.load()) {
		std::lock_guard<std::mutex> lock(audioRecordingMutex);
		audioRingBuffer.reset();
	}
}

void ofxVlc4PlayerRecorder::prepareAudioRecordingBuffer(int newSampleRate, int newChannelCount) {
	if (!audioRecordingActive.load()) {
		return;
	}

	std::lock_guard<std::mutex> lock(audioRecordingMutex);
	audioRingBuffer.allocate(
		static_cast<size_t>(newSampleRate) *
		static_cast<size_t>(newChannelCount) *
		kBufferedAudioSeconds);
	audioRingBuffer.clear();
}

bool ofxVlc4PlayerRecorder::isVideoRecording() const {
	return videoRecordingActive.load();
}

bool ofxVlc4PlayerRecorder::isAudioRecording() const {
	return audioRecordingActive.load();
}

uint64_t ofxVlc4PlayerRecorder::getRecordedAudioOverrunCount() const {
	return audioRingBuffer.getOverrunCount();
}

uint64_t ofxVlc4PlayerRecorder::getRecordedAudioUnderrunCount() const {
	return audioRingBuffer.getUnderrunCount();
}

int ofxVlc4PlayerRecorder::textureOpen(void * data, void ** datap, uint64_t * sizep) {
	auto * recorder = static_cast<ofxVlc4PlayerRecorder *>(data);
	if (!recorder || !recorder->videoRecordingActive.load()) {
		return -1;
	}

	if (datap) {
		*datap = recorder;
	}
	if (sizep) {
		*sizep = std::numeric_limits<uint64_t>::max();
	}

	std::lock_guard<std::mutex> lock(recorder->recordingMutex);
	recorder->recordingReadOffset = 0;
	return 0;
}

long long ofxVlc4PlayerRecorder::textureRead(void * data, unsigned char * dst, size_t size) {
	auto * recorder = static_cast<ofxVlc4PlayerRecorder *>(data);
	if (!recorder || !dst || size == 0 || !recorder->videoRecordingActive.load()) {
		return 0;
	}

	std::lock_guard<std::mutex> lock(recorder->recordingMutex);
	if (recorder->recordingFrameSize == 0 || !recorder->recordingPixels.isAllocated() || !recorder->recordingPixels.getData()) {
		return 0;
	}

	const unsigned char * src = recorder->recordingPixels.getData();
	size_t copied = 0;
	while (copied < size) {
		const size_t offset = static_cast<size_t>(recorder->recordingReadOffset % recorder->recordingFrameSize);
		const size_t chunkSize = std::min(size - copied, recorder->recordingFrameSize - offset);
		std::memcpy(dst + copied, src + offset, chunkSize);
		copied += chunkSize;
		recorder->recordingReadOffset = (recorder->recordingReadOffset + chunkSize) % recorder->recordingFrameSize;
	}

	return static_cast<long long>(copied);
}

int ofxVlc4PlayerRecorder::textureSeek(void * data, uint64_t offset) {
	auto * recorder = static_cast<ofxVlc4PlayerRecorder *>(data);
	if (!recorder) {
		return -1;
	}

	std::lock_guard<std::mutex> lock(recorder->recordingMutex);
	if (recorder->recordingFrameSize == 0) {
		return -1;
	}

	recorder->recordingReadOffset = offset % recorder->recordingFrameSize;
	return 0;
}

void ofxVlc4PlayerRecorder::textureClose(void * data) {
	auto * recorder = static_cast<ofxVlc4PlayerRecorder *>(data);
	if (!recorder) {
		return;
	}

	std::lock_guard<std::mutex> lock(recorder->recordingMutex);
	recorder->recordingReadOffset = 0;
}
