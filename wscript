# -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

from waflib import Utils
import os

VERSION = '0.1.0'
APPNAME = 'ndn-cpp-cocomo'
GIT_TAG_PREFIX = 'ndn-cpp-cocomo-'

def options(opt):
    opt.load(['compiler_cxx', 'gnu_dirs'])
    opt.load(['default-compiler-flags', 'coverage', 'sanitizers'],
             tooldir=['.waf-tools'])

    optgrp = opt.add_option_group('ndn-cpp-cocomo Options')
    optgrp.add_option('--with-tests', action='store_true', default=False,
                      help='Build unit tests')

    optgrp.add_option('--with-examples', action='store_true', default=False,
                   help='Build examples')

def configure(conf):
    conf.load(['compiler_cxx', 'gnu_dirs',
               'default-compiler-flags'])

    conf.env.WITH_TESTS = conf.options.with_tests
    conf.env.WITH_EXAMPLES = conf.options.with_examples

    conf.check_compiler_flags()

    # Loading "late" to prevent tests from being compiled with profiling flags
    conf.load('coverage')
    conf.load('sanitizers')

    # If there happens to be a static library, waf will put the corresponding -L flags
    # before dynamic library flags.  This can result in compilation failure when the
    # system has a different version of the ndncert library installed.
    conf.env.prepend_value('STLIBPATH', ['.'])

    conf.define_cond('HAVE_TESTS', conf.env.WITH_TESTS)
    conf.define('SYSCONFDIR', conf.env.SYSCONFDIR)
    # The config header will contain all defines that were added using conf.define()
    # or conf.define_cond().  Everything that was added directly to conf.env.DEFINES
    # will not appear in the config header, but will instead be passed directly to the
    # compiler on the command line.
    conf.write_config_header('src/ndn-cpp-cocomo-config.hpp', define_prefix='NDN_CPP_COCOMO_')

def build(bld):
    bld.shlib(target='ndn-cpp-cocomo',
              vnum=VERSION,
              cnum=VERSION,
              source=bld.path.ant_glob('src/**/*.cpp'),
              use='',
              includes='src',
              export_includes='src')

    bld(features='subst',
        source='libndn-cpp-cocomo.pc.in',
        target='libndn-cpp-cocomo.pc',
        install_path='${LIBDIR}/pkgconfig',
        VERSION=VERSION)

    if bld.env.WITH_TESTS:
        bld.recurse('tests')

    if bld.env.WITH_EXAMPLES:
        bld.recurse('examples')

    bld.install_files(
        dest='${INCLUDEDIR}/ndn-cpp-cocomo',
        files=bld.path.ant_glob('src/**/*.hpp'),
        cwd=bld.path.find_dir('src'),
        relative_trick=True)

    bld.install_files('${INCLUDEDIR}/ndn-cpp-cocomo',
                      bld.path.find_resource('src/ndn-cpp-cocomo-config.hpp'))