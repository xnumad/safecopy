#!/bin/sh

test_name="safecopy, continuing aborted safecopy run with arbitrary sizes"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "copying reference files for safecopy"
	cp "test1.dat" "$testsuite_tmpdir/test2.dat" >/dev/null 2>&1
	cp "test2.dat" "$testsuite_tmpdir/test3.dat" >/dev/null 2>&1
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test2.dat"
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test3.dat"
	
	testsuite_debug "Test, safecopy must copy the first half of the damaged file"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 1* -l 10 debug "$testsuite_tmpdir/test1.dat" >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"

	testsuite_debug "Test, safecopy must continue with the second half"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 1* -c 10 debug "$testsuite_tmpdir/test2.dat" >"$testsuite_tmpdir/test2.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test2.out"
	fi
	testsuite_assert_files_identical "test2.dat" "$testsuite_tmpdir/test2.dat"
	
	testsuite_debug "Test, continue in the middle with limited length"
	testsuite_debug " Must recover soft recoverable blocks and not touch rest"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 3 -b 1024 -f 1* -c 5 -l 13 debug "$testsuite_tmpdir/test3.dat" >"$testsuite_tmpdir/test3.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test3.out"
	fi
	testsuite_assert_files_identical "test3.dat" "$testsuite_tmpdir/test3.dat"

}

testsuite_runtest

