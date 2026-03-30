#pragma once
#include "ofSoundBuffer.h"
#include <atomic>
#include <cstddef>
#include <vector>

class ofxVlc4PlayerRingBuffer {
public:
	explicit ofxVlc4PlayerRingBuffer(size_t size = 0);

	void clear();
	void reset();
	size_t size() const { return _capacity; }

	void allocate(size_t size);

	void readIntoBuffer(ofSoundBuffer & buffer);
	void readIntoBuffer(ofSoundBuffer & buffer, float gain);

	void readIntoVector(std::vector<float> & data);
	void readIntoVector(std::vector<float> & data, float gain);

	void writeFromBuffer(const ofSoundBuffer & buffer);

	size_t write(const float * src, size_t sampleCount);
	size_t read(float * dst, size_t sampleCount);
	size_t read(float * dst, size_t sampleCount, float gain);

	size_t getReadPosition();
	size_t getNumReadableSamples() const;
	size_t getNumWritableSamples() const;

	uint64_t getOverrunCount() const { return _overruns.load(std::memory_order_relaxed); }
	uint64_t getUnderrunCount() const { return _underruns.load(std::memory_order_relaxed); }

private:
	size_t writeBegin(float *& first, size_t & firstCount, float *& second, size_t & secondCount);
	void writeEnd(size_t numSamples);

	size_t readBegin(const float *& first, size_t & firstCount, const float *& second, size_t & secondCount);
	void readEnd(size_t numSamples);

	std::vector<float> _buffer;
	size_t _capacity = 0;
	size_t _mask = 0;

	alignas(64) std::atomic<size_t> _readStart { 0 };
	alignas(64) std::atomic<size_t> _writeStart { 0 };

	std::atomic<uint64_t> _overruns { 0 };
	std::atomic<uint64_t> _underruns { 0 };
};
