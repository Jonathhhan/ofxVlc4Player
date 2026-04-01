#include "ofVlcPlayer4Gui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <vector>

namespace {
constexpr float kSectionSpacing = 14.0f;
constexpr float kButtonSpacing = 6.0f;
constexpr float kActionButtonWidth = 124.0f;
constexpr float kProjectMButtonRowWidth = (kActionButtonWidth * 3.0f) + (kButtonSpacing * 2.0f);
constexpr float kDualActionButtonWidth = (kProjectMButtonRowWidth - kButtonSpacing) * 0.5f;
constexpr float kInputLabelPadding = 60.0f;
constexpr float kPreviewInnerBorder = 5.0f;
constexpr float kOuterWindowPadding = 15.0f;
constexpr float kDisplayWindowPadding = kOuterWindowPadding;
constexpr float kWindowBorderSize = kPreviewInnerBorder;
const ImVec2 kLabelInnerSpacing(10.0f, 6.0f);

const ImVec4 kUiBg(0.10f, 0.11f, 0.12f, 0.98f);
const ImVec4 kUiChildBg(0.14f, 0.15f, 0.16f, 0.98f);
const ImVec4 kUiFrame(0.18f, 0.19f, 0.20f, 1.0f);
const ImVec4 kUiFrameHover(0.22f, 0.24f, 0.25f, 1.0f);
const ImVec4 kUiFrameActive(0.27f, 0.29f, 0.31f, 1.0f);
const ImVec4 kUiBorder(0.24f, 0.25f, 0.27f, 1.0f);
const ImVec4 kUiAccent(0.35f, 0.56f, 0.86f, 0.92f);
const ImVec4 kUiAccentHover(0.42f, 0.63f, 0.91f, 1.0f);
const ImVec4 kUiAccentActive(0.28f, 0.49f, 0.79f, 1.0f);
const ImVec4 kUiAccentBright(0.55f, 0.72f, 0.96f, 1.0f);
const ImVec4 kUiEqualizerCurve(0.42f, 0.63f, 0.91f, 0.96f);
const ImVec4 kUiEqualizerHandleLine(0.35f, 0.56f, 0.86f, 0.42f);
const ImVec4 kUiAnalyzerFill(0.28f, 0.49f, 0.79f, 0.20f);
const ImVec4 kUiAnalyzerLine(0.42f, 0.63f, 0.91f, 0.48f);
const ImVec4 kUiAnalyzerPeak(0.55f, 0.72f, 0.96f, 0.82f);
const ImVec4 kUiGreySelected(0.23f, 0.25f, 0.27f, 0.96f);
const ImVec4 kUiGreyHover(0.29f, 0.31f, 0.33f, 0.92f);
const ImVec4 kUiTitleBg(0.16f, 0.17f, 0.18f, 1.0f);
const ImVec4 kUiTitleBgActive(0.19f, 0.20f, 0.22f, 1.0f);
const ImVec4 kUiEmptyDisplay(0.0f, 0.0f, 0.0f, 1.0f);
constexpr float kPreviewWindowWidth = 720.0f;
constexpr float kPreviewMaxWidth = 680.0f;
constexpr float kDefaultPreviewAspect = 4.0f / 3.0f;
constexpr float kRemoteWindowX = 20.0f;
constexpr float kRemoteWindowY = 24.0f;
constexpr size_t kEqualizerSpectrumPointCount = 512;
constexpr size_t kEqualizerResponseSampleCount = 256;
constexpr int kAnalyzerBarCount = 72;
constexpr float kAnalyzerPeakHoldSeconds = 0.32f;
constexpr float kAnalyzerPeakReleasePerSecond = 1.10f;
constexpr float kEqualizerAmpWriteEpsilon = 0.01f;
constexpr std::array<float, 10> kEqualizerGuideFrequenciesHz = {
	31.5f, 63.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};
constexpr std::array<float, 4> kEqualizerDbLabelMarkers = { 18.0f, 12.0f, -12.0f, -18.0f };
constexpr std::array<AnalyzerDisplayStyle, 4> kAnalyzerDisplayStyles = {
	AnalyzerDisplayStyle::Studio,
	AnalyzerDisplayStyle::Mastering,
	AnalyzerDisplayStyle::RtaBars,
	AnalyzerDisplayStyle::Hybrid
};
constexpr std::array<EqualizerBandLayout, 7> kEqualizerBandLayouts = {
	EqualizerBandLayout::Graphic30,
	EqualizerBandLayout::Graphic16,
	EqualizerBandLayout::Graphic10,
	EqualizerBandLayout::Mix8,
	EqualizerBandLayout::Broad6,
	EqualizerBandLayout::Mastering5,
	EqualizerBandLayout::Broad3
};
constexpr std::array<float, 30> kGraphic30BandTargetsHz = {
	25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f, 200.0f,
	250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f,
	2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
};
constexpr std::array<float, 16> kGraphic16BandTargetsHz = {
	25.0f, 40.0f, 63.0f, 100.0f, 160.0f, 250.0f, 400.0f, 630.0f,
	1000.0f, 1600.0f, 2500.0f, 4000.0f, 6300.0f, 10000.0f, 16000.0f, 20000.0f
};
constexpr std::array<float, 10> kGraphic10BandTargetsHz = {
	31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};
constexpr std::array<float, 8> kMix8BandTargetsHz = {
	31.25f, 62.5f, 160.0f, 400.0f, 1000.0f, 2500.0f, 6300.0f, 16000.0f
};
constexpr std::array<float, 6> kBroad6BandTargetsHz = {
	62.5f, 160.0f, 400.0f, 1000.0f, 3000.0f, 12000.0f
};
constexpr std::array<float, 5> kMastering5BandTargetsHz = {
	80.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f
};
constexpr std::array<float, 3> kBroad3BandTargetsHz = {
	90.0f, 1000.0f, 11000.0f
};

struct PlaylistDragPayload {
	int index = -1;
};

struct EqualizerBandHandle {
	float targetFrequencyHz = 0.0f;
	int lowerBandIndex = -1;
	int upperBandIndex = -1;
	float upperWeight = 0.0f;
};

struct EqualizerBandTargetView {
	const float * data = nullptr;
	size_t size = 0;
};

int findNearestPointIndex(const std::vector<ImVec2> & points, const ImVec2 & target) {
	int nearestIndex = -1;
	float nearestDistance = std::numeric_limits<float>::max();
	for (int pointIndex = 0; pointIndex < static_cast<int>(points.size()); ++pointIndex) {
		const float dx = target.x - points[pointIndex].x;
		const float dy = target.y - points[pointIndex].y;
		const float distance = std::sqrt((dx * dx) + (dy * dy));
		if (distance < nearestDistance) {
			nearestDistance = distance;
			nearestIndex = pointIndex;
		}
	}

	return nearestIndex;
}

std::vector<ImVec2> buildAnchoredPolyline(const std::vector<ImVec2> & points, float minX, float maxX) {
	std::vector<ImVec2> linePoints;
	linePoints.reserve(points.size() + 2);
	if (points.empty()) {
		return linePoints;
	}

	if (std::abs(points.front().x - minX) > 1.0f) {
		linePoints.emplace_back(minX, points.front().y);
	}
	linePoints.insert(linePoints.end(), points.begin(), points.end());
	if (std::abs(points.back().x - maxX) > 1.0f) {
		linePoints.emplace_back(maxX, points.back().y);
	}

	if (linePoints.size() < 2) {
		return points;
	}

	return linePoints;
}

void drawTexturePreview(
	const ofTexture & texture,
	float displayWidth,
	float displayHeight,
	float maxWidth,
	bool fillWindow,
	bool flipVertical,
	float innerBorder = kPreviewInnerBorder,
	ImU32 textureBackgroundColor = 0);

float computePreviewWindowHeight(float sourceWidth, float sourceHeight, float windowWidth, float maxPreviewWidth);
bool isValidPlaylistIndex(const std::vector<std::string> & playlist, int index);

EqualizerBandTargetView equalizerBandLayoutTargets(EqualizerBandLayout layout) {
	switch (layout) {
	case EqualizerBandLayout::Graphic30:
		return { kGraphic30BandTargetsHz.data(), kGraphic30BandTargetsHz.size() };
	case EqualizerBandLayout::Graphic16:
		return { kGraphic16BandTargetsHz.data(), kGraphic16BandTargetsHz.size() };
	case EqualizerBandLayout::Mix8:
		return { kMix8BandTargetsHz.data(), kMix8BandTargetsHz.size() };
	case EqualizerBandLayout::Broad6:
		return { kBroad6BandTargetsHz.data(), kBroad6BandTargetsHz.size() };
	case EqualizerBandLayout::Mastering5:
		return { kMastering5BandTargetsHz.data(), kMastering5BandTargetsHz.size() };
	case EqualizerBandLayout::Broad3:
		return { kBroad3BandTargetsHz.data(), kBroad3BandTargetsHz.size() };
	case EqualizerBandLayout::Graphic10:
	default:
		return { kGraphic10BandTargetsHz.data(), kGraphic10BandTargetsHz.size() };
	}
}

std::vector<EqualizerBandHandle> buildEqualizerBandHandles(const std::vector<float> & bandFrequencies, EqualizerBandLayout layout) {
	std::vector<EqualizerBandHandle> handles;
	if (bandFrequencies.empty()) {
		return handles;
	}

	const EqualizerBandTargetView targetFrequencies = equalizerBandLayoutTargets(layout);
	handles.reserve(targetFrequencies.size);
	for (size_t targetIndex = 0; targetIndex < targetFrequencies.size; ++targetIndex) {
		const float targetFrequency = targetFrequencies.data[targetIndex];
		const float clampedTarget = std::clamp(targetFrequency, bandFrequencies.front(), bandFrequencies.back());
		EqualizerBandHandle handle {};
		handle.targetFrequencyHz = clampedTarget;

		if (clampedTarget <= bandFrequencies.front()) {
			handle.lowerBandIndex = 0;
			handle.upperBandIndex = 0;
			handles.push_back(handle);
			continue;
		}
		if (clampedTarget >= bandFrequencies.back()) {
			const int lastBandIndex = static_cast<int>(bandFrequencies.size()) - 1;
			handle.lowerBandIndex = lastBandIndex;
			handle.upperBandIndex = lastBandIndex;
			handles.push_back(handle);
			continue;
		}

		for (int bandIndex = 1; bandIndex < static_cast<int>(bandFrequencies.size()); ++bandIndex) {
			const float lowerFrequency = bandFrequencies[static_cast<size_t>(bandIndex - 1)];
			const float upperFrequency = bandFrequencies[static_cast<size_t>(bandIndex)];
			if (clampedTarget > upperFrequency) {
				continue;
			}

			handle.lowerBandIndex = bandIndex - 1;
			handle.upperBandIndex = bandIndex;
			if (upperFrequency > lowerFrequency && lowerFrequency > 0.0f) {
				const float logLower = std::log(lowerFrequency);
				const float logUpper = std::log(upperFrequency);
				const float logTarget = std::log(clampedTarget);
				handle.upperWeight = std::clamp((logTarget - logLower) / (logUpper - logLower), 0.0f, 1.0f);
			}
			break;
		}

		if (handle.lowerBandIndex < 0) {
			handle.lowerBandIndex = 0;
			handle.upperBandIndex = 0;
		}
		handles.push_back(handle);
	}

	return handles;
}

float getEqualizerHandleAmp(ofxVlc4Player & player, const EqualizerBandHandle & handle) {
	if (handle.lowerBandIndex < 0) {
		return 0.0f;
	}
	if (handle.lowerBandIndex == handle.upperBandIndex) {
		return player.getEqualizerBandAmp(handle.lowerBandIndex);
	}

	const float lowerAmp = player.getEqualizerBandAmp(handle.lowerBandIndex);
	const float upperAmp = player.getEqualizerBandAmp(handle.upperBandIndex);
	return ofLerp(lowerAmp, upperAmp, handle.upperWeight);
}

float getEqualizerHandleAmp(const std::vector<float> & bandAmps, const EqualizerBandHandle & handle) {
	if (handle.lowerBandIndex < 0 || handle.lowerBandIndex >= static_cast<int>(bandAmps.size())) {
		return 0.0f;
	}
	if (handle.lowerBandIndex == handle.upperBandIndex || handle.upperBandIndex < 0 ||
		handle.upperBandIndex >= static_cast<int>(bandAmps.size())) {
		return bandAmps[static_cast<size_t>(handle.lowerBandIndex)];
	}

	return ofLerp(
		bandAmps[static_cast<size_t>(handle.lowerBandIndex)],
		bandAmps[static_cast<size_t>(handle.upperBandIndex)],
		handle.upperWeight);
}

std::vector<float> buildEqualizerDisplayCurve(
	const std::vector<float> & bandFrequencies,
	const std::vector<float> & bandAmps,
	size_t sampleCount,
	float minFrequency,
	float maxFrequency) {
	std::vector<float> curve(sampleCount, 0.0f);
	if (sampleCount == 0 || bandFrequencies.empty() || bandFrequencies.size() != bandAmps.size()) {
		return curve;
	}

	std::vector<float> logFrequencies(bandFrequencies.size(), 0.0f);
	for (size_t i = 0; i < bandFrequencies.size(); ++i) {
		logFrequencies[i] = std::log(std::max(bandFrequencies[i], 1.0f));
	}

	const float logMinFrequency = std::log(std::max(minFrequency, 1.0f));
	const float logMaxFrequency = std::log(std::max(maxFrequency, minFrequency + 1.0f));
	const size_t lastBandIndex = bandFrequencies.size() - 1;
	for (size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
		const float t = sampleCount <= 1
			? 0.0f
			: static_cast<float>(sampleIndex) / static_cast<float>(sampleCount - 1);
		const float logTarget = ofLerp(logMinFrequency, logMaxFrequency, t);

		float weightedGain = 0.0f;
		float totalWeight = 0.0f;
		for (size_t bandIndex = 0; bandIndex < bandFrequencies.size(); ++bandIndex) {
			const float leftSpan = bandIndex > 0
				? (logFrequencies[bandIndex] - logFrequencies[bandIndex - 1])
				: ((bandFrequencies.size() > 1) ? (logFrequencies[1] - logFrequencies[0]) : 0.45f);
			const float rightSpan = bandIndex < lastBandIndex
				? (logFrequencies[bandIndex + 1] - logFrequencies[bandIndex])
				: ((bandFrequencies.size() > 1) ? (logFrequencies[lastBandIndex] - logFrequencies[lastBandIndex - 1]) : 0.45f);
			const float sigma = std::max(0.18f, 0.85f * 0.5f * (leftSpan + rightSpan));

			float distance = std::abs(logTarget - logFrequencies[bandIndex]);
			if (bandIndex == 0 && logTarget < logFrequencies[bandIndex]) {
				distance *= 0.38f;
			} else if (bandIndex == lastBandIndex && logTarget > logFrequencies[bandIndex]) {
				distance *= 0.38f;
			}

			const float normalized = distance / sigma;
			const float weight = std::exp(-0.5f * normalized * normalized);
			weightedGain += bandAmps[bandIndex] * weight;
			totalWeight += weight;
		}

		curve[sampleIndex] = totalWeight > 0.0f ? (weightedGain / totalWeight) : 0.0f;
	}

	return curve;
}

void setEqualizerHandleAmp(ofxVlc4Player & player, const EqualizerBandHandle & handle, float targetAmp) {
	const float clampedTarget = ofClamp(targetAmp, -20.0f, 20.0f);
	if (handle.lowerBandIndex < 0) {
		return;
	}
	if (handle.lowerBandIndex == handle.upperBandIndex) {
		const float currentAmp = player.getEqualizerBandAmp(handle.lowerBandIndex);
		if (std::abs(currentAmp - clampedTarget) > kEqualizerAmpWriteEpsilon) {
			player.setEqualizerBandAmp(handle.lowerBandIndex, clampedTarget);
		}
		return;
	}

	const float currentLowerAmp = player.getEqualizerBandAmp(handle.lowerBandIndex);
	const float currentUpperAmp = player.getEqualizerBandAmp(handle.upperBandIndex);
	const float lowerWeight = 1.0f - handle.upperWeight;
	const float upperWeight = handle.upperWeight;
	const float currentAmp = ofLerp(currentLowerAmp, currentUpperAmp, handle.upperWeight);
	const float delta = clampedTarget - currentAmp;
	const float weightEnergy = std::max((lowerWeight * lowerWeight) + (upperWeight * upperWeight), 1.0e-6f);

	const float lowerAmp = currentLowerAmp + (delta * lowerWeight / weightEnergy);
	const float upperAmp = currentUpperAmp + (delta * upperWeight / weightEnergy);
	if (std::abs(lowerAmp - currentLowerAmp) > kEqualizerAmpWriteEpsilon) {
		player.setEqualizerBandAmp(handle.lowerBandIndex, lowerAmp);
	}
	if (std::abs(upperAmp - currentUpperAmp) > kEqualizerAmpWriteEpsilon) {
		player.setEqualizerBandAmp(handle.upperBandIndex, upperAmp);
	}
}

const char * analyzerDisplayStyleLabel(AnalyzerDisplayStyle style) {
	switch (style) {
	case AnalyzerDisplayStyle::Mastering:
		return "Mastering";
	case AnalyzerDisplayStyle::RtaBars:
		return "RTA Bars";
	case AnalyzerDisplayStyle::Hybrid:
		return "Hybrid";
	case AnalyzerDisplayStyle::Studio:
	default:
		return "Studio";
	}
}

const char * equalizerBandLayoutLabel(EqualizerBandLayout layout) {
	switch (layout) {
	case EqualizerBandLayout::Graphic30:
		return "Graphic 30-Band";
	case EqualizerBandLayout::Graphic16:
		return "Graphic 16-Band";
	case EqualizerBandLayout::Mix8:
		return "Mix 8-Band";
	case EqualizerBandLayout::Broad6:
		return "Broad 6-Band";
	case EqualizerBandLayout::Mastering5:
		return "Mastering 5-Band";
	case EqualizerBandLayout::Broad3:
		return "Broad 3-Band";
	case EqualizerBandLayout::Graphic10:
	default:
		return "Graphic 10-Band";
	}
}

std::string buildTimeText(ofxVlc4Player & player, bool showRemainingTime) {
	const int currentSeconds = player.getTime() / 1000;
	const int totalLengthSeconds = static_cast<int>(player.getLength() / 1000.0f);
	const int displaySeconds = showRemainingTime
		? std::max(0, totalLengthSeconds - currentSeconds)
		: currentSeconds;

	const int hours = displaySeconds / 3600;
	const int minutes = (displaySeconds % 3600) / 60;
	const int seconds = displaySeconds % 60;
	char timeLabel[16];
	std::snprintf(timeLabel, sizeof(timeLabel), "%02d:%02d:%02d", hours, minutes, seconds);

	return showRemainingTime
		? "Time:  -" + std::string(timeLabel)
		: "Time:  " + std::string(timeLabel);
}

std::string formatDurationSeconds(int totalSeconds) {
	const int hours = totalSeconds / 3600;
	const int minutes = (totalSeconds % 3600) / 60;
	const int seconds = totalSeconds % 60;
	char timeLabel[16];
	std::snprintf(timeLabel, sizeof(timeLabel), "%02d:%02d:%02d", hours, minutes, seconds);
	return timeLabel;
}

std::string formatFileSize(uintmax_t bytes) {
	static const char * units[] = { "B", "KB", "MB", "GB", "TB" };
	double value = static_cast<double>(bytes);
	size_t unitIndex = 0;
	while (value >= 1024.0 && unitIndex + 1 < std::size(units)) {
		value /= 1024.0;
		++unitIndex;
	}

	std::ostringstream stream;
	stream << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 1) << value << " " << units[unitIndex];
	return stream.str();
}

