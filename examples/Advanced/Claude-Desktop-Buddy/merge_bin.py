# SPDX-FileCopyrightText: Copyright (c) 2025 lbuque
#
# SPDX-License-Identifier: MIT

import os
import sys
import time
from os.path import join

Import("env")

try:
    import esptool
except:
    try:
        sys.path.append(join(env['UPLOADER'], '..'))
        import esptool
    except:
        env.Execute("$PYTHONEXE -m pip install esptool")
        import esptool

verbose = True

# Filesystem data partition subtypes (spiffs / littlefs / fatfs)
FS_SUBTYPES = ("spiffs", "littlefs", "fat")

def parse_partition_table(env):
    """Parse the resolved partition table CSV and return a list of dicts,
    one per partition, with keys: name, type, subtype, offset, size, flags."""
    import csv

    partitions_csv = env.subst("$PARTITIONS_TABLE_CSV")
    if not partitions_csv or not os.path.isfile(partitions_csv):
        return []

    partitions = []
    with open(partitions_csv) as f:
        for row in csv.reader(f):
            row = [c.strip() for c in row]
            if not row or not row[0] or row[0].startswith("#"):
                continue
            # Pad to 6 columns so the optional "flags" field is always present
            row += [""] * (6 - len(row))
            name, ptype, subtype, offset, size, flags = row[:6]
            partitions.append({
                "name": name,
                "type": ptype,
                "subtype": subtype,
                "offset": int(offset, 0) if offset else None,
                "size": int(size, 0) if size else None,
                "flags": flags,
            })
    return partitions

def print_partition_table(env):
    """Print the partition table CSV path and every field of each partition."""
    partitions_csv = env.subst("$PARTITIONS_TABLE_CSV")
    print("Partition table: {}".format(partitions_csv))
    print("{:<10} {:<6} {:<10} {:>10} {:>10} {:<6}".format(
        "Name", "Type", "SubType", "Offset", "Size", "Flags"))
    for p in parse_partition_table(env):
        offset = "{:#x}".format(p["offset"]) if p["offset"] is not None else "auto"
        size = "{:#x}".format(p["size"]) if p["size"] is not None else "auto"
        print("{:<10} {:<6} {:<10} {:>10} {:>10} {:<6}".format(
            p["name"], p["type"], p["subtype"], offset, size, p["flags"]))

def get_fs_partition(env):
    """Return (offset, size) of the filesystem, matching the flash address that
    PlatformIO actually uses ($FS_START). For fatfs the lorol plugin reserves
    the first 4096-byte sector, so the image is flashed at partition offset
    + 0x1000 (see platform-espressif32 builder/main.py)."""
    filesystem = env.BoardConfig().get("build.filesystem", "spiffs")
    for p in parse_partition_table(env):
        if p["type"] == "data" and p["subtype"] in FS_SUBTYPES:
            offset, size = p["offset"], p["size"]
            if filesystem == "fatfs":
                offset += 0x1000
                size -= 0x1000
            return offset, size
    return None

def get_fs_image_path(env):
    """Return the expected path of the filesystem image in the build dir."""
    fs_name = env.subst("$ESP32_FS_IMAGE_NAME")
    if not fs_name:
        return None
    if not fs_name.endswith(".bin"):
        fs_name += ".bin"
    return join(env['PROJECT_BUILD_DIR'], env['PIOENV'], fs_name)

def get_fs_image(env):
    """Return the path of the built filesystem image, or None if missing."""
    fs_image = get_fs_image_path(env)
    return fs_image if fs_image and os.path.isfile(fs_image) else None

def build_fs_image(env):
    """Build a fresh filesystem image so the merged firmware always contains
    the latest files. A plain `pio run`/`buildprog` does NOT build the
    filesystem, so without this the FS region would be merged as blank (0xFF).
    Disable with `merge_bin_build_fs = no` in platformio.ini."""
    if env.GetProjectOption("merge_bin_build_fs", default="yes").lower() in ("no", "false", "0"):
        return
    if not get_fs_partition(env):
        return
    print("Building filesystem image before merge ...")
    # buildfs is independent of buildprog, so this does not recurse.
    env.Execute('"$PYTHONEXE" -m platformio run -e {0} -t buildfs'.format(env['PIOENV']))

def merge_bin_files(env):
    upload_cmd = env['UPLOADERFLAGS']

    chip_type = env['BOARD_MCU']
    board_config = env.BoardConfig()
    flash_size = board_config.get("upload.flash_size", "4MB")
    flash_mode = env['BOARD_FLASH_MODE']

    OUTPUT_DIR = join(env['PROJECT_BUILD_DIR'], '..', 'bin')
    output_dir = env.GetProjectOption('merge_bin_output_dir', default=OUTPUT_DIR)
    output_file = env.GetProjectOption('merge_bin_output_file', default="{0}_{1}_{2}.bin".format(
        os.path.basename(env['PROJECT_DIR']),
        env['PIOENV'],
        time.strftime("%Y%m%d_%H%M%S", time.localtime())
    ))

    if not os.path.exists(output_dir):
        os.mkdir(output_dir)

    outputFilename = join(output_dir, output_file)

    version_tuple = tuple(map(int, esptool.__version__.split('.')[:2]))

    commands = []
    commands.append('--chip')
    commands.append(chip_type)

    if version_tuple >= (5, 0):
        commands.append('merge-bin')
    else:
        commands.append('merge_bin')

    commands.append('-o')
    commands.append(outputFilename)

    if version_tuple >= (5, 0):
        commands.append('--flash-size')
    else:
        commands.append('--flash_size')
    commands.append(flash_size)

    for item in env['FLASH_EXTRA_IMAGES']:
        commands.append(item[0])
        commands.append(upload_cmd[upload_cmd.index(item[0]) + 1])

    commands.append(env['ESP32_APP_OFFSET'])
    commands.append(join(env['PROJECT_BUILD_DIR'], env['PIOENV'], '{}.bin'.format(env['PROGNAME'])))

    # Make sure the filesystem image is fresh, then merge it at its offset
    # build_fs_image(env)
    fs = get_fs_partition(env)
    fs_image = get_fs_image(env)
    if fs and fs_image:
        print("Merging filesystem image {} at {:#x}".format(fs_image, fs[0]))
        commands.append(hex(fs[0]))
        commands.append(fs_image)
    elif fs and not fs_image:
        print("Filesystem partition found but no image built; "
              "run 'pio run -t buildfs' to include it.")

    esptool.main(commands)

def after_buildprog(source, target, env):
    if verbose:
        for item in env['FLASH_EXTRA_IMAGES']:
            print("Extra image: {} at offset {}".format(item[1], item[0]))
        print("File system: {}".format(env["ESP32_FS_IMAGE_NAME"]))
        print_partition_table(env)
        fs = get_fs_partition(env)
        if fs:
            print("FS_START: {0:#x} (size {1:#x})".format(fs[0], fs[1]))
        else:
            print("FS_START: <no filesystem partition>")
    merge_bin_files(env)

env.AddPostAction("buildprog", after_buildprog)
