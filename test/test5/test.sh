#!/bin/sh

test_name="safecopy, copying from completely unreadable file"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "Test, no marking, output file is 0 bytes, badblock list complete"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 4* -o "$testsuite_tmpdir/test1.badblocks" debug "$testsuite_tmpdir/test1.dat" >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test1.badblocks"

	testsuite_debug "Test, marking, output file is correct size all marked, badblock list complete"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 4* -o "$testsuite_tmpdir/test2.badblocks" debug "$testsuite_tmpdir/test2.dat" >"$testsuite_tmpdir/test2.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test2.out"
	fi
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test2.badblocks"

}

testsuite_runtest

