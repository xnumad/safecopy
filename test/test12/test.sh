#!/bin/sh

test_name="safecopy, correct handling of include blocks when marking badblocks (bug #2854324)"

source "../libtestsuite.sh"

function test_current() {

	testsuite_debug "copying reference files for safecopy"
	cp "test1.dat" "$testsuite_tmpdir/test1.dat" >/dev/null 2>&1
	
	testsuite_debug "Test, safecopy must not overwrite data between include blocks"
	LD_PRELOAD="$preload" $safecopy -M "MARKBAAD" -R 2 -b 1024 -f 1* -I iblocks debug "$testsuite_tmpdir/test1.dat" >"$testsuite_tmpdir/test1.out" 2>&1
	if [ $? != 1 ]; then
		testsuite_error "Run of safecopy returned wrong exit code. Output:"
                testsuite_debug_file "$testsuite_tmpdir/test1.out"
	fi
	testsuite_assert_files_identical "test1.dat" "$testsuite_tmpdir/test1.dat"


}

testsuite_runtest

