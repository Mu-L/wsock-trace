::
:: This .bat is currently called from 'appveyor.yml' at the
:: 'build_script:' stage and then with the 'init' argument.
:: The 'clean' argument is only meant to be used when running
:: this .bat file locally.

@echo off

if %1. == init.  goto init
if %1. == clean. goto clean

echo Usage: %0 "init / clean"
exit /b 1

:init
::
:: Download stuff to here if not cached:
::
set CI_DIR=C:\projects\wsock-trace\CI-temp
md %CI_DIR% 2> NUL
cd %CI_DIR%

if exist IP46-COUNTRY.BIN (
  echo 'IP46-COUNTRY.BIN' already exist.
) else (
  echo Downloading and decompressing 'IP46-COUNTRY.BIN.xz'
  curl -O -# http://www.watt-32.net/CI/IP46-COUNTRY.BIN.xz
  7z x IP46-COUNTRY.BIN.xz > NUL
)

if exist IP4-ASN.CSV (
  echo 'IP4-ASN.CSV' already exist.
) else (
  echo Downloading and decompressing 'IP4-ASN.CSV.xz'
  curl -O -# http://www.watt-32.net/CI/IP4-ASN.CSV.xz
  7z x IP4-ASN.CSV.xz > NUL
)

if exist IP6-ASN.CSV (
  echo 'IP6-ASN.CSV' already exist.
) else (
  echo Downloading and decompressing 'IP6-ASN.CSV.xz'
  curl -O -# http://www.watt-32.net/CI/IP6-ASN.CSV.xz
  7z x IP6-ASN.CSV.xz > NUL
)

cd ..

::
:: The "CPU" and "BUILDER" agnostic init-stage.
::
echo Generating %CD%\wsock_trace.appveyor...
echo #                                                                         > wsock_trace.appveyor
echo # This file was generated from %0.                                       >> wsock_trace.appveyor
echo # DO NOT EDIT!                                                           >> wsock_trace.appveyor
echo #                                                                        >> wsock_trace.appveyor
echo [core]                                                                   >> wsock_trace.appveyor
echo   trace_level              = %%WSOCK_TRACE_LEVEL%%                       >> wsock_trace.appveyor
echo   trace_indent             = 2                                           >> wsock_trace.appveyor
echo   trace_caller             = 1                                           >> wsock_trace.appveyor
echo   trace_report             = 0                                           >> wsock_trace.appveyor
echo   trace_binmode            = 1                                           >> wsock_trace.appveyor
echo   trace_time               = relative                                    >> wsock_trace.appveyor
echo   trace_time_usec          = 1                                           >> wsock_trace.appveyor
echo   trace_max_len            = %%COLUMNS%%                                 >> wsock_trace.appveyor
echo   callee_level             = 1                                           >> wsock_trace.appveyor
echo   cpp_demangle             = 1                                           >> wsock_trace.appveyor
echo   short_errors             = 1                                           >> wsock_trace.appveyor
echo   use_full_path            = 1                                           >> wsock_trace.appveyor
echo   use_toolhlp32            = 1                                           >> wsock_trace.appveyor
echo   dump_modules             = 0                                           >> wsock_trace.appveyor
echo   dump_select              = 1                                           >> wsock_trace.appveyor
echo   dump_hostent             = 1                                           >> wsock_trace.appveyor
echo   dump_protoent            = 1                                           >> wsock_trace.appveyor
echo   dump_servent             = 1                                           >> wsock_trace.appveyor
echo   dump_nameinfo            = 1                                           >> wsock_trace.appveyor
echo   dump_wsaprotocol_info    = 1                                           >> wsock_trace.appveyor
echo   dump_wsanetwork_events   = 1                                           >> wsock_trace.appveyor
echo   dump_namespace_providers = 1                                           >> wsock_trace.appveyor
echo   dump_data                = 1                                           >> wsock_trace.appveyor
echo   dump_tcpinfo             = 1                                           >> wsock_trace.appveyor
echo   max_data                 = 5000                                        >> wsock_trace.appveyor
echo   max_displacement         = 1000                                        >> wsock_trace.appveyor
echo   exclude                  = htons,htonl,inet_addr                       >> wsock_trace.appveyor
echo   hosts_file               = %CD%\CI\appveyor-hosts                      >> wsock_trace.appveyor
echo   services_file            = %CD%\CI\appveyor-services                   >> wsock_trace.appveyor
echo   use_winhttp              = 0                                           >> wsock_trace.appveyor
echo   nice_numbers             = 1                                           >> wsock_trace.appveyor

echo [geoip]                                                                  >> wsock_trace.appveyor
echo   enable                   = 1                                           >> wsock_trace.appveyor
echo   show_position            = 1                                           >> wsock_trace.appveyor
echo   show_map_url             = 1                                           >> wsock_trace.appveyor
echo   max_days                 = 10                                          >> wsock_trace.appveyor
echo   ip4_file                 = %CD%\GeoIP.csv                              >> wsock_trace.appveyor
echo   ip6_file                 = %CD%\GeoIP6.csv                             >> wsock_trace.appveyor
echo   ip2location_bin_file     = %CI_DIR%\IP46-COUNTRY.BIN                   >> wsock_trace.appveyor

