
###########
RED='\033[0;31m'
NC='\033[0m' # No Color
GREEN='\033[0;32m'
BLUE='\033[0;34m'
###########

# Input arguments
WORKER_ID=$1		#The first command line argument is a unique ID
minK=$2			#Minimum depth k
maxK=$3			#Maximum depth k
steps=$4		#Increasing step

LOGS="convert.log"


# Simple logger (for Bash 4 and later)
log(){
  printf '%(%Y-%m-%d %H:%M:%S)T [Worker#%s] %s\n' '-1' "$WORKER_ID" "$1" >>worker.log
}

# This is a just a function to print the output as a log with timestamp
_log2() {
        printf " $(date +'[%F %T]') - $1\n"
}



#*********************************************#
#*******     Convert SMV to DIMACS     *******#
#*********************************************#
gen_dimacs(){
	SMV=$1
	onlyFNameK="$(basename "${SMV}")"
	onlyNameNoExtension="${onlyFNameK%.*}"

	if test -f "$SMV" ; then
	{
	  
	  for k in $(seq $minK $steps $((${maxK}+1))); do
			_log2 $BLUE" *** Convert $onlyNameNoExtension.smv to $onlyNameNoExtension_$k.dimacs *** "$NC 
			timeout 6000 ./bsaltic -conv=1 -k=$k $SMV >> $LOGS 
		done
		
	} || {
		_log2  $RED"(1) Could not convert "$SMV" to DIMACS (SMV->DIMACS converter crashed)."$NC;
	}
	else
		_log2  $RED"$SMV file doesn't exist."$NC;
	fi
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
		gen_dimacs $task
		log "Finished   task: ${task}"
done
