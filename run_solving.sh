
###########
RED='\033[0;31m'
NC='\033[0m' # No Color
GREEN='\033[0;32m'
BLUE='\033[0;34m'
###########


if [ $# -lt 4 ]; then
  printf "Not enough Arguments: "$0" <Maximum Process> <time limit> <BMC on/off: {1,0}> <Output csv name of runs>.\n"
  exit 1
fi



###########
RED='\033[0;31m'
NC='\033[0m' # No Color
GREEN='\033[0;32m'
BLUE='\033[0;34m'
###########

WORKER_ID=$1		#The first command line argument is a unique ID
TimeLIMIT=$2
ENABLE_BMC=$3
CSV_RESULTS=$4".csv"

echo "Solver Configuration,Instance,Wall Time,Result Code" > $CSV_RESULTS


# Simple logger (for Bash 4 and later)
log(){
  printf '%(%Y-%m-%d %H:%M:%S)T [Worker#%s] %s\n' '-1' "$WORKER_ID" "$1" >>worker.log
}

# This is a just a function to print the output as a log with timestamp
_log2() {
        printf " $(date +'[%F %T]') - $1\n"
}


solve() {
	DIMACS=$1
	fname="$(basename "${DIMACS}")"
  SOLVER="ORIGINAL"
  OPTION1=""

  if [ $ENABLE_BMC == 1 ]; then
    SOLVER="BMC"
    OPTION1="--bmc"
  fi

	printOUTPUT="${DIMACS%.*}_"$SOLVER".output"
	echo "" > $printOUTPUT

	timeout $TimeLIMIT ./cadical $OPTION1 -t $TimeLIMIT  $DIMACS >> $printOUTPUT
	
	code=`grep \^"s " $printOUTPUT | head -1 | cut -c 3-`
	#time=`grep "c \[" $printOUTPUT | head -1 | cut -d "[" -f2 | cut -d "]" -f1`
  time=`grep "total real time since initialization" $printOUTPUT | head -1 | cut -d" " -f7`
	
	if [[ $code = "UNKNOWN"  ||  $code = "" ]]; then
		time=-1
		code="UNKNOWN"
	fi
	
	echo $SOLVER","$fname","$time","$code >> $CSV_RESULTS
	
	_log2 $GREEN"Instance "$fname" DONE."$NC
}





# Wait for the queue
while [ ! -e myqueue ]
do
  sleep 1
done

touch mylock
exec 3<myqueue # FD3 <- queue
exec 4<mylock  # FD4 <- lock

while true
do

  # Read the next task from the queue
  flock 4
  IFS= read -r -u 3 task
  flock -u 4
  if [ -z "$task" ]
  then
    sleep 1
    continue
  fi
  log "Processing task: ${task}"
  solve $task
  log "Finished   task: ${task}"
  
done



