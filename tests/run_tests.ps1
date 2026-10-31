$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
  New-Item -ItemType Directory -Force -Path tests/build | Out-Null
  $zig = Join-Path $projectRoot '.tools/ziglang/zig.exe'
  & $zig clang -xc++ -c -std=c++17 -Wall -Wextra -Werror -Itests/fakes -Iinclude -I.tools/ziglang/lib/include -I.tools/ziglang/lib/libc/include/any-windows-any tests/test_firmware.cpp -o tests/build/test_firmware.o
  if ($LASTEXITCODE -ne 0) { throw 'Host compilation failed' }
  $libs = (Get-ChildItem -Path .tools/zig-cache/o -Recurse -Filter *.lib).FullName
  & $zig lld-link /OUT:tests/build/test_firmware.exe /SUBSYSTEM:CONSOLE .tools/zig-cache/o/1c00ddd36bd820d422760f71e74ede7b/crt2.obj tests/build/test_firmware.o $libs
  if ($LASTEXITCODE -ne 0) { throw 'Host link failed' }
  & ./tests/build/test_firmware.exe
  if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
} finally { Pop-Location }
