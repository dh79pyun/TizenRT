#!/usr/bin/env bash

###########################################################################
#
# Copyright 2019 Samsung Electronics All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
# either express or implied. See the License for the specific
# language governing permissions and limitations under the License.
#
###########################################################################

###########################################################################
#
# Copyright 2019 NXP Semiconductors All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
# either express or implied. See the License for the specific
# language governing permissions and limitations under the License.
#
###########################################################################
# imxrt1050-evk_download.sh

WARNING="\n########################################\n
	WARNING:If download fails, don't forget to replug\n
	and reset the board before next try!!\n
	#################################################\n"

echo -e $WARNING

THIS_PATH=`test -d ${0%/*} && cd ${0%/*}; pwd`

# When location of this script is changed, only TOP_PATH should be changed together!!!
TOP_PATH=${THIS_PATH}/../../..
BOOTSTRAP_SCRIPT=${THIS_PATH}/bootstrap.sh

OS_PATH=${TOP_PATH}/os
OUTBIN_PATH=${TOP_PATH}/build/output/bin
TTYDEV="/dev/ttyACM0"
SUDO=sudo

# Following to read partition information
PARTITION_KCONFIG=${OS_PATH}/board/common/Kconfig
source ${OS_PATH}/.config

FLASH_START_ADDR=0x60000000
TINYARA_BIN=${OUTBIN_PATH}/tinyara.bin

##Utility function for sanity check##
function imxrt1050_sanity_check()
{
        if [ ! -f ${CONFIG} ];then
                echo "No .config file"
                exit 1
        fi

        source ${CONFIG}
        if [[ "${CONFIG_ARCH_BOARD_IMXRT1050_EVK}" != "y" ]];then
                echo "Target is NOT IMXRT1050_EVK"
                exit 1
        fi

        if [ ! -f ${TINYARA_BIN} ]; then
                echo "missing file ${TINYARA_BIN}"
                exit 1
        fi

        if fuser -s ${TTYDEV};then
                echo "${TTYDEV} is used by another process, can't proceed"
                exit 1
        fi
}

#Bootstrap to set communicaiton with blhost
#Input: None
function bootstrap()
{
	source ${BOOTSTRAP_SCRIPT}
	sleep 1

	${BLHOST} -p ${TTYDEV} -- fill-memory 0x2000 0x04 0xc0233007
	sleep 1
	${BLHOST} -p ${TTYDEV} -- configure-memory 0x09 0x2000
}

#Utility function to erase a part of flash
#Input: Address and length
function flash_erase()
{
	${SUDO} ${BLHOST} -p ${TTYDEV} -- flash-erase-region $1 $2
	echo "Successfull erased flash region from $1 of size $2 KB"
}


#Utility function to write a binary on a flash partition
#Input: Address and filepath
function flash_write()
{
	${SUDO} ${BLHOST} -p ${TTYDEV} -- write-memory $1 $2
}

#Utility function to match partition name to binary name
#NOTE: Hard-coded for now
function get_executable_name()
{
	case $1 in
		kernel) echo "tinyara.bin";;
		app) echo "tinyara_user.bin";;
		micom) echo "micom";;
		wifi) echo "wifi";;
		*) echo "No Binary Match"
	esac
}	

##Utility function to get partition index ##
function get_partition_index()
{
        case $1 in
                kernel | Kernel | KERNEL) echo "0";;
                app | App | APP) echo "1";;
                micom | Micom | MICOM) echo "2";;
                wifi | Wifi | WIFI) echo "4";;
                *) echo "No Matching Partition"
                exit 1
        esac
}

##Help utility##
function imxrt1050_dwld_help()
{
        cat <<EOF
        HELP:
                make download ERASE [PARTITION(S)]
                make download ALL or [PARTITION(S)]
        PARTITION(S):
                 [${uniq_parts[@]}]  NOTE:case sensitive

        For examples:
                make download ALL
                make download kernel app
                make download app
                make download ERASE kernel
EOF
}


