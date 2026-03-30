#include "VlcRemoteGui.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>

namespace {
constexpr float kTransportSpacing = 4.0f;
constexpr float kSectionSpacing = 14.0f;
constexpr float kButtonSpacing = 6.0f;
constexpr float kActionButtonWidth = 124.0f;
constexpr float kProjectMButtonRowWidth = (kActionButtonWidth * 3.0f) + (kButtonSpacing * 2.0f);
constexpr float kDualActionButtonWidth = (kProjectMButtonRowWidth - kButtonSpacing) * 0.5f;
constexpr float kInputLabelPadding = 60.0f;
constexpr float kWindowBorderSize = 5.0f;
const ImVec2 kLabelInnerSpacing(10.0f, 6.0f);

const ImVec4 kUiBg(0.09f, 0.09f, 0.10f, 0.98f);
const ImVec4 kUiChildBg(0.14f, 0.14f, 0.15f, 0.98f);
const ImVec4 kUiFrame(0.17f, 0.17f, 0.18f, 1.0f);
const ImVec4 kUiFrameHover(0.22f, 0.22f, 0.23f, 1.0f);
const ImVec4 kUiFrameActive(0.27f, 0.27f, 0.29f, 1.0f);
const ImVec4 kUiBorder = kUiChildBg;
const ImVec4 kUiOrange(0.95f, 0.43f, 0.10f, 0.90f);
const ImVec4 kUiOrangeHover(0.85f, 0.37f, 0.08f, 1.0f);
const ImVec4 kUiOrangeActive(0.78f, 0.30f, 0.07f, 1.0f);
const ImVec4 kUiOrangeBright(0.96f, 0.53f, 0.17f, 1.0f);
const ImVec4 kUiGreySelected(0.22f, 0.22f, 0.24f, 0.96f);
const ImVec4 kUiGreyHover(0.28f, 0.28f, 0.30f, 0.92f);
constexpr float kPreviewWindowWidth = 720.0f;
constexpr float kPreviewMaxWidth = 680.0f;
constexpr float kRemoteWindowX = 20.0f;
constexpr float kRemoteWindowY = 24.0f;

struct PlaylistDragPayload {
	int index = -1;
};

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

void drawTexturePreview(
	const ofTexture & texture,
	float displayWidth,
	float displayHeight,
	float maxWidth,
	bool fillWindow = false,
	bool flipVertical = false) {
	const ImVec2 contentStart = ImGui::GetCursorPos();
	const ImVec2 availableRegion = ImGui::GetContentRegionAvail();

	if (!texture.isAllocated() || displayWidth <= 0.0f || displayHeight <= 0.0f) {
		const char * unavailableText = "Not available";
		const float unavailableWidth = ImGui::CalcTextSize(unavailableText).x;
		const float unavailableHeight = ImGui::GetTextLineHeight();
		if (availableRegion.x > unavailableWidth || availableRegion.y > unavailableHeight) {
			ImGui::SetCursorPos(ImVec2(
				contentStart.x + std::max(0.0f, (availableRegion.x - unavailableWidth) * 0.5f),
				contentStart.y + std::max(0.0f, (availableRegion.y - unavailableHeight) * 0.5f)));
		}
		ImGui::TextDisabled("%s", unavailableText);
		return;
	}

	float drawWidth = 0.0f;
	float drawHeight = 0.0f;
	if (fillWindow) {
		drawWidth = std::max(1.0f, availableRegion.x);
		drawHeight = std::max(1.0f, availableRegion.y);
	} else {
		const float availableWidth = std::max(1.0f, std::min(maxWidth, availableRegion.x));
		const float aspect = std::max(displayWidth / displayHeight, 0.0001f);
		drawWidth = availableWidth;
		drawHeight = std::max(1.0f, drawWidth / aspect);
		if (drawHeight > availableRegion.y) {
			drawHeight = std::max(1.0f, availableRegion.y);
			drawWidth = std::max(1.0f, drawHeight * aspect);
		}
	}

	ImGui::SetCursorPos(ImVec2(
		contentStart.x + std::max(0.0f, (availableRegion.x - drawWidth) * 0.5f),
		contentStart.y + std::max(0.0f, (availableRegion.y - drawHeight) * 0.5f)));

	const ImTextureID textureId = (ImTextureID)(uintptr_t)texture.getTextureData().textureID;
	ImGui::Image(
		textureId,
		ImVec2(drawWidth, drawHeight),
		flipVertical ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f),
		flipVertical ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f));
}

