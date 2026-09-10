param(
	[Parameter(Mandatory = $true)]
	[ValidateSet("Codex", "Cursor")]
	[string]$Provider,

	[Parameter(Mandatory = $true)]
	[string]$ProjectSavedDirectory,

	[string]$NpmPath = "",
	[string]$CodexAcpVersion = "1.11.0",
	[string]$PortableNodeVersion = "v24.18.0",
	[string]$PortableNodeArchiveSha256 =
		"0ae68406b42d7725661da979b1403ec9926da205c6770827f33aac9d8f26e821",
	[string]$CursorInstallerPath = "",
	[string]$CursorInstallerSha256 = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
[Net.ServicePointManager]::SecurityProtocol =
	[Net.ServicePointManager]::SecurityProtocol -bor
	[Net.SecurityProtocolType]::Tls12

function Write-InstallerProgress
{
	param(
		[int]$Percent,
		[string]$Stage,
		[string]$Component
	)

	$boundedPercent = [Math]::Max(0, [Math]::Min(100, $Percent))
	[Console]::Out.WriteLine(
		"UEBRIDGE_PROGRESS|$boundedPercent|$Stage|$Component")
	[Console]::Out.Flush()
}

function Invoke-TrackedProcess
{
	param(
		[string]$Executable,
		[string]$Arguments,
		[string]$WorkingDirectory,
		[int]$StartPercent,
		[int]$EndPercent,
		[string]$Stage,
		[string]$Component
	)

	$startInfo = New-Object System.Diagnostics.ProcessStartInfo
	$startInfo.FileName = $Executable
	$startInfo.Arguments = $Arguments
	$startInfo.WorkingDirectory = $WorkingDirectory
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$process = New-Object System.Diagnostics.Process
	$process.StartInfo = $startInfo
	if (-not $process.Start())
	{
		throw "Unable to start $Component."
	}
	$standardOutputTask = $process.StandardOutput.ReadToEndAsync()
	$standardErrorTask = $process.StandardError.ReadToEndAsync()

	$percent = $StartPercent
	Write-InstallerProgress $percent $Stage $Component
	while (-not $process.WaitForExit(1000))
	{
		if ($percent -lt ($EndPercent - 1))
		{
			$percent++
			Write-InstallerProgress $percent $Stage $Component
		}
	}
	$process.WaitForExit()
	$processExitCode = $process.ExitCode
	$standardOutput = $standardOutputTask.GetAwaiter().GetResult()
	$standardError = $standardErrorTask.GetAwaiter().GetResult()
	if (-not [string]::IsNullOrWhiteSpace($standardOutput))
	{
		Write-Output $standardOutput.Trim()
	}
	if ($processExitCode -ne 0)
	{
		if (-not [string]::IsNullOrWhiteSpace($standardError))
		{
			Write-Output $standardError.Trim()
		}
		throw "$Component failed with exit code $processExitCode. $standardError"
	}

	Write-InstallerProgress $EndPercent $Stage $Component
}

function Invoke-CapturedProcess
{
	param(
		[string]$Executable,
		[string]$Arguments,
		[string]$WorkingDirectory,
		[Text.Encoding]$OutputEncoding = [Text.Encoding]::Unicode
	)

	$startInfo = New-Object System.Diagnostics.ProcessStartInfo
	$startInfo.FileName = $Executable
	$startInfo.Arguments = $Arguments
	$startInfo.WorkingDirectory = $WorkingDirectory
	$startInfo.UseShellExecute = $false
	$startInfo.CreateNoWindow = $true
	$startInfo.RedirectStandardOutput = $true
	$startInfo.RedirectStandardError = $true
	$startInfo.StandardOutputEncoding = $OutputEncoding
	$startInfo.StandardErrorEncoding = $OutputEncoding
	$process = New-Object System.Diagnostics.Process
	$process.StartInfo = $startInfo
	if (-not $process.Start())
	{
		throw "Unable to start $Executable."
	}
	$standardOutput = $process.StandardOutput.ReadToEnd()
	$standardError = $process.StandardError.ReadToEnd()
	$process.WaitForExit()
	return [pscustomobject]@{
		ExitCode = $process.ExitCode
		StandardOutput = $standardOutput
		StandardError = $standardError
	}
}

function Install-PortableNode
{
	param(
		[string]$SavedDirectory,
		[string]$Version,
		[string]$ArchiveSha256
	)

	if ($Version -notmatch '^v\d+\.\d+\.\d+$' -or
		$ArchiveSha256 -notmatch '^[0-9a-fA-F]{64}$')
	{
		throw "Portable Node.js requires a pinned version and SHA-256."
	}

	Write-InstallerProgress 5 "detecting" "Node.js"
	$runtimeRoot = Join-Path $SavedDirectory "UnrealAgent\Runtime\Node\$Version"
	$existingNpm = Get-ChildItem `
		-LiteralPath $runtimeRoot `
		-Filter "npm.cmd" `
		-Recurse `
		-File `
		-ErrorAction SilentlyContinue |
		Select-Object -First 1
	if ($null -ne $existingNpm)
	{
		Write-InstallerProgress 22 "ready" "Node.js"
		return $existingNpm.FullName
	}

	Write-InstallerProgress 8 "verifying-version" "Node.js $Version"
	$archiveName = "node-$Version-win-x64.zip"
	$downloadUrl = "https://nodejs.org/dist/$Version/$archiveName"
	$downloadRoot = Join-Path $SavedDirectory "UnrealAgent\Downloads"
	$archivePath = Join-Path $downloadRoot $archiveName
	New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
	New-Item -ItemType Directory -Force -Path $runtimeRoot | Out-Null

	Write-InstallerProgress 10 "downloading" "Node.js $Version"
	Add-Type -AssemblyName System.Net.Http
	$httpClient = New-Object System.Net.Http.HttpClient
	$response = $httpClient.GetAsync(
		$downloadUrl,
		[System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
	$response.EnsureSuccessStatusCode() | Out-Null
	$totalBytes = $response.Content.Headers.ContentLength
	$inputStream = $response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
	$outputStream = [IO.File]::Create($archivePath)
	$buffer = New-Object byte[] (1024 * 1024)
	$downloadedBytes = [long]0
	$lastReportedPercent = 10
	try
	{
		while (($readCount = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0)
		{
			$outputStream.Write($buffer, 0, $readCount)
			$downloadedBytes += $readCount
			if ($totalBytes -gt 0)
			{
				$mappedPercent = 10 + [Math]::Floor(
					($downloadedBytes / $totalBytes) * 8)
				if ($mappedPercent -gt $lastReportedPercent)
				{
					$lastReportedPercent = $mappedPercent
					Write-InstallerProgress $mappedPercent "downloading" "Node.js $version"
				}
			}
		}
	}
	finally
	{
		$outputStream.Dispose()
		$inputStream.Dispose()
		$response.Dispose()
		$httpClient.Dispose()
	}

	$actualArchiveSha256 = (Get-FileHash `
		-LiteralPath $archivePath `
		-Algorithm SHA256).Hash
	if (-not $actualArchiveSha256.Equals(
		$ArchiveSha256,
		[StringComparison]::OrdinalIgnoreCase))
	{
		Remove-Item -Force -LiteralPath $archivePath
		throw "Node.js archive SHA-256 verification failed."
	}

	Write-InstallerProgress 19 "extracting" "Node.js $Version"
	Expand-Archive -LiteralPath $archivePath -DestinationPath $runtimeRoot -Force
	Remove-Item -Force -LiteralPath $archivePath
	$installedNpm = Get-ChildItem `
		-LiteralPath $runtimeRoot `
		-Filter "npm.cmd" `
		-Recurse `
		-File |
		Select-Object -First 1
	if ($null -eq $installedNpm)
	{
		throw "The downloaded Node.js package did not contain npm.cmd."
	}
	Write-InstallerProgress 22 "ready" "Node.js $Version"
	return $installedNpm.FullName
}

function Install-CodexProvider
{
	param(
		[string]$SavedDirectory,
		[string]$DetectedNpmPath,
		[string]$Version
	)

	Write-InstallerProgress 2 "preparing" "Codex CLI"
	$effectiveNpmPath = $DetectedNpmPath
	if ([string]::IsNullOrWhiteSpace($effectiveNpmPath) -or
		-not (Test-Path -LiteralPath $effectiveNpmPath -PathType Leaf))
	{
		$effectiveNpmPath = Install-PortableNode `
			$SavedDirectory `
			$PortableNodeVersion `
			$PortableNodeArchiveSha256
	}
	else
	{
		Write-InstallerProgress 22 "ready" "Node.js and npm"
	}
	$nodeDirectory = Split-Path -Parent $effectiveNpmPath
	if (-not [string]::IsNullOrWhiteSpace($nodeDirectory))
	{
		$env:PATH = "$nodeDirectory;$env:PATH"
	}

	$installRoot = Join-Path $SavedDirectory "UnrealAgent\ACPAdapters\Codex"
	New-Item -ItemType Directory -Force -Path $installRoot | Out-Null
	$lockedVersion = "1.11.0"
	if ($Version -ne $lockedVersion)
	{
		throw "Codex ACP $Version has no bundled lockfile. Expected $lockedVersion."
	}
	$packageSource = Join-Path $PSScriptRoot "CodexACP.package.json"
	$lockSource = Join-Path $PSScriptRoot "CodexACP.package-lock.json"
	$expectedLockSha256 =
		"626f780f4238ed192609e0da98c1aa909cb2097c3edfbca2acbdbee515728fa9"
	if (-not (Test-Path -LiteralPath $packageSource -PathType Leaf) -or
		-not (Test-Path -LiteralPath $lockSource -PathType Leaf))
	{
		throw "The bundled Codex ACP package manifest or lockfile is missing."
	}
	$actualLockSha256 = (Get-FileHash `
		-LiteralPath $lockSource `
		-Algorithm SHA256).Hash
	if (-not $actualLockSha256.Equals(
		$expectedLockSha256,
		[StringComparison]::OrdinalIgnoreCase))
	{
		throw "The bundled Codex ACP lockfile failed SHA-256 verification."
	}
	Copy-Item `
		-LiteralPath $packageSource `
		-Destination (Join-Path $installRoot "package.json") `
		-Force
	Copy-Item `
		-LiteralPath $lockSource `
		-Destination (Join-Path $installRoot "package-lock.json") `
		-Force
	$escapedNpmPath = $effectiveNpmPath.Replace('"', '""')
	$escapedInstallRoot = $installRoot.Replace('"', '""')
	$npmArguments = "/d /s /c `"`"$escapedNpmPath`" ci --prefix `"$escapedInstallRoot`" --omit=dev --ignore-scripts --no-audit --no-fund`""
	Invoke-TrackedProcess `
		-Executable $env:COMSPEC `
		-Arguments $npmArguments `
		-WorkingDirectory $installRoot `
		-StartPercent 25 `
		-EndPercent 85 `
		-Stage "installing" `
		-Component "Codex CLI and Codex ACP"

	Write-InstallerProgress 90 "verifying" "Codex ACP"
	$adapterPath = Join-Path $installRoot "node_modules\.bin\codex-acp.cmd"
	$codexPath = Join-Path $installRoot "node_modules\.bin\codex.cmd"
	$nativeCodexPath = Join-Path $installRoot "node_modules\@openai\codex-win32-x64\vendor\x86_64-pc-windows-msvc\bin\codex.exe"
	if (-not (Test-Path -LiteralPath $adapterPath -PathType Leaf))
	{
		throw "Codex ACP was installed but codex-acp.cmd is missing."
	}
	if (-not (Test-Path -LiteralPath $codexPath -PathType Leaf))
	{
		throw "Codex ACP was installed but codex.cmd is missing."
	}
	if (-not (Test-Path -LiteralPath $nativeCodexPath -PathType Leaf))
	{
		throw "Codex ACP was installed but the native Windows Codex executable is missing."
	}
	$nodeExecutable = Join-Path $nodeDirectory "node.exe"
	$managedNodeExecutable = Join-Path (Split-Path -Parent $codexPath) "node.exe"
	if ((Test-Path -LiteralPath $nodeExecutable -PathType Leaf) -and
		-not (Test-Path -LiteralPath $managedNodeExecutable -PathType Leaf))
	{
		Copy-Item `
			-LiteralPath $nodeExecutable `
			-Destination $managedNodeExecutable
	}

	$verifyArguments = "/d /s /c `"`"$adapterPath`" --version`""
	Invoke-TrackedProcess `
		-Executable $env:COMSPEC `
		-Arguments $verifyArguments `
		-WorkingDirectory $installRoot `
		-StartPercent 92 `
		-EndPercent 97 `
		-Stage "verifying" `
		-Component "Codex ACP"
	Write-InstallerProgress 100 "complete" "Codex CLI and Codex ACP"
}

function Install-CursorProvider
{
	param(
		[string]$SavedDirectory,
		[string]$VerifiedInstallerPath,
		[string]$VerifiedInstallerSha256
	)

	Write-InstallerProgress 3 "detecting" "Windows Subsystem for Linux"
	$wslPath = Join-Path $env:SystemRoot "System32\wsl.exe"
	if (-not (Test-Path -LiteralPath $wslPath -PathType Leaf))
	{
		throw "WSL is unavailable. Enable Windows Subsystem for Linux and restart Windows, then retry."
	}

	$distributionResult = Invoke-CapturedProcess `
		-Executable $wslPath `
		-Arguments "--list --quiet" `
		-WorkingDirectory $SavedDirectory
	$distributions = $distributionResult.StandardOutput -replace "`0", ""
	$distribution = ($distributions -split "`r?`n" |
		ForEach-Object { $_.Replace([string][char]0xFEFF, "").Trim() } |
		Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
		Select-Object -First 1)
	if ($distributionResult.ExitCode -ne 0 -or
		[string]::IsNullOrWhiteSpace($distribution))
	{
		Write-InstallerProgress 8 "installing" "WSL Ubuntu"
		$wslInstallProcess = Start-Process `
			-FilePath $wslPath `
			-ArgumentList @("--install", "-d", "Ubuntu", "--no-launch") `
			-Verb RunAs `
			-WindowStyle Hidden `
			-Wait `
			-PassThru
		if ($wslInstallProcess.ExitCode -ne 0)
		{
			throw "Automatic WSL Ubuntu installation failed with exit code $($wslInstallProcess.ExitCode)."
		}

		$postInstallResult = Invoke-CapturedProcess `
			-Executable $wslPath `
			-Arguments "--list --quiet" `
			-WorkingDirectory $SavedDirectory
		$postInstallDistributions =
			$postInstallResult.StandardOutput -replace "`0", ""
		$distribution = ($postInstallDistributions -split "`r?`n" |
			ForEach-Object { $_.Replace([string][char]0xFEFF, "").Trim() } |
			Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
			Select-Object -First 1)
		if ($postInstallResult.ExitCode -ne 0 -or
			[string]::IsNullOrWhiteSpace($distribution))
		{
			throw "WSL Ubuntu was enabled successfully. Restart Windows once, then click install again to continue automatically."
		}
	}

	if ($distribution -notmatch "^[A-Za-z0-9._-]+$")
	{
		throw "The detected WSL distribution name contains unsupported characters."
	}

	Write-InstallerProgress 15 "ready" "WSL $($distribution.Trim())"
	$agentProbeArguments = "-d $($distribution.Trim()) -u root -e test -x /root/.local/bin/agent"
	$agentProbeResult = Invoke-CapturedProcess `
		-Executable $wslPath `
		-Arguments $agentProbeArguments `
		-WorkingDirectory $SavedDirectory
	if ($agentProbeResult.ExitCode -ne 0)
	{
		if ([string]::IsNullOrWhiteSpace($VerifiedInstallerPath) -or
			$VerifiedInstallerSha256 -notmatch '^[0-9a-fA-F]{64}$')
		{
			throw "Cursor Agent is not installed. Supply a version-pinned installer with -CursorInstallerPath and -CursorInstallerSha256. Automatic latest-script execution is disabled."
		}
		$resolvedInstallerPath = (Resolve-Path $VerifiedInstallerPath).Path
		$actualInstallerSha256 = (Get-FileHash `
			-LiteralPath $resolvedInstallerPath `
			-Algorithm SHA256).Hash
		if (-not $actualInstallerSha256.Equals(
			$VerifiedInstallerSha256,
			[StringComparison]::OrdinalIgnoreCase))
		{
			throw "Cursor Agent installer SHA-256 verification failed."
		}

		Write-InstallerProgress 18 "staging" "Cursor Agent installer"
		$downloadRoot = Join-Path $SavedDirectory "UnrealAgent\Downloads"
		$cursorInstallerPath = Join-Path $downloadRoot "cursor-agent-installer.sh"
		New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
		Copy-Item `
			-LiteralPath $resolvedInstallerPath `
			-Destination $cursorInstallerPath `
			-Force

		$normalizedInstallerPath = $cursorInstallerPath.Replace("\", "/")
		if ($normalizedInstallerPath -notmatch "^([A-Za-z]):/(.+)$")
		{
			throw "Unable to convert the Cursor installer path for WSL."
		}
		$driveLetter = $Matches[1].ToLowerInvariant()
		$relativeInstallerPath = $Matches[2]
		$wslInstallerPath = "/mnt/$driveLetter/$relativeInstallerPath"
		$installArguments = "-d $($distribution.Trim()) -u root -e bash $wslInstallerPath"
		Invoke-TrackedProcess `
			-Executable $wslPath `
			-Arguments $installArguments `
			-WorkingDirectory $SavedDirectory `
			-StartPercent 20 `
			-EndPercent 82 `
			-Stage "downloading-installing" `
			-Component "Cursor Agent CLI"
	}
	else
	{
		Write-InstallerProgress 82 "ready" "Cursor Agent CLI"
	}

	Write-InstallerProgress 88 "verifying" "Cursor Agent CLI"
	$verifyArguments = "-d $($distribution.Trim()) -u root -e /root/.local/bin/agent --version"
	Invoke-TrackedProcess `
		-Executable $wslPath `
		-Arguments $verifyArguments `
		-WorkingDirectory $SavedDirectory `
		-StartPercent 90 `
		-EndPercent 96 `
		-Stage "verifying" `
		-Component "Cursor Agent CLI"

	Write-InstallerProgress 97 "configuring" "Cursor Agent launcher"
	$launcherRoot = Join-Path $SavedDirectory "UnrealAgent\ACPAdapters\Cursor"
	$launcherPath = Join-Path $launcherRoot "cursor-agent.cmd"
	$logRoot = Join-Path $SavedDirectory "UnrealAgent\Logs"
	New-Item -ItemType Directory -Force -Path $launcherRoot | Out-Null
	New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
	$launcher = "@echo off`r`nwsl.exe 2>nul -d $($distribution.Trim()) -u root -e /root/.local/bin/agent %*`r`n"
	[IO.File]::WriteAllText($launcherPath, $launcher, [Text.Encoding]::ASCII)
	Write-InstallerProgress 100 "complete" "Cursor Agent CLI"
}

try
{
	$cleanSavedDirectory = $ProjectSavedDirectory.Trim().Trim('"')
	if ([string]::IsNullOrWhiteSpace($cleanSavedDirectory))
	{
		throw "The project Saved directory argument is empty."
	}
	$savedDirectory = [IO.Path]::GetFullPath($cleanSavedDirectory)
	New-Item -ItemType Directory -Force -Path $savedDirectory | Out-Null

	if ($Provider -eq "Codex")
	{
		Install-CodexProvider $savedDirectory $NpmPath $CodexAcpVersion
	}
	else
	{
		Install-CursorProvider `
			$savedDirectory `
			$CursorInstallerPath `
			$CursorInstallerSha256
	}

	$providerErrorLogPath = Join-Path $savedDirectory "UnrealAgent\Logs\provider-installer-error.json"
	if (Test-Path -LiteralPath $providerErrorLogPath -PathType Leaf)
	{
		Remove-Item -LiteralPath $providerErrorLogPath -Force
	}
}
catch
{
	Write-InstallerProgress 0 "failed" $Provider
	$exception = $_.Exception
	$baseException = $exception.GetBaseException()
	$errorType = $baseException.GetType().FullName
	$errorCode = "unexpected-error"
	$nativeErrorCode = $null
	if ($baseException -is [ComponentModel.Win32Exception])
	{
		$nativeErrorCode = $baseException.NativeErrorCode
		if ($nativeErrorCode -eq 1223)
		{
			$errorCode = "administrator-approval-canceled"
		}
		elseif ($nativeErrorCode -eq 740)
		{
			$errorCode = "administrator-approval-required"
		}
	}
	elseif ($baseException.HResult -eq -2147024156)
	{
		$errorCode = "administrator-approval-required"
	}
	elseif ($baseException.Message -match "WSL.+exit code")
	{
		$errorCode = "wsl-install-failed"
	}
	elseif ($baseException.Message -match "Restart Windows")
	{
		$errorCode = "windows-restart-required"
	}

	$errorRoot = if ($null -ne (Get-Variable savedDirectory -ErrorAction SilentlyContinue))
	{
		Join-Path $savedDirectory "UnrealAgent\Logs"
	}
	else
	{
		Join-Path $env:TEMP "UnrealAgent"
	}
	New-Item -ItemType Directory -Force -Path $errorRoot | Out-Null
	$errorLogPath = Join-Path $errorRoot "provider-installer-error.json"
	$errorRecord = [ordered]@{
		provider = $Provider
		code = $errorCode
		type = $errorType
		hresult = "0x{0:X8}" -f ($baseException.HResult -band 0xffffffffL)
		native_error_code = $nativeErrorCode
		message = $baseException.Message
		occurred_at_utc = [DateTime]::UtcNow.ToString("o")
	}
	[IO.File]::WriteAllText(
		$errorLogPath,
		($errorRecord | ConvertTo-Json),
		(New-Object Text.UTF8Encoding($false)))
	[Console]::Error.WriteLine(
		"UEBRIDGE_ERROR|$Provider|$errorCode|$errorType|$($errorRecord.hresult)")
	[Console]::Error.Flush()
	exit 1
}
