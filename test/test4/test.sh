#!/bin/sh

test_name="safecopy, copying from file with unreadable end"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "Test, output file is cut short but badblocks output must state the missing blocks."
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 4* -o "$testsuite_tmpdir/test1.badblocks" debug "$testsuite_tmpdir/test1.dat" >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test1.badblocks"

	testsuite_debug "Test, same test again, now with a recovered soft error."
	LD_PRELOAD="$preload" $safecopy -R 3 -b 1024 -f 4* -o "$testsuite_tmpdir/test2.badblocks" debug "$testsuite_tmpdir/test2.dat" >"$testsuite_tmpdir/test2.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test2.out"
	fi
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test2.badblocks" "$testsuite_tmpdir/test2.badblocks"

	testsuite_debug "Test, with marking, output file has correct length."
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 4* -o "$testsuite_tmpdir/test3.badblocks" debug "$testsuite_tmpdir/test3.dat" >"$testsuite_tmpdir/test3.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test3.out"
	fi
	testsuite_assert_files_identical "test3.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test3.badblocks"

	testsuite_debug "Test, same test again, now with a recovered soft error."
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 3 -b 1024 -f 4* -o "$testsuite_tmpdir/test4.badblocks" debug "$testsuite_tmpdir/test4.dat" >"$testsuite_tmpdir/test4.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test4.out"
	fi
	testsuite_assert_files_identical "test4.dat" "$testsuite_tmpdir/test4.dat"
	testsuite_assert_files_identical "test2.badblocks" "$testsuite_tmpdir/test4.badblocks"
}

testsuite_runtest