float computePreviewWindowHeight(float sourceWidth, float sourceHeight, float windowWidth, float maxPreviewWidth) {
	if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
		return 420.0f;
	}

	const ImGuiStyle & style = ImGui::GetStyle();
	const float titleBarHeight = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;
	const float contentWidth = std::max(1.0f, windowWidth - style.WindowPadding.x * 2.0f);
	const float drawWidth = std::max(1.0f, std::min(maxPreviewWidth, contentWidth));
	const float aspect = std::max(sourceWidth / sourceHeight, 0.0001f);
	const float previewHeight = drawWidth / aspect;
	const float border = std::max(0.0f, (contentWidth - drawWidth) * 0.5f);
	return std::max(220.0f, titleBarHeight + style.WindowPadding.y * 2.0f + previewHeight + border * 2.0f);
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

void VlcRemoteGui::setup() {
	gui.setup(nullptr, false, ImGuiConfigFlags_ViewportsEnable, true);
	ImGui::GetIO().IniFilename = nullptr;
	ImGui::GetIO().ConfigViewportsNoAutoMerge = true;
}

void VlcRemoteGui::draw(
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

void VlcRemoteGui::drawImGui(
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

	const ImVec2 defaultGuiSize(520.0f, 730.0f);
	ImGui::SetNextWindowPos(ImVec2(kRemoteWindowX, kRemoteWindowY), ImGuiCond_Once);
	ImGui::SetNextWindowSize(defaultGuiSize, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints(
		ImVec2(defaultGuiSize.x, 420.0f),
		ImVec2(defaultGuiSize.x, std::numeric_limits<float>::max()));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kWindowBorderSize);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 6.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 7.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, ImVec2(0.5f, 0.5f));

	ImGui::PushStyleColor(ImGuiCol_WindowBg, kUiBg);
	ImGui::PushStyleColor(ImGuiCol_TitleBg, kUiOrange);
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kUiOrange);
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kUiOrangeHover);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, kUiChildBg);
	ImGui::PushStyleColor(ImGuiCol_Border, kUiBorder);
	ImGui::PushStyleColor(ImGuiCol_Separator, kUiChildBg);
	ImGui::PushStyleColor(ImGuiCol_SeparatorHovered, kUiChildBg);
	ImGui::PushStyleColor(ImGuiCol_SeparatorActive, kUiChildBg);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, kUiFrame);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kUiFrameHover);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kUiFrameActive);
	ImGui::PushStyleColor(ImGuiCol_Button, kUiOrange);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kUiOrangeHover);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, kUiOrangeActive);
	ImGui::PushStyleColor(ImGuiCol_SliderGrab, kUiOrangeBright);
	ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, kUiOrangeActive);
	ImGui::PushStyleColor(ImGuiCol_CheckMark, kUiOrangeBright);
	ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, kUiOrange);
	ImGui::PushStyleColor(ImGuiCol_Header, kUiOrange);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kUiOrangeHover);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, kUiOrangeActive);

	ImGui::Begin("VLC Remote", nullptr, ImGuiWindowFlags_NoCollapse);
	const bool hasPlaylist = !player.getPlaylist().empty();
	drawHeaderSection(player, hasPlaylist);
	drawTransportSection(player, hasPlaylist);
	drawPositionSection(player, hasPlaylist);
	drawPlaybackOptionsSection(
		player,
		projectMInitialized,
		randomProjectMPreset,
		reloadProjectMPresets,
		reloadProjectMTextures,
		loadPlayerProjectMTexture,
		loadCustomProjectMTexture);
	drawPlaylistSection(player);
	drawPathSection(player, hasPlaylist, addPathToPlaylist);
	ImGui::End();

	if (showVideoWindow || showProjectMWindow || showDisplayFullscreen) {
		drawVisualWindows(
			projectM,
			projectMInitialized,
			videoPreviewTexture,
			videoPreviewWidth,
			videoPreviewHeight);
	}

	ImGui::PopStyleColor(22);
	ImGui::PopStyleVar(9);
}

