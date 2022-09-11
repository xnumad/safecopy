#!/bin/false
# library include file with global variables and for safecopy test suite
# safecopy and everything with it is copyright 2009 CorvusCorax
# distributed under the terms and conditions of GPL v2 or higher

if [ -z "$testsuite_defined" ]; then
testsuite_defined=1

if [ -z "$testsuite_quiet" ]; then
	if echo "$@" |grep -q -- "quiet"; then
		testsuite_quiet=1
	else
		testsuite_quiet=0
	fi
fi

#rundir
if [ -z "$testsuite_rundir" ]; then
	testsuite_rundir=$( pwd )
fi

#basedir and testdir
if [ -z "$testsuite_basedir" -o -z "$testsuite_testdir" ]; then
	if [ -e "./libtestsuite.sh" ]; then
		testsuite_testdir=$( pwd )
		cd ..
		testsuite_basedir=$( pwd )
	elif [ -e "../libtestsuite.sh" ]; then
		cd ..
		testsuite_testdir=$( pwd )
		cd ..
		testsuite_basedir=$( pwd )
	elif [ -e "test/libtestsuite.sh" ]; then
		testsuite_basedir=$( pwd )
		cd "test"
		testsuite_testdir=$( pwd )
	else
		echo "Error. Could not determine base directory."
		echo "Please run $0 from its own directory."
		exit 1
	fi
	cd "$testsuite_rundir"
fi

#tmpdir
if [ -z "$testsuite_tmpdir" ]; then
	testsuite_tmpdir="$testsuite_testdir/tmpdir"
fi
if [ -e "$testsuite_tmpdir" ]; then
	echo "Error. Temporary directory $testsuite_tmpdir already exists."
	echo "Please move it out of the way before running $0"
	echo "since we hesitate overwriting existing data."
	exit 1
fi

#find tests
if [ -z "$testsuite_tests" ]; then
	cd "$tesstsuite_testdir"
	testsuite_tests=$( find . -wholename "*test*/test.sh" |grep -oE "test[0-9]+" | grep -oE "[0-9]+" | sort -n )
fi

#number of errors occured
if [ -z "$testsuite_errors" ]; then
	testsuite_errors=0
fi

#other global vars
#testsuite_errormessage
#testsuite_errorflag

#functions

function testsuite_setup() {
# sets up the tmpdir
	if [ -e "$testsuite_tmpdir" ]; then
		testsuite_cleanup
	fi
	mkdir "$testsuite_tmpdir"
	if ! [ -d "$testsuite_tmpdir" ]; then
		echo "Error. Could not create tmp directory"
		echo "$testsuite_tmpdir"
		exit 1
	fi
	testsuite_errorflag=0
}

function testsuite_cleanup() {
# cleans up tmpdir after testsuite is done
	if [ -d "$testsuite_tmpdir" ]; then
		rm -rf "$testsuite_tmpdir"
	fi
	if [ -d "$testsuite_tmpdir" ]; then
		echo "Error. Could not remove tmp directory"
		exit 1
	fi
}

function testsuite_error() {
	testsuite_errormessage="$testsuite_errormessage Error: $@\\n"
	testsuite_debugmessage="$testsuite_debugmessage Error: $@\\n"
	testsuite_errorflag=1
	return 1
}

function testsuite_info() {
	echo "$1" |sed -e 's/\\n/\n'
}

function testsuite_debug() {
	testsuite_debugmessage="$testsuite_debugmessage $@\\n"
}

function testsuite_debug_file() {
	file0="$1"
	testsuite_debugmessage="$testsuite_debugmessage -- ..."
	testsuite_debug $( tail -n 20 $file0 | sed -e 's/^/\\n  -- /g' )
}

function testsuite_assert_file_exists() {
	file0="$1"
	if ! [ -e "$file0" ]; then
		testsuite_error "file $file0 does not exist"
		return 1
	fi
	return 0
}

function testsuite_assert_files_identical() {
	file1="$1"
	file2="$2"
	if ! testsuite_assert_file_exists "$file1"; then
		return 1
	fi
	if ! testsuite_assert_file_exists "$file2"; then
		return 1
	fi
	if ! diff --brief "$file1" "$file2" >/dev/null 2>&1; then
		testsuite_error "file $file1 and $file2 do not match"
		return 1
	fi
	return 0
}

function testsuite_assert_filesize() {
	file1="$1"
	filesize="$2"
	if ! testsuite_assert_file_exists "$file1"; then
		return 1
	fi
	size=$( du -b "$file1" | cut -f 1  )
	if ! [ "$size" -eq "$filesize" ]; then
		testsuite_error "file $file1 has size $size, should be $filesize"
		return 1
	fi
	return 0
}

function testsuite_runtest() {
	if [ ! -e ../libtestsuite.sh ]; then
		echo "Error. Could not find myself."
		echo "Please run test from its own directory."
		exit 1
	fi
	testsuite_setup
	testsuite_debugmessage=""
	echo -n "Testing $test_name: "
	test_current
	echo "$testsuite_debugmessage" | sed -e 's/\\n/\n/g' >"$testsuite_tmpdir/errormessage.txt"
	if [ "$testsuite_errorflag" -eq 1 ]; then
		testsuite_errors=$(( testsuite_errors+1 ))
		echo "FAILED"
		if [ "$testsuite_quiet" -eq "0" ]; then
			echo "Details:"
			echo "$testsuite_debugmessage" | sed -e 's/\\n/\n/g'
		else
			echo "Details ommitted, check errormessage.txt"
		fi

		return 1
	else
		testsuite_cleanup
		echo "OK"
		return 0
	fi

}

function testsuite_runall() {
	num=0
	for current in $testsuite_tests; do
		cd "$testsuite_testdir/test$current"
		echo "Running test $current:"
		source "test.sh"
		if [ "$testsuite_errorflag" -eq 1 ]; then
			testsuite_quiet=1;
			echo "storing temporary files of failed test$current in"
			echo "$testsuite_testdir/tmp_failed_test$current"
			mv "$testsuite_tmpdir" "$testsuite_testdir/tmp_failed_test$current"
		fi
		echo
		cd "$testsuite_rundir"
		num=$(( num+1 ))
	done
	if [ "$testsuite_errors" -gt "0" ]; then
		echo "$testsuite_errors of $num tests FAILED"
		return 1
	else
		echo "Overall: OK"
		return 0
	fi
}

source "$testsuite_testdir/config_in.sh"

fi
