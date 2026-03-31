#include "ofVlcPlayer4Gui.h"

#include <algorithm>
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
constexpr int kVisibleEqualizerBands = 8;

struct PlaylistDragPayload {
	int index = -1;
};

void drawTexturePreview(
	const ofTexture & texture,
	float displayWidth,
	float displayHeight,
	float maxWidth,
	bool fillWindow,
	bool flipVertical);

float computePreviewWindowHeight(float sourceWidth, float sourceHeight, float windowWidth, float maxPreviewWidth);
bool isValidPlaylistIndex(const std::vector<std::string> & playlist, int index);

std::vector<int> buildVisibleEqualizerBandIndices(int bandCount) {
	std::vector<int> visibleBandIndices;
	if (bandCount <= 0) {
		return visibleBandIndices;
	}

	const int visibleBandCount = std::min(kVisibleEqualizerBands, bandCount);
	visibleBandIndices.reserve(visibleBandCount);
	if (visibleBandCount == 1) {
		visibleBandIndices.push_back(0);
		return visibleBandIndices;
	}

	for (int i = 0; i < visibleBandCount; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(visibleBandCount - 1);
		const int bandIndex = static_cast<int>(std::round(t * static_cast<float>(bandCount - 1)));
		if (visibleBandIndices.empty() || visibleBandIndices.back() != bandIndex) {
			visibleBandIndices.push_back(bandIndex);
		}
	}

	return visibleBandIndices;
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

	const int direction = (wheel > 0.0f) ? -1 : 1;
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
	const float direction = (wheel > 0.0f) ? -1.0f : 1.0f;
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
		flipVertical);
	if (!fullscreen) {
		const ImVec2 currentPos = ImGui::GetWindowPos();
		lastWindowPos = glm::vec2(currentPos.x, currentPos.y);
		restoreWindowPosition = false;
	}
	ImGui::End();

	ImGui::PopStyleVar();
}