void VlcRemoteGui::drawHeaderSection(ofxVlc4Player & player, bool hasPlaylist) {
	std::string currentTitle = "Nothing loaded";
	const std::string currentPath = player.getCurrentPath();
	const std::string currentFileName = player.getCurrentFileName();
	if (!currentFileName.empty()) {
		currentTitle = "Media: " + currentFileName;
	}

	const std::string timeText = buildTimeText(player, showRemainingTime);

	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
	ImGui::TextUnformatted(currentTitle.c_str());
	if (!currentPath.empty() && ImGui::IsItemClicked(0)) {
		ImGui::SetClipboardText(currentPath.c_str());
	}
	if (!currentPath.empty() && ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Click to copy media path");
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

void VlcRemoteGui::drawTransportSection(ofxVlc4Player & player, bool hasPlaylist) {
	const float controlWidth = (ImGui::GetContentRegionAvail().x - 4.0f * kTransportSpacing) / 5.0f;
	ImGui::BeginDisabled(!hasPlaylist);
	if (ImGui::Button("Play", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		if (selectedIndex >= 0 && selectedIndex != player.getCurrentIndex()) {
			player.playIndex(selectedIndex);
		} else {
			player.play();
		}
	}

	ImGui::SameLine(0.0f, kTransportSpacing);
	if (ImGui::Button("Pause", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		player.pause();
	}

	ImGui::SameLine(0.0f, kTransportSpacing);
	if (ImGui::Button("Stop", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		player.stop();
	}

	ImGui::SameLine(0.0f, kTransportSpacing);
	if (ImGui::Button("Prev", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		player.previousMediaListItem();
	}

	ImGui::SameLine(0.0f, kTransportSpacing);
	if (ImGui::Button("Next", ImVec2(controlWidth, 0.0f)) && hasPlaylist) {
		followPlaybackSelectionEnabled = true;
		player.nextMediaListItem();
	}
	ImGui::EndDisabled();
}

void VlcRemoteGui::drawPositionSection(ofxVlc4Player & player, bool hasPlaylist) {
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
	const ImVec4 seekFillColor = sliderActive ? kUiOrangeActive : (seekHovered ? kUiOrangeHover : kUiOrange);
	drawList->AddRectFilled(seekMin, seekMax, ImGui::GetColorU32(seekTrackColor), 0.0f);
	drawList->AddRectFilled(seekMin, ImVec2(knobX, seekMax.y), ImGui::GetColorU32(seekFillColor), 0.0f);
	ImGui::PopStyleVar();
}

void VlcRemoteGui::drawPlaybackOptionsSection(
	ofxVlc4Player & player,
	bool projectMInitialized,
	const std::function<void()> & randomProjectMPreset,
	const std::function<void()> & reloadProjectMPresets,
	const std::function<void()> & reloadProjectMTextures,
	const std::function<void()> & loadPlayerProjectMTexture,
	const std::function<bool(const std::string &)> & loadCustomProjectMTexture) {
	static const char * fullscreenSources[] = { "Video", "projectM" };

	ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, kLabelInnerSpacing);
	ImGui::PushItemWidth(140.0f);
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kUiFrameHover);
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kUiFrameActive);
	if (ImGui::SliderInt("Volume", &volume, 0, 100)) {
		player.setVolume(volume);
	}
	if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			volume = ofClamp(volume + static_cast<int>(wheel * 5.0f), 0, 100);
			player.setVolume(volume);
		}
	}
	ImGui::PopStyleColor(2);
	ImGui::PopItemWidth();
	ImGui::PopStyleVar();
	ImGui::SameLine(0.0f, kSectionSpacing);

	static int playbackModeIndex = 0;
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
			ImGui::Button("Load Custom Texture", ImVec2(kDualActionButtonWidth, 0.0f)) || submittedTexturePath;
		if (loadTextureRequested && loadCustomProjectMTexture(projectMTexturePath)) {
			projectMTexturePath[0] = '\0';
		}
		ImGui::SameLine(0.0f, kButtonSpacing);
		if (ImGui::Button("Load Player Texture", ImVec2(kDualActionButtonWidth, 0.0f))) {
			loadPlayerProjectMTexture();
		}

		ImGui::Separator();
	}

	ImGui::SeparatorText("Playlist");
}