std::string formatEqualizerFrequency(float frequencyHz) {
	if (frequencyHz <= 0.0f) {
		return "";
	}

	std::ostringstream stream;
	if (frequencyHz >= 1000.0f) {
		const float kiloHertz = frequencyHz / 1000.0f;
		const float roundedKiloHertz = std::round(kiloHertz);
		if (std::abs(kiloHertz - roundedKiloHertz) < 0.05f) {
			stream << std::fixed << std::setprecision(0) << roundedKiloHertz << " kHz";
		} else {
			stream << std::fixed << std::setprecision(1) << kiloHertz << " kHz";
		}
	} else {
		stream << std::fixed << std::setprecision(0) << frequencyHz << " Hz";
	}
	return stream.str();
}

std::string formatLastWriteTime(const std::filesystem::path & path) {
	std::error_code ec;
	const auto lastWrite = std::filesystem::last_write_time(path, ec);
	if (ec) {
		return "";
	}

	const auto adjusted = lastWrite - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now();
	const auto systemTime = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(adjusted));
	std::tm localTime {};
#ifdef _WIN32
	localtime_s(&localTime, &systemTime);
#else
	localtime_r(&systemTime, &localTime);
#endif
	std::ostringstream stream;
	stream << std::put_time(&localTime, "%Y-%m-%d %H:%M");
	return stream.str();
}

std::string resolveDisplayFileName(const std::string & path, const std::string & fileName) {
	if (!fileName.empty()) {
		return fileName;
	}
	if (!path.empty() && path.find("://") == std::string::npos) {
		return ofFilePath::getFileName(path);
	}
	return "";
}

MediaDisplayState resolveMediaDisplayState(
	ofxVlc4Player & player,
	int selectedIndex) {
	const int currentPlayingIndex = player.getCurrentIndex();
	const int mediaIndex = isValidPlaylistIndex(player.getPlaylist(), currentPlayingIndex)
		? currentPlayingIndex
		: (isValidPlaylistIndex(player.getPlaylist(), selectedIndex) ? selectedIndex : -1);

	const std::string path = mediaIndex >= 0
		? player.getPathAtIndex(mediaIndex)
		: player.getCurrentPath();
	const std::string fileName = resolveDisplayFileName(
		path,
		mediaIndex >= 0 ? player.getFileNameAtIndex(mediaIndex) : player.getCurrentFileName());
	const auto metadata = mediaIndex >= 0
		? player.getMetadataAtIndex(mediaIndex)
		: player.getCurrentMetadata();

	return {
		path,
		fileName,
		metadata
	};
}