echo [asn]                                                                    >> wsock_trace.appveyor
echo   enable        = 1                                                      >> wsock_trace.appveyor
echo  #asn_csv_file  = %CI_DIR%\IP4-ASN.CSV                                   >> wsock_trace.appveyor
echo  #asn_csv_file  = %CI_DIR%\IP6-ASN.CSV                                   >> wsock_trace.appveyor
echo   asn_bin_file  = %CD%\IPFire-database.db                                >> wsock_trace.appveyor
echo   asn_bin_url   = https://location.ipfire.org/databases/1/location.db.xz >> wsock_trace.appveyor
echo   xz_decompress = 1                                                      >> wsock_trace.appveyor
echo   max_days      = 1                                                      >> wsock_trace.appveyor

echo [iana]                                                                  >> wsock_trace.appveyor
echo   enable        = 0                                                     >> wsock_trace.appveyor
echo   ip4_file      = %CD%\IPv4-address-space.csv                           >> wsock_trace.appveyor
echo   ip6_file      = %CD%\IPv6-unicast-address-assignments.csv             >> wsock_trace.appveyor

echo [idna]                                                                  >> wsock_trace.appveyor
echo   enable          = 1                                                   >> wsock_trace.appveyor
echo   use_winidn      = 0                                                   >> wsock_trace.appveyor
echo   codepage        = 0                                                   >> wsock_trace.appveyor
echo   fix_getaddrinfo = 0                                                   >> wsock_trace.appveyor

echo [lua]                                                                   >> wsock_trace.appveyor
echo   enable       = %%USE_LUAJIT%%                                         >> wsock_trace.appveyor
echo   trace_level  = 1                                                      >> wsock_trace.appveyor
echo   profile      = 1                                                      >> wsock_trace.appveyor
echo   lua_init     = %CD%\src\wsock_trace_init.lua                          >> wsock_trace.appveyor
echo   lua_exit     = %CD%\src\wsock_trace_exit.lua                          >> wsock_trace.appveyor

echo [dnsbl]                                                                 >> wsock_trace.appveyor
echo   enable       = 1                                                      >> wsock_trace.appveyor
echo   max_days     = 1                                                      >> wsock_trace.appveyor
echo   drop_file    = %CD%\DROP.txt                                          >> wsock_trace.appveyor
echo   dropv6_file  = %CD%\DROPv6.txt                                        >> wsock_trace.appveyor
echo   drop_url     = http://www.spamhaus.org/drop/drop.txt                  >> wsock_trace.appveyor
echo   dropv6_url   = http://www.spamhaus.org/drop/dropv6.txt                >> wsock_trace.appveyor

echo [firewall]                                                              >> wsock_trace.appveyor
echo   enable    = 0                                                         >> wsock_trace.appveyor
echo   show_ipv4 = 1                                                         >> wsock_trace.appveyor
echo   show_ipv6 = 0                                                         >> wsock_trace.appveyor
echo   show_all  = 0                                                         >> wsock_trace.appveyor
echo   api_level = 3                                                         >> wsock_trace.appveyor

::
:: Windows-Defender thinks generating a 'hosts' file is suspicious.
:: Even generating '%CD%\CI\appveyor-hosts' triggers Windows-Defender.
:: Therefore generate it by the 'type' command below.
::
:: Ref:
::   https://www.microsoft.com/en-us/wdsi/threats/malware-encyclopedia-description?name=Trojan%3aBAT%2fQhost!gen&threatid=2147649092
::
echo Generating %CD%\CI\appveyor-hosts file...
type %CD%\CI\appveyor-hosts-contents.txt > %CD%\CI\appveyor-hosts

::
:: These should survive until 'build_script' for 'msvc', get to run.
::
set WSOCK_TRACE=%CD%\wsock_trace.appveyor
set WSOCK_TRACE_LEVEL=2
set COLUMNS=120

::
:: Some issue forces me to put the generated 'wsock_trace.appveyor'
:: in AppVeyor's %APPDATA% directory.
::
if exist c:\Users\appveyor\AppData\Roaming\. copy wsock_trace.appveyor c:\Users\appveyor\AppData\Roaming\wsock_trace > NUL
exit /b 0

::
:: Cleanup after a local 'CI\appveyor-script <builder>'.
:: This is not used by AppVeyor itself (not refered in appveyor.yml).
::
:clean
del /Q %CI_DIR%\IP46-COUNTRY.BIN %CI_DIR%\IP4-ASN.CSV %CI_DIR%\IP6-ASN.CSV 2> NUL
del /Q %CD%\wsock_trace.appveyor %CD%\CI\appveyor-hosts 2> NUL
echo Cleaning done.
exit /b 0
