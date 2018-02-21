#! /bin/bash

## TODO: PORTING TO PYTHON

SCRIPT=$(readlink -f "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

CODEGEN=$SCRIPTPATH/../src_generator/codegen.py
DISTREF_DIR=$SCRIPTPATH/../

INCLUDE_DIR=$DISTREF_DIR/core/include
KEY_DIR=$DISTREF_DIR/user_source/keys

# %%% APP specific parameters %%%
# e.g.,  to compile test files, the input should be as follows,
# TARGET_DIR=$DISTREF_DIR/gen_source/test
# APP_DIR=$DISTREF_DIR/test
# OBJ_DIR=$DISTREF_DIR/user_source/samples/objects
TARGET_DIR=$2
APP_DIR=$3
OBJ_DIR=$4

HISTORY=$TARGET_DIR/.filehistory
NEW_HISTORY=$TARGET_DIR/.new_filehistory

function print_log {
	echo "  $1"
}

function fn_generate {
	# ./codegen.py generate [ options ] 
	# options
	# -a target applications
	# -i include for referencing during symbol analyzing
	# -k keys
	# -o objects

	$CODEGEN generate \
		-a $APP_DIR \
		-i $INCLUDE_DIR \
		-k $KEY_DIR \
		-o $OBJ_DIR \
		-O $TARGET_DIR

	local local_result=$?
	local __resultvar=$1
	eval $__resultvar="'$local_result'"
}

# Clean generated source code
if [ "$1" == "clean" ]; then
	if [ -f $HISTORY ]; then
		print_log "Clean S6 generated source code."
		rm $HISTORY
		rm -rf $TARGET_DIR
	else
		print_log "No S6 generated source code to clean"
	fi

	exit 0
fi

# Create target dir to locate generated source code
if [ ! -d $TARGET_DIR ]; then
	mkdir $TARGET_DIR

	if [ $? -ne 0 ]; then
		print_log "Fail to generate $TARGET_DIR folder"
		exit 1
	fi
fi

# Create user-level source code history
for file in `ls -1 $KEY_DIR/*.hh $KEY_DIR/*.cpp \
	$OBJ_DIR/*.hh $OBJ_DIR/*.cpp \
	$APP_DIR/*.hh $APP_DIR/*.cpp \
	2> /dev/null`
do
	if [ -f $file ]; then
		mtime=`stat -c "%Y" $file`
		echo $file $mtime >> $NEW_HISTORY
	fi
done

# Generate source code
if [ ! -f $HISTORY ]; then
	print_log "Start generating S6 source code"
	fn_generate result

	if [ $result -eq 0 ]; then
		mv $NEW_HISTORY $HISTORY
		print_log "Success to generate S6 source code"
	else
		rm $NEW_HISTORY
		print_log "Fail to generate S6 source code"
	fi

	exit $result

elif ! cmp -s $HISTORY $NEW_HISTORY ; then
	print_log "Start generating S6 source code"
	fn_generate result
	
	if [ $result -eq 0 ]; then
		mv $NEW_HISTORY $HISTORY
		print_log "Success to generate S6 source code"
	else
		rm $NEW_HISTORY
		print_log "Fail to generate S6 source code"
	fi

	exit $result
else
	print_log "Do not re-generate source code: No changes on user_source history."
	rm $NEW_HISTORY
	exit 0
fi
