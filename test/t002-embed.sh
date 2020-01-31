#!/usr/bin/env bash

(
	echo '-h and --help should print usage information' &&

	steg-png embed -h >out &&
	grep "usage: steg-png embed" out &&
	steg-png embed --help >out &&
	grep "usage: steg-png embed" out
) && (
	echo '-m and --message should embed the message in the file' &&

	steg-png embed -m "hello world" resources/test.png >out 2>err &&
	grep -e "in.*test\.png" out &&
	grep -e "out.*test\.png\.steg" out &&
	grep -e "compression factor:" out &&
	grep -e "chunks embedded in file:" out &&
	steg-png extract -o out test.png.steg &&
	grep -e "hello world" out &&
	steg-png embed --message "hello world1" resources/test.png >out 2>err &&
	steg-png extract -o out test.png.steg &&
	grep -e "hello world1" out
) && (
	echo '-f and --file should embed file' &&

	echo "hello world" | hexdump -C >in &&
	steg-png embed -f in resources/test.png >out &&
	grep -e "in.*test\.png" out &&
	grep -e "out.*test\.png\.steg" out &&
	grep -e "compression factor:" out &&
	grep -e "chunks embedded in file:" out &&
	steg-png extract -o out test.png.steg &&
	cmp out in &&
	steg-png embed --file in resources/test.png >out &&
	steg-png extract -o out test.png.steg &&
	cmp out in
) && (
	echo '-f or --file with unknown file should fail' &&

	echo "hello world" | hexdump -C >in &&
	! steg-png embed -f in123 resources/test.png 2>err &&
	grep -i "no such file or directory" err &&
	! steg-png embed --file in123 resources/test.png 2>err &&
	grep -i "no such file or directory" err
) && (
	echo '-o should output to new file' &&

	echo "hello world" | hexdump -C >in &&
	steg-png embed -f in -o steg resources/test.png >out &&
	steg-png extract -o out steg &&
	cmp out in &&
	steg-png embed --file in --output steg resources/test.png >out &&
	steg-png extract -o out steg &&
	cmp out in
) && (
	echo 'mixing -f and -m should fail' &&

	echo "hello world" | hexdump -C >in &&
	! steg-png embed -f in -m "hello world" resources/test.png 2>err &&
	grep "cannot mix --file and --message options" err
) && (
	echo '-q should silence output' &&

	steg-png embed -m "hello world" -q resources/test.png >out &&
	! [ -s out ] &&
	steg-png embed -m "hello world" --quiet resources/test.png >out &&
	! [ -s out ]
) && (
	echo 'unknown input file should fail' &&

	! steg-png embed -m "hello world"  resources/unknown 2>err &&
	grep -i "no such file or directory" err
) && (
	echo "taking input from stdin should embed correctly" &&

	echo "hello world" >in &&
	cat in | steg-png embed -o steg resources/test.png &&
	steg-png extract -o out steg &&
	grep "hello world" out
) || (
	>&2 echo "failure" &&
	exit 1
)
