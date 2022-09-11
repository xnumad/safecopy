#!/bin/sh

test_name="safecopy, ability to simply copy a regular file"

source "../libtestsuite.sh"

function test_current() {
	testsuite_debug "Test if a copy of a file created with safecopy is identical to the source."
	$safecopy "test.dat" "$testsuite_tmpdir/test.dat" >"$testsuite_tmpdir/test.out" 2>&1
	if [ $? != 0 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
		testsuite_debug_file "$testsuite_tmpdir/test.out"
	fi
	testsuite_assert_files_identical "test.dat" "$testsuite_tmpdir/test.dat"

}

testsuite_runtest


