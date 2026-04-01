#include "ofxVlc4PlayerRingBuffer.h"

#include <algorithm>
#include <cstring>

namespace {
static size_t nextPowerOfTwo(size_t value) {
	if (value < 2) return 2;
	--value;
	for (size_t shift = 1; shift < sizeof(size_t) * 8; shift <<= 1) {
		value |= value >> shift;
	}
	return value + 1;
}
} // namespace

ofxVlc4PlayerRingBuffer::ofxVlc4PlayerRingBuffer(size_t size) {
	allocate(size);
}

void ofxVlc4PlayerRingBuffer::allocate(size_t size) {
	_capacity = nextPowerOfTwo(std::max<size_t>(size, 2));
	_mask = _capacity - 1;
	_buffer.assign(_capacity, 0.0f);
	_readStart.store(0, std::memory_order_relaxed);
	_writeStart.store(0, std::memory_order_relaxed);
	_overruns.store(0, std::memory_order_relaxed);
	_underruns.store(0, std::memory_order_relaxed);
}

void ofxVlc4PlayerRingBuffer::clear() {
	_readStart.store(0, std::memory_order_relaxed);
	_writeStart.store(0, std::memory_order_relaxed);
	std::fill(_buffer.begin(), _buffer.end(), 0.0f);
}

void ofxVlc4PlayerRingBuffer::reset() {
	_readStart.store(0, std::memory_order_release);
	_writeStart.store(0, std::memory_order_release);
}

