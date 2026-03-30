<#
.SYNOPSIS
Downloads libvlc runtime files from the latest VLC nightly and public libvlc headers from the VLC GitHub master branch,
and installs them into the addon-local libvlc layout.

.EXAMPLE
.\install-libvlc.ps1

.EXAMPLE
.\install-libvlc.ps1 -ZipUrl "https://artifacts.videolan.org/vlc/nightly-win64-llvm/20260312-0501/vlc-4.0.0-dev-win64-259d873b.zip"
#>
[CmdletBinding()]
param(
	[string]$ZipUrl = "",
	[string]$NightlyIndexUrl = "https://artifacts.videolan.org/vlc/nightly-win64-llvm/",
	[string]$HeaderZipUrl = "https://github.com/videolan/vlc/archive/refs/heads/master.zip",
	[string]$AddonRoot = "",
	[switch]$KeepArchive,
	[switch]$KeepExtracted
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-AddonRoot([string]$StartDirectory) {
	$Current = Resolve-Path -LiteralPath $StartDirectory
	while ($null -ne $Current) {
		$Candidate = $Current.Path
		if ((Test-Path -LiteralPath (Join-Path $Candidate 'libs')) -and (Test-Path -LiteralPath (Join-Path $Candidate 'addon_config.mk'))) {
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
		if (-not [string]::IsNullOrWhiteSpace($Candidate) -and (Test-Path -LiteralPath $Candidate)) {
			return (Resolve-Path -LiteralPath $Candidate).Path
		}
	}
	return $null
}

function Find-FirstFileByName([string]$Root, [string]$Name) {
	if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path -LiteralPath $Root)) {
		return $null
	}

	$Match = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $Name -ErrorAction SilentlyContinue | Select-Object -First 1
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

function Resolve-ContentRoot([string]$ExtractRoot) {
	$TopLevelDirectories = @(Get-ChildItem -LiteralPath $ExtractRoot -Directory -ErrorAction SilentlyContinue)
	if ($TopLevelDirectories.Count -eq 1) {
		return $TopLevelDirectories[0].FullName
	}
	return $ExtractRoot
}

function Resolve-IncludeRoot([string]$Root) {
	if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path -LiteralPath $Root)) {
		return $null
	}

	return Find-FirstPath @(
		(Join-Path $Root 'include\vlc'),
		(Join-Path $Root 'sdk\include\vlc')
	)
}

function Copy-DirectoryContents([string]$SourceDirectory, [string]$TargetDirectory) {
	if ([string]::IsNullOrWhiteSpace($SourceDirectory) -or -not (Test-Path -LiteralPath $SourceDirectory)) {
		return
	}

	Reset-Directory $TargetDirectory
	Copy-Item -Path (Join-Path $SourceDirectory '*') -Destination $TargetDirectory -Recurse -Force
}

function Copy-HeadersFromIncludeRoot([string]$SourceIncludeDirectory, [string]$TargetIncludeDirectory) {
	$HeaderFiles = Get-ChildItem -LiteralPath $SourceIncludeDirectory -File |
		Where-Object { $_.Extension -in @('.h', '.hpp') }

	$Destination = Join-Path $TargetIncludeDirectory 'vlc'
	Ensure-Directory $Destination
	foreach ($HeaderFile in $HeaderFiles) {
		Copy-Item -LiteralPath $HeaderFile.FullName -Destination (Join-Path $Destination $HeaderFile.Name) -Force
	}
}

function Get-ExampleBinDirectories([string]$AddonRootPath) {
	return @(Get-ChildItem -LiteralPath $AddonRootPath -Directory -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -like '*Example*' } |
		ForEach-Object {
			Join-Path $_.FullName 'bin'
		})
}

function Copy-RuntimeToExampleBins([string]$LibvlcDll, [string]$LibvlccoreDll, [string]$PluginsSourceRoot, [string]$LuaSourceRoot, [string[]]$ExampleBinDirectories) {
	foreach ($BinDirectory in $ExampleBinDirectories) {
		Ensure-Directory $BinDirectory
		Copy-OptionalFile $LibvlcDll $BinDirectory
		Copy-OptionalFile $LibvlccoreDll $BinDirectory
		Copy-DirectoryContents $PluginsSourceRoot (Join-Path $BinDirectory 'plugins')
		Copy-DirectoryContents $LuaSourceRoot (Join-Path $BinDirectory 'lua')
	}
}

function Resolve-LatestNightlyZipUrl([string]$IndexUrl) {
	$IndexResponse = Invoke-WebRequest -UseBasicParsing -Uri $IndexUrl
	$NightlyMatches = [regex]::Matches($IndexResponse.Content, 'href="(\d{8}-\d{4}/)"')
	if ($NightlyMatches.Count -eq 0) {
		throw "Could not find a nightly build directory at '$IndexUrl'."
	}

	$NightlyRelativePath = ($NightlyMatches | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Descending | Select-Object -First 1)
	$NightlyDirectoryUrl = [System.Uri]::new([System.Uri]$IndexUrl, $NightlyRelativePath).AbsoluteUri

	$NightlyResponse = Invoke-WebRequest -UseBasicParsing -Uri $NightlyDirectoryUrl
	$ZipMatches = [regex]::Matches($NightlyResponse.Content, 'href="(vlc-.*-win64-.*\.zip)"')
	if ($ZipMatches.Count -eq 0) {
		throw "Could not find a nightly ZIP inside '$NightlyDirectoryUrl'."
	}

	$ZipRelativePath = $ZipMatches[0].Groups[1].Value
	return [System.Uri]::new([System.Uri]$NightlyDirectoryUrl, $ZipRelativePath).AbsoluteUri
}

