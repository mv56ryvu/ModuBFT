n=32
machines="127.0.0.1,127.0.0.1,127.0.0.1,127.0.0.1,10.10.5.35,10.10.5.36,10.10.5.37,10.10.5.38,10.10.5.39,10.10.5.40,10.10.5.41,10.10.5.42,
			10.10.5.61,10.10.5.62,10.10.5.63,10.10.5.64,10.10.5.65,10.10.5.66,10.10.5.67,10.10.5.68,10.10.5.69,10.10.5.70,10.10.5.71,10.10.5.72,
			10.10.5.201,10.10.5.202,10.10.5.203,10.10.5.204,10.10.5.205,10.10.5.206,10.10.5.207,10.10.5.208"
ports="12340,12343,12346,12349,12352,123455,12358,12361,12364,12367,12370,12373,
		12376,12379,12382,12385,12388,12391,12394,12397,12400,12403,12406,12409,
		12412,12415,12418,12421,12424,12427,12430,12433"


num_peer=$2
num_client=$3
total_nodes=$((num_peer+num_client))

size=$1

role_list="2"

for r in `seq 2 $num_peer`
do
	role_list="$role_list,1"
done

for r in `seq 1 $num_client`
do
	role_list="$role_list,0"
done

ip_list=`echo $machines | cut -d ',' -f1`
port_list=`echo $ports | cut -d ',' -f1`

for id in `seq 2 $total_nodes` 
do
	nth=`echo $machines | cut -d ',' -f$id`
	ip_list="$ip_list,$nth"
	nth=`echo $ports | cut -d ',' -f$id`
	port_list="$port_list,$nth"
done

echo "NODES: $ip_list"
echo "ROLES: $role_list"
echo "PORTS: $port_list"

echo -n "COPY: "
for x in `seq 1 $total_nodes`
do
	host=`echo $machines | cut -d ',' -f$x`	
    scp ./cli* $host:/tmp/ > /dev/null
    echo -n "$x "
done
echo ""


specclient=$((num_peer+1))
nextclient=$((num_peer+2))

for x in `seq 1 $num_peer`
do
	host=`echo $machines | cut -d ',' -f$x`
    nodeId=$((x-1))
    (ssh $host "/tmp/cli-time $nodeId $ip_list $port_list $role_list $size" | grep "Through" | awk '{print $3}' > /tmp/$host.log &)
done

for x in `seq $nextclient $total_nodes`
do
	host=`echo $machines | cut -d ',' -f$x`
    nodeId=$((x-1))    
    (ssh $host "/tmp/cli-time $nodeId $ip_list $port_list $role_list $size" | grep "Through" | awk '{print $3}' > /tmp/$host.log &)
done

host=`echo $machines | cut -d ',' -f$specclient`
ssh $host "/tmp/cli-time-lat $num_peer $ip_list $port_list $role_list $size 1" | grep "Through" | awk '{print $3}' > /tmp/$host.log 

sleep 1

echo -n "CLEANUP: " 

for x in `seq 1 $total_nodes`
do
	host=`echo $machines | cut -d ',' -f$x`
    ssh $host "killall cli*" 2> /dev/null
    echo -n "$x "
done
echo ""

#spechost=`echo $machines | cut -d ',' -f$specclient`
#cp /tmp/$spechost.log ./lat-logs/size$1-peer$2-client$3.log

nbt=0
tput=0
for x in `seq $total_nodes -1 $specclient`
do
	host=`echo $machines | cut -d ',' -f$x`
	frac=`cat /tmp/$host.log`
	tput=$((tput+frac))
	nbt=`cat /tmp/$host.log`
	rm /tmp/$host.log
done

echo "TPUT: $tput"
echo "NBT: $nbt"


