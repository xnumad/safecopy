#!/bin/sh

test_name="safecopy, incremental recovery with exclude blocks"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "copying reference files for safecopy"
	cp "test1.dat" "$testsuite_tmpdir/test2.dat" >/dev/null 2>&1
	cp "test1.dat" "$testsuite_tmpdir/test3.dat" >/dev/null 2>&1
	cp "test1.dat" "$testsuite_tmpdir/test4.dat" >/dev/null 2>&1
	cp "test1.dat" "$testsuite_tmpdir/test5.dat" >/dev/null 2>&1
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test4.dat"
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test5.dat"

	testsuite_debug "Test, first run with big skipsize and resolution to produce output with holes"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 4* -r 4* -X xblocks -o "$testsuite_tmpdir/test1.badblocks" debug "$testsuite_tmpdir/test1.dat" >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test1.badblocks"

	testsuite_debug "Test, incremental, big skipsize, small resolution."
	testsuite_debug " Must recover some data, not touch exclude blocks"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 8* -X xblocks -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test2.badblocks" debug "$testsuite_tmpdir/test2.dat" >"$testsuite_tmpdir/test2.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test2.out"
	fi
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test2.badblocks" "$testsuite_tmpdir/test2.badblocks"
	
	testsuite_debug "Test, incremental, small skipsize and resolution."
	testsuite_debug " Must recover all recoverable data within size limit, not touch exclude blocks"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 1* -X xblocks -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test3.badblocks" debug "$testsuite_tmpdir/test3.dat" >"$testsuite_tmpdir/test3.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test3.out"
	fi
	testsuite_assert_files_identical "test3.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test3.badblocks" "$testsuite_tmpdir/test3.badblocks"

	testsuite_debug "Test, incremental, big skipsize, small resolution, with marking."
	testsuite_debug " Must recover data at the end of bad areas but not overwrite already recovered data."
	testsuite_debug " Must not touch exclude blocks."
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 8* -X xblocks -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test4.badblocks" debug "$testsuite_tmpdir/test4.dat" >"$testsuite_tmpdir/test4.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test4.out"
	fi
	testsuite_assert_files_identical "test4.dat" "$testsuite_tmpdir/test4.dat"
	testsuite_assert_files_identical "test2.badblocks" "$testsuite_tmpdir/test4.badblocks"
	
	testsuite_debug "Test, incremental, small skipsize and resolution, with marking."
	testsuite_debug " Must recover all recoverable data, and mark the rest  within size limit"
	testsuite_debug " Must not touch exclude blocks."
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 1* -X xblocks -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test5.badblocks" debug "$testsuite_tmpdir/test5.dat" >"$testsuite_tmpdir/test5.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test5.out"
	fi
	testsuite_assert_files_identical "test5.dat" "$testsuite_tmpdir/test5.dat"
	testsuite_assert_files_identical "test3.badblocks" "$testsuite_tmpdir/test5.badblocks"

}

testsuite_runtest

