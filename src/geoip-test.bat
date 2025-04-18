@echo off
::
:: Simple test for geoip/IP2Loc.
:: Rewrite this into a Python script some day.
::
setlocal
set TEST_INPUT_4=%TEMP%\geoip-addr_4.test
set TEST_INPUT_6=%TEMP%\geoip-addr_6.test

if %1. == -h. (
  echo Usage: %0 [options]
  echo ^    -h:   this help.
  echo ^    -r4:  test 10 random IPv4 addresses.
  echo ^    -r6:  test 10 random IPv6 addresses.
  echo ^    -4:   test using addresses in "%TEST_INPUT_4%".
  echo ^    -6:   test using addresses in "%TEST_INPUT_6%".
  echo ^   The above will be run via "ws_tool.exe geoip .."
  exit /b 0
)

if not exist %~dp0ws_tool.exe (
  echo The needed 'ws_tool.exe' program was not found.
  exit /b 0
)

if %1. == -r4. (
  %~dp0ws_tool.exe geoip %1 %2 %3 %4
  exit /b 0
)

if %1. == -r6. (
  %~dp0ws_tool.exe geoip %1 %2 %3 %4
  exit /b 0
)

if %1. == -4. (
  shift
  call :generate_test_input_4
  %~dp0ws_tool.exe geoip -4 %2 %3 %4 < %TEST_INPUT_4%
  exit /b 0
)

if %1. == -6. (
  shift
  call :generate_test_input_6
  %~dp0ws_tool.exe geoip -6 %2 %3 %4 < %TEST_INPUT_6%
  exit /b 0
)

call :generate_test_input_4
%~dp0ws_tool.exe geoip -4 %1 %2 %3 < %TEST_INPUT_4%
exit /b 0

:generate_test_input_4
  ::
  :: Previously data from:
  ::  ..\IP2Location-C-Library\test\country_test_ipv4_data.txt
  :: + some more.
  ::
  echo Generating %TEST_INPUT_4%...

  echo 19.5.10.1         > %TEST_INPUT_4%
  echo 25.5.10.2        >> %TEST_INPUT_4%
  echo 43.5.10.3        >> %TEST_INPUT_4%
  echo 47.5.10.4        >> %TEST_INPUT_4%
  echo 53.5.10.6        >> %TEST_INPUT_4%
  echo 81.5.10.8        >> %TEST_INPUT_4%
  echo 85.5.10.0        >> %TEST_INPUT_4%
  echo 194.38.123.15    >> %TEST_INPUT_4%
  echo 218.156.62.27    >> %TEST_INPUT_4%
  echo 78.33.206.219    >> %TEST_INPUT_4%
  echo 96.135.76.208    >> %TEST_INPUT_4%
  echo 192.31.5.47      >> %TEST_INPUT_4%
  echo 244.210.76.66    >> %TEST_INPUT_4%
  echo 170.185.77.168   >> %TEST_INPUT_4%
  echo 201.227.72.250   >> %TEST_INPUT_4%
  echo 226.212.139.179  >> %TEST_INPUT_4%
  echo 247.140.100.112  >> %TEST_INPUT_4%
  ::
  :: Add some IPv4 addresses from SpamHaus' DROP.txt too:
  ::
  echo 23.226.48.10  # part of 23.226.48.0/20  ; SBL322605 >> %TEST_INPUT_4%
  echo 84.238.160.4  # part of 84.238.160.0/22 ; SBL339089 >> %TEST_INPUT_4%
  exit /b

:generate_test_input_6
  ::
  :: Previously data from:
  ::  ..\IP2Location-C-Library\test\country_test_ipv6_data.txt
  :: + some more.
  ::
  echo Generating %TEST_INPUT_6%...

  echo 2001:0200:0102::      # JP  > %TEST_INPUT_6%
  echo 2a01:04f8:0d16:25c2:: # DE >> %TEST_INPUT_6%
  echo 2a01:04f8:0d16:26c2:: # DE >> %TEST_INPUT_6%
  echo 2a01:ad20::           # ES >> %TEST_INPUT_6%
  echo 2a01:af60::           # PL >> %TEST_INPUT_6%
  echo 2a01:b200::           # SK >> %TEST_INPUT_6%
  echo 2a01:b340::           # IE >> %TEST_INPUT_6%
  echo 2a01:b4c0::           # CZ >> %TEST_INPUT_6%
  echo 2a01:b600:8001::      # IT >> %TEST_INPUT_6%
  echo 2a01:b6c0::           # SE >> %TEST_INPUT_6%

  ::
  :: Add some special IPv6 addresses too:
  ::
  echo fd00:a41::50          # non-global   >> %TEST_INPUT_6%
  echo 2002::1               # 6to4 prefix  >> %TEST_INPUT_6%
  echo 2001:0::50            # Teredo       >> %TEST_INPUT_6%
  echo 3FFE:831F::50         # Teredo old   >> %TEST_INPUT_6%

  ::
  :: Add some IPv6 addresses from SpamHaus' DROPv6.txt too:
  ::
  echo 2a06:f680::3   # part of 2a06:f680::/29 ; SBL303641 >> %TEST_INPUT_6%
  echo 2a07:9b80::33  # part of 2a07:9b80::/29 ; SBL342980 >> %TEST_INPUT_6%
  exit /b
