#!/bin/bash

#env

NODE_VERSION="v12.13.0"
MYSQLIP=`echo ${MYSQL_HOST}`
USER=`echo ${MYSQL_USER}`
PASS=`echo ${MYSQL_ROOT_PASSWORD}`
PORT=`echo ${MYSQL_PORT}`
REBUILD=`echo ${REBUILD}`
INET=`echo ${INET}`
#hostname存在, 则优先使用hostname
DOMAIN=`echo ${DOMAIN}`

if [ "$USER" == "" ]; then
    USER="root"
fi

if [ "$PORT" == "" ]; then
    PORT="3306"
fi

if [ "$INET" == "" ]; then
   INET=(eth0)
fi

if [ "$REBUILD" != "true" ]; then
  REBUILD="false"
fi

if [ "$SLAVE" != "true" ]; then
    SLAVE="false"
fi

HOSTIP=""

if [ "$DOMAIN" != "" ]; then
  HOSTIP=$DOMAIN
else 

  #获取主机hostip
  for IP in ${INET[@]};
  do
      HOSTIP=`ifconfig | grep ${IP} -A3 | grep inet | grep broad | awk '{print $2}'    `
      echo $HOSTIP $IP
      if [ "$HOSTIP" != "127.0.0.1" ] && [ "$HOSTIP" != "" ]; then
        break
      fi
  done

  if [ "$HOSTIP" == "127.0.0.1" ] || [ "$HOSTIP" == "" ]; then
      echo "HOSTIP:[$HOSTIP], not valid. HOSTIP must not be 127.0.0.1 or empty."
      exit 1
  fi
fi

#######################################################

WORKDIR=$(cd $(dirname $0); pwd)

#######################################################
INSTALL_PATH=/usr/local/app

mkdir -p ${INSTALL_PATH}

source ~/.bashrc

if [ "$SLAVE" != "true" ]; then
  if [ ! -d ${WORKDIR}/web ]; then
      echo "no web exits, please copy TarsWeb to ${WORKDIR}/web first."
      exit 1
  fi
fi

cd ${WORKDIR}

export TARS_IN_DOCKER="true"

#mkdir dir for docker run
mkdir -p /data/tars/app_log
mkdir -p /data/tars/web_log
mkdir -p /data/tars/demo_log
mkdir -p /data/tars/patchs
mkdir -p /data/tars/remote_app_log
mkdir -p /data/tars/tarsnode-data
#解决 windows 系统 重启 docker服务或宿主机导致共享内存清空无法启动的问题
formathex(){
    local tmp=$(printf %x $1)
    local numlen=$(echo $tmp |awk '{print length($0)}')
    f="x"
    while [ $numlen -le 8 ]
    do
      f="${f}0"
      numlen=$((${numlen} + 1))
    done
    printf "${f}%x"  $1
}

for file in $(find /data/tars/tarsnode-data -name "SemKey.dat")
do
    if test -f $file
    then
      ipcid=$(cat $file)
      noipc=$(ipcs -m | grep -c $(formathex $ipcid))
      if [ "${noipc}" == "0" ];then        
        echo  "共享内存不存在,删除 $file"
        rm -f $file
      fi
    fi
done

trap 'exit' SIGTERM SIGINT

echo "start tars install"

./tars-install.sh ${MYSQLIP} ${PASS} ${HOSTIP} ${REBUILD} ${SLAVE} ${USER} ${PORT} ${INSTALL_PATH}
if [ $? != 0 ]; then
    echo  "tars-install.sh error"
    exit 1
fi

echo "install tars success. begin check server..."

while [ 1 ]
do
    sh ${INSTALL_PATH}/tars/tarsregistry/util/monitor.sh
    sh ${INSTALL_PATH}/tars/tarsnode/util/monitor.sh
    sleep 5
done


