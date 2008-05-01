#!/bin/sh

verbose=false

if [ "x$1" = "x-v" ]
then
	verbose=true
	out=/dev/stdout
	err=/dev/stderr
else
	out=/dev/null
	err=/dev/null
fi

pts=5
timeout=30
preservefs=n

echo_n () {
	# suns can't echo -n, and Mac OS X can't echo "x\c"
	# assume argument has no doublequotes
	awk 'BEGIN { printf("'"$*"'"); }' </dev/null
}

runbochs () {
	# Find the address of the kernel readline function,
	# which the kernel monitor uses to read commands interactively.
	brkaddr=`grep 'readline$' obj/kern/kernel.sym | sed -e's/ .*$//g'`
	#echo "brkaddr $brkaddr"

	# Run Bochs, setting a breakpoint at readline(),
	# and feeding in appropriate commands to run, then quit.
	t0=`date +%s.%N 2>/dev/null`
	(
		# The sleeps are necessary in some Bochs to 
		# make it parse each line separately.  Sleeping 
		# here sure beats waiting for the timeout.
		echo vbreak 0x8:0x$brkaddr
		sleep .5
		echo c
		# EOF will do just fine to quit.
	) | (
		ulimit -t $timeout
		# date
		bochs -q 'display_library: nogui' \
			'parport1: enabled=1, file="bochs.out"' 
		# date
	) >$out 2>$err
	t1=`date +%s.%N 2>/dev/null`
	time=`echo "scale=1; ($t1-$t0)/1" | sed 's/.N/.0/g' | bc 2>/dev/null`
	time="(${time}s)"
}

# Usage: runtest <tagname> <defs> <strings...>
runtest () {
	perl -e "print '$1: '"
	rm -f obj/kern/init.o obj/kern/kernel obj/kern/bochs.img 
	[ "$preservefs" = y ] || rm -f obj/fs/fs.img
	if $verbose
	then
		echo "gmake $2... "
	fi
	gmake $2 >$out
	if [ $? -ne 0 ]
	then
		echo gmake $2 failed 
		exit 1
	fi
	runbochs
	if [ ! -s bochs.out ]
	then
		echo 'no bochs.out'
	else
		shift
		shift
		continuetest "$@"
	fi
}

quicktest () {
	perl -e "print '$1: '"
	shift
	continuetest "$@"
}

stubtest () {
    perl -e "print qq|$1: OK $2\n|";
    shift
    score=`expr $pts + $score`
}

continuetest () {
	okay=yes

	not=false
	for i
	do
		if [ "x$i" = "x!" ]
		then
			not=true
		elif $not
		then
			if egrep "^$i\$" bochs.out >/dev/null
			then
				echo "got unexpected line '$i'"
				if $verbose
				then
					exit 1
				fi
				okay=no
			fi
			not=false
		else
			egrep "^$i\$" bochs.out >/dev/null
			if [ $? -ne 0 ]
			then
				echo "missing '$i'"
				if $verbose
				then
					exit 1
				fi
				okay=no
			fi
			not=false
		fi
	done
	if [ "$okay" = "yes" ]
	then
		score=`expr $pts + $score`
		echo OK $time
	else
		echo WRONG $time
	fi
}

# Usage: runtest1 [-tag <tagname>] <progname> [-Ddef...] STRINGS...
runtest1 () {
	if [ $1 = -tag ]
	then
		shift
		tag=$1
		prog=$2
		shift
		shift
	else
		tag=$1
		prog=$1
		shift
	fi
	runtest1_defs=
	while expr "x$1" : 'x-D.*' >/dev/null; do
		runtest1_defs="DEFS+='$1' $runtest1_defs"
		shift
	done
	runtest "$tag" "DEFS='-DTEST=_binary_obj_user_${prog}_start' DEFS+='-DTESTSIZE=_binary_obj_user_${prog}_size' $runtest1_defs" "$@"
}

score=0

# 20 points - run-icode
pts=20
runtest1 -tag 'updated file system switch [icode]' icode \
	'icode: read /motd' \
	'This is /motd, the message of the day.' \
	'icode: spawn /init' \
	'init: running' \
	'init: data seems okay' \
	'icode: exiting' \
	'init: bss seems okay' \
	"init: args: 'init' 'initarg1' 'initarg2'" \
	'init: running sh' \

pts=10
runtest1 -tag 'PTE_SHARE [testpteshare]' testpteshare \
	'fork handles PTE_SHARE right' \
	'spawn handles PTE_SHARE right' \

# 10 points - run-testfdsharing
pts=10
runtest1 -tag 'fd sharing [testfdsharing]' testfdsharing \
	'read in parent succeeded' \
	'read in child succeeded' 

# 10 points - run-testpipe
pts=10
runtest1 -tag 'pipe [testpipe]' testpipe \
	'pipe read closed properly' \
	'pipe write closed properly' \

# 10 points - run-testpiperace
pts=10
runtest1 -tag 'pipe race [testpiperace]' testpiperace \
	! 'child detected race' \
	! 'RACE: pipe appears closed' \
	"race didn't happen" \

# 10 points - run-testpiperace2
pts=10
runtest1 -tag 'pipe race 2 [testpiperace2]' testpiperace2 \
	! 'RACE: pipe appears closed' \
	! 'child detected race' \
	"race didn't happen" \

# 10 points - run-primespipe
pts=10
timeout=120
echo 'The primespipe test has up to 2 minutes to complete.  Be patient.'
runtest1 -tag 'primespipe' primespipe \
	! 1 2 3 ! 4 5 ! 6 7 ! 8 ! 9 \
	! 10 11 ! 12 13 ! 14 ! 15 ! 16 17 ! 18 19 \
	! 20 ! 21 ! 22 23 ! 24 ! 25 ! 26 ! 27 ! 28 29 \
	! 30 31 ! 32 ! 33 ! 34 ! 35 ! 36 37 ! 38 ! 39 \
	541 1009 1097

# 20 points - run-testshell
pts=20
timeout=60
runtest1 -tag 'shell [testshell]' testshell \
	'shell ran correctly' \

echo "Score: $score/100"

if [ $score -lt 100 ]; then
	exit 1
fi