function Find-VsToolPaths() {
	$ToolNames = @('dumpbin.exe', 'lib.exe')
	$SearchRoots = @(
		'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64',
		'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\MSVC\14.50.35717\bin\HostX86\x64'
	)

	$Paths = @{}
	foreach ($ToolName in $ToolNames) {
		$Command = Get-Command $ToolName -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($null -ne $Command -and -not [string]::IsNullOrWhiteSpace($Command.Source)) {
			$Paths[$ToolName] = $Command.Source
			continue
		}

		$Candidates = @($SearchRoots | ForEach-Object { Join-Path $_ $ToolName })
		$Resolved = Find-FirstPath $Candidates
		if (-not [string]::IsNullOrWhiteSpace($Resolved)) {
			$Paths[$ToolName] = $Resolved
		}
	}
	return $Paths
}

function New-ImportLibraryFromDll([string]$DllPath, [string]$OutputLibPath, [string]$TempDirectory) {
	$ToolPaths = Find-VsToolPaths
	$DumpbinPath = $ToolPaths['dumpbin.exe']
	$LibExePath = $ToolPaths['lib.exe']
	if ([string]::IsNullOrWhiteSpace($DumpbinPath) -or [string]::IsNullOrWhiteSpace($LibExePath)) {
		throw "Could not find dumpbin.exe and lib.exe to generate $(Split-Path $OutputLibPath -Leaf)."
	}

	$BaseName = [System.IO.Path]::GetFileNameWithoutExtension($DllPath)
	$DefPath = Join-Path $TempDirectory ($BaseName + '.def')
	$ExportLines = & $DumpbinPath /exports $DllPath 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "dumpbin.exe failed while reading exports from $(Split-Path $DllPath -Leaf)."
	}

	$ExportNames = New-Object System.Collections.Generic.List[string]
	$InExportsTable = $false
	foreach ($Line in $ExportLines) {
		if ($Line -match '^\s+ordinal\s+hint\s+RVA\s+name$') {
			$InExportsTable = $true
			continue
		}
		if (-not $InExportsTable) { continue }
		if ($Line -match '^\s*Summary$') { break }
		if ($Line -match '^\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(.+)$') {
			$Name = $Matches[1].Trim()
			$Forwarder = $Name.IndexOf('=')
			if ($Forwarder -ge 0) {
				$Name = $Name.Substring(0, $Forwarder).Trim()
			}
			if (-not [string]::IsNullOrWhiteSpace($Name)) {
				$ExportNames.Add($Name)
			}
		}
	}

	if ($ExportNames.Count -eq 0) {
		throw "Could not extract any exports from $(Split-Path $DllPath -Leaf)."
	}

	(@("LIBRARY $(Split-Path $DllPath -Leaf)", 'EXPORTS') + $ExportNames) | Set-Content -Path $DefPath
	& $LibExePath /def:$DefPath /machine:x64 /out:$OutputLibPath | Out-Null
	if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $OutputLibPath)) {
		throw "lib.exe failed while generating $(Split-Path $OutputLibPath -Leaf)."
	}
}

function Remove-StaleRuntimeFromLibraryDirectory([string]$TargetLibraryDirectory) {
	$StaleRuntimeFiles = @(
		(Join-Path $TargetLibraryDirectory 'libvlc.dll'),
		(Join-Path $TargetLibraryDirectory 'libvlccore.dll')
	)
	foreach ($StaleRuntimeFile in $StaleRuntimeFiles) {
		if (Test-Path -LiteralPath $StaleRuntimeFile) {
			Remove-Item -LiteralPath $StaleRuntimeFile -Force
		}
	}

	foreach ($StaleRuntimeDirectory in @((Join-Path $TargetLibraryDirectory 'plugins'), (Join-Path $TargetLibraryDirectory 'lua'))) {
		if (Test-Path -LiteralPath $StaleRuntimeDirectory) {
			Remove-Item -LiteralPath $StaleRuntimeDirectory -Recurse -Force
		}
	}
}

if ([string]::IsNullOrWhiteSpace($AddonRoot)) {
	$ScriptDirectory = if (-not [string]::IsNullOrWhiteSpace($PSScriptRoot)) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
	if ([string]::IsNullOrWhiteSpace($ScriptDirectory)) {
		throw 'Could not determine the script directory.'
	}
	$AddonRoot = Resolve-AddonRoot $ScriptDirectory
}

Write-Step 'Preparing install paths'

