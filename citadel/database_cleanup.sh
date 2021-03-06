#!/bin/bash

die () {
	echo Exiting.
	exit 1
}

DATA_DIR="/usr/local/citadel"

usage() {
	echo "Usage: database_cleanup.sh [ -h citadel_dir ]"
	exit 2
}

PARSED_ARGUMENTS=$(getopt -a -n database_cleanup.sh -o h: -- "$@")
VALID_ARGUMENTS=$?
if [ "$VALID_ARGUMENTS" != "0" ]; then
	usage
fi

eval set -- "$PARSED_ARGUMENTS"
while :
do
	case "$1" in
		-h | --alpha)
			DATA_DIR=${2}
			shift 2
			;;
		# -- means the end of the arguments; drop this, and break out of the while loop
		--) shift; break ;;
		# If invalid options were passed, then getopt should have reported an error,
		# which we checked as VALID_ARGUMENTS when getopt was called...
		*) echo "Unexpected option: $1 - this should not happen."
			usage
			;;
	esac
done

DATA_DIR=$DATA_DIR/data

# If we're on a Docker or Easy Install system, use our own db_ tools.
if [ -x /usr/local/ctdlsupport/bin/db_dump ] ; then
	export PATH=/usr/local/ctdlsupport/bin:$PATH
	RECOVER=/usr/local/ctdlsupport/bin/db_recover
	DUMP=/usr/local/ctdlsupport/bin/db_dump
	LOAD=/usr/local/ctdlsupport/bin/db_load

# otherwise look in /usr/local
elif [ -x /usr/local/bin/db_dump ] ; then
	export PATH=/usr/local/bin:$PATH
	RECOVER=/usr/local/bin/db_recover
	DUMP=/usr/local/bin/db_dump
	LOAD=/usr/local/bin/db_load

# usual install
else
	if test -f /usr/bin/db_dump; then 
		RECOVER=/usr/bin/db_recover
		DUMP=/usr/bin/db_dump
		LOAD=/usr/bin/db_load
	else
		if test -n "`ls /usr/bin/db?*recover`"; then
			# seems we have something debian alike thats adding version in the filename
			if test "`ls /usr/bin/db*recover |wc -l`" -gt "1"; then 
				echo "Warning: you have more than one version of the Berkeley DB utilities installed." 1>&2
				echo "Using the latest one." 1>&2
				RECOVER=`ls /usr/bin/db*recover |sort |tail -n 1`
				DUMP=`ls /usr/bin/db*dump |sort |tail -n 1`
				LOAD=`ls /usr/bin/db*load |sort |tail -n 1`
			else
				RECOVER=`ls /usr/bin/db*recover`
				DUMP=`ls /usr/bin/db*dump`
				LOAD=`ls /usr/bin/db*load`
			fi
		else
			echo "database_cleanup.sh cannot find the Berkeley DB utilities.  Exiting." 1>&2
			die
		fi

	fi
fi

# Ok, let's begin.

clear
cat <<!

Citadel Database Cleanup
---------------------------

This script exports, deletes, and re-imports your database.  If you have
any data corruption issues, this program may be able to clean them up for you.
 
Please note that this program does a Berkeley DB dump/load, not a Citadel
export.  The export files are not generated by the Citadel export module.

WARNING #1:
  MAKE A BACKUP OF YOUR DATA BEFORE ATTEMPTING THIS.  There is no guarantee
  that this will work (in case of disk full, power failure, program crash)!

WARNING #2:
  citserver must NOT be running while you do this.

WARNING #3:
  Please try "cd $DATA_DIR; $RECOVER -c" first. Run citserver afterwards to 
  revalidate whether its fixed or not, No news might be good news. Use this
  tool only if that one fails to fix your problem.

WARNING #4:
  You must have an amount of free space in /tmp that is at least twice
  the size of your database. see the following output:

`df -h`

you will need `du -sh $DATA_DIR|sed "s;/.*;;"` of free space.

!

echo We will attempt to look for a Citadel database in $DATA_DIR
echo -n "Do you want to continue? "

read yesno
case "$yesno" in
	"y" | "Y" | "yes" | "YES" | "Yes" )
		echo 
		echo DO NOT INTERRUPT THIS PROCESS.
		echo
	;;
	* )
		exit
esac

for x in 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d
do
	filename=cdb.$x
	echo Dumping $filename
	$DUMP -h $DATA_DIR $filename >/tmp/CitaDump.$x || {
		echo error $?
		die
	}
	rm -vf $DATA_DIR/$filename
done

echo Removing old databases
rm -vf $DATA_DIR/*

for x in 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d
do
	filename=cdb.$x
	echo Loading $filename
	$LOAD -h $DATA_DIR $filename </tmp/CitaDump.$x && {
		rm -f /tmp/CitaDump.$x
	}
done

echo 
echo Dump/load operation complete.  Start your Citadel server now.
echo
