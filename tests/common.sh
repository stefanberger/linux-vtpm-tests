# For the license, see the LICENSE file in the root directory.

# wait for a char device to appear
# @param1: device name
# @param2: timeout in s
wait_for_device() {
	local dev="$1"
	local to="$2"

	while [ $to -ge 0 ] && [ ! -c ${dev} ]; do
		sleep 1
		to=$((to-1))
	done

	if [ -c ${dev} ]; then
		return 0
	fi
	return 1
}

