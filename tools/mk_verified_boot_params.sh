#!/bin/bash

if [ $# -ne 3 ]; then
  echo "Usage: mk_verified_boot_params.sh <system.img> <system-qemu.img> <VerifiedBootParams.textproto>"
  exit 1
fi

# TODO(mattwach): After avbtool calc_kernel_cmdline (or equiv) is ready, we
# should change this script to use that subcommand instead.  The benefit
# will be more stable parsing.

# Example Output From avbtool info_image --image system.img
#
# Footer version:           1.0
# Image size:               2684354560 bytes
# Original image size:      2641915904 bytes
# VBMeta offset:            2683781120
# VBMeta size:              1024 bytes
# --
# Minimum libavb version:   1.0
# Header Block:             256 bytes
# Authentication Block:     0 bytes
# Auxiliary Block:          768 bytes
# Algorithm:                NONE
# Rollback Index:           0
# Flags:                    0
# Release String:           'avbtool 1.1.0'
# Descriptors:
#     Hashtree descriptor:
#       Version of dm-verity:  1
#       Image Size:            2641915904 bytes
#       Tree Offset:           2641915904
#       Tree Size:             20811776 bytes
#       Data Block Size:       4096 bytes
#       Hash Block Size:       4096 bytes
#       FEC num roots:         2
#       FEC offset:            2662727680
#       FEC size:              21053440 bytes
#       Hash Algorithm:        sha1
#       Partition Name:        system
#       Salt:                  27e6b2075f9a47160d21f824b923f4ebdb755c3f
#       Root Digest:           07e8da3c6ffe2ea52d14ad342beb6bb15060cd45
#       Flags:                 0
#     Kernel Cmdline descriptor:
#       Flags:                 1
#       Kernel Cmdline:        'dm="1 vroot none ro 1,0 5159992 verity 1 PARTUUID=$(ANDROID_SYSTEM_PARTUUID) PARTUUID=$(ANDROID_SYSTEM_PARTUUID) 4096 4096 644999 644999 sha1 07e8da3c6ffe2ea52d14ad342beb6bb15060cd45 27e6b2075f9a47160d21f824b923f4ebdb755c3f 10 $(ANDROID_VERITY_MODE) ignore_zero_blocks use_fec_from_device PARTUUID=$(ANDROID_SYSTEM_PARTUUID) fec_roots 2 fec_blocks 650080 fec_start 650080" root=/dev/dm-0'
#     Kernel Cmdline descriptor:
#       Flags:                 2
#       Kernel Cmdline:        'root=PARTUUID=$(ANDROID_SYSTEM_PARTUUID)'

set -e

function die {
  echo $1 >&2
  echo "tools/mk_verified_boot_kernel_options.sh might need a fix"
  exit 1
}

# Incrementing major version causes emulator binaries that do not support the
# version to ignore this file.  This can be useful if there is a change
# not supported by older emulator binaries.
readonly MAJOR_VERSION=1

readonly SRCIMG=$1
readonly QEMU_IMG=$2
readonly TARGET=$3
readonly INFO_IMAGE=$(dirname $TARGET)/SrcImgInfo.txt

# Use sgdisk to determine the partition UUID
[[ $(${SGDISK:-sgdisk} --info 1 $QEMU_IMG | grep "Partition name:" | awk '{print $3}') == "'system'" ]] || die "Partition 1 is not named 'system'."
readonly GUID=$(${SGDISK:-sgdisk} --info 1 $QEMU_IMG | grep "Partition unique GUID:" | awk '{print $4}')
[[ $GUID =~ [[:xdigit:]]{8}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{12} ]] || die "GUID looks incorrect: $GUID"


# Get the information and discard all but the "Kernel Cmdline: " parameters
${AVBTOOL:-avbtool} info_image --image $SRCIMG > $INFO_IMAGE
readonly CMDLINE=$(cat $INFO_IMAGE | grep -e grep -e "Kernel Cmdline: \+'dm=" | head -1)

# At this point, CMDLINE is a single line, similar to
#
# Kernel Cmdline:  'dm="1 vroot none ro 1,0 5159992 verity 1 ...
#
# The rest of the script extracts params from this line to create a
# commandline usable by the emulator.
#
# TODO: fec options do not work yet because they require a kernel of >=4.5.
# The emulator is running a 4.4 kernel.  This script ignores options
# for now...

dm_match_regex="dm=\"([^\"]*)\""
[[ "$CMDLINE" =~ $dm_match_regex ]]

[[ ${#BASH_REMATCH[*]} -eq 2 ]] || die "Missing dm section: $CMDLINE"

readonly DM_SECTION=${BASH_REMATCH[1]}
readonly DM_SPLIT=($(echo $DM_SECTION | tr ' ' '\n'))

# Capture everything into a named variable
readonly START_BLOCK=0
readonly SECTOR_COUNT=${DM_SPLIT[5]}
readonly VERITY_VERSION=${DM_SPLIT[7]}
readonly DATA_DEVICE="PARTUUID=$GUID"
readonly HASH_DEVICE="PARTUUID=$GUID"
readonly DATA_BLOCK_SIZE=${DM_SPLIT[10]}
readonly HASH_BLOCK_SIZE=${DM_SPLIT[11]}
readonly NUM_BLOCKS=${DM_SPLIT[12]}
readonly HASH_BLOCK_OFFSET=${DM_SPLIT[13]}
readonly HASH_ALGORITHM=${DM_SPLIT[14]}
readonly ROOT_DIGEST=${DM_SPLIT[15]}
readonly SALT=${DM_SPLIT[16]}
readonly NUM_OPTIONAL_PARAMS=1

# Sanity Checks
[[ $ROOT_DIGEST =~ [[:xdigit:]]{40} ]] || die "ROOT_DIGEST looks incorrect: $ROOT_DIGEST"
[[ $SALT =~ [[:xdigit:]]{40} ]] || die "SALT looks incorrect: $SALT"

HEADER_COMMENT="# dm=\"1 vroot none ro 1,$START_BLOCK $SECTOR_COUNT verity $VERITY_VERSION $DATA_DEVICE $HASH_DEVICE $DATA_BLOCK_SIZE $HASH_BLOCK_SIZE $NUM_BLOCKS $HASH_BLOCK_OFFSET $HASH_ALGORITHM $ROOT_DIGEST $SALT $NUM_OPTIONAL_PARAMS ignore_zero_blocks\" androidboot.veritymode=enforcing root=/dev/dm-0"

echo $HEADER_COMMENT > $TARGET
echo "major_version: $MAJOR_VERSION" >> $TARGET
echo "dm_param: \"1\"" >> $TARGET
echo "dm_param: \"vroot\"  # name" >> $TARGET
echo "dm_param: \"none\"  # UUID" >> $TARGET
echo "dm_param: \"ro\"  # Read-only" >> $TARGET
echo "dm_param: \"1,$START_BLOCK\"  # Start block" >> $TARGET
echo "dm_param: \"$SECTOR_COUNT\"  # Sector count" >> $TARGET
echo "dm_param: \"verity\"  # Type" >> $TARGET
echo "dm_param: \"$VERITY_VERSION\"  # Version" >> $TARGET
echo "dm_param: \"$DATA_DEVICE\"  # Data device" >> $TARGET
echo "dm_param: \"$HASH_DEVICE\"  # Hash device" >> $TARGET
echo "dm_param: \"$DATA_BLOCK_SIZE\"  # Data block size" >> $TARGET
echo "dm_param: \"$HASH_BLOCK_SIZE\"  # Hash block size" >> $TARGET
echo "dm_param: \"$NUM_BLOCKS\"  # Number of blocks" >> $TARGET
echo "dm_param: \"$HASH_BLOCK_OFFSET\"  # Hash block offset" >> $TARGET
echo "dm_param: \"$HASH_ALGORITHM\"  # Hash algorithm" >> $TARGET
echo "dm_param: \"$ROOT_DIGEST\"  # Root digest" >> $TARGET
echo "dm_param: \"$SALT\"  # Salt" >> $TARGET
echo "dm_param: \"$NUM_OPTIONAL_PARAMS\"  # Num optional params" >> $TARGET
echo "dm_param: \"ignore_zero_blocks\"" >> $TARGET

echo "param: \"androidboot.veritymode=enforcing\"" >> $TARGET
echo "param: \"androidboot.verifiedbootstate=orange\"" >> $TARGET
echo "param: \"root=/dev/dm-0\"" >> $TARGET



