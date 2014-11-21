#!/bin/bash
#set -x
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

#We run this block once
#multiple commands source this file, so we check if we already set up the environment
if [ "$TAT_CONFIG_DO_ONCE_BEFORE_UTILITY" != "yes" ]; then
    export TAT_CONFIG_DO_ONCE_BEFORE_UTILITY=yes

    export BASEDIR=`readlink -f $DIR/../`

    # path to tools
    export GIT_BIN=`which git`
    export GPG_BIN=$BASEDIR/lib/local/gpg


    # general path config
    export LOGFILE=$BASEDIR/tat.log

    #this must be below $BASEDIR
    export CONFDIR=$BASEDIR/conf

    export RESOURCE_DIR=$BASEDIR/`cat $BASEDIR/conf/local/resource-dir`
    export TOR_REPO=`cat $CONFDIR/local/tor-source/git-master`
    export LIBEVENT_REPO=`cat $CONFDIR/local/libevent-source/git-master`

    # resource dir paths
    export TOR_SRCDIR=$RESOURCE_DIR/tor/
    export OPENSSL_SRCDIR=$RESOURCE_DIR/openssl/
    export LIBEVENT_SRCDIR=$RESOURCE_DIR/libevent/


    export GPG_DIR=$CONFDIR/gnupg/
    if [ ! -d "$GPG_DIR" ]; then
        mkdir -p $GPG_DIR
    fi

    # Current versions
    export VERSION_LIBEVENT=$(cd $LIBEVENT_SRCDIR 2>/dev/null; $GIT_BIN describe --tags $($GIT_BIN rev-list --tags --max-count=1) || echo "NONE")
    export VERSION_OPENSSL=$(basename $(readlink -f "$OPENSSL_SRCDIR/current/") 2>/dev/null || echo "NONE")
    export VERSION_TOR=$(cd $TOR_SRCDIR 2>/dev/null; $GIT_BIN describe --tags $($GIT_BIN rev-list --tags --max-count=1) || echo "NONE")

    #Logging
    export LOG_LEVEL_STDERR=4
    export LOG_LEVEL_NODE=4
    export LOG_LEVEL_GLOBAL=3

    #Key evaluation recursion
    export MAX_KEY_RECURSION=5
fi



#Utility Functions
function log() {
    #get log_level
    level="$2"
    [ -z "$level" ] && level=0
    [ "$level" -eq "$level" ] 2>/dev/null || echo "ERROR: not a valid log level: $level (message: $1)"
    [ "$level" -eq "$level" ] 2>/dev/null || return
    #Get node-nickname or 'local'
    name=`basename "$nodepath"`
    [ -z "$name" ] && name="local"

    #Construct log line
    out="tat [$level] ($name): $1"
    line="$(date --iso-8601=s) $out"

    #Output to stderr
    indent=""
    for i in $(seq $level); do indent+=" "; done
    stderrout="tat [$level] ($name): ${indent}${1}"
    [ "$LOG_LEVEL_STDERR" -ge "$level" ] && echo "$stderrout" 1>&2

    #Output to global log file
    [ "$LOG_LEVEL_GLOBAL" -ge "$level" ] && echo "$line" >> $LOGFILE

    #Output to node log file
    if [ -n "$nodepath" ]; then
        node_log_path="$nodepath/.log/"
        [ -d "$node_log_path" ] || mkdir -p "$node_log_path"
        [ "$LOG_LEVEL_NODE" -ge "$level" ] && echo "$line" >> $node_log_path/$name.log
    fi
}

function debug_log() {
    echo >&2 "   $@"
}


function error_exit() {
    code="$2"
    [ -z "$code" ] && code=999
    log "ERROR: $1"
    exit $code
}


function ask_yes_or_no() {
    read -p "$1 ([y]es or [N]o): "
    case $(echo $REPLY | tr '[A-Z]' '[a-z]') in
        y|yes) echo "yes" ;;
        *)     echo "no" ;;
    esac
}

function get_node_key_path() {
    this="get_node_key_path"
    nodepath=$1
    [ -z $nodepath ] && error_exit "$this: missing nodepath"
    [ -e $nodepath/.conf/nickname ] || error_exit "$this: $nodepath is not a node"

    key=$2
    [ -z "$key" ] && error_exit "$this: No key supplied (path: $nodepath)"

    log "$this: searching for key '$key' for $nodepath" 5
    #debug_log "Node: $nodepath"
    #debug_log "Key: $key"

    current=`readlink -f $nodepath`
    #set -x
    while [ -z $keypath ]; do
        if  [ "$current" == "`readlink -f $CONFDIR`" ]; then
            error_exit "$this: key '$key' not found. ($nodepath)" 10
        fi

        if  [ "$current" == "/" ]; then
            error_exit "$this: key $key not found. And I really should not be here.($nodepath)" 10
        fi

        if [ -e $current/.conf/$key ]; then
            keypath="$current/.conf/$key"
    #        debug_log "found $keypath"
        fi

        current=`dirname $current` || break
    done

    log "$this: found path for key '$key' : $keypath" 5
    echo $keypath
}

function get_node_key_value() {
    nodepath=$1
    key=$2
    recursion_depth=$3
    this="get_node_key_value"

    [ -z "$recursion_depth" ] && recursion_depth=0
    log "$this: depth: $recursion_depth" 6
    [ "$recursion_depth" -gt "$MAX_KEY_RECURSION" ] && error_exit "$this: key '$key': maximum recursion depth reached." 42
    path=`get_node_key_path $nodepath $key` || error_exit "$this: key '$key' not found. ($nodepath)"
    if [  "$?" -eq "0" ] ; then
        all=`cat $path`
        recursion_depth=$(($recursion_depth+1))
        for tag in `rgrep -o "{{[a-zA-Z0-9_\-]*}}" $path`; do
            log "$this: found $tag, substituting" 5
            key=`echo $tag | tr -d '{' | tr -d '}'`
            value=`get_node_key_value $nodepath $key $recursion_depth + 1`
            rc=$?
            case $rc in
                0)
                    ;;
                42)
                    exit 42
                    ;;
                *)
                    error_exit "$this: key '$key' not found. ($nodepath)"
                    ;;
            esac
            log "$this: torrc: substituting $tag with $value" 5
            #all=`echo "$all" | sed s?"$tag"?"$value"?g`
            all=$(echo "$all" | awk -v t="${tag}" -v r="${value}" '{gsub(t,r)}1' )
        done

        echo "$all"
    else
        error_exit "$this: missing_key"
    fi
}

#We run this block once, but we need the utility functions (error_exit for now)
if [ "$TAT_CONFIG_DO_ONCE_AFTER_UTILITY" != "yes" ]; then
    export TAT_CONFIG_DO_ONCE_AFTER_UTILITY=yes

    #Check for required software
    for binary in git rsync gpg wget ssh uuid; do
        which $binary >/dev/null || error_exit "'$binary' is not installed."
    done

    #Check if we want to use tor for all connections
    if [ `cat $BASEDIR/conf/local/use-tor` != "no" ]; then
        torify=`which torify` || error_exit "torify not installed!"
        torsocks=`which torsocks` || error_exit "torsocks not installed!"
    else
        torify=""
        torsocks=""
        [ "$tat_warned_not_tor" != "yes" ] && log "WARNING: connections to software sources and tor hosts are not routed through tor!"
        export tat_warned_not_tor=yes
    fi

    #Disable torsocks messages
    export TORSOCKS_DEBUG=-1
    export torify
    export torsocks
fi
