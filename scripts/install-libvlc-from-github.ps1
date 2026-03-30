<##
.SYNOPSIS
Downloads a libvlc release archive and installs it into the addon-local libvlc layout.

.EXAMPLE
.\install-libvlc-from-github.ps1

.EXAMPLE
.\install-libvlc-from-github.ps1 -ZipUrl "https://artifacts.videolan.org/vlc/nightly-win64-llvm/20260312-0501/vlc-4.0.0-dev-win64-259d873b.zip"
#>
[CmdletBinding()]
param(
	[string]$ZipUrl = "",

	[string]$NightlyIndexUrl = "https://artifacts.videolan.org/vlc/nightly-win64-llvm/",

	[string]$AddonRoot = "",

	[switch]$KeepArchive,

	[switch]$KeepExtracted
)

$ErrorActionPreference = "Stop"

function Resolve-AddonRoot([string]$StartDirectory) {
	$Current = Resolve-Path -LiteralPath $StartDirectory

	while ($null -ne $Current) {
		$Candidate = $Current.Path
		$HasLibs = Test-Path -LiteralPath (Join-Path $Candidate "libs")
		$HasAddonConfig = Test-Path -LiteralPath (Join-Path $Candidate "addon_config.mk")

		if ($HasLibs -and $HasAddonConfig) {
			return $Candidate
		}

		$Parent = Split-Path -Parent $Candidate
		if ([string]::IsNullOrWhiteSpace($Parent) -or $Parent -eq $Candidate) {
			break
		}

		$Current = Resolve-Path -LiteralPath $Parent
	}

	throw "Could not determine addon root from '$StartDirectory'."
}

if ([string]::IsNullOrWhiteSpace($AddonRoot)) {
	$ScriptDirectory = $PSScriptRoot
	if ([string]::IsNullOrWhiteSpace($ScriptDirectory)) {
		$ScriptPath = $MyInvocation.MyCommand.Path
		if ([string]::IsNullOrWhiteSpace($ScriptPath)) {
			throw "Could not determine the script directory."
		}
		$ScriptDirectory = Split-Path -Parent $ScriptPath
	}

	$AddonRoot = Resolve-AddonRoot $ScriptDirectory
}

function Write-Step([string]$Message) {
	Write-Host "==> $Message" -ForegroundColor Cyan
}

function Ensure-Directory([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) {
		New-Item -ItemType Directory -Path $Path | Out-Null
	}
}

function Reset-Directory([string]$Path) {
	if (Test-Path -LiteralPath $Path) {
		Remove-Item -LiteralPath $Path -Recurse -Force
	}
	New-Item -ItemType Directory -Path $Path | Out-Null
}

function Find-FirstPath([string[]]$Candidates) {
	foreach ($Candidate in $Candidates) {
		if (Test-Path -LiteralPath $Candidate) {
			return (Resolve-Path -LiteralPath $Candidate).Path
		}
	}

	return $null
}

function Find-FirstFileByName([string]$Root, [string]$Name) {
	$Match = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name | Select-Object -First 1
	if ($null -eq $Match) {
		return $null
	}

	return $Match.FullName
}

function Copy-OptionalFile([string]$Source, [string]$DestinationDirectory) {
	if ([string]::IsNullOrWhiteSpace($Source) -or -not (Test-Path -LiteralPath $Source)) {
		return
	}

	Copy-Item -LiteralPath $Source -Destination (Join-Path $DestinationDirectory (Split-Path $Source -Leaf)) -Force
}

function Resolve-LatestNightlyZipUrl([string]$IndexUrl) {
	$IndexResponse = Invoke-WebRequest -Uri $IndexUrl
	$NightlyLink = $IndexResponse.Links |
		Where-Object { $_.href -match '^\d{8}-\d{4}/$' } |
		Sort-Object href -Descending |
		Select-Object -First 1

	if ($null -eq $NightlyLink) {
		throw "Could not find a nightly build directory at '$IndexUrl'."
	}

	$NightlyDirectoryUrl = [System.Uri]::new([System.Uri]$IndexUrl, $NightlyLink.href).AbsoluteUri
	$NightlyResponse = Invoke-WebRequest -Uri $NightlyDirectoryUrl
	$ZipLink = $NightlyResponse.Links |
		Where-Object { $_.href -match '^vlc-.*-win64-.*\.zip$' } |
		Select-Object -First 1

	if ($null -eq $ZipLink) {
		throw "Could not find a nightly ZIP inside '$NightlyDirectoryUrl'."
	}

	return [System.Uri]::new([System.Uri]$NightlyDirectoryUrl, $ZipLink.href).AbsoluteUri
}

Write-Step "Preparing install paths"

