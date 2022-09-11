#!/bin/sh

test_name="safecopy, incremental recovery with blocksize variation"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "copying reference files for safecopy"
	cp "test2.dat" "$testsuite_tmpdir/test3.dat" >/dev/null 2>&1
	cp "test2.dat" "$testsuite_tmpdir/test4.dat" >/dev/null 2>&1
	cp "test2.dat" "$testsuite_tmpdir/test5.dat" >/dev/null 2>&1
	cp "test2.dat" "$testsuite_tmpdir/test6.dat" >/dev/null 2>&1

	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test4.dat"
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test5.dat"
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test6.dat"

	testsuite_debug "Test, first run with big skipsize and resolution to produce output with holes"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1150 -f 4* -r 4* -o "$testsuite_tmpdir/test1.badblocks" debug "$testsuite_tmpdir/test1.dat" >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test1.badblocks"

	testsuite_debug "Test, first run with big skipsize and resolution to produce output with holes + marking"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1150 -f 4* -r 4* -o "$testsuite_tmpdir/test2.badblocks" debug "$testsuite_tmpdir/test2.dat" >"$testsuite_tmpdir/test2.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test2.out"
	fi
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test1.badblocks" "$testsuite_tmpdir/test2.badblocks"

	testsuite_debug "Test, incremental, big skipsize, small resolution."
	testsuite_debug " Must transpose badblocks to correct new sectorsize."
	testsuite_debug " Must recover data at the end of bad areas but not overwrite already recovered data."
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 8* -i 1150 -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test3.badblocks" debug "$testsuite_tmpdir/test3.dat" >"$testsuite_tmpdir/test3.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test3.out"
	fi
	testsuite_assert_files_identical "test3.dat" "$testsuite_tmpdir/test3.dat"
	testsuite_assert_files_identical "test3.badblocks" "$testsuite_tmpdir/test3.badblocks"
	
	testsuite_debug "Test, incremental, small skipsize and atomic resolution."
	testsuite_debug " Must recover all recoverable data"
	LD_PRELOAD="$preload" $safecopy -R 2 -b 1024 -f 1* -r 1 -i 1150 -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test4.badblocks" debug "$testsuite_tmpdir/test4.dat" >"$testsuite_tmpdir/test4.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test4.out"
	fi
	testsuite_assert_files_identical "test4.dat" "$testsuite_tmpdir/test4.dat"
	testsuite_assert_files_identical "test4.badblocks" "$testsuite_tmpdir/test4.badblocks"

	testsuite_debug "Test, incremental, big skipsize, small resolution this time with marking."
	testsuite_debug " Must recover data at the end of bad areas."
	testsuite_debug " this one may overwrite successfully rescued data as long"
	testsuite_debug " as it affects only blocks marked as bad in the include file"
	testsuite_debug " (will affect block 7 starting at position 8050"
	testsuite_debug "  since its marked for the bad data starting at 9000)"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 8* -i 1150 -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test5.badblocks" debug "$testsuite_tmpdir/test5.dat" >"$testsuite_tmpdir/test5.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test5.out"
	fi
	testsuite_assert_files_identical "test5.dat" "$testsuite_tmpdir/test5.dat"
	testsuite_assert_files_identical "test3.badblocks" "$testsuite_tmpdir/test5.badblocks"
	
	testsuite_debug "Test, incremental, small skipsize and atomic resolution, with marking."
	testsuite_debug " Must only differ from test4.dat by shifts of marker strings, no real data differences"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 1* -r 1 -i 1150 -I "$testsuite_tmpdir/test1.badblocks" -o "$testsuite_tmpdir/test6.badblocks" debug "$testsuite_tmpdir/test6.dat" >"$testsuite_tmpdir/test6.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test6.out"
	fi
	testsuite_assert_files_identical "test6.dat" "$testsuite_tmpdir/test6.dat"
	testsuite_assert_files_identical "test4.badblocks" "$testsuite_tmpdir/test6.badblocks"

}

testsuite_runtest

