#pragma once

#include "ofMain.h"
#include "ofxImGui.h"
#include "ofxProjectM.h"
#include "ofxVlc4Player.h"

#include <functional>
#include <set>
#include <utility>
#include <vector>

struct MediaDisplayState {
	std::string path;
	std::string fileName;
	std::vector<std::pair<std::string, std::string>> metadata;
};

enum class AnaglyphColorMode {
	Disabled = 0,
	RedCyan,
	GreenMagenta,
	AmberBlue
};

class ofVlcPlayer4Gui {
public:
	void setup();
	void draw(
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
		const std::function<bool(const std::string &)> & loadCustomProjectMTexture);
	void updateSelection(ofxVlc4Player & player);
	void handleDragEvent(
		const ofDragInfo & dragInfo,
		ofxVlc4Player & player,
		const std::function<int(const std::string &)> & addPathToPlaylist);
	bool shouldRenderProjectMPreview() const;
	bool isAnaglyphEnabled() const;
	AnaglyphColorMode getAnaglyphColorMode() const;
	bool isAnaglyphSwapEyesEnabled() const;
	float getAnaglyphEyeSeparation() const;

private:
	void drawImGui(
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
		const std::function<bool(const std::string &)> & loadCustomProjectMTexture);
	void drawHeaderSection(
		ofxVlc4Player & player,
		bool hasPlaylist,
		const MediaDisplayState & mediaDisplayState);
	void drawTransportSection(ofxVlc4Player & player, bool hasPlaylist);
	void drawPositionSection(ofxVlc4Player & player, bool hasPlaylist);
	void handleImGuiShortcuts(
		ofxVlc4Player & player,
		bool projectMInitialized,
		const std::function<void()> & randomProjectMPreset);
	void drawEqualizerSection(ofxVlc4Player & player);
	void drawVideo3DSection(ofxVlc4Player & player);
	void drawPlaybackOptionsSection(
		ofxVlc4Player & player,
		bool hasPlaylist,
		bool projectMInitialized,
		const std::function<int(const std::string &)> & addPathToPlaylist,
		const std::function<void()> & randomProjectMPreset,
		const std::function<void()> & reloadProjectMPresets,
		const std::function<void()> & reloadProjectMTextures,
		const std::function<void()> & loadPlayerProjectMTexture,
		const std::function<bool(const std::string &)> & loadCustomProjectMTexture);
	void drawMediaInfoSection(const MediaDisplayState & mediaDisplayState);
	void drawPlaylistSection(ofxVlc4Player & player);
	void drawPathSection(
		ofxVlc4Player & player,
		bool hasPlaylist,
		const std::function<int(const std::string &)> & addPathToPlaylist);
	void drawVisualWindows(
		ofxProjectM & projectM,
		bool projectMInitialized,
		const ofTexture & videoPreviewTexture,
		float videoPreviewWidth,
		float videoPreviewHeight);
	void deleteSelected(ofxVlc4Player & player);
	void normalizeSelection(ofxVlc4Player & player);
	void followCurrentTrack(ofxVlc4Player & player);

	ofxImGui::Gui gui;

	int selectedIndex = -1;
	int lastClickedIndex = -1;
	std::set<int> selectedIndices;

	int volume = 50;
	int lastNonZeroVolume = 50;
	char addPath[1024] = "";
	char projectMTexturePath[1024] = "";
	bool positionSliderActive = false;
	float pendingSeekPosition = 0.0f;

	bool showVideoWindow = true;
	bool showProjectMWindow = false;
	bool showDisplayFullscreen = false;
	int fullscreenDisplaySource = 0;
	glm::vec2 lastVideoWindowPos = { 560.0f, 24.0f };
	glm::vec2 lastProjectMWindowPos = { 560.0f, 500.0f };
	bool restoreVideoWindowPosition = false;
	bool restoreProjectMWindowPosition = false;
	bool wasVideoFullscreen = false;
	bool wasProjectMFullscreen = false;
	bool followPlaybackSelectionEnabled = true;
	bool showRemainingTime = false;
	bool anaglyphEnabled = false;
	AnaglyphColorMode anaglyphColorMode = AnaglyphColorMode::Disabled;
	bool anaglyphSwapEyes = false;
	float anaglyphEyeSeparation = 0.0f;
	std::vector<float> equalizerControlYs;
	int lastEqualizerPresetIndex = -9999;
};
