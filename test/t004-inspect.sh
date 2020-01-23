#!/usr/bin/env bash

(
	echo 'stEG chunk is not first chunk in the file' &&

	steg-png embed -m "hello world" resources/test.png &&
	steg-png inspect test.png.steg >out &&
	! grep "chunks: stEG" out
) && (
	echo 'inspect should list chuns in file' &&

	steg-png inspect resources/test.png >out &&
	grep -e "^chunks:" -C 0 out >chunks &&
	grep "IHDR" chunks &&
	grep "IEND" chunks &&
	! grep "stEG" chunks &&
	steg-png embed -m "hello world" resources/test.png &&
	steg-png inspect test.png.steg >out &&
	grep -e "^chunks:" -C 0 out >chunks &&
	grep "IHDR" chunks &&
	grep "IEND" chunks &&
	grep "stEG" chunks
) && (
	echo '--filter should filter chunks by chunk type' &&

	steg-png inspect --filter IEND resources/test.png >out &&
	! grep "chunk type: IHDR" out &&
	steg-png inspect resources/test.png >out &&
	grep "chunk type: IHDR" out &&
	steg-png embed -m "hello world" resources/test.png &&
	steg-png inspect --filter stEG test.png.steg >out &&
	grep "chunk type: stEG" out &&
	steg-png inspect test.png.steg >out &&
	grep "chunk type: stEG" out
) && (
	echo '--critical should only show critical chunks' &&

	steg-png embed -m "hello world" resources/test.png &&
	steg-png inspect --critical test.png.steg >out &&
	! grep "chunk type: stEG" out &&
	! grep "chunk type: iTXt" out &&
	grep "chunk type: IEND" out
) && (
	echo '--ancillary should only show ancillary chunks' &&

	steg-png embed -m "hello world" resources/test.png &&
	steg-png inspect --ancillary test.png.steg >out &&
	grep "chunk type: stEG" out &&
	grep "chunk type: iTXt" out &&
	! grep "chunk type: IEND" out &&
	! grep "chunk type: IHDR" out
) && (
	echo '--hexdump should display hexdump of chunk data' &&

	steg-png inspect --hexdump resources/test.png >out &&
	grep -e '^[0-9a-z]\{8\}  \(\([0-9a-z]\)\{2\} \)\{16\} |.*|$' out
) || (
	>&2 echo "failure" &&
	exit 1
)