void drawCurrentMediaCopyTooltip() {
	if (ImGui::BeginTooltip()) {
		ImGui::TextUnformatted("Click to copy media path");
		ImGui::EndTooltip();
	}
}

void drawCurrentMediaInfoContent(
	const std::string & currentPath,
	const std::string & fileName,
	const std::vector<std::pair<std::string, std::string>> & metadata) {
	std::string title;
	for (const auto & [entryLabel, entryValue] : metadata) {
		if (entryLabel == "Title" && !entryValue.empty()) {
			title = entryValue;
			break;
		}
	}
	const std::string displayName = !title.empty() ? title : fileName;
	if (!displayName.empty()) {
		ImGui::TextUnformatted(displayName.c_str());
	}
	if (!fileName.empty() && !title.empty() && fileName != title) {
		ImGui::TextDisabled("%s", fileName.c_str());
	}
	ImGui::Separator();
	for (const auto & [entryLabel, entryValue] : metadata) {
		if (entryValue.empty()) {
			continue;
		}
		if (entryLabel == "Title" || entryLabel == "Artwork URL") {
			continue;
		}
		if (entryLabel == "Duration") {
			ImGui::TextWrapped("Duration: %s", formatDurationSeconds(ofToInt(entryValue)).c_str());
			continue;
		}
		if (entryLabel == "Date") {
			ImGui::TextWrapped("Year: %s", entryValue.c_str());
			continue;
		}
		ImGui::TextWrapped("%s: %s", entryLabel.c_str(), entryValue.c_str());
	}

	const bool isUri = currentPath.find("://") != std::string::npos;
	ImGui::Separator();
	ImGui::Text("Type: %s", isUri ? "URI" : "File");

	const std::string extension = ofFilePath::getFileExt(currentPath);
	if (!extension.empty()) {
		ImGui::Text("Extension: %s", ofToLower(extension).c_str());
	}

	if (!isUri) {
		const std::filesystem::path fsPath(currentPath);
		std::error_code ec;
		if (std::filesystem::exists(fsPath, ec) && !ec) {
			if (std::filesystem::is_regular_file(fsPath, ec) && !ec) {
				const auto bytes = std::filesystem::file_size(fsPath, ec);
				if (!ec) {
					ImGui::Text("Size: %s", formatFileSize(bytes).c_str());
				}
			}

			const std::string modified = formatLastWriteTime(fsPath);
			if (!modified.empty()) {
				ImGui::Text("Modified: %s", modified.c_str());
			}
		}
	}
}

bool applyHoveredWheelStep(int & value, int minValue, int maxValue) {
	if (!ImGui::IsItemHovered()) {
		return false;
	}

	const float wheel = ImGui::GetIO().MouseWheel;
	if (wheel == 0.0f) {
		return false;
	}

	const int direction = (wheel < 0.0f) ? 1 : -1;
	const int newValue = ofClamp(value + direction, minValue, maxValue);
	if (newValue == value) {
		return false;
	}

	value = newValue;
	return true;
}

bool applyHoveredWheelStep(float & value, float minValue, float maxValue, float step) {
	if (!ImGui::IsItemHovered()) {
		return false;
	}

	const auto & io = ImGui::GetIO();
	const float wheel = io.MouseWheel;
	if (wheel == 0.0f || step <= 0.0f) {
		return false;
	}

	const float effectiveStep = io.KeyCtrl ? 0.1f : step;
	const float direction = (wheel > 0.0f) ? 1.0f : -1.0f;
	const float newValue = ofClamp(value + direction * effectiveStep, minValue, maxValue);
	if (newValue == value) {
		return false;
	}

	value = newValue;
	return true;
}

void drawPreviewWindow(
	const char * title,
	bool & openFlag,
	const ofTexture & texture,
	float sourceWidth,
	float sourceHeight,
	bool flipVertical,
	bool fullscreen,
	const ImVec2 & fullscreenPos,
	const ImVec2 & fullscreenSize,
	glm::vec2 & lastWindowPos,
	bool & restoreWindowPosition) {
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (fullscreen) {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, kUiEmptyDisplay);
		ImGui::SetNextWindowPos(fullscreenPos, ImGuiCond_Always);
		ImGui::SetNextWindowSize(fullscreenSize, ImGuiCond_Always);
		flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
	} else {
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kDisplayWindowPadding, kDisplayWindowPadding));
		ImGui::SetNextWindowSize(
			ImVec2(kPreviewWindowWidth, computePreviewWindowHeight(sourceWidth, sourceHeight, kPreviewWindowWidth, kPreviewMaxWidth)),
			ImGuiCond_Always);
		ImGui::SetNextWindowPos(
			ImVec2(lastWindowPos.x, lastWindowPos.y),
			restoreWindowPosition ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
	}

	ImGui::Begin(title, &openFlag, flags);
	drawTexturePreview(
		texture,
		sourceWidth,
		sourceHeight,
		fullscreen ? fullscreenSize.x : kPreviewMaxWidth,
		false,
		flipVertical,
		fullscreen ? 0.0f : kPreviewInnerBorder,
		ImGui::GetColorU32(fullscreen ? kUiEmptyDisplay : kUiBorder));
	if (!fullscreen) {
		const ImVec2 currentPos = ImGui::GetWindowPos();
		lastWindowPos = glm::vec2(currentPos.x, currentPos.y);
		restoreWindowPosition = false;
	}
	ImGui::End();

	ImGui::PopStyleVar();
	if (fullscreen) {
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);
	}
}

void drawTexturePreview(
	const ofTexture & texture,
	float displayWidth,
	float displayHeight,
	float maxWidth,
	bool fillWindow,
	bool flipVertical,
	float innerBorder,
	ImU32 textureBackgroundColor) {
	const ImVec2 contentStart = ImGui::GetCursorPos();
	const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
	const ImVec2 screenStart = ImGui::GetCursorScreenPos();
	const ImVec2 insetRegion(
		std::max(1.0f, availableRegion.x - innerBorder * 2.0f),
		std::max(1.0f, availableRegion.y - innerBorder * 2.0f));
	ImDrawList * drawList = ImGui::GetWindowDrawList();
	if (textureBackgroundColor == 0) {
		textureBackgroundColor = ImGui::GetColorU32(kUiBorder);
	}

	float drawWidth = 0.0f;
	float drawHeight = 0.0f;
	if (displayWidth <= 0.0f || displayHeight <= 0.0f) {
		// Keep an explicit fallback aspect here so empty preview windows stay visually stable.
		const float availableWidth = std::max(1.0f, std::min(maxWidth, insetRegion.x));
		drawWidth = availableWidth;
		drawHeight = std::max(1.0f, drawWidth / kDefaultPreviewAspect);
		if (drawHeight > insetRegion.y) {
			drawHeight = std::max(1.0f, insetRegion.y);
			drawWidth = std::max(1.0f, drawHeight * kDefaultPreviewAspect);
		}
	} else if (fillWindow) {
		drawWidth = std::max(1.0f, insetRegion.x);
		drawHeight = std::max(1.0f, insetRegion.y);
	} else {
		const float availableWidth = std::max(1.0f, std::min(maxWidth, insetRegion.x));
		const float aspect = std::max(displayWidth / displayHeight, 0.0001f);
		drawWidth = availableWidth;
		drawHeight = std::max(1.0f, drawWidth / aspect);
		if (drawHeight > insetRegion.y) {
			drawHeight = std::max(1.0f, insetRegion.y);
			drawWidth = std::max(1.0f, drawHeight * aspect);
		}
	}

	const ImVec2 drawPos(
		contentStart.x + innerBorder + std::max(0.0f, (insetRegion.x - drawWidth) * 0.5f),
		contentStart.y + innerBorder + std::max(0.0f, (insetRegion.y - drawHeight) * 0.5f));
	const ImVec2 drawScreenPos(
		screenStart.x + (drawPos.x - contentStart.x),
		screenStart.y + (drawPos.y - contentStart.y));

	if (!texture.isAllocated()) {
		drawList->AddRectFilled(
			drawScreenPos,
			ImVec2(drawScreenPos.x + drawWidth, drawScreenPos.y + drawHeight),
			ImGui::GetColorU32(kUiEmptyDisplay));
		ImGui::SetCursorPos(drawPos);
		ImGui::Dummy(ImVec2(drawWidth, drawHeight));
		return;
	}

	drawList->AddRectFilled(
		drawScreenPos,
		ImVec2(drawScreenPos.x + drawWidth, drawScreenPos.y + drawHeight),
		textureBackgroundColor);

	ImGui::SetCursorPos(drawPos);

	const ImTextureID textureId = (ImTextureID)(uintptr_t)texture.getTextureData().textureID;
	const float textureWidth = std::max(1.0f, static_cast<float>(texture.getWidth()));
	const float textureHeight = std::max(1.0f, static_cast<float>(texture.getHeight()));
	const float uvMaxX = std::clamp(displayWidth / textureWidth, 0.0f, 1.0f);
	const float uvMaxY = std::clamp(displayHeight / textureHeight, 0.0f, 1.0f);
	ImGui::Image(
		textureId,
		ImVec2(drawWidth, drawHeight),
		flipVertical ? ImVec2(0.0f, uvMaxY) : ImVec2(0.0f, 0.0f),
		flipVertical ? ImVec2(uvMaxX, 0.0f) : ImVec2(uvMaxX, uvMaxY));
}

float computePreviewWindowHeight(float sourceWidth, float sourceHeight, float windowWidth, float maxPreviewWidth) {
	const ImGuiStyle & style = ImGui::GetStyle();
	const float titleBarHeight = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
	const float contentWidth = std::max(1.0f, windowWidth - style.WindowPadding.x * 2.0f);
	const float innerContentWidth = std::max(1.0f, contentWidth - kPreviewInnerBorder * 2.0f);
	const float drawWidth = std::max(1.0f, std::min(maxPreviewWidth, innerContentWidth));
	const float aspect = (sourceWidth > 0.0f && sourceHeight > 0.0f)
		? std::max(sourceWidth / sourceHeight, 0.0001f)
		: kDefaultPreviewAspect;
	const float previewHeight = drawWidth / aspect;
	return std::max(
		220.0f,
		titleBarHeight + style.WindowPadding.y * 2.0f + previewHeight + kPreviewInnerBorder * 2.0f);
}

std::vector<int> sortedDescending(const std::set<int> & selection) {
	std::vector<int> out(selection.begin(), selection.end());
	std::sort(out.rbegin(), out.rend());
	return out;
}

bool isValidPlaylistIndex(const std::vector<std::string> & playlist, int index) {
	return index >= 0 && index < static_cast<int>(playlist.size());
}

int remapIndexAfterMove(int index, int fromIndex, int movedTo) {
	if (index < 0) {
		return index;
	}

	if (index == fromIndex) {
		return movedTo;
	}

	if (fromIndex < movedTo) {
		if (index > fromIndex && index <= movedTo) {
			return index - 1;
		}
		return index;
	}

	if (index >= movedTo && index < fromIndex) {
		return index + 1;
	}

	return index;
}

