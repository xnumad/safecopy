#!/bin/sh

test_name="safecopy, restoring data with persistant and non-persistant IO errors"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "Test, softerrors must be recovered, hard errors stated in badblocks file."
	LD_PRELOAD="$preload" $safecopy -R 3 -b 1024 -f 4* -o "$testsuite_tmpdir/test1.badblocks" debug "$testsuite_tmpdir/test1.dat" >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test1.badblocks"

	testsuite_debug "Test, no recovery of soft errors and skipping of blocks behind soft errors. (7,8,9)"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 4* -o "$testsuite_tmpdir/test2.badblocks" debug "$testsuite_tmpdir/test2.dat" >"$testsuite_tmpdir/test2.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test2.out"
	fi
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test2.badblocks" "$testsuite_tmpdir/test2.badblocks"

	testsuite_debug "Test, no recovery of soft errors but no skipped blocks either."
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 2* -o "$testsuite_tmpdir/test3.badblocks" debug "$testsuite_tmpdir/test3.dat" >"$testsuite_tmpdir/test3.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test3.out"
	fi
	testsuite_assert_files_identical "test3.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test3.badblocks" "$testsuite_tmpdir/test3.badblocks"

}

testsuite_runtest