void VlcRemoteGui::drawPlaylistSection(ofxVlc4Player & player) {
	const auto & playlist = player.getPlaylist();
	const int currentPlaying = player.getCurrentIndex();
	const ImGuiPayload * activePayload = ImGui::GetDragDropPayload();
	const bool draggingPlaylistItem = activePayload && activePayload->IsDataType("PLAYLIST_INDEX");
	const auto * dragPayload = draggingPlaylistItem ? static_cast<const PlaylistDragPayload *>(activePayload->Data) : nullptr;
	float playlistHeight = ImGui::GetContentRegionAvail().y - 104.0f;
	if (playlistHeight < 220.0f) {
		playlistHeight = 220.0f;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
	if (ImGui::BeginChild("playlist_child", ImVec2(0, playlistHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
		const float playlistInsetX = kLabelInnerSpacing.x;
		const float playlistInsetY = kLabelInnerSpacing.y;
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
				baseColor = kUiOrange;
				hoverColor = kUiOrangeHover;
				activeColor = kUiOrangeActive;
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
					selectedIndex = -1;
					lastClickedIndex = -1;
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
			ImGui::Dummy(ImVec2(0.0f, playlistInsetY));
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

void VlcRemoteGui::drawPathSection(
	ofxVlc4Player & player,
	bool hasPlaylist,
	const std::function<int(const std::string &)> & addPathToPlaylist) {
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

void VlcRemoteGui::drawVisualWindows(
	ofxProjectM & projectM,
	bool projectMInitialized,
	const ofTexture & videoPreviewTexture,
	float videoPreviewWidth,
	float videoPreviewHeight) {
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
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (fullscreenVideo) {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		}
		if (fullscreenVideo) {
			ImGui::SetNextWindowPos(fullscreenPos, ImGuiCond_Always);
			ImGui::SetNextWindowSize(fullscreenSize, ImGuiCond_Always);
			flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
		} else {
			ImGui::SetNextWindowSize(
				ImVec2(kPreviewWindowWidth, computePreviewWindowHeight(videoPreviewWidth, videoPreviewHeight, kPreviewWindowWidth, kPreviewMaxWidth)),
				ImGuiCond_Always);
			ImGui::SetNextWindowPos(
				ImVec2(lastVideoWindowPos.x, lastVideoWindowPos.y),
				restoreVideoWindowPosition ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
		}
		ImGui::Begin("Video Display", &showVideoWindow, flags);
		drawTexturePreview(
			videoPreviewTexture,
			videoPreviewWidth,
			videoPreviewHeight,
			fullscreenVideo ? fullscreenSize.x : kPreviewMaxWidth,
			false,
			false);
		if (!fullscreenVideo) {
			const ImVec2 currentPos = ImGui::GetWindowPos();
			lastVideoWindowPos = glm::vec2(currentPos.x, currentPos.y);
			restoreVideoWindowPosition = false;
		}
		ImGui::End();
		if (fullscreenVideo) {
			ImGui::PopStyleVar();
		}
	}

	if (showNormalProjectMWindow || fullscreenProjectM) {
		const ofTexture emptyTexture;
		const ofTexture & projectMTexture = projectMInitialized ? projectM.getTexture() : emptyTexture;
		const float projectMWidth = projectMInitialized ? static_cast<float>(projectMTexture.getWidth()) : 0.0f;
		const float projectMHeight = projectMInitialized ? static_cast<float>(projectMTexture.getHeight()) : 0.0f;
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (fullscreenProjectM) {
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		}
		if (fullscreenProjectM) {
			ImGui::SetNextWindowPos(fullscreenPos, ImGuiCond_Always);
			ImGui::SetNextWindowSize(fullscreenSize, ImGuiCond_Always);
			flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
		} else {
			ImGui::SetNextWindowSize(
				ImVec2(kPreviewWindowWidth, computePreviewWindowHeight(projectMWidth, projectMHeight, kPreviewWindowWidth, kPreviewMaxWidth)),
				ImGuiCond_Always);
			ImGui::SetNextWindowPos(
				ImVec2(lastProjectMWindowPos.x, lastProjectMWindowPos.y),
				restoreProjectMWindowPosition ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
		}
		ImGui::Begin("projectM Display", &showProjectMWindow, flags);
		drawTexturePreview(
			projectMTexture,
			projectMWidth,
			projectMHeight,
			fullscreenProjectM ? fullscreenSize.x : kPreviewMaxWidth,
			false,
			true);
		if (!fullscreenProjectM) {
			const ImVec2 currentPos = ImGui::GetWindowPos();
			lastProjectMWindowPos = glm::vec2(currentPos.x, currentPos.y);
			restoreProjectMWindowPosition = false;
		}
		ImGui::End();
		if (fullscreenProjectM) {
			ImGui::PopStyleVar();
		}
	}
}

void VlcRemoteGui::deleteSelected(ofxVlc4Player & player) {
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

void VlcRemoteGui::normalizeSelection(ofxVlc4Player & player) {
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

void VlcRemoteGui::followCurrentTrack(ofxVlc4Player & player) {
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

void VlcRemoteGui::updateSelection(ofxVlc4Player & player) {
	normalizeSelection(player);
	followCurrentTrack(player);
}

void VlcRemoteGui::handleKeyPressed(int key, ofxVlc4Player & player, ofxProjectM & projectM, bool projectMInitialized) {
	if (key == OF_KEY_DEL || key == OF_KEY_BACKSPACE) {
		deleteSelected(player);
		return;
	}

	if (key == ' ') {
		if (player.isPlaying()) {
			player.pause();
		} else {
			followPlaybackSelectionEnabled = true;
			player.play();
		}
	}

	if (key == OF_KEY_RIGHT) {
		followPlaybackSelectionEnabled = true;
		player.nextMediaListItem();
	}
	if (key == OF_KEY_LEFT) {
		followPlaybackSelectionEnabled = true;
		player.previousMediaListItem();
	}

	if (key == 'm' && projectMInitialized) {
		projectM.randomPreset();
	}
}

void VlcRemoteGui::handleDragEvent(
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

bool VlcRemoteGui::shouldRenderVideoPreview() const {
	return showVideoWindow || (showDisplayFullscreen && fullscreenDisplaySource == 0);
}

bool VlcRemoteGui::shouldRenderProjectMPreview() const {
	return showProjectMWindow || (showDisplayFullscreen && fullscreenDisplaySource == 1);
}
