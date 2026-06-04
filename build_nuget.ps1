# Builds ShaderGenerator.exe, smoke-tests it, and packs the NuGet package.
# Mirrors the axodox-common CI setup; run locally or from appveyor.yml.

$ErrorActionPreference = 'Stop'

# Initialize build environment
Write-Host 'Finding Visual Studio...' -ForegroundColor Magenta
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -property installationPath
Write-Host $vsPath

Write-Host 'Importing environment variables...' -ForegroundColor Magenta
cmd.exe /c "call `"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" && set > %temp%\vcvars.txt"
Get-Content "$env:temp\vcvars.txt" | ForEach-Object {
  if ($_ -match "^(.*?)=(.*)$") {
    Set-Content "env:\$($matches[1])" $matches[2]
  }
}

$nuget = "$PSScriptRoot\nuget\nuget.exe"

# Restore NuGet packages (CppWinRT is referenced via packages.config)
Write-Host 'Restoring packages...' -ForegroundColor Magenta
& $nuget restore "$PSScriptRoot\ShaderCompiler.sln"
if ($LastExitCode -ne 0) { throw "Package restore failed!" }

# Build the generator. The package ships a single ShaderGenerator.exe used as a build tool,
# so we build Release|x64; the project's post-build event copies it to nuget\bin.
$coreCount = (Get-CimInstance -Class Win32_ComputerSystem).NumberOfLogicalProcessors
$config = "Release"
$platform = "x64"

Write-Host "Building ShaderGenerator $platform $config..." -ForegroundColor Magenta
MSBuild.exe "$PSScriptRoot\ShaderGenerator\ShaderGenerator.vcxproj" -p:Configuration=$config -p:Platform=$platform -m:$coreCount -v:m

if ($LastExitCode -ne 0) {
  Write-Host "Building $platform $config failed!" -ForegroundColor Red
  throw "Building $platform $config failed!"
}
Write-Host "Building $platform $config succeeded!" -ForegroundColor Green

# Smoke test: run the freshly built generator against the sample shader and confirm it emits output.
Write-Host 'Smoke testing the generator...' -ForegroundColor Magenta
$exe = "$PSScriptRoot\nuget\bin\ShaderGenerator.exe"
if (-not (Test-Path $exe)) { throw "ShaderGenerator.exe was not produced at $exe" }

$smokeDir = Join-Path $env:temp 'ShaderGeneratorSmoke'
Remove-Item -Path $smokeDir -Recurse -Force -ErrorAction SilentlyContinue
$outDir = Join-Path $smokeDir 'output'
$hdrDir = Join-Path $smokeDir 'header'
New-Item -Path $outDir -ItemType Directory -Force | Out-Null
New-Item -Path $hdrDir -ItemType Directory -Force | Out-Null

$shader = "$PSScriptRoot\Test\ComputeShader.hlsl"
& $exe "-i=$shader" "-h=$hdrDir\" "-n=ShaderGenerator::Smoke" "-o=$outDir\" "-p=1"
if ($LastExitCode -ne 0) { throw "Smoke test failed: generator exited with code $LastExitCode" }

$csg = Get-ChildItem -Path $outDir -Filter *.csg -ErrorAction SilentlyContinue
if (-not $csg) { throw "Smoke test failed: no .csg output was produced" }
Write-Host "Smoke test passed ($($csg.Name) produced)." -ForegroundColor Green

# Pack nuget
Write-Host 'Creating output directory...' -ForegroundColor Magenta
New-Item -Path "$PSScriptRoot\Output" -ItemType Directory -Force | Out-Null

Write-Host 'Patching nuspec...' -ForegroundColor Magenta
$nuspec = [xml](Get-Content "$PSScriptRoot\nuget\ShaderGenerator.nuspec")

$nuspec.package.metadata.version = if ($null -ne $env:APPVEYOR_BUILD_VERSION) { $env:APPVEYOR_BUILD_VERSION } else { "1.0.0.0" }
$nuspec.package.metadata.repository.branch = if ($null -ne $env:APPVEYOR_REPO_BRANCH) { $env:APPVEYOR_REPO_BRANCH } else { "master" }
$commit = if ($null -ne $env:APPVEYOR_REPO_COMMIT) { $env:APPVEYOR_REPO_COMMIT } else { $null }
if ($null -ne $commit) {
  $nuspec.package.metadata.repository.SetAttribute("commit", $commit)
}

$patched = "$PSScriptRoot\nuget\ShaderGenerator.Patched.nuspec"
$nuspec.Save($patched)

Write-Host 'Creating nuget package...' -ForegroundColor Magenta
# -BasePath keeps the nuspec's relative src paths (bin\, include\, build\native\) resolving against the nuget folder.
& $nuget pack $patched -BasePath "$PSScriptRoot\nuget" -OutputDirectory "$PSScriptRoot\Output"
$packExit = $LastExitCode
Remove-Item -Path $patched -ErrorAction SilentlyContinue
if ($packExit -ne 0) { throw "nuget pack failed!" }
