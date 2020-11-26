# -*- coding: utf-8 -*-

name = 'hdcycles'

version = '0.7.23'

authors = [
    'benjamin.skinner',
]

requires = [
    'usdcycles',
    'cycles-1.13',
]


variants = [
    ['platform-windows', 'arch-x64', 'os-windows-10', 'usd-20.05-ta.1.2'],
    ['platform-windows', 'arch-x64', 'os-windows-10', 'usd-19.11-houdini'],
    ['platform-windows', 'arch-x64', 'os-windows-10', 'usd-20.08-houdini'],
]

build_system = "cmake"

# Release this as an internal package
with scope("config") as c:
    import sys
    if 'win' in str(sys.platform):
        c.release_packages_path = "R:/int"
    else:
        c.release_packages_path = "/r/int"

@early()
def private_build_requires():
    import sys
    if 'win' in str(sys.platform):
        return ['visual_studio']
    else:
        return ['gcc-7']

def pre_build_commands():
    env.HDCYCLES_BUILD_VERSION_MAJOR.set(this.version.major)
    env.HDCYCLES_BUILD_VERSION_MINOR.set(this.version.minor)
    env.HDCYCLES_BUILD_VERSION_PATCH.set(this.version.patch)

    env.HDCYCLES_BUILD_VERSION.set(str(this.version))

def commands():        
    env.HDCYCLES_ROOT.set('{root}')
    env.HDCYCLES_PLUGIN_ROOT.set('{root}/plugin')
    env.HDCYCLES_TOOLS_ROOT.set('{root}/tools')

    env.PXR_PLUGINPATH_NAME.append('{0}/usd/ndrCycles/resources'.format(env.HDCYCLES_PLUGIN_ROOT))
    env.PXR_PLUGINPATH_NAME.append('{0}/usd/hdCycles/resources'.format(env.HDCYCLES_PLUGIN_ROOT))

    env.PATH.append("{0}".format(env.HDCYCLES_TOOLS_ROOT))