$LibVlcRoot = Join-Path $AddonRoot "libs\libvlc"
$TargetIncludeDirectory = Join-Path $LibVlcRoot "include"
$TargetLibraryDirectory = Join-Path $LibVlcRoot "lib\vs"

$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxVlc4Player-libvlc-" + [guid]::NewGuid().ToString("N"))
$ArchivePath = Join-Path $TempRoot "libvlc.zip"
$ExtractPath = Join-Path $TempRoot "extract"

Ensure-Directory $TempRoot
Ensure-Directory $ExtractPath

if ([string]::IsNullOrWhiteSpace($ZipUrl)) {
	Write-Step "Resolving latest nightly ZIP"
	$ZipUrl = Resolve-LatestNightlyZipUrl $NightlyIndexUrl
}

Write-Step "Downloading VLC archive"
Write-Host "     $ZipUrl"
Invoke-WebRequest -Uri $ZipUrl -OutFile $ArchivePath

Write-Step "Extracting archive"
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $ExtractPath -Force

$ContentRoot = $ExtractPath
$TopLevelDirectories = @(Get-ChildItem -LiteralPath $ExtractPath -Directory)
if ($TopLevelDirectories.Count -eq 1) {
	$ContentRoot = $TopLevelDirectories[0].FullName
}

$IncludeRoot = Find-FirstPath @(
	(Join-Path $ContentRoot "sdk\include"),
	(Join-Path $ContentRoot "include")
)

if ([string]::IsNullOrWhiteSpace($IncludeRoot)) {
	throw "Could not find an include directory in the downloaded archive."
}

$NestedHeaderSourceRoot = Find-FirstPath @(
	(Join-Path $IncludeRoot "vlc")
)

$LibvlcImportLibrary = Find-FirstFileByName $ContentRoot "libvlc.lib"
$LibvlccoreImportLibrary = Find-FirstFileByName $ContentRoot "libvlccore.lib"

if ([string]::IsNullOrWhiteSpace($LibvlcImportLibrary) -or [string]::IsNullOrWhiteSpace($LibvlccoreImportLibrary)) {
	throw "Could not find libvlc.lib and libvlccore.lib in the downloaded archive."
}

$LibvlcDll = Find-FirstFileByName $ContentRoot "libvlc.dll"
$LibvlccoreDll = Find-FirstFileByName $ContentRoot "libvlccore.dll"
$AxvlcDll = Find-FirstFileByName $ContentRoot "axvlc.dll"

if ([string]::IsNullOrWhiteSpace($LibvlcDll) -or [string]::IsNullOrWhiteSpace($LibvlccoreDll)) {
	throw "Could not find libvlc.dll and libvlccore.dll in the downloaded archive."
}

Write-Step "Installing headers and import libraries into addon libs/libvlc"
Reset-Directory $TargetIncludeDirectory
Ensure-Directory $TargetLibraryDirectory

Get-ChildItem -LiteralPath $IncludeRoot -File |
	Where-Object { $_.Extension -in @(".h", ".hpp") } |
	ForEach-Object {
		Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $TargetIncludeDirectory $_.Name) -Force
	}

if (-not [string]::IsNullOrWhiteSpace($NestedHeaderSourceRoot)) {
	Get-ChildItem -LiteralPath $NestedHeaderSourceRoot -File |
		Where-Object { $_.Extension -in @(".h", ".hpp") } |
		ForEach-Object {
			Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $TargetIncludeDirectory $_.Name) -Force
		}
}

Copy-Item -LiteralPath $LibvlcImportLibrary -Destination (Join-Path $TargetLibraryDirectory "libvlc.lib") -Force
Copy-Item -LiteralPath $LibvlccoreImportLibrary -Destination (Join-Path $TargetLibraryDirectory "libvlccore.lib") -Force

Write-Step "Installing runtime DLLs into addon libs/libvlc/lib/vs"
Copy-Item -LiteralPath $LibvlcDll -Destination (Join-Path $TargetLibraryDirectory "libvlc.dll") -Force
Copy-Item -LiteralPath $LibvlccoreDll -Destination (Join-Path $TargetLibraryDirectory "libvlccore.dll") -Force
Copy-OptionalFile $AxvlcDll $TargetLibraryDirectory

if (-not $KeepArchive -and (Test-Path -LiteralPath $ArchivePath)) {
	Remove-Item -LiteralPath $ArchivePath -Force
}

if (-not $KeepExtracted -and (Test-Path -LiteralPath $TempRoot)) {
	Remove-Item -LiteralPath $TempRoot -Recurse -Force
}

Write-Step "Done"
Write-Host ""
Write-Host "Installed libvlc into:" -ForegroundColor Green
Write-Host "  $TargetIncludeDirectory"
Write-Host "  $TargetLibraryDirectory"
Write-Host "  runtime DLLs in $TargetLibraryDirectory"