void drawTexturePreview(
	const ofTexture & texture,
	float displayWidth,
	float displayHeight,
	float maxWidth,
	bool fillWindow = false,
	bool flipVertical = false) {
	const ImVec2 contentStart = ImGui::GetCursorPos();
	const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
	const ImVec2 screenStart = ImGui::GetCursorScreenPos();
	const ImVec2 insetRegion(
		std::max(1.0f, availableRegion.x - kPreviewInnerBorder * 2.0f),
		std::max(1.0f, availableRegion.y - kPreviewInnerBorder * 2.0f));
	ImDrawList * drawList = ImGui::GetWindowDrawList();

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
		contentStart.x + kPreviewInnerBorder + std::max(0.0f, (insetRegion.x - drawWidth) * 0.5f),
		contentStart.y + kPreviewInnerBorder + std::max(0.0f, (insetRegion.y - drawHeight) * 0.5f));
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
		ImGui::GetColorU32(kUiBorder));

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

	const int presetCount = player.getEqualizerPresetCount();
	std::string presetPreview = "Custom";
	int presetIndex = player.getEqualizerPresetIndex();
	if (presetIndex >= 0) {
		const std::string presetName = player.getEqualizerPresetName(presetIndex);
		if (!presetName.empty()) {
			presetPreview = presetName;
		}
	}

	if (ImGui::BeginCombo("Preset", presetPreview.c_str())) {
		const bool customSelected = (presetIndex < 0);
		if (ImGui::Selectable("Custom", customSelected)) {
			player.resetEqualizer();
		}

		for (int i = 0; i < presetCount; ++i) {
			const std::string presetName = player.getEqualizerPresetName(i);
			const bool selected = (presetIndex == i);
			if (ImGui::Selectable(presetName.c_str(), selected)) {
				player.applyEqualizerPreset(i);
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	int wheelPresetIndex = presetIndex;
	if (applyHoveredWheelStep(wheelPresetIndex, -1, presetCount - 1)) {
		if (wheelPresetIndex < 0) {
			player.resetEqualizer();
		} else {
			player.applyEqualizerPreset(wheelPresetIndex);
		}
		presetIndex = player.getEqualizerPresetIndex();
	}

	const int bandCount = player.getEqualizerBandCount();
	if (bandCount <= 0) {
		return;
	}

	const std::vector<int> visibleBandIndices = buildVisibleEqualizerBandIndices(bandCount);

	if (ImGui::Button("Flat", ImVec2(kActionButtonWidth, 0.0f))) {
		player.resetEqualizer();
		equalizerControlYs.assign(visibleBandIndices.size(), 0.5f);
		lastEqualizerPresetIndex = player.getEqualizerPresetIndex();
	}

	if (equalizerControlYs.size() != visibleBandIndices.size() ||
		lastEqualizerPresetIndex != player.getEqualizerPresetIndex()) {
		equalizerControlYs.resize(visibleBandIndices.size());
		for (int i = 0; i < static_cast<int>(visibleBandIndices.size()); ++i) {
			const float amp = player.getEqualizerBandAmp(visibleBandIndices[i]);
			equalizerControlYs[i] = 1.0f - ((amp - (-20.0f)) / 40.0f);
			equalizerControlYs[i] = std::clamp(equalizerControlYs[i], 0.0f, 1.0f);
		}
		lastEqualizerPresetIndex = player.getEqualizerPresetIndex();
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
	const float frequencyLabelY = graphMax.y - ImGui::GetFontSize() - 4.0f;

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

	const float zeroLineY = graphMin.y + graphHeight * 0.5f;
	drawList->AddLine(
		ImVec2(graphMin.x, zeroLineY),
		ImVec2(graphMax.x, zeroLineY),
		ImGui::GetColorU32(kUiAccentBright),
		1.0f);

	std::vector<ImVec2> points;
	points.reserve(visibleBandIndices.size());
	for (int i = 0; i < static_cast<int>(visibleBandIndices.size()); ++i) {
		const float xT = visibleBandIndices.size() <= 1
			? 0.5f
			: static_cast<float>(i) / static_cast<float>(visibleBandIndices.size() - 1);
		const float yT = std::clamp(equalizerControlYs[i], 0.0f, 1.0f);
		points.emplace_back(
			graphMin.x + graphWidth * xT,
			graphMin.y + graphHeight * yT);
	}

	if (points.size() >= 2) {
		drawList->AddPolyline(
			points.data(),
			static_cast<int>(points.size()),
			ImGui::GetColorU32(kUiAccent),
			ImDrawFlags_None,
			2.0f);
	}

	static int activeEqualizerBand = -1;
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const ImVec2 mousePos = ImGui::GetIO().MousePos;
		float bestDistance = std::numeric_limits<float>::max();
		activeEqualizerBand = -1;
		for (int i = 0; i < static_cast<int>(points.size()); ++i) {
			const float dx = mousePos.x - points[i].x;
			const float dy = mousePos.y - points[i].y;
			const float distance = std::sqrt(dx * dx + dy * dy);
			if (distance < bestDistance) {
				bestDistance = distance;
				activeEqualizerBand = i;
			}
		}
	}

	if (active && activeEqualizerBand >= 0 && activeEqualizerBand < static_cast<int>(visibleBandIndices.size())) {
		const float mouseXT = std::clamp((ImGui::GetIO().MousePos.x - graphMin.x) / graphWidth, 0.0f, 1.0f);
		const float mouseY = std::clamp(ImGui::GetIO().MousePos.y, graphMin.y, graphMax.y);
		const float mouseYT = std::clamp((mouseY - graphMin.y) / graphHeight, 0.0f, 1.0f);
		int nearestBand = 0;
		float nearestDistance = std::numeric_limits<float>::max();
		for (int i = 0; i < static_cast<int>(points.size()); ++i) {
			const float pointXT = visibleBandIndices.size() <= 1
				? 0.5f
				: static_cast<float>(i) / static_cast<float>(visibleBandIndices.size() - 1);
			const float distance = std::abs(mouseXT - pointXT);
			if (distance < nearestDistance) {
				nearestDistance = distance;
				nearestBand = i;
			}
		}

		activeEqualizerBand = nearestBand;
		equalizerControlYs[activeEqualizerBand] = mouseYT;
		const float amp = ofMap(mouseYT, 1.0f, 0.0f, minDb, maxDb, true);
		player.setEqualizerBandAmp(visibleBandIndices[activeEqualizerBand], amp);
	}

	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		activeEqualizerBand = -1;
	}

	int hoveredBand = -1;
	if (hovered) {
		const ImVec2 mousePos = ImGui::GetIO().MousePos;
		float bestDistance = std::numeric_limits<float>::max();
		for (int i = 0; i < static_cast<int>(points.size()); ++i) {
			const float dx = mousePos.x - points[i].x;
			const float dy = mousePos.y - points[i].y;
			const float distance = std::sqrt(dx * dx + dy * dy);
			if (distance < bestDistance) {
				bestDistance = distance;
				hoveredBand = i;
			}
		}
	}

	for (int i = 0; i < static_cast<int>(points.size()); ++i) {
		const bool highlighted = (i == activeEqualizerBand) || (i == hoveredBand);
		drawList->AddCircleFilled(
			points[i],
			highlighted ? 5.5f : 4.5f,
			ImGui::GetColorU32(highlighted ? kUiAccentBright : kUiAccent));
		drawList->AddCircle(
			points[i],
			highlighted ? 5.5f : 4.5f,
			ImGui::GetColorU32(kUiBg),
			0,
			1.0f);

		const std::string frequencyLabel = formatEqualizerFrequency(player.getEqualizerBandFrequency(visibleBandIndices[i]));
		const ImVec2 labelSize = ImGui::CalcTextSize(frequencyLabel.c_str());
		drawList->AddText(
			ImVec2(
				std::clamp(points[i].x - labelSize.x * 0.5f, graphMin.x + 2.0f, graphMax.x - labelSize.x - 2.0f),
				frequencyLabelY),
			ImGui::GetColorU32(highlighted ? kUiAccentBright : kUiBorder),
			frequencyLabel.c_str());
	}

	if (hoveredBand >= 0 && hoveredBand < static_cast<int>(visibleBandIndices.size()) && ImGui::BeginTooltip()) {
		const int bandIndex = visibleBandIndices[hoveredBand];
		ImGui::TextUnformatted(formatEqualizerFrequency(player.getEqualizerBandFrequency(bandIndex)).c_str());
		ImGui::Text("%.1f dB", player.getEqualizerBandAmp(bandIndex));
		ImGui::EndTooltip();
	}
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

	ImGui::TextDisabled("Projection");
	int projectionIndex = static_cast<int>(player.getVideoProjectionMode()) + 1;
	ImGui::PushItemWidth(220.0f);
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
	ImGui::PopItemWidth();

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

	ImGui::Separator();
	ImGui::TextDisabled("Anaglyph");
	ImGui::Checkbox("Anaglyph", &anaglyphEnabled);
	if (anaglyphEnabled) {
		int colorModeIndex = static_cast<int>(anaglyphColorMode);
		ImGui::PushItemWidth(220.0f);
		if (ImGui::Combo("Colors", &colorModeIndex, anaglyphColorModes, IM_ARRAYSIZE(anaglyphColorModes))) {
			anaglyphColorMode = static_cast<AnaglyphColorMode>(colorModeIndex);
		}
		if (applyHoveredWheelStep(colorModeIndex, 0, IM_ARRAYSIZE(anaglyphColorModes) - 1)) {
			anaglyphColorMode = static_cast<AnaglyphColorMode>(colorModeIndex);
		}
		ImGui::PopItemWidth();
		ImGui::Checkbox("Swap Eyes", &anaglyphSwapEyes);
		if (ImGui::SliderFloat("Separation", &anaglyphEyeSeparation, -0.15f, 0.15f, "%.2f")) {
			anaglyphEyeSeparation = ofClamp(anaglyphEyeSeparation, -0.15f, 0.15f);
		}
		applyHoveredWheelStep(anaglyphEyeSeparation, -0.15f, 0.15f, 0.01f);
	}

	if (ImGui::Button("Reset View", ImVec2(kActionButtonWidth, 0.0f))) {
		player.resetVideoViewpoint();
	}
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
