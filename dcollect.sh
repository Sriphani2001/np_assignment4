#!/bin/bash

##Variables update as to fit your scenario
portFORK=8282
portTHREAD=8281
testing=1

head -c 10000 < /dev/urandom > big
head -c 1000 < /dev/urandom > small 

if [[ "$testing" == "0" ]]; then
    ## Use for Data collection
    echo "Real Data Collection"
    CONCURRENCY=60
    REPEAT=32
else
    ## Use for _TESTING__
    echo "**TESTING only**"
    CONCURRENCY=5
    REPEAT=10
fi


## Clean up statistics files
rm -rf statistics_fork.log
rm -rf statistics_thread.log

echo "Concurrency = $CONCURRENCY, REPETITONS = $REPEAT."

echo "*** Basic server testing 1."
echo ""
echo "Create a RANDOM text file."
randomString=$(echo $RANDOM | md5sum | head -c 20)
echo -e "$randomString"  > randomFile

##echo "Testing that the forked server can retrevie the newly created randomFile"
fString=$(curl -s http://127.0.0.1:$portFORK/randomFile)


if [[ "$randomString" == "$fString" ]]; then
    echo "SUMMARY: The Forked server, correctly delivered the file (randomFile)."
else
    echo "ERRROR: The Forked server, _did not_ work as expectec (randomFile)."
    echo -e "RandomSRC: $randomString"  
    echo "   server: $fString"
    exit 1
fi

##echo "Testing that the threaded server can retrevie the newly created randomFile"
fString=$(curl -s http://127.0.0.1:$portTHREAD/randomFile)


if [[ "$randomString" == "$fString" ]]; then
    echo "SUMMARY: The Forked server, correctly delivered the file (randomFile)."
else
    echo "ERROR: The Forked server, _did not_ work as expectec (randomFile)."
    echo -e "RandomSRC: $randomString"  
    echo "   server: $fString"
    exit 1
fi


echo " "

echo "*** Basic server testing 2"
echo " "
echo "Create a randomly named file. "
fname=$(mktemp -p $(pwd) );
randomString2=$(echo $RANDOM | md5sum | head -c 20)
echo "Writing '$randomString2' to $fname"
echo "$randomString2" > $fname

url=$(basename "$fname" );


echo "Giving the filesystem some time to stabilize."
sleep 2

##echo "Testing that the forked server can retrevie the newly created $url"
fString=$(curl -s http://127.0.0.1:$portFORK/$url)
if [[ "$randomString2" == "$fString" ]]; then
    echo "SUMMARY: The Forked server, correctly delivered randomly named the file."
else
    echo "ERROR: The Forked server, _did not_ work as expected (random filename)."
    echo -e "RandomSRC: $randomString2"  
    echo "   server: $fString"
    exit 1
fi

##echo "Testing that the forked server can retrevie the newly created $url"
fString=$(curl -s http://127.0.0.1:$portTHREAD/$url)
if [[ "$randomString2" == "$fString" ]]; then
    echo "SUMMARY: The Threaded server, correctly delivered randomly named file."
else
    echo "ERROR: The Threaded server, _did not_ work as expected (random filename)"
    echo -e "RandomSRC: $randomString2"  
    echo "   server: $fString"
    exit 1
fi
echo "" 

echo "*** Performance forked server."
echo " "


## Remove any performance data files.
rm -rf perf_*.txt
for ((i=1;i<CONCURRENCY;i++)); do
    for ((k=1;k<REPEAT;k++)); do
	bonkers=$(ab -n 10000 -c $i http://127.0.0.1:$portFORK/big 2>/dev/null | grep 'Requests per second');
	value=$(echo "$bonkers" | awk '{print $4}');
	echo "C=$i,$k => $value";
	if [[ -z "$value" ]]; then
	    echo "ERROR: No usefull data was collected from AB, this is an serious ISSUE."
	    echo "ERROR: Check server on http://127.0.0.1:$portFORK/big "
	    exit 1
	fi
	echo "$value" >> "perf_$i.txt" ;
    done;
    echo "Done all repetitions for $i, doing statistics (with awk). "
    statistics=$(awk '{for(i=1;i<=NF;i++) {sum[i] += $i; sumsq[i] += ($i)^2}} 
          END {for (i=	     1;i<=NF;i++) { 
          printf "%f %f \n", sum[i]/NR, sqrt((sumsq[i]-sum[i]^2/NR)/NR)}
         }' perf_$i.txt)
    echo "$i => $statistics " | tee -a statistics_fork.log
done


echo "*** Performance threaded server."
echo " "

## Remove any performance data files. 
rm -rf perf_*.txt
for ((i=1;i<CONCURRENCY;i++)); do
    for ((k=1;k<REPEAT;k++)); do
	bonkers=$(ab -n 10000 -c $i http://127.0.0.1:$portTHREAD/big 2>/dev/null | grep 'Requests per second');
	value=$(echo "$bonkers" | awk '{print $4}');
	echo "C=$i,$k => $value";
	if [[ -z "$value" ]]; then
	    echo "ERROR: No usefull data was collected from AB, this is an serious ISSUE."
	    echo "ERROR: Check server on http://127.0.0.1:$portTHREAD/big "
	    exit 1
	fi
	echo "$value" >> "perf_$i.txt" ;
    done;
    echo "Done all repetitions for $i, doing statistics (with awk). "
    statistics=$(awk '{for(i=1;i<=NF;i++) {sum[i] += $i; sumsq[i] += ($i)^2}} 
          END {for (i=	     1;i<=NF;i++) { 
          printf "%f %f \n", sum[i]/NR, sqrt((sumsq[i]-sum[i]^2/NR)/NR)}
         }' perf_$i.txt)
    echo "$i => $statistics " | tee -a statistics_thread.log
done


gnuplot dcollect.p

echo "SUMMARY: Did it work?"