$LibVlcRoot = Join-Path $AddonRoot 'libs\libvlc'
$TargetIncludeDirectory = Join-Path $LibVlcRoot 'include'
$TargetLibraryDirectory = Join-Path $LibVlcRoot 'lib\vs'
$ExampleBinDirectories = Get-ExampleBinDirectories $AddonRoot

$TempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('ofxVlc4Player-libvlc-' + [guid]::NewGuid().ToString('N'))
$ArchivePath = Join-Path $TempRoot 'libvlc.zip'
$ExtractPath = Join-Path $TempRoot 'extract'
$HeaderArchivePath = Join-Path $TempRoot 'vlc-headers.zip'
$HeaderExtractPath = Join-Path $TempRoot 'headers-extract'
$GeneratedLibDirectory = Join-Path $TempRoot 'generated-libs'

Ensure-Directory $TempRoot
Ensure-Directory $ExtractPath
Ensure-Directory $HeaderExtractPath
Ensure-Directory $GeneratedLibDirectory

if ([string]::IsNullOrWhiteSpace($ZipUrl)) {
	Write-Step 'Resolving latest nightly ZIP'
	$ZipUrl = Resolve-LatestNightlyZipUrl $NightlyIndexUrl
}

Write-Step 'Downloading VLC archive'
Write-Host "     $ZipUrl"
Invoke-WebRequest -UseBasicParsing -Uri $ZipUrl -OutFile $ArchivePath

Write-Step 'Extracting archive'
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $ExtractPath -Force
$ContentRoot = Resolve-ContentRoot $ExtractPath

Write-Step 'Downloading VLC headers from GitHub master'
Write-Host "     $HeaderZipUrl"
Invoke-WebRequest -UseBasicParsing -Uri $HeaderZipUrl -OutFile $HeaderArchivePath

Write-Step 'Extracting VLC headers'
Expand-Archive -LiteralPath $HeaderArchivePath -DestinationPath $HeaderExtractPath -Force
$IncludeRoot = Resolve-IncludeRoot (Resolve-ContentRoot $HeaderExtractPath)
if ([string]::IsNullOrWhiteSpace($IncludeRoot)) {
	throw 'Could not find the public libvlc header directory in the GitHub master archive.'
}

$LibvlcDll = Find-FirstFileByName $ContentRoot 'libvlc.dll'
$LibvlccoreDll = Find-FirstFileByName $ContentRoot 'libvlccore.dll'
$PluginsSourceRoot = Find-FirstPath @((Join-Path $ContentRoot 'plugins'))
$LuaSourceRoot = Find-FirstPath @((Join-Path $ContentRoot 'lua'))

if ([string]::IsNullOrWhiteSpace($LibvlcDll) -or [string]::IsNullOrWhiteSpace($LibvlccoreDll)) {
	throw 'Could not find libvlc.dll and libvlccore.dll in the downloaded archive.'
}

$LibvlcImportLibrary = Find-FirstFileByName $ContentRoot 'libvlc.lib'
if ([string]::IsNullOrWhiteSpace($LibvlcImportLibrary)) {
	Write-Step 'Generating libvlc.lib from libvlc.dll'
	$LibvlcImportLibrary = Join-Path $GeneratedLibDirectory 'libvlc.lib'
	New-ImportLibraryFromDll $LibvlcDll $LibvlcImportLibrary $GeneratedLibDirectory
}

Write-Step 'Installing headers and runtime into addon libs/libvlc'
Reset-Directory $TargetIncludeDirectory
Ensure-Directory $TargetLibraryDirectory
Copy-HeadersFromIncludeRoot $IncludeRoot $TargetIncludeDirectory
Copy-Item -LiteralPath $LibvlcImportLibrary -Destination (Join-Path $TargetLibraryDirectory 'libvlc.lib') -Force
Remove-StaleRuntimeFromLibraryDirectory $TargetLibraryDirectory

Write-Step 'Copying VLC runtime into example bin folders'
Copy-RuntimeToExampleBins $LibvlcDll $LibvlccoreDll $PluginsSourceRoot $LuaSourceRoot $ExampleBinDirectories

if (-not $KeepArchive) {
	if (Test-Path -LiteralPath $ArchivePath) { Remove-Item -LiteralPath $ArchivePath -Force }
	if (Test-Path -LiteralPath $HeaderArchivePath) { Remove-Item -LiteralPath $HeaderArchivePath -Force }
}

if (-not $KeepExtracted -and (Test-Path -LiteralPath $TempRoot)) {
	Remove-Item -LiteralPath $TempRoot -Recurse -Force
}

Write-Step 'Done'
Write-Host ''
Write-Host 'Installed libvlc into:' -ForegroundColor Green
Write-Host "  headers: $TargetIncludeDirectory"
Write-Host "  import lib: $TargetLibraryDirectory"
if ($ExampleBinDirectories.Count -gt 0) {
	Write-Host '  runtime copied to example bin folders:'
	foreach ($ExampleBinDirectory in $ExampleBinDirectories) {
		Write-Host "    $ExampleBinDirectory"
	}
}