##Utility function to read paritions from .config or Kconfig##
function get_configured_partitions()
{
        local configured_parts
        # Read Partitions
        if [[ -z ${CONFIG_FLASH_PART_NAME} ]];then

                configured_parts=`grep -A 2 'config FLASH_PART_NAME' ${PARTITION_KCONFIG} | sed -n 's/\tdefault "\(.*\)".*/\1/p'`
        else
                configured_parts=${CONFIG_FLASH_PART_NAME}
        fi

        echo $configured_parts
}

##Utility function to get the partition sizes from .config or Kconfig
function get_partition_sizes()
{
        local sizes_str
        #Read Partition Sizes
        if [[ -z ${CONFIG_FLASH_PART_SIZE} ]]
        then
                sizes_str=`grep -A 2 'config FLASH_PART_SIZE' ${PARTITION_KCONFIG} | sed -n 's/\tdefault "\(.*\)".*/\1/p'`
        else
                sizes_str=${CONFIG_FLASH_PART_SIZE}
        fi

        echo $sizes_str
}

# Start here

#Sanity Check
imxrt1050_sanity_check

parts=$(get_configured_partitions)
IFS=',' read -ra parts <<< "$parts"

sizes=$(get_partition_sizes)
IFS=',' read -ra sizes <<< "$sizes"


#Calculate Flash Offset
num=${#sizes[@]}
offsets[0]=`printf "0x%X" ${FLASH_START_ADDR}`

for (( i=1; i<=$num-1; i++ ))
do
	val=$((sizes[$i-1] * 1024))

	offsets[$i]=`printf "0x%X" $((offsets[$i-1] + ${val}))`
done

#Dump Info
echo "PARTIION OFFSETS: ${offsets[@]}"
echo "PARTITION SIZES: ${sizes[@]}"
echo "PARTIION NAMES: ${parts[@]}"

if test $# -eq 0; then
	echo "FAIL!! NO Given partition. Refer \"PARTITION NAMES\" above."
	imxrt1050_dwld_help 1>&2
	exit 1
fi

uniq_parts=($(printf "%s\n" "${parts[@]}" | sort -u));
cmd_args=$@

#Validate arguments
for i in ${cmd_args[@]};do

	if [[ "${i}" == "ERASE" || "${i}" == "ALL" ]];then
		continue;
	fi

	for j in ${uniq_parts[@]};do
		if [[ "${i}" == "${j}" ]];then
			result=yes
		fi
	done

	if [[ "$result" != "yes" ]];then
		echo "FAIL!! Given \"${i}\" partition is not available. Refer \"PARTITION NAMES\" above."
		imxrt1050_dwld_help
		exit 1
	fi
	result=no
done



#bootstrap
bootstrap

case $1 in
#Download ALL option
ALL)
        for part in ${uniq_parts[@]}; do
                if [[ "$part" == "userfs" ]];then
                        continue
                fi
                gidx=$(get_partition_index $part)
                flash_erase ${offsets[$gidx]} ${sizes[$gidx]}
                exe_name=$(get_executable_name ${parts[$gidx]})
                flash_write ${offsets[$gidx]} ${OUTBIN_PATH}/${exe_name}
        done
        ;;
#Download ERASE <list of partitions>
ERASE)
        while test $# -gt 1
        do
                chk=$2
                for i in "${!parts[@]}"; do
                   if [[ "${parts[$i]}" = "${chk}" ]]; then
                        flash_erase ${offsets[${i}]} ${sizes[${i}]}
                   fi
                done
                shift
        done
        ;;
#Download <list of partitions>
*)
        while test $# -gt 0
        do
                chk=$1
                for i in "${!uniq_parts[@]}"; do
                   if [[ "${uniq_parts[$i]}" = "${chk}" ]]; then
                        gidx=$(get_partition_index ${chk})
                        flash_erase ${offsets[$gidx]} ${sizes[$gidx]}
                        exe_name=$(get_executable_name ${chk})
                        flash_write ${offsets[$gidx]} ${OUTBIN_PATH}/${exe_name}
                   fi
                done
                shift
        done
esac

