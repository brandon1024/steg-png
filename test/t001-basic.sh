#!/usr/bin/env bash

# verify that steg-png is installed on the path
(
	steg-png -h &&
	steg-png --help >out &&
	grep "usage: steg-png" out &&
	echo "success"
) || (
	echo "failure" &&
	exit 1
)