void movePlaylistItemAndSelection(
	ofxVlc4Player & player,
	int fromIndex,
	int insertIndex,
	std::set<int> & selectedIndices,
	int & selectedIndex,
	int & lastClickedIndex) {
	if (fromIndex == insertIndex || fromIndex + 1 == insertIndex) {
		return;
	}

	int movedTo = insertIndex;
	if (fromIndex < insertIndex) {
		movedTo--;
	}

	player.movePlaylistItem(fromIndex, insertIndex);

	std::set<int> remappedSelection;
	for (int idx : selectedIndices) {
		remappedSelection.insert(remapIndexAfterMove(idx, fromIndex, movedTo));
	}
	selectedIndices = std::move(remappedSelection);
	selectedIndex = remapIndexAfterMove(selectedIndex, fromIndex, movedTo);
	lastClickedIndex = remapIndexAfterMove(lastClickedIndex, fromIndex, movedTo);
}

int computeInsertIndexForDrag(int hoveredRow, int grabbedIndex) {
	return (grabbedIndex < hoveredRow) ? (hoveredRow + 1) : hoveredRow;
}
}

void ofVlcPlayer4Gui::setup() {
	gui.setup(nullptr, false, ImGuiConfigFlags_ViewportsEnable, true);
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
}

void ofVlcPlayer4Gui::draw(
	ofxVlc4Player & player,
	ofxProjectM & projectM,
	bool projectMInitialized,
	const ofTexture & videoPreviewTexture,
	float videoPreviewWidth,
	float videoPreviewHeight,
	const std::function<int(const std::string &)> & addPathToPlaylist,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture) {
	gui.begin();
	drawImGui(
		player,
		projectM,
		projectMInitialized,
		videoPreviewTexture,
		videoPreviewWidth,
		videoPreviewHeight,
		addPathToPlaylist,
		randomProjectMPreset,
		reloadProjectMPresets,
		reloadProjectMTextures,
		loadPlayerProjectMTexture,
		loadCustomProjectMTexture);
	gui.end();
	gui.draw();
}

void ofVlcPlayer4Gui::drawImGui(
	ofxVlc4Player & player,
	ofxProjectM & projectM,
	bool projectMInitialized,
	const ofTexture & videoPreviewTexture,
	float videoPreviewWidth,
	float videoPreviewHeight,
	const std::function<int(const std::string &)> & addPathToPlaylist,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture) {
	if (showDisplayFullscreen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
		showDisplayFullscreen = false;
	}

	const float remoteWindowWidth = 520.0f;
	ImGui::SetNextWindowPos(ImVec2(kRemoteWindowX, kRemoteWindowY), ImGuiCond_Once);
	ImGui::SetNextWindowSizeConstraints(
		ImVec2(remoteWindowWidth, 0.0f),
		ImVec2(remoteWindowWidth, std::numeric_limits<float>::max()));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kWindowBorderSize);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 7.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.5f, 0.5f));

	ImGui::PushStyleColor(ImGuiCol_WindowBg, kUiBg);
	ImGui::PushStyleColor(ImGuiCol_TitleBg, kUiTitleBg);
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kUiTitleBgActive);
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kUiTitleBg);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kUiChildBg);
	ImGui::PushStyleColor(ImGuiCol_Border, kUiBorder);
	ImGui::PushStyleColor(ImGuiCol_Separator, kUiBorder);
	ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, kUiBorder);
	ImGui::PushStyleColor(ImGuiCol_SeparatorActive, kUiBorder);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, kUiFrame);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kUiFrameHover);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kUiFrameActive);
	ImGui::PushStyleColor(ImGuiCol_Button, kUiAccent);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kUiAccentHover);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, kUiAccentActive);
	ImGui::PushStyleColor(ImGuiCol_SliderGrab, kUiAccentBright);
	ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, kUiAccentActive);
	ImGui::PushStyleColor(ImGuiCol_CheckMark, kUiAccentBright);
	ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, kUiAccent);
	ImGui::PushStyleColor(ImGuiCol_Header, kUiAccent);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kUiAccentHover);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, kUiAccentActive);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kOuterWindowPadding, kOuterWindowPadding));
	ImGui::Begin("ofxVlc4Player", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
	handleImGuiShortcuts(player, projectMInitialized, randomProjectMPreset);
	const bool hasPlaylist = !player.getPlaylist().empty();
	const MediaDisplayState displayState = resolveMediaDisplayState(player, selectedIndex);
	drawHeaderSection(player, hasPlaylist, displayState);
	drawTransportSection(player, hasPlaylist);
	drawPositionSection(player, hasPlaylist);
	drawPlaybackOptionsSection(
		player,
		hasPlaylist,
		projectMInitialized,
		addPathToPlaylist,
		randomProjectMPreset,
		reloadProjectMPresets,
		reloadProjectMTextures,
		loadPlayerProjectMTexture,
		loadCustomProjectMTexture);
	drawMediaInfoSection(displayState);
	ImGui::End();
	ImGui::PopStyleVar();

	if (showVideoWindow || showProjectMWindow || showDisplayFullscreen) {
		drawVisualWindows(
			projectM,
			projectMInitialized,
			videoPreviewTexture,
			videoPreviewWidth,
			videoPreviewHeight);
	}

	ImGui::PopStyleColor(22);
	ImGui::PopStyleVar(8);
}

void ofVlcPlayer4Gui::drawHeaderSection(
	ofxVlc4Player & player,
	bool hasPlaylist,
	const MediaDisplayState & mediaDisplayState) {
	std::string currentTitle = "Nothing loaded";
	const auto selectedTitleIt = std::find_if(
		mediaDisplayState.metadata.begin(),
		mediaDisplayState.metadata.end(),
		[](const auto & entry) { return entry.first == "Title" && !entry.second.empty(); });
	const std::string displayName = selectedTitleIt != mediaDisplayState.metadata.end()
		? selectedTitleIt->second
		: mediaDisplayState.fileName;
	if (!displayName.empty()) {
		currentTitle = "Media: " + displayName;
	}

	const std::string timeText = buildTimeText(player, showRemainingTime);

	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
	ImGui::TextUnformatted(currentTitle.c_str());
	if (!mediaDisplayState.path.empty() && ImGui::IsItemClicked(0)) {
		ImGui::SetClipboardText(mediaDisplayState.path.c_str());
	}
	if (!mediaDisplayState.path.empty() && ImGui::IsItemHovered()) {
		drawCurrentMediaCopyTooltip();
	}
	ImGui::PopTextWrapPos();
	ImGui::TextColored(
		ImVec4(0.68f, 0.68f, 0.70f, 1.0f),
		hasPlaylist ? timeText.c_str() : "Drop files or paste a media path");
	if (hasPlaylist && ImGui::IsItemClicked(0)) {
		showRemainingTime = !showRemainingTime;
	}
	ImGui::Separator();
}

