###########
# project #
###########
#meson setup build --buildtype=debug(default) -g -O0
#meson setup build --buildtype=release -O3 -DNDEBUG
#meson setup build --buildtype=plain 

project('happycrash', 'c', version: '0.0.0', meson_version: '>= 1.1')

project_arguments = ['-g', '-frecord-gcc-switches']

cc = meson.get_compiler('c')
localPath = meson.current_source_dir()

version = {
  'mmr': meson.project_version().split('.'),
  'str': meson.project_version(),
  'dsc': ''
}

if get_option('eveloper')
  custom_target('xrelease',
    build_by_default: false,
    build_always_stale: true,
    command: [
      'meson.developer',
      '-d', 'elfutils',
      '-D', version['dsc'],
    ],
    output: 'xrelease'
  )
endif

###########
# include #
###########

includeDir  = include_directories('include')

###############
# source file #
###############
appsrc = [ 'src/happycrash.c' ]

libsrc = [ 'src/libhappycrash.c' ]

testsrc= [ 'src/test.c' ]

##########################
# libraries dependencies #
##########################
applibDeps  = [ cc.find_library('m', required : true) ] # math
applibDeps += [ dependency('libelf', required: true) ]
applibDeps += [ dependency('libdw', required: true) ]

liblibDeps  = [ cc.find_library('m', required : true) ] # math
liblibDeps += [ dependency('libelf', required: true) ]

if get_option('testing')
  testlibDeps  = [ cc.find_library('happycrash', dirs: [ localPath + '/build' ], static: true, required : true) ]
  testlibDeps += [ cc.find_library('m', required : true) ] # math
  testlibDeps += [ dependency('libelf', required: true) ]
  testlibDeps += [ dependency('libdw', required: true) ]
endif

##############
# data files #
##############

#############
# man files #
#############

#########################
# bash completion files #
#########################

##################
# compiler flags #
##################

# warnings
if get_option('testing')
  add_project_arguments('-g', language: 'c')
  add_project_arguments('-frecord-gcc-switches', language: 'c')
endif
add_project_arguments('-Wall', language: 'c')
add_project_arguments('-Wextra', language: 'c')
add_project_arguments('-Wuninitialized', language: 'c')
add_project_arguments('-Winit-self', language: 'c')
add_project_arguments('-fstrict-aliasing', language: 'c')
add_project_arguments('-Wstrict-aliasing=2', language: 'c')
add_project_arguments('-Wstrict-overflow', language: 'c')
add_project_arguments('-Wfloat-equal', language: 'c')
add_project_arguments('-Wvla', language: 'c')

# unicode var name
add_project_arguments('-fextended-identifiers', language: 'c')

#reentrant malloc
add_project_arguments('-pthread', language: 'c')
add_project_link_arguments('-pthread', language:'c')

# standard
add_project_arguments('-std=gnu2x', language: 'c')

# open mp
if get_option('openmp') 
  message('openmp enabled')
  add_project_arguments('-fopenmp', language:'c')
  add_project_link_arguments('-fopenmp', language:'c')
  add_project_arguments('-DOMP_ENABLE=1', language: 'c')
endif 

# optimization
optimize = get_option('optimize')
if( optimize == 'aggressive' )
  message('agressive optimization enabled')
  add_project_arguments('-O2', language: 'c')
  add_project_arguments('-march=native', language: 'c')
  add_project_arguments('-mtune=native', language: 'c')
  if( get_option('autovectorization') )
    message('vectorization enabled')
    add_project_arguments('-ftree-vectorize', language:'c')
    add_project_arguments('-DVECTORIZE=1', language:'c')
  endif
elif( optimize == 'normal' )
  message('normal optimization enabled')
  add_project_arguments('-O2', language: 'c')
elif( optimize == 'debug' )
  message('debug optimization enabled')
  add_project_arguments('-O1', language: 'c') 
  add_project_arguments('-fsanitize=address', language: 'c')
  add_project_link_arguments('-fsanitize=address', language: 'c')
  add_project_arguments('-fno-omit-frame-pointer', language: 'c')
  libDeps += [ cc.find_library('libasan', required : true) ]
endif

##########
# Define #
##########

add_project_arguments('-DVERSION_STR="' + version['str'] + '"', language: 'c')
add_project_arguments('-DVERSION_MAJ="' + version['mmr'][0] + '"', language: 'c')
add_project_arguments('-DVERSION_MIN="' + version['mmr'][1] + '"', language: 'c')
add_project_arguments('-DVERSION_REV="' + version['mmr'][2] + '"', language: 'c')

#########################
# software dependencies #
#########################

#################
# Custom Target #
#################

#########
# debug #
#########

###########
# license #
###########

message('copyright vbextreme 2024')
message('released under GPLv3')

#########
# build #
#########

if get_option('testing')
  hc = find_program('happycrash',  dirs: [ localPath + '/build' ], required: true)
  elf = executable('mytest.elf', testsrc, include_directories: includeDir, dependencies: testlibDeps, install: false)
  custom_target('happycrash',
    build_by_default: true,
    input: elf,
    output: 'mytest',
    command: [ hc, '--version', '1.22.333', '-s', '-e' , '@INPUT@', '-o', '@OUTPUT@' ]
  )
else
  hc_app = executable(meson.project_name(), appsrc, include_directories: includeDir, dependencies: applibDeps, install: false)
  hc_lib = static_library(meson.project_name(), libsrc, include_directories: includeDir, dependencies: liblibDeps, install: false)
  hc_dep = declare_dependency(link_with: hc_lib, include_directories: 'include')
  meson.override_dependency('libhappycrash', hc_dep)
  meson.override_find_program('happycrash', hc_app)
endif


















