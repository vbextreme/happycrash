HappyCrash v0.0.0
==================

Introduction:
=============
Introduction to the HappyCrash system, a two-phase crash reporting tool designed to generate detailed backtraces from production binaries without requiring full debug symbols. HappyCrash extracts DWARF debug information at build-time and embeds a compressed debug database into binaries, enabling runtime crash analysis even after symbol stripping.

Description:
============
Traditional crash reporting faces a deployment trade-off: full debug symbols enable detailed backtraces but significantly increase binary size, while stripped binaries are compact but produce minimal crash information (raw addresses only). This creates challenges for production deployments where binary size matters but post-mortem debugging requires source-level context.
<br><br>
HappyCrash solves this by decoupling debug information extraction from runtime crash handling, allowing binaries to be stripped of full DWARF data while retaining the ability to map crash addresses to source locations (file, function, line number).
<br><br>
Phase 1 (Build-Time): The happycrash executable processes ELF binaries with DWARF debug information, extracting address-to-source mappings and compressing them into a custom database format. This database is embedded into a new ELF section called happycrash.db. The original binary can then be stripped of full debug symbols while retaining the embedded database.
<br><br>
Phase 2 (Runtime): The libhappycrash static library links with applications to provide crash interception. When initialized via happycrash_begin(), it installs signal handlers and loads the embedded debug database from memory. Upon crashes, happycrash_backtrace() resolves stack addresses to source locations using the database and outputs detailed backtraces to stderr.

Getting Started
===============
## Dependency
```
libelf libdw
```
Debian/Ubuntu:
```
sudo apt-get install libelf-dev libdw-dev
```
Fedora:
```
sudo dnf install elfutils-devel elfutils-libelf-devel
```
Arch Linux
```
sudo pacman -S libelf
```

## Build
```
meson setup build --buildtype=plain -Doptimize='normal'
cd build
ninja
```
in the build directory we will get happycrash software and libhappycrash.a

## How to
first of all you will have to compile your software with the -g -frecord-gcc-switches flags, and link your software with -L ./build -lhappycrash.a.<br>
### Enable crash
after main need to call happycrash_begin per enable the crash system.<br>
happycrash allocate resource, can enable signal with option flags
```
#include <happycrash.h>

int main(){
    happycrash_begin(HAPPY_FULL);
}
```
### panic
raise a manual panic is simple
```
happycrash_panic("my error message %s", "oh nooooo");
```

### Usage
now that you have all the necessary code and debug flags you will have to use the software to convert this information and remove it.

```
-e        <input elf with full debug symbol>
-o        <elf output with happycrash symbol>
-s        <strip debug symbol from output>
--version <store version and display this on crash>
```
to get our elf we should run the command:
```
./build/happycrash -e mysoftware.withdebug -o mysoftware -s --version 0.0.1
```

### Extra Meson Build
if you use meson build it's more simple use happycrash.<br>
create directory subprojects in your project and copy wrap file<br>
```
cp happycrash/happycrash.wrap myproject/subprojects/
```
on your meson.build add this
```
#enable happycrash
happycrash = subproject('happycrash')

#add library in deps array
deps += [ dependency('libhappycrash', fallback: 'libhappycrash', required: true) ]

#add required flags
add_project_arguments(happycrash.get_variable('project_arguments'), language: 'c')

#get happycrash app
happycrash_app = find_program('happycrash', required: true)

#create your elf
elf = executable('@0@.elf'.format(meson.project_name()), src, include_directories: includeDir, dependencies: deps, install: false)

#convert debug info
custom_target('happycrash',
  build_by_default: true,
  input: elf,
  output: meson.project_name(),
  install: true,
  install_dir: get_option('bindir'),
  command: [ hc, '--version', '1.2.3', '-s', '-e' , '@INPUT@', '-o', '@OUTPUT@' ]
)

```


Bug:
====
try to writing many


