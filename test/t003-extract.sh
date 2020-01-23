#!/usr/bin/env bash

(
	echo '-h and --help should print usage information' &&

	steg-png extract -h >out &&
	grep "usage: steg-png extract" out &&
	steg-png extract --help >out &&
	grep "usage: steg-png extract" out
) && (
	echo 'extract should create file with hidden data' &&

	steg-png embed -m "hello world" resources/test.png &&
	steg-png extract test.png.steg &&
	grep -e "hello world" test.png.steg &&
	cp test.png.steg.out in
	steg-png embed -f in resources/test.png &&
	steg-png extract test.png.steg &&
	cmp test.png.steg.out in
) && (
	echo '-o and --out should output to given file' &&

	steg-png embed -m "hello world" resources/test.png &&
	steg-png extract -o out test.png.steg &&
	grep -e "hello world" out &&
	cp out in
	steg-png embed -f in resources/test.png &&
	steg-png extract --output out test.png.steg &&
	cmp out in
) && (
	echo '--hexdump should output a cannonical hexdump of the embedded data' &&

	steg-png embed -m "hello world" resources/test.png &&
	steg-png extract --hexdump test.png.steg >out &&
	grep "hello world" out
) && (
	echo '--hexdump and --output should create a file' &&

	steg-png embed -m "hello world" resources/test.png &&
	steg-png extract --hexdump -o outf test.png.steg >out &&
	grep "hello world" out &&
	grep "hello world" outf
) || (
	>&2 echo "failure" &&
	exit 1
)