void ofVlcPlayer4Gui::drawTransportSection(ofxVlc4Player & player, bool hasPlaylist) {
	const float controlWidth = (ImGui::GetContentRegionAvail().x - 4.0f * kButtonSpacing) / 5.0f;
	ImGui::BeginDisabled(!hasPlaylist);
	if (ImGui::Button("Play", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		if (selectedIndex >= 0 && selectedIndex != player.getCurrentIndex()) {
			player.playIndex(selectedIndex);
		} else {
			player.play();
		}
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Pause", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		player.pause();
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Stop", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		player.stop();
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Prev", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		player.previousMediaListItem();
	}

	ImGui::SameLine(0.0f, kButtonSpacing);
	if (ImGui::Button("Next", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		player.nextMediaListItem();
	}
	ImGui::EndDisabled();
}

void ofVlcPlayer4Gui::drawPositionSection(ofxVlc4Player & player, bool hasPlaylist) {
	const float actualPosition = hasPlaylist ? player.getPosition() : 0.0f;
	const bool canSeek = hasPlaylist && player.isSeekable();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Position");
	ImGui::SameLine();

	const ImVec2 seekBarSize(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
	ImGui::InvisibleButton("##Position", seekBarSize);
	const bool sliderActive = canSeek && ImGui::IsItemActive();
	const bool seekReleased = positionSliderActive && !sliderActive;

	const ImVec2 seekMin = ImGui::GetItemRectMin();
	const ImVec2 seekMax = ImGui::GetItemRectMax();
	const float seekWidth = seekMax.x - seekMin.x;
	const bool seekHovered = canSeek && ImGui::IsItemHovered();
	const ImVec2 mousePos = ImGui::GetIO().MousePos;
	const bool releasedInsideSeek =
		mousePos.x >= seekMin.x && mousePos.x <= seekMax.x &&
		mousePos.y >= seekMin.y && mousePos.y <= seekMax.y;
	if (sliderActive && seekWidth > 0.0f) {
		const float normalizedTarget = std::clamp((ImGui::GetIO().MousePos.x - seekMin.x) / seekWidth, 0.0f, 1.0f);
		pendingSeekPosition = normalizedTarget;
	}
	if (seekReleased && canSeek && releasedInsideSeek) {
		player.setPosition(pendingSeekPosition);
	}
	positionSliderActive = sliderActive;

	auto * drawList = ImGui::GetWindowDrawList();
	const float knobX = seekMin.x + seekWidth * std::clamp(actualPosition, 0.0f, 1.0f);
	const ImVec4 seekTrackColor = sliderActive ? kUiFrameActive : (seekHovered ? kUiFrameHover : kUiFrame);
	const ImVec4 seekFillColor = sliderActive ? kUiAccentActive : (seekHovered ? kUiAccentHover : kUiAccent);
	drawList->AddRectFilled(seekMin, seekMax, ImGui::GetColorU32(seekTrackColor), 0.0f);
	drawList->AddRectFilled(seekMin, ImVec2(knobX, seekMax.y), ImGui::GetColorU32(seekFillColor), 0.0f);
	ImGui::PopStyleVar();
}

void ofVlcPlayer4Gui::drawPlaybackOptionsSection(
	ofxVlc4Player & player,
	bool hasPlaylist,
	bool projectMInitialized,
	const std::function<int(const std::string &)> & addPathToPlaylist,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture) {
	static const char * fullscreenSources[] = { "Video", "projectM" };

	volume = player.currentVolume.load();
	if (volume > 0) {
		lastNonZeroVolume = volume;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(140.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kUiFrameHover);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kUiFrameActive);
	if (ImGui::SliderInt("Volume", &volume, 0, 100)) {
		player.setVolume(volume);
		if (volume > 0) {
			lastNonZeroVolume = volume;
		}
	}
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
		const auto & io = ImGui::GetIO();
		const float wheel = io.MouseWheel;
		if (wheel != 0.0f) {
			const int step = io.KeyCtrl ? 1 : 5;
			const int direction = (wheel > 0.0f) ? 1 : -1;
			volume = ofClamp(volume + direction * step, 0, 100);
			player.setVolume(volume);
			if (volume > 0) {
				lastNonZeroVolume = volume;
			}
		}
	}
	ImGui::PopStyleColor(2);
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
	ImGui::SameLine(0.0f, kSectionSpacing);

	int playbackModeIndex = 0;
	auto mode = player.getPlaybackMode();
	if (mode == ofxVlc4Player::PlaybackMode::Repeat) {
		playbackModeIndex = 1;
	} else if (mode == ofxVlc4Player::PlaybackMode::Loop) {
		playbackModeIndex = 2;
	} else {
		playbackModeIndex = 0;
	}

	const char * playbackModes[] = { "Default", "Repeat", "Loop" };
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(108.0f);
	if (ImGui::Combo("Mode", &playbackModeIndex, playbackModes, IM_ARRAYSIZE(playbackModes))) {
		player.setPlaybackMode(static_cast<ofxVlc4Player::PlaybackMode>(playbackModeIndex));
	}
	if (applyHoveredWheelStep(playbackModeIndex, 0, IM_ARRAYSIZE(playbackModes) - 1)) {
		player.setPlaybackMode(static_cast<ofxVlc4Player::PlaybackMode>(playbackModeIndex));
	}
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();

	ImGui::SameLine(0.0f, kSectionSpacing);
	bool shuffle = player.isShuffleEnabled();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	if (ImGui::Checkbox("Shuffle", &shuffle)) {
		player.setShuffleEnabled(shuffle);
	}
	ImGui::PopStyleVar();
	ImGui::Separator();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::Checkbox("Video Window", &showVideoWindow);
	ImGui::SameLine(0.0f, kSectionSpacing);
	ImGui::Checkbox("projectM Window", &showProjectMWindow);
	ImGui::PopStyleVar();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::Checkbox("Fullscreen", &showDisplayFullscreen);
	ImGui::SameLine(0.0f, kSectionSpacing);
	ImGui::PushItemWidth(126.0f);
	ImGui::Combo("Source", &fullscreenDisplaySource, fullscreenSources, IM_ARRAYSIZE(fullscreenSources));
	applyHoveredWheelStep(fullscreenDisplaySource, 0, IM_ARRAYSIZE(fullscreenSources) - 1);
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
	ImGui::Separator();

	drawEqualizerSection(player);
	drawVideoAdjustmentsSection(player);
	drawVideo3DSection(player);

	if (ImGui::CollapsingHeader("Playlist", ImGuiTreeNodeFlags_DefaultOpen)) {
		drawPlaylistSection(player);
		drawPathSection(player, hasPlaylist, addPathToPlaylist);
	}

	if (ImGui::CollapsingHeader("projectM")) {
		ImGui::BeginDisabled(!projectMInitialized);
		if (ImGui::Button("Random Preset", ImVec2(kActionButtonWidth, 0.0f))) {
			randomProjectMPreset();
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Reload Presets", ImVec2(kActionButtonWidth, 0.0f))) {
			reloadProjectMPresets();
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Reload Textures", ImVec2(kActionButtonWidth, 0.0f))) {
			reloadProjectMTextures();
		}
		ImGui::EndDisabled();

		ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
		ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - kInputLabelPadding);
		const bool submittedTexturePath = ImGui::InputText(
			"Path##projectMTexture",
			projectMTexturePath,
			sizeof(projectMTexturePath),
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();

		const bool loadTextureRequested =
			ImGui::Button("Load Image Texture", ImVec2(kDualActionButtonWidth, 0.0f)) || submittedTexturePath;
		if (loadTextureRequested && loadCustomProjectMTexture(projectMTexturePath)) {
			projectMTexturePath[0] = '\0';
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Load Video Texture", ImVec2(kDualActionButtonWidth, 0.0f))) {
			loadPlayerProjectMTexture();
		}
	}

	if (ImGui::CollapsingHeader("Legend")) {
		ImGui::TextUnformatted("Space  Play / Pause");
		ImGui::TextUnformatted("S      Stop");
		ImGui::TextUnformatted("N      Next");
		ImGui::TextUnformatted("P      Previous");
		ImGui::TextUnformatted("Left   Previous");
		ImGui::TextUnformatted("Right  Next");
		ImGui::TextUnformatted("M      Mute");
		ImGui::TextUnformatted("F      Toggle Fullscreen");
		ImGui::TextUnformatted("+/-    Volume");
		ImGui::TextUnformatted("R      Random projectM preset");
		ImGui::TextUnformatted("Del    Delete selected playlist items");
	}
}

void ofVlcPlayer4Gui::drawEqualizerSection(ofxVlc4Player & player) {
	if (!ImGui::CollapsingHeader("Equalizer")) {
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(220.0f);

	const int bandCount = player.getEqualizerBandCount();
	if (bandCount <= 0) {
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();
		return;
	}

	std::vector<float> bandFrequencies;
	std::vector<float> bandAmps;
	bandFrequencies.reserve(static_cast<size_t>(bandCount));
	bandAmps.reserve(static_cast<size_t>(bandCount));
	for (int bandIndex = 0; bandIndex < bandCount; ++bandIndex) {
		bandFrequencies.push_back(player.getEqualizerBandFrequency(bandIndex));
		bandAmps.push_back(player.getEqualizerBandAmp(bandIndex));
	}

	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::BeginCombo("Bands", equalizerBandLayoutLabel(equalizerBandLayout))) {
		for (EqualizerBandLayout layout : kEqualizerBandLayouts) {
			const bool selected = (equalizerBandLayout == layout);
			if (ImGui::Selectable(equalizerBandLayoutLabel(layout), selected)) {
				equalizerBandLayout = layout;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	int bandLayoutIndex = static_cast<int>(equalizerBandLayout);
	if (applyHoveredWheelStep(bandLayoutIndex, 0, static_cast<int>(kEqualizerBandLayouts.size()) - 1)) {
		equalizerBandLayout = kEqualizerBandLayouts[static_cast<size_t>(bandLayoutIndex)];
	}

	const std::vector<EqualizerBandHandle> visibleBandHandles = buildEqualizerBandHandles(bandFrequencies, equalizerBandLayout);
	if (visibleBandHandles.empty()) {
		ImGui::PopItemWidth();
		ImGui::PopStyleVar();
		return;
	}

	float preamp = player.getEqualizerPreamp();
	ImGui::SetNextItemWidth(220.0f);
	if (ImGui::SliderFloat("Preamp", &preamp, -20.0f, 20.0f, "%.1f dB")) {
		player.setEqualizerPreamp(preamp);
	}
	if (applyHoveredWheelStep(preamp, -20.0f, 20.0f, 0.5f)) {
		player.setEqualizerPreamp(preamp);
	}

	if (ImGui::Button("Reset", ImVec2(kActionButtonWidth, 0.0f))) {
		player.resetEqualizer();
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(160.0f);
	if (ImGui::BeginCombo("Analyzer", analyzerDisplayStyleLabel(analyzerDisplayStyle))) {
		for (AnalyzerDisplayStyle style : kAnalyzerDisplayStyles) {
			const bool selected = (analyzerDisplayStyle == style);
			if (ImGui::Selectable(analyzerDisplayStyleLabel(style), selected)) {
				analyzerDisplayStyle = style;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	int analyzerStyleIndex = static_cast<int>(analyzerDisplayStyle);
	if (applyHoveredWheelStep(analyzerStyleIndex, 0, static_cast<int>(kAnalyzerDisplayStyles.size()) - 1)) {
		analyzerDisplayStyle = kAnalyzerDisplayStyles[static_cast<size_t>(analyzerStyleIndex)];
	}

	const ImVec2 graphSize(ImGui::GetContentRegionAvail().x, 170.0f);
	const ImVec2 graphPos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##equalizerGraph", graphSize);

	ImDrawList * drawList = ImGui::GetWindowDrawList();
	const ImVec2 graphMin = graphPos;
	const ImVec2 graphMax(graphPos.x + graphSize.x, graphPos.y + graphSize.y);
	const float graphWidth = std::max(1.0f, graphSize.x);
	const float graphHeight = std::max(1.0f, graphSize.y);
	const float minDb = -20.0f;
	const float maxDb = 20.0f;
	const std::vector<float> spectrumLevels = player.getEqualizerSpectrumLevels(kEqualizerSpectrumPointCount);
	const float minEqFrequency = std::max(bandFrequencies.front(), 20.0f);
	const float maxEqFrequency = std::max(
		bandFrequencies.back(),
		minEqFrequency * 2.0f);
	const auto frequencyToGraphXT = [&](float frequencyHz) {
		const float clampedFrequency = std::clamp(frequencyHz, minEqFrequency, maxEqFrequency);
		if (maxEqFrequency <= minEqFrequency) {
			return 0.5f;
		}

		return std::clamp(
			std::log(clampedFrequency / minEqFrequency) / std::log(maxEqFrequency / minEqFrequency),
			0.0f,
			1.0f);
	};
	const auto ampToGraphYT = [&](float amp) {
		return std::clamp(1.0f - ((amp - minDb) / (maxDb - minDb)), 0.0f, 1.0f);
	};

	drawList->AddRectFilled(graphMin, graphMax, ImGui::GetColorU32(kUiChildBg));
	drawList->AddRect(graphMin, graphMax, ImGui::GetColorU32(kUiBorder), 0.0f, 0, 1.0f);

	for (int line = 1; line < 4; ++line) {
		const float t = static_cast<float>(line) / 4.0f;
		const float y = graphMin.y + graphHeight * t;
		drawList->AddLine(
			ImVec2(graphMin.x, y),
			ImVec2(graphMax.x, y),
			ImGui::GetColorU32(kUiFrame));
	}

	for (float guideFrequencyHz : kEqualizerGuideFrequenciesHz) {
		if (guideFrequencyHz <= minEqFrequency || guideFrequencyHz >= maxEqFrequency) {
			continue;
		}

		const float x = graphMin.x + (graphWidth * frequencyToGraphXT(guideFrequencyHz));
		drawList->AddLine(
			ImVec2(x, graphMin.y),
			ImVec2(x, graphMax.y),
			ImGui::GetColorU32(ImVec4(kUiFrame.x, kUiFrame.y, kUiFrame.z, 0.75f)));
	}

	for (float dbMarker : kEqualizerDbLabelMarkers) {
		const float y = graphMin.y + (graphHeight * ampToGraphYT(dbMarker));
		char label[8];
		std::snprintf(label, sizeof(label), dbMarker > 0.0f ? "+%.0f" : "%.0f", dbMarker);
		drawList->AddText(
			ImVec2(graphMin.x + 6.0f, y - ImGui::GetFontSize() * 0.5f),
			ImGui::GetColorU32(kUiBorder),
			label);
	}

	std::vector<ImVec2> spectrumPoints;
	spectrumPoints.reserve(spectrumLevels.size());
	for (int i = 0; i < static_cast<int>(spectrumLevels.size()); ++i) {
		const float xT = spectrumLevels.size() <= 1
			? 0.5f
			: static_cast<float>(i) / static_cast<float>(spectrumLevels.size() - 1);
		const float level = std::clamp(spectrumLevels[static_cast<size_t>(i)], 0.0f, 1.0f);

		spectrumPoints.emplace_back(
			graphMin.x + graphWidth * xT,
			graphMax.y - graphHeight * level);
	}

	const float nowSeconds = static_cast<float>(ImGui::GetTime());
	float deltaSeconds = 1.0f / 60.0f;
	if (lastAnalyzerPeakUpdateTime > 0.0) {
		deltaSeconds = std::clamp(static_cast<float>(nowSeconds - lastAnalyzerPeakUpdateTime), 1.0f / 240.0f, 0.25f);
	}
	lastAnalyzerPeakUpdateTime = nowSeconds;

	if (analyzerPeakHoldLevels.size() != spectrumLevels.size() ||
		analyzerPeakHoldTimers.size() != spectrumLevels.size()) {
		analyzerPeakHoldLevels = spectrumLevels;
		analyzerPeakHoldTimers.assign(spectrumLevels.size(), kAnalyzerPeakHoldSeconds);
	} else {
		for (size_t i = 0; i < spectrumLevels.size(); ++i) {
			if (spectrumLevels[i] >= analyzerPeakHoldLevels[i]) {
				analyzerPeakHoldLevels[i] = spectrumLevels[i];
				analyzerPeakHoldTimers[i] = kAnalyzerPeakHoldSeconds;
			} else if (analyzerPeakHoldTimers[i] > 0.0f) {
				analyzerPeakHoldTimers[i] = std::max(0.0f, analyzerPeakHoldTimers[i] - deltaSeconds);
			} else {
				analyzerPeakHoldLevels[i] = std::max(
					spectrumLevels[i],
					analyzerPeakHoldLevels[i] - (kAnalyzerPeakReleasePerSecond * deltaSeconds));
			}
		}
	}

	std::vector<ImVec2> peakHoldPoints;
	peakHoldPoints.reserve(analyzerPeakHoldLevels.size());
	for (int i = 0; i < static_cast<int>(analyzerPeakHoldLevels.size()); ++i) {
		const float xT = analyzerPeakHoldLevels.size() <= 1
			? 0.5f
			: static_cast<float>(i) / static_cast<float>(analyzerPeakHoldLevels.size() - 1);
		const float level = std::clamp(analyzerPeakHoldLevels[static_cast<size_t>(i)], 0.0f, 1.0f);
		peakHoldPoints.emplace_back(
			graphMin.x + graphWidth * xT,
			graphMax.y - graphHeight * level);
	}

	if (spectrumPoints.size() >= 2) {
		const auto drawSpectrumLine = [&]() {
			drawList->AddPolyline(
				spectrumPoints.data(),
				static_cast<int>(spectrumPoints.size()),
				ImGui::GetColorU32(kUiAnalyzerLine),
				ImDrawFlags_None,
				1.5f);
		};
		const auto drawSpectrumFill = [&]() {
			for (size_t i = 1; i < spectrumPoints.size(); ++i) {
				const ImVec2 & left = spectrumPoints[i - 1];
				const ImVec2 & right = spectrumPoints[i];
				drawList->AddQuadFilled(
					ImVec2(left.x, graphMax.y),
					left,
					right,
					ImVec2(right.x, graphMax.y),
					ImGui::GetColorU32(kUiAnalyzerFill));
			}
		};
		const auto drawPeakHoldLine = [&]() {
			if (peakHoldPoints.size() < 2) {
				return;
			}

			drawList->AddPolyline(
				peakHoldPoints.data(),
				static_cast<int>(peakHoldPoints.size()),
				ImGui::GetColorU32(kUiAnalyzerPeak),
				ImDrawFlags_None,
				1.0f);
		};
		const auto drawSpectrumBars = [&](bool drawBarPeakHold) {
			const int barCount = std::min(kAnalyzerBarCount, static_cast<int>(spectrumLevels.size()));
			if (barCount <= 0) {
				return;
			}

			const float step = static_cast<float>(spectrumLevels.size()) / static_cast<float>(barCount);
			const float gap = 1.0f;
			for (int barIndex = 0; barIndex < barCount; ++barIndex) {
				const int start = static_cast<int>(std::floor(step * static_cast<float>(barIndex)));
				const int end = std::min(
					static_cast<int>(spectrumLevels.size()),
					static_cast<int>(std::floor(step * static_cast<float>(barIndex + 1))));
				if (end <= start) {
					continue;
				}

				float peakLevel = 0.0f;
				for (int sampleIndex = start; sampleIndex < end; ++sampleIndex) {
					peakLevel = std::max(peakLevel, spectrumLevels[static_cast<size_t>(sampleIndex)]);
				}

				const float x0 = graphMin.x + (graphWidth * static_cast<float>(barIndex) / static_cast<float>(barCount));
				const float x1 = graphMin.x + (graphWidth * static_cast<float>(barIndex + 1) / static_cast<float>(barCount));
				const float topY = graphMax.y - (graphHeight * std::clamp(peakLevel, 0.0f, 1.0f));
				drawList->AddRectFilled(
					ImVec2(x0 + gap, topY),
					ImVec2(std::max(x0 + gap, x1 - gap), graphMax.y),
					ImGui::GetColorU32(kUiAnalyzerFill));

				if (drawBarPeakHold && barIndex < static_cast<int>(analyzerPeakHoldLevels.size())) {
					const int peakSample = std::min(
						static_cast<int>(analyzerPeakHoldLevels.size()) - 1,
						static_cast<int>(std::round((static_cast<float>(barIndex) / static_cast<float>(std::max(barCount - 1, 1))) *
							static_cast<float>(analyzerPeakHoldLevels.size() - 1))));
					const float peakLevel = std::clamp(analyzerPeakHoldLevels[static_cast<size_t>(peakSample)], 0.0f, 1.0f);
					const float peakY = graphMax.y - (graphHeight * peakLevel);
					drawList->AddLine(
						ImVec2(x0 + gap, peakY),
						ImVec2(std::max(x0 + gap, x1 - gap), peakY),
						ImGui::GetColorU32(kUiAnalyzerPeak),
						1.0f);
				}
			}
		};

		switch (analyzerDisplayStyle) {
		case AnalyzerDisplayStyle::Mastering:
			drawSpectrumLine();
			drawPeakHoldLine();
			break;
		case AnalyzerDisplayStyle::RtaBars:
			drawSpectrumBars(true);
			break;
		case AnalyzerDisplayStyle::Hybrid:
			drawSpectrumBars(false);
			drawSpectrumLine();
			drawPeakHoldLine();
			break;
		case AnalyzerDisplayStyle::Studio:
		default:
			drawSpectrumFill();
			drawSpectrumLine();
			break;
		}
	}

	std::vector<float> displayCurve = buildEqualizerDisplayCurve(
		bandFrequencies,
		bandAmps,
		kEqualizerResponseSampleCount,
		minEqFrequency,
		maxEqFrequency);
	std::vector<float> handleAmps(visibleBandHandles.size(), 0.0f);
	for (size_t i = 0; i < visibleBandHandles.size(); ++i) {
		handleAmps[i] = getEqualizerHandleAmp(bandAmps, visibleBandHandles[i]);
	}

	std::vector<ImVec2> points;
	points.reserve(visibleBandHandles.size());
	for (int i = 0; i < static_cast<int>(visibleBandHandles.size()); ++i) {
		const float xT = frequencyToGraphXT(visibleBandHandles[static_cast<size_t>(i)].targetFrequencyHz);
		const float yT = ampToGraphYT(handleAmps[static_cast<size_t>(i)]);
		points.emplace_back(
			graphMin.x + graphWidth * xT,
			graphMin.y + graphHeight * yT);
	}

	std::vector<ImVec2> curvePoints;
	curvePoints.reserve(displayCurve.size());
	for (size_t sampleIndex = 0; sampleIndex < displayCurve.size(); ++sampleIndex) {
		const float t = displayCurve.size() <= 1
			? 0.0f
			: static_cast<float>(sampleIndex) / static_cast<float>(displayCurve.size() - 1);
		curvePoints.emplace_back(
			graphMin.x + (graphWidth * t),
			graphMin.y + (graphHeight * ampToGraphYT(displayCurve[sampleIndex])));
	}
	if (curvePoints.size() < 2) {
		curvePoints = points;
	}

	const std::vector<ImVec2> handleLinePoints = buildAnchoredPolyline(points, graphMin.x, graphMax.x);

	if (curvePoints.size() >= 2) {
		drawList->AddPolyline(
			curvePoints.data(),
			static_cast<int>(curvePoints.size()),
			ImGui::GetColorU32(kUiEqualizerCurve),
			ImDrawFlags_None,
			2.0f);
	}
	if (handleLinePoints.size() >= 2) {
		drawList->AddPolyline(
			handleLinePoints.data(),
			static_cast<int>(handleLinePoints.size()),
			ImGui::GetColorU32(kUiEqualizerHandleLine),
			ImDrawFlags_None,
			1.0f);
	}

	static int activeEqualizerBand = -1;
	static bool skipDragUntilMouseRelease = false;
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		skipDragUntilMouseRelease = false;
		activeEqualizerBand = findNearestPointIndex(points, ImGui::GetIO().MousePos);
	}
	if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		const int resetBand = findNearestPointIndex(points, ImGui::GetIO().MousePos);
		if (resetBand >= 0) {
			setEqualizerHandleAmp(player, visibleBandHandles[static_cast<size_t>(resetBand)], 0.0f);
			activeEqualizerBand = resetBand;
			skipDragUntilMouseRelease = true;
		}
	}

	if (active && !skipDragUntilMouseRelease &&
		activeEqualizerBand >= 0 && activeEqualizerBand < static_cast<int>(visibleBandHandles.size())) {
		const float mouseXT = std::clamp((ImGui::GetIO().MousePos.x - graphMin.x) / graphWidth, 0.0f, 1.0f);
		const float mouseY = std::clamp(ImGui::GetIO().MousePos.y, graphMin.y, graphMax.y);
		const float mouseYT = std::clamp((mouseY - graphMin.y) / graphHeight, 0.0f, 1.0f);
		int nearestBand = 0;
		float nearestDistance = std::numeric_limits<float>::max();
		for (int i = 0; i < static_cast<int>(points.size()); ++i) {
			const float pointXT = frequencyToGraphXT(visibleBandHandles[static_cast<size_t>(i)].targetFrequencyHz);
			const float distance = std::abs(mouseXT - pointXT);
			if (distance < nearestDistance) {
				nearestDistance = distance;
				nearestBand = i;
			}
		}

		activeEqualizerBand = nearestBand;
		const float amp = ofMap(mouseYT, 1.0f, 0.0f, minDb, maxDb, true);
		setEqualizerHandleAmp(player, visibleBandHandles[static_cast<size_t>(activeEqualizerBand)], amp);
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		activeEqualizerBand = -1;
		skipDragUntilMouseRelease = false;
	}

	int hoveredBand = -1;
	if (hovered) {
		hoveredBand = findNearestPointIndex(points, ImGui::GetIO().MousePos);
	}

	const float baseHandleRadius = visibleBandHandles.size() >= 24
		? 2.6f
		: (visibleBandHandles.size() >= 16 ? 3.2f : 4.5f);
	for (int i = 0; i < static_cast<int>(points.size()); ++i) {
		const bool highlighted = (i == activeEqualizerBand) || (i == hoveredBand);
		const float handleRadius = highlighted ? (baseHandleRadius + 1.0f) : baseHandleRadius;
		drawList->AddCircleFilled(
			points[i],
			handleRadius,
			ImGui::GetColorU32(highlighted ? kUiAccentBright : kUiAccent));
		drawList->AddCircle(
			points[i],
			handleRadius,
			ImGui::GetColorU32(kUiBg),
			0,
			1.0f);
	}

	if (hoveredBand >= 0 && hoveredBand < static_cast<int>(visibleBandHandles.size()) && ImGui::BeginTooltip()) {
		const EqualizerBandHandle & handle = visibleBandHandles[static_cast<size_t>(hoveredBand)];
		ImGui::TextUnformatted(formatEqualizerFrequency(handle.targetFrequencyHz).c_str());
		ImGui::Text("%.1f dB", getEqualizerHandleAmp(player, handle));
		if (handle.lowerBandIndex != handle.upperBandIndex) {
			ImGui::Separator();
			ImGui::TextDisabled(
				"blend: %s / %s",
				formatEqualizerFrequency(bandFrequencies[static_cast<size_t>(handle.lowerBandIndex)]).c_str(),
				formatEqualizerFrequency(bandFrequencies[static_cast<size_t>(handle.upperBandIndex)]).c_str());
		}
		ImGui::EndTooltip();
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4Gui::drawVideoAdjustmentsSection(ofxVlc4Player & player) {
	if (!ImGui::CollapsingHeader("Video Adjustments")) {
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(220.0f);

	const auto drawAdjustmentSlider = [&](const char * label,
									  float & value,
									  float minValue,
									  float maxValue,
									  const char * format,
									  float wheelStep,
									  const std::function<void(float)> & applyValue) {
		if (ImGui::SliderFloat(label, &value, minValue, maxValue, format)) {
			applyValue(value);
		}
		if (applyHoveredWheelStep(value, minValue, maxValue, wheelStep)) {
			applyValue(value);
		}
	};

	bool videoAdjustmentsEnabled = player.isVideoAdjustmentsEnabled();
	if (ImGui::Checkbox("Enable", &videoAdjustmentsEnabled)) {
		player.setVideoAdjustmentsEnabled(videoAdjustmentsEnabled);
	}

	float brightness = player.getVideoBrightness();
	drawAdjustmentSlider("Brightness", brightness, 0.0f, 2.0f, "%.2f", 0.1f, [&](float value) {
		player.setVideoBrightness(value);
	});

	float contrast = player.getVideoContrast();
	drawAdjustmentSlider("Contrast", contrast, 0.0f, 2.0f, "%.2f", 0.1f, [&](float value) {
		player.setVideoContrast(value);
	});

	float saturation = player.getVideoSaturation();
	drawAdjustmentSlider("Saturation", saturation, 0.0f, 3.0f, "%.2f", 0.1f, [&](float value) {
		player.setVideoSaturation(value);
	});

	float gamma = player.getVideoGamma();
	drawAdjustmentSlider("Gamma", gamma, 0.5f, 2.5f, "%.2f", 0.1f, [&](float value) {
		player.setVideoGamma(value);
	});

	float hue = player.getVideoHue();
	if (hue > 180.0f) {
		hue -= 360.0f;
	}
	drawAdjustmentSlider("Hue", hue, -180.0f, 180.0f, "%.0f deg", 5.0f, [&](float value) {
		player.setVideoHue(value);
	});

	if (ImGui::Button("Reset", ImVec2(kActionButtonWidth, 0.0f))) {
		player.resetVideoAdjustments();
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4Gui::drawVideo3DSection(ofxVlc4Player & player) {
	if (!ImGui::CollapsingHeader("3D Video")) {
		return;
	}

	static const char * projectionModes[] = {
		"Auto",
		"Rectangular",
		"360 Equirectangular",
		"Cubemap"
	};
	static const char * stereoModes[] = {
		"Auto",
		"Stereo",
		"Left Eye",
		"Right Eye",
		"Side By Side"
	};
	static const char * anaglyphColorModes[] = {
		"Red / Cyan",
		"Green / Magenta",
		"Amber / Blue"
	};

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(220.0f);

	ImGui::TextDisabled("Projection");
	int projectionIndex = static_cast<int>(player.getVideoProjectionMode()) + 1;
	if (ImGui::Combo("Projection", &projectionIndex, projectionModes, IM_ARRAYSIZE(projectionModes))) {
		player.setVideoProjectionMode(static_cast<ofxVlc4Player::VideoProjectionMode>(projectionIndex - 1));
	}
	if (applyHoveredWheelStep(projectionIndex, 0, IM_ARRAYSIZE(projectionModes) - 1)) {
		player.setVideoProjectionMode(static_cast<ofxVlc4Player::VideoProjectionMode>(projectionIndex - 1));
	}

	int stereoIndex = static_cast<int>(player.getVideoStereoMode());
	if (ImGui::Combo("Stereo", &stereoIndex, stereoModes, IM_ARRAYSIZE(stereoModes))) {
		player.setVideoStereoMode(static_cast<ofxVlc4Player::VideoStereoMode>(stereoIndex));
	}
	if (applyHoveredWheelStep(stereoIndex, 0, IM_ARRAYSIZE(stereoModes) - 1)) {
		player.setVideoStereoMode(static_cast<ofxVlc4Player::VideoStereoMode>(stereoIndex));
	}

	ImGui::Separator();
	ImGui::TextDisabled("View");
	float yaw = player.getVideoYaw();
	float pitch = player.getVideoPitch();
	float roll = player.getVideoRoll();
	float fov = player.getVideoFov();

	if (ImGui::SliderFloat("Yaw", &yaw, -180.0f, 180.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (applyHoveredWheelStep(yaw, -180.0f, 180.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ImGui::SliderFloat("Pitch", &pitch, -90.0f, 90.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (applyHoveredWheelStep(pitch, -90.0f, 90.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ImGui::SliderFloat("Roll", &roll, -180.0f, 180.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (applyHoveredWheelStep(roll, -180.0f, 180.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ImGui::SliderFloat("FOV", &fov, 1.0f, 179.0f, "%.0f deg")) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (applyHoveredWheelStep(fov, 1.0f, 179.0f, 1.0f)) {
		player.setVideoViewpoint(yaw, pitch, roll, fov);
	}
	if (ImGui::Button("Reset", ImVec2(kActionButtonWidth, 0.0f))) {
		player.resetVideoViewpoint();
	}

	ImGui::Separator();
	ImGui::TextDisabled("Anaglyph");
	ImGui::Checkbox("Anaglyph", &anaglyphEnabled);
	if (anaglyphEnabled) {
		int colorModeIndex = static_cast<int>(anaglyphColorMode);
		if (ImGui::Combo("Colors", &colorModeIndex, anaglyphColorModes, IM_ARRAYSIZE(anaglyphColorModes))) {
			anaglyphColorMode = static_cast<AnaglyphColorMode>(colorModeIndex);
		}
		if (applyHoveredWheelStep(colorModeIndex, 0, IM_ARRAYSIZE(anaglyphColorModes) - 1)) {
			anaglyphColorMode = static_cast<AnaglyphColorMode>(colorModeIndex);
		}
		ImGui::Checkbox("Swap Eyes", &anaglyphSwapEyes);
		if (ImGui::SliderFloat("Separation", &anaglyphEyeSeparation, -0.15f, 0.15f, "%.2f")) {
			anaglyphEyeSeparation = ofClamp(anaglyphEyeSeparation, -0.15f, 0.15f);
		}
		applyHoveredWheelStep(anaglyphEyeSeparation, -0.15f, 0.15f, 0.01f);
		ImGui::Separator();
	}

	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
}

void ofVlcPlayer4Gui::drawMediaInfoSection(const MediaDisplayState & mediaDisplayState) {
	if (ImGui::CollapsingHeader("Media Info")) {
		if (!mediaDisplayState.fileName.empty() || !mediaDisplayState.metadata.empty()) {
			drawCurrentMediaInfoContent(
				mediaDisplayState.path,
				mediaDisplayState.fileName,
				mediaDisplayState.metadata);
		} else {
			ImGui::TextDisabled("No media selected");
		}
	}
}

void ofVlcPlayer4Gui::handleImGuiShortcuts(
	ofxVlc4Player & player,
	bool projectMInitialized,
	const std::function<void()> & randomProjectMPreset) {
	if (ImGui::GetIO().WantTextInput) {
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
		if (player.isPlaying()) {
			player.pause();
		} else {
			followPlaybackSelectionEnabled = true;
			player.play();
		}
	}

	if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
		player.stop();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
		if (volume > 0) {
			lastNonZeroVolume = volume;
			volume = 0;
		} else {
			volume = std::max(1, lastNonZeroVolume);
		}
		player.setVolume(volume);
	}

	if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
		showDisplayFullscreen = !showDisplayFullscreen;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) {
		volume = ofClamp(volume + 5, 0, 100);
		player.setVolume(volume);
		if (volume > 0) {
			lastNonZeroVolume = volume;
		}
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) {
		volume = ofClamp(volume - 5, 0, 100);
		player.setVolume(volume);
		if (volume > 0) {
			lastNonZeroVolume = volume;
		}
	}

	if (ImGui::IsKeyPressed(ImGuiKey_N, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
		followPlaybackSelectionEnabled = true;
		player.nextMediaListItem();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_P, false) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
		followPlaybackSelectionEnabled = true;
		player.previousMediaListItem();
	}

	if (projectMInitialized && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
		randomProjectMPreset();
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
		deleteSelected(player);
	}
}

void ofVlcPlayer4Gui::drawPlaylistSection(ofxVlc4Player & player) {
	const auto & playlist = player.getPlaylist();
	const int currentPlaying = player.getCurrentIndex();
	const ImGuiPayload * activePayload = ImGui::GetDragDropPayload();
	const bool draggingPlaylistItem = activePayload && activePayload->IsDataType("PLAYLIST_INDEX");
	const auto * dragPayload = draggingPlaylistItem ? static_cast<const PlaylistDragPayload *>(activePayload->Data) : nullptr;
	const ImGuiStyle & style = ImGui::GetStyle();
	const float playlistInsetY = 5.0f;
	const float textRowHeight = ImGui::GetTextLineHeight();
	const float additionalRowHeight = textRowHeight + style.ItemSpacing.y;
	const float childPaddingY = kDisplayWindowPadding * 2.0f;
	const int visibleRowCount = std::max(1, static_cast<int>(playlist.size()));
	const float playlistRowsHeight =
		textRowHeight + (additionalRowHeight * (visibleRowCount - 1));
	const float desiredPlaylistHeight =
		childPaddingY + playlistInsetY + playlistRowsHeight - textRowHeight - style.ItemSpacing.y + 10.0f;
	const float playlistHeight = std::min(desiredPlaylistHeight, 300.0f);

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kDisplayWindowPadding, 5.0f));
	if (ImGui::BeginChild("playlist_child", ImVec2(0, playlistHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
		const float playlistInsetX = kLabelInnerSpacing.x;
		ImGui::Dummy(ImVec2(0.0f, playlistInsetY));
		static int lastScrolledToPlayingIndex = -1;
		static int liveDragIndex = -1;
		const int draggedIndex = dragPayload ? dragPayload->index : -1;
		if (!draggingPlaylistItem) {
			liveDragIndex = -1;
		} else if (liveDragIndex < 0) {
			liveDragIndex = draggedIndex;
		}

		std::vector<int> displayOrder(playlist.size());
		std::iota(displayOrder.begin(), displayOrder.end(), 0);

		for (int row = 0; row < static_cast<int>(displayOrder.size()); ++row) {
			const int i = displayOrder[row];
			const bool isSelected = selectedIndices.count(i) > 0;
			const bool isPlaying = (i == currentPlaying);
			std::string label = ofToString(row) + " - " + ofFilePath::getFileName(playlist[i]);

			ImVec4 baseColor = ImVec4(0, 0, 0, 0);
			ImVec4 hoverColor = kUiGreyHover;
			ImVec4 activeColor = kUiGreyHover;
			if (isSelected) {
				baseColor = kUiGreySelected;
			}
			if (isPlaying) {
				baseColor = kUiAccent;
				hoverColor = kUiAccentHover;
				activeColor = kUiAccentActive;
			}

			ImGui::PushStyleColor(ImGuiCol_Header, baseColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hoverColor);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, activeColor);
			if (isSelected && !isPlaying) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
			}

			const bool drawHighlighted = isSelected || isPlaying;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + playlistInsetX);
			const bool clicked = ImGui::Selectable(label.c_str(), drawHighlighted, 0, ImVec2(ImGui::GetContentRegionAvail().x - playlistInsetX, 0.0f));
			const bool doubleClicked = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
				PlaylistDragPayload newDragPayload;
				newDragPayload.index = i;
				ImGui::SetDragDropPayload("PLAYLIST_INDEX", &newDragPayload, sizeof(newDragPayload));
				ImGui::TextUnformatted("1 Item Dragged");
				ImGui::EndDragDropSource();
			}

			if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) && draggingPlaylistItem) {
				const int insertIndex = computeInsertIndexForDrag(i, liveDragIndex);
				if (liveDragIndex != insertIndex && liveDragIndex + 1 != insertIndex) {
					const int newIndex = (liveDragIndex < insertIndex) ? (insertIndex - 1) : insertIndex;
					movePlaylistItemAndSelection(
						player,
						liveDragIndex,
						insertIndex,
						selectedIndices,
						selectedIndex,
						lastClickedIndex);
					liveDragIndex = newIndex;
				}
			}

			ImGui::PushStyleColor(ImGuiCol_DragDropTarget, ImVec4(0, 0, 0, 0));
			if (ImGui::BeginDragDropTarget()) {
				if (ImGui::AcceptDragDropPayload("PLAYLIST_INDEX")) {
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopStyleColor();

			if (isPlaying && currentPlaying >= 0 && currentPlaying != lastScrolledToPlayingIndex) {
				ImGui::SetScrollHereY(0.35f);
				lastScrolledToPlayingIndex = currentPlaying;
			}

			if (clicked && !doubleClicked) {
				const bool multi = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
				const bool range = ImGui::GetIO().KeyShift;

				if (range && !playlist.empty()) {
					selectedIndices.clear();
					if (lastClickedIndex >= 0) {
						const int a = std::min(lastClickedIndex, i);
						const int b = std::max(lastClickedIndex, i);
						for (int j = a; j <= b; ++j) {
							selectedIndices.insert(j);
						}
					} else {
						selectedIndices.insert(i);
					}
					selectedIndex = i;
					lastClickedIndex = i;
					followPlaybackSelectionEnabled = false;
				} else if (multi) {
					if (selectedIndices.count(i) > 0) {
						selectedIndices.erase(i);
					} else {
						selectedIndices.insert(i);
					}
					selectedIndex = i;
					lastClickedIndex = i;
					followPlaybackSelectionEnabled = false;
				} else {
					selectedIndices.clear();
					selectedIndices.insert(i);
					selectedIndex = i;
					lastClickedIndex = i;
					followPlaybackSelectionEnabled = false;
				}
			}

			if (isSelected && !isPlaying) {
				ImGui::PopStyleColor();
			}
			ImGui::PopStyleColor(3);

			if (doubleClicked) {
				followPlaybackSelectionEnabled = false;
				player.playIndex(i);
			}
		}

		if (!playlist.empty()) {
			if (ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) && draggingPlaylistItem) {
				if (liveDragIndex >= 0 && liveDragIndex + 1 != static_cast<int>(playlist.size())) {
					movePlaylistItemAndSelection(
						player,
						liveDragIndex,
						static_cast<int>(playlist.size()),
						selectedIndices,
						selectedIndex,
						lastClickedIndex);
					liveDragIndex = static_cast<int>(playlist.size()) - 1;
				}
			}

			ImGui::PushStyleColor(ImGuiCol_DragDropTarget, ImVec4(0, 0, 0, 0));
			if (ImGui::BeginDragDropTarget()) {
				if (ImGui::AcceptDragDropPayload("PLAYLIST_INDEX")) {
				}
				ImGui::EndDragDropTarget();
			}
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

void ofVlcPlayer4Gui::drawPathSection(
	ofxVlc4Player & player,
	bool hasPlaylist,
	const std::function<int(const std::string &)> & addPathToPlaylist) {
	// Keep playlist actions grouped here so the collapsible section owns all add/delete mutations.
	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - kInputLabelPadding);
	bool submittedPath = ImGui::InputText("Path##playlistAdd", addPath, sizeof(addPath), ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
	submittedPath = ImGui::Button("Add", ImVec2(kActionButtonWidth, 0.0f)) || submittedPath;
	ImGui::SameLine(0.0f, kButtonSpacing);
	ImGui::BeginDisabled(selectedIndices.empty());
	if (ImGui::Button("Delete Selected", ImVec2(kActionButtonWidth, 0.0f)) && !selectedIndices.empty()) {
		deleteSelected(player);
	}
	ImGui::EndDisabled();
	ImGui::SameLine(0.0f, kButtonSpacing);
	ImGui::BeginDisabled(!hasPlaylist);
	if (ImGui::Button("Delete All", ImVec2(kActionButtonWidth, 0.0f)) && hasPlaylist) {
		player.clearPlaylist();
		selectedIndices.clear();
		selectedIndex = -1;
		lastClickedIndex = -1;
	}
	ImGui::EndDisabled();

	if (submittedPath) {
		if (addPathToPlaylist(addPath) > 0) {
			addPath[0] = '\0';
		}
	}
}

void ofVlcPlayer4Gui::drawVisualWindows(
	ofxProjectM & projectM,
	bool projectMInitialized,
	const ofTexture & videoPreviewTexture,
	float videoPreviewWidth,
	float videoPreviewHeight) {
	const ofTexture emptyTexture;
	const ofTexture & projectMTexture = projectMInitialized ? projectM.getTexture() : emptyTexture;
	const float projectMWidth = projectMInitialized ? static_cast<float>(projectMTexture.getWidth()) : 0.0f;
	const float projectMHeight = projectMInitialized ? static_cast<float>(projectMTexture.getHeight()) : 0.0f;
	const float videoWindowWidth = (videoPreviewWidth > 0.0f) ? videoPreviewWidth : projectMWidth;
	const float videoWindowHeight = (videoPreviewHeight > 0.0f) ? videoPreviewHeight : projectMHeight;

	const ImVec2 fullscreenPos(0.0f, 0.0f);
	const ImVec2 fullscreenSize(
		static_cast<float>(ofGetScreenWidth()),
		static_cast<float>(ofGetScreenHeight()));
	const bool fullscreenVideo = showDisplayFullscreen && fullscreenDisplaySource == 0;
	const bool fullscreenProjectM = showDisplayFullscreen && fullscreenDisplaySource == 1;
	const bool showNormalVideoWindow = showVideoWindow && !fullscreenVideo;
	const bool showNormalProjectMWindow = showProjectMWindow && !fullscreenProjectM;

	if (wasVideoFullscreen && !fullscreenVideo) {
		restoreVideoWindowPosition = true;
	}
	if (wasProjectMFullscreen && !fullscreenProjectM) {
		restoreProjectMWindowPosition = true;
	}
	wasVideoFullscreen = fullscreenVideo;
	wasProjectMFullscreen = fullscreenProjectM;

	if (showNormalVideoWindow || fullscreenVideo) {
		drawPreviewWindow(
			"Video Display",
			showVideoWindow,
			videoPreviewTexture,
			videoWindowWidth,
			videoWindowHeight,
			false,
			fullscreenVideo,
			fullscreenPos,
			fullscreenSize,
			lastVideoWindowPos,
			restoreVideoWindowPosition);
	}

	if (showNormalProjectMWindow || fullscreenProjectM) {
		drawPreviewWindow(
			"projectM Display",
			showProjectMWindow,
			projectMTexture,
			projectMWidth,
			projectMHeight,
			true,
			fullscreenProjectM,
			fullscreenPos,
			fullscreenSize,
			lastProjectMWindowPos,
			restoreProjectMWindowPosition);
	}
}

void ofVlcPlayer4Gui::deleteSelected(ofxVlc4Player & player) {
	if (selectedIndices.empty()) {
		return;
	}

	int fallbackIndex = *selectedIndices.begin();
	const auto toDelete = sortedDescending(selectedIndices);
	for (int idx : toDelete) {
		player.removeFromPlaylist(idx);
	}

	selectedIndices.clear();
	selectedIndex = fallbackIndex;
	lastClickedIndex = fallbackIndex;
	normalizeSelection(player);
}

void ofVlcPlayer4Gui::normalizeSelection(ofxVlc4Player & player) {
	const auto & playlist = player.getPlaylist();
	if (playlist.empty()) {
		selectedIndices.clear();
		selectedIndex = -1;
		lastClickedIndex = -1;
		return;
	}

	std::set<int> normalized;
	for (int idx : selectedIndices) {
		if (isValidPlaylistIndex(playlist, idx)) {
			normalized.insert(idx);
		}
	}
	selectedIndices = std::move(normalized);

	if (!isValidPlaylistIndex(playlist, selectedIndex)) {
		selectedIndex = selectedIndices.empty() ? -1 : *selectedIndices.rbegin();
	}

	if (!isValidPlaylistIndex(playlist, lastClickedIndex)) {
		lastClickedIndex = selectedIndex;
	}
}

void ofVlcPlayer4Gui::followCurrentTrack(ofxVlc4Player & player) {
	if (!followPlaybackSelectionEnabled || selectedIndices.empty()) {
		return;
	}

	const int currentPlaying = player.getCurrentIndex();
	if (!isValidPlaylistIndex(player.getPlaylist(), currentPlaying)) {
		return;
	}

	if (selectedIndices.size() <= 1 && selectedIndex != currentPlaying) {
		selectedIndices.clear();
		selectedIndices.insert(currentPlaying);
		selectedIndex = currentPlaying;
		lastClickedIndex = currentPlaying;
	}
}

void ofVlcPlayer4Gui::updateSelection(ofxVlc4Player & player) {
	normalizeSelection(player);
	followCurrentTrack(player);
}

void ofVlcPlayer4Gui::handleDragEvent(
	const ofDragInfo & dragInfo,
	ofxVlc4Player & player,
	const std::function<int(const std::string &)> & addPathToPlaylist) {
	int addedCount = 0;
	for (const auto & file : dragInfo.files) {
		addedCount += addPathToPlaylist(file.string());
	}

	if (addedCount > 0) {
		normalizeSelection(player);
	}
}

bool ofVlcPlayer4Gui::shouldRenderProjectMPreview() const {
	return showProjectMWindow || (showDisplayFullscreen && fullscreenDisplaySource == 1);
}

bool ofVlcPlayer4Gui::isAnaglyphEnabled() const {
	return anaglyphEnabled;
}

AnaglyphColorMode ofVlcPlayer4Gui::getAnaglyphColorMode() const {
	return anaglyphColorMode;
}

bool ofVlcPlayer4Gui::isAnaglyphSwapEyesEnabled() const {
	return anaglyphSwapEyes;
}

float ofVlcPlayer4Gui::getAnaglyphEyeSeparation() const {
	return anaglyphEyeSeparation;
}
