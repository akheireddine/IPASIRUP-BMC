#!/bin/bash

if [ $# -lt 2 ]; then
  printf "Not enough Arguments: "$0" <worker script> <status: start, stop, status> [OPTIONS].\n"
  exit 1
fi

WorkerSCRIPT=$1
CMD=$2

if [ $CMD = "start" ]; then
	if [ $WorkerSCRIPT = "run_solving.sh" ]; then
		if [ $# -lt 6 ]; then
			  printf "Not enough Arguments for run_solving.sh task. Need to specify the following:\n"
			  printf "\t<Maximum Pool> \n\t<time limit> \n\t<Solver: maple,kissat> \n\t<Output csv name of runs> \n\t[OPTIONS].\n"

		exit 1
		fi
	else
		if [ $# -lt 6 ]; then
			printf "Not enough argument when for translatorWorker.sh task. Need to specify the following:\n"
			printf "\t<Maximum Pool> \n\t<Minimum bound K> \n\t<Maximum bound K> \n\t<Increasing step>\n"
			exit 1
		fi
	fi
fi

# Input arguments
WORKERS=$3		#The pool of workers size:
minK=$4			#Minimum depth k   OR time limit
maxK=$5			#Maximum depth k   OR solver name
steps=$6		#Increasing step   OR output csv name
option1=$7		#Other options like -log when using bsaltic tool
option2=$8


# Check the pool status
status(){
  alive=0
  for p in pid.*
  do
    [ "$p" = 'pid.*' ] && break
    pid=`echo $p | cut -d '.' -f 2`
    #pid="${p:4}"
    wk=`ps -fp "$pid" 2>/dev/null | sed -n 's/.* '$WorkerSCRIPT' //p'`
    if [ ! -z "$wk" ]
    then
      alive=$((alive+1))
      #let "alive++"
      [ $1 = 0 ] && printf 'Worker %s is running, PID %s\n' "$wk" "$pid"
    else
      rm -f "$p"
    fi
  done
  if [ $1 = 0 ]
  then
    [ $alive = 0 ] && printf 'NOK\n' || printf 'OK: %s/%s\n' $alive "$WORKERS"
  fi
  return $alive
}

# Stop the pool
stop(){
  for p in pid.*
  do
    [ "$p" = 'pid.*' ] && break
    pid=`echo $p | cut -d '.' -f 2`
    #pid="${p:4}"
    wk=`ps -fp "$pid" 2>/dev/null | sed -n 's/.* '$WorkerSCRIPT' //p'`
    if [ ! -z "$wk" ]
    then
      kill -9 "$pid" 2>/dev/null
      sleep 0
      kill -0 "$pid" 2>/dev/null && sleep 1 && kill -9 "$pid" 2>/dev/null
    fi
    rm -f "$p"
  done
  rm -f myqueue mylock
}

# Start the pool
run(){
  status 1
  [ $? != 0 ] && printf 'Already running\n' && exit 0

  # Setup the queue
  rm -f myqueue mylock
  #rm convert.log
  mkfifo myqueue

  # Launch N workers in parallel
  for i in `seq $WORKERS`
  do
	/bin/bash $WorkerSCRIPT $i $minK $maxK $steps $option1 $option2 &
    touch pid.$!
  done
}

case $CMD in
  "start")
    run
    ;;
  "stop")
    stop
    ;;
  "status")
    status 0
    ;;
  *)
    printf 'Unsupported command\n'
    ;;
esac
