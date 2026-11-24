# Builds the Windows release: x64, with the Visual C++ runtime linked in so
# nothing else needs installing. Leaves the renamed plugins in
# build-release\stage\.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package_release.ps1
#
# Building with RISSETDROP_INSTALL_PLUGINS off leaves the installed plugins alone.

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-release'
$stage = Join-Path $build 'stage'

function Invoke-Checked {
    # $args[1..0] would count down and pass the command to itself
    if ($args.Count -gt 1) { & $args[0] $args[1..($args.Count - 1)] } else { & $args[0] }
    if ($LASTEXITCODE -ne 0) { throw "$($args[0]) failed with exit code $LASTEXITCODE" }
}

Invoke-Checked cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 `
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DSMTG_USE_STATIC_CRT=ON `
    -DRISSETDROP_INSTALL_PLUGINS=OFF -DRISSETDROP_BUILD_TESTS=ON
Invoke-Checked cmake --build $build --config Release --parallel

# The release passes the DSP checks before it is staged
Invoke-Checked (Join-Path $build 'Release\risset_render_test.exe')

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory $stage | Out-Null

# A VST3 bundle's inner binary must carry the bundle's name
$vst3 = Join-Path $stage 'RissetDrop.vst3'
Copy-Item -Recurse (Join-Path $build 'VST3\Release\rissetdrop_vst3_plugin.vst3') $vst3
Rename-Item (Join-Path $vst3 'Contents\x86_64-win\rissetdrop_vst3_plugin.vst3') 'RissetDrop.vst3'

Copy-Item (Join-Path $build 'Release\rissetdrop_clap_plugin.clap') (Join-Path $stage 'RissetDrop.clap')
Copy-Item (Join-Path $build 'Release\RissetDrop.exe') (Join-Path $stage 'RissetDrop.exe')

Write-Host "Staged in $stage"