size_t ofxVlc4PlayerRingBuffer::getNumReadableSamples() const {
	const auto writeStart = _writeStart.load(std::memory_order_acquire);
	const auto readStart = _readStart.load(std::memory_order_acquire);
	return (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
}

size_t ofxVlc4PlayerRingBuffer::getNumWritableSamples() const {
	return _capacity - getNumReadableSamples();
}

size_t ofxVlc4PlayerRingBuffer::writeBegin(float *& first, size_t & firstCount, float *& second, size_t & secondCount) {
	const auto writeStart = _writeStart.load(std::memory_order_relaxed);
	const auto readStart = _readStart.load(std::memory_order_acquire);

	const size_t readable = (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
	const size_t writable = _capacity - readable;

	const auto readPosition = readStart & _mask;
	const auto writePosition = writeStart & _mask;

	first = &_buffer[writePosition];
	second = &_buffer[0];

	if (writePosition >= readPosition) {
		firstCount = std::min(_capacity - writePosition, writable);
		secondCount = writable - firstCount;
	} else {
		firstCount = writable;
		secondCount = 0;
	}
	return writable;
}

void ofxVlc4PlayerRingBuffer::writeEnd(size_t numSamples) {
	const auto writeStart = _writeStart.load(std::memory_order_relaxed);
	_writeStart.store(writeStart + numSamples, std::memory_order_release);
}

size_t ofxVlc4PlayerRingBuffer::readBegin(const float *& first, size_t & firstCount, const float *& second, size_t & secondCount) {
	const auto readStart = _readStart.load(std::memory_order_relaxed);
	const auto writeStart = _writeStart.load(std::memory_order_acquire);

	const size_t readable = (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
	const auto readPosition = readStart & _mask;
	const auto writePosition = writeStart & _mask;

	first = &_buffer[readPosition];
	second = &_buffer[0];

	if (writePosition >= readPosition) {
		firstCount = readable;
		secondCount = 0;
	} else {
		firstCount = _capacity - readPosition;
		secondCount = readable - firstCount;
	}

	return readable;
}

void ofxVlc4PlayerRingBuffer::readEnd(size_t numSamples) {
	const auto readStart = _readStart.load(std::memory_order_relaxed);
	_readStart.store(readStart + numSamples, std::memory_order_release);
}

size_t ofxVlc4PlayerRingBuffer::getReadPosition() {
	return _capacity ? (_readStart.load(std::memory_order_acquire) & _mask) : 0;
}

size_t ofxVlc4PlayerRingBuffer::write(const float * src, size_t wanted) {
	if (!src || wanted == 0 || _capacity == 0) return 0;

	float * dst[2];
	size_t count[2] = { 0, 0 };
	size_t consumed = 0;

	writeBegin(dst[0], count[0], dst[1], count[1]);

	for (size_t i = 0; i < 2; ++i) {
		const size_t todo = std::min(wanted - consumed, count[i]);
		if (todo > 0) {
			std::memcpy(dst[i], src + consumed, todo * sizeof(float));
			consumed += todo;
		}
	}

	writeEnd(consumed);

	if (consumed < wanted) {
		_overruns.fetch_add(1, std::memory_order_relaxed);
	}

	return consumed;
}

size_t ofxVlc4PlayerRingBuffer::read(float * dst, size_t wanted) {
	if (!dst || wanted == 0 || _capacity == 0) return 0;

	const float * src[2];
	size_t count[2] = { 0, 0 };
	size_t filled = 0;

	readBegin(src[0], count[0], src[1], count[1]);

	for (size_t i = 0; i < 2; ++i) {
		const size_t todo = std::min(wanted - filled, count[i]);
		if (todo > 0) {
			std::memcpy(dst + filled, src[i], todo * sizeof(float));
			filled += todo;
		}
	}

	readEnd(filled);

	if (filled < wanted) {
		std::memset(dst + filled, 0, (wanted - filled) * sizeof(float));
		_underruns.fetch_add(1, std::memory_order_relaxed);
	}

	return filled;
}

size_t ofxVlc4PlayerRingBuffer::read(float * dst, size_t wanted, float gain) {
	const size_t filled = read(dst, wanted);

	if (gain != 1.0f) {
		for (size_t i = 0; i < wanted; ++i) {
			dst[i] *= gain;
		}
	}

	return filled;
}

size_t ofxVlc4PlayerRingBuffer::peekLatest(float * dst, size_t wanted) const {
	if (!dst || wanted == 0 || _capacity == 0) return 0;

	const auto writeStart = _writeStart.load(std::memory_order_acquire);
	const auto readStart = _readStart.load(std::memory_order_acquire);
	const size_t readable = (writeStart > readStart) ? std::min(writeStart - readStart, _capacity) : 0;
	const size_t copied = std::min(wanted, readable);
	const size_t zeroPad = wanted - copied;

	if (zeroPad > 0) {
		std::memset(dst, 0, zeroPad * sizeof(float));
	}
	if (copied == 0) {
		return 0;
	}

	const size_t startIndex = (writeStart - copied) & _mask;
	const size_t firstCount = std::min(_capacity - startIndex, copied);
	std::memcpy(dst + zeroPad, _buffer.data() + startIndex, firstCount * sizeof(float));

	const size_t secondCount = copied - firstCount;
	if (secondCount > 0) {
		std::memcpy(dst + zeroPad + firstCount, _buffer.data(), secondCount * sizeof(float));
	}

	return copied;
}

size_t ofxVlc4PlayerRingBuffer::peekLatest(float * dst, size_t wanted, float gain) const {
	const size_t copied = peekLatest(dst, wanted);
	if (gain != 1.0f) {
		for (size_t i = 0; i < wanted; ++i) {
			dst[i] *= gain;
		}
	}

	return copied;
}

void ofxVlc4PlayerRingBuffer::readIntoVector(std::vector<float> & data) {
	if (!data.empty()) {
		read(data.data(), data.size());
	}
}

void ofxVlc4PlayerRingBuffer::readIntoVector(std::vector<float> & data, float gain) {
	if (!data.empty()) {
		read(data.data(), data.size(), gain);
	}
}

void ofxVlc4PlayerRingBuffer::readIntoBuffer(ofSoundBuffer & buffer) {
	auto & data = buffer.getBuffer();
	if (!data.empty()) {
		read(data.data(), data.size());
	}
}

void ofxVlc4PlayerRingBuffer::readIntoBuffer(ofSoundBuffer & buffer, float gain) {
	auto & data = buffer.getBuffer();
	if (!data.empty()) {
		read(data.data(), data.size(), gain);
	}
}

void ofxVlc4PlayerRingBuffer::writeFromBuffer(const ofSoundBuffer & buffer) {
	const auto & data = buffer.getBuffer();
	if (!data.empty()) {
		write(data.data(), data.size());
	}
}
