#tor administrator toolkit

##Motivation

managing multiple tor nodes on different hosts with various configurations is hard and boring. Doing it while maintaining good operational security makes it even more tedious.

tat tries to make it easy to

 * manage multiple tor nodes
 * built the most recent software versions and verify the integrity of the source code
 * regularly verify the integrity of the deployed nodes
 * adapt and extend it to individual requirements.
 * log all actions and results
 * route all communication to the nodes and software sites through tor

##Concept

tat consists of:

 * the `tat` script, which is a wrapper for calling the actual operations
 * these operations are in `lib`, with operations affecting a node below `lib/node` and local operations below `lib/local`
 * the local configuration and configuration for all managed nodes below `conf`

###Configuration hierarchy
The information about all nodes is stored in the directory `conf/nodes/all`.

This directory contains a hierarchy of directories, with the lowest level, the leaves, representing the actual tor nodes.

Every directory may have children (direct subdirectories) and a special `.conf` subdirectory. This contains files which names are used as keys to configure the tor node. If a certain key for a node is required, the search starts at the `.conf` directory of the node and if it is not present there recursively moves to its parent.

The content of the .conf-file with the matching name will replace the placeholders in double curly braces in the 'torrc-template' and 'service-template' files.

The `torrc-template` and `service-template` are also files in `.conf` directories, they are searched for by `lib/node/generate_node_conf`.

Also shell executables like `tat_execute` and `tat_setup-build-env` are stored there, this provides an easy method to modify actions to the nodes.

####An example

conf/nodes/all:

    conf/nodes/all
    |--.conf
    |  |--torrc-template
    |  |--orPort (443)
    |  |--numCPUs (1)
    |--mytorserver.com
       |--.conf
       |  |--numCPUs (2)
       |  |--hostname (mytorserver.com)
       |--myfirstnode
          |--.conf
             |--nickname (myfirstnode)

torrc-template:

    Nickname            {{nickname}}
    NumCPUs             {{numCPUs}}
    ORPort              {{orPort}}

Generated myfirstnode.rc:

    Nickname            myfirstnode
    NumCPUs             2
    ORPort              443

`{{nickname}}` is resolved directly in the node, `{{numCPUs}}` in the host-level (myserver.net) which overrides the default value of 1. `{{orPort}}` is neither set for the node nor for the host, so the root-level default (443) is used.

The key-files may contain curly braces placeholders themselves, which are again resolved from the node level, up to a maximal recursion depth set by `MAX_RECURSION_DEPTH` in `/lib/common.inc`.
###Verification

Tor and libevent developers sign release tags in the respective git repositories with gpg. openssl provides a signature for the released tarballs. These cryptographic signatures should be verified whenever the source is updated. tat does so automatically

To detect manipulation of deployed nodes, hashes of the installed files can be used. tat stores a hash of the hashes of all files in the installation directories (`/opt/tor/dist/libevent`, `/opt/tor/dist/openssl` and `/opt/tor/dist/tor`) on deployment. These hashes, and so the integrity of these directories, are verified on each update or on demand.

###Logging
Every operation is logged to the global log `tat.log`.

For every node, every operation affecting it is also logged to `NODEPATH/.log/`.

The verbosity level for all logging activity can be set via the `LOG_LEVEL_*` settings in `lib/common.inc`.

###Extendability

tat is a collection of bash scripts and provides easy ways to handle special cases, extend it to provide new functionality.

`conf/` contains the configuration for all nodes, as well as the local settings.
`lib/` contains all scripts, separated in `lib/local/` for node-independent actions (like get_openssl) and `lib/node/` for all possible actions on a node. `lib/common.inc` reads the local configuration and provides utility functions.

If you know some shell scripting, reading through these scripts should give you a good insight how tat works and how it can be extended.

###Requirements

tat is a collection of bash scripts and uses following tools to operate:

 * git
 * gpg
 * torify and torsocks (optional if you set `conf/local/use-tor` to 'no')
 * wget
 * ssh
 * rsync
 * uuid

Make sure that these tools are installed and in the (system) $PATH to use tat.

###Settings

 * `conf/local/use-tor`: if this file contains anything but "no", all traffic to the software sources as well as to the nodes is routed through tor, via torify/torsocks. This means slower operations and sometimes stalling.
 * `conf/local/resource-dir`: where the downloaded sources are stored, default is `resources/`.
 * `conf/local/libevent-source/`: several settings for getting and verifying the sources for libevent
 * `conf/local/tor-source/`: several settings for getting and verifying the sources for tor

##Usage


###Get sourcecode

    ./tat update-source

This command retrieves the most recent sources for libevent, openssl and tor, and verifies the gpg-signatures.

###List all nodes

    ./tat list PATH

This returns the paths to all nodes in PATH, or all nodes (PATH=conf/nodes/all) if PATH is not given.

An entry in the `conf` directory tree is identified as a node if it has a `nickname` key which should match the name of the directory.

This behaviour for the PATH argument is common for all commands working on nodes. So `./tat status conf/nodes/all/linux` shows the status of all nodes below that path. This is done in the main `./tat` wrapper, which lists all affected nodes and asks for confirmation before any operations which might change the state of the node. The operations in `lib/node/` can also be called on their own, with the path to a single node as the first argument.


###Deploy

    ./tat deploy PATH

####Setup and deploy a new node
Assume we want to deploy a tor node to the host **mytorserver.net** with the IP **2.3.4.5**, which runs **ubuntu**. The nickname of the node should be **myfirstnode**.

Note: At the moment, the `tat_setup-build-env` is only given for the `conf/nodes/all/linux/ubuntu`. It installs the neccessary packages to build and run tor,libevent and openssl and is specific to ubuntu and possibly debian. Deployment on nodes not below that level will fail with `ERROR: get_node_key_path: key 'tat_setup-build-env' not found.` To resolve this, copy the `tat_setup-build-env` for ubuntu to a new place (e.g. `conf/nodes/all/linux/arch`) and modify it appropriatly (change `apt-get` to `pacman`, etc.)

For a new node, we first need to add configuration for the host and the node in the configuration tree, which means creating several files and directories.

First setup the host, as a child of `conf/nodes/all/linux/ubuntu`

    cd conf/nodes/all/linux/ubuntu/
    mkdir -p mytorserver.net/.conf/ssh
    echo mytorserver.net >mytorserver.net/.conf/hostname

To execute commands on the host and copy files, it will be accessed by ssh.
We genereate the id_rsa and id_rsa.pub files to access the host into the created `.conf/ssh` directory.

    ssh-keygen -t rsa -b 4096 -C "tat@USERNAME" -o -f mytorserver.net/.conf/ssh/id_rsa

The content of `id_rsa.pub` must be added to the `.ssh/authorized_keys` file of root on the host!

If the host is not accessed as root, edit the `.ssh/authorized_keys` for the respective user and override the `username` key for the host

    echo "toradmin" > mytorserver.net/.conf/username

Next we setup the node, starting from the directory of the host:

    cd conf/nodes/all/linux/ubuntu/mytorserver.net
    mkdir -p myfirstnode/.conf
    echo myfirstnode >myfirstnode/.conf/nickname
    echo 2.3.4.5 >myfirstnode/.conf/nodeIP

The `conf/nodes/all/` directory should now look like this:

    conf/nodes/all
    |--.conf
    |  |--...
    |--mytorserver.com
       |--.conf
       |  |--hostname (mytorserver.com)
       |  |--ssh
       |     |--id_rsa
       |     |--id_rsa.pub
       |--myfirstnode
          |--.conf
             |--nickname (myfirstnode)
             |--nodeIP (2.3.4.5)


Deploy the node:

    ./tat deploy conf/nodes/all/linux/mytorserver.net/myfirstnode

The deploy script (lib/node/deploy_node) will run through the following steps:

 * make sure that the build and runtime requirements are installed.
 * setup group, user and file paths on the host
 * copy sources of libevent, openssl and tor to the host and built them
 * remember the version and hash of the installed software
 * generate torrc and init script for the node and copy it to the host

The node is not started automatically, do that by running

    ./tat start conf/nodes/all/linux/mytorserver.net/myfirstnode

If you manage multiple nodes, you should put the fingerprints of all your nodes in the "MyFamily" setting of every torrc file. This will change whenever you deploy a new node. To update them, first generate a `myFamily` keyfile for all nodes:

    ./tat generate-myFamily

And then send it out to all nodes:

    ./tat reconfigure

####Update a node
The same command works to update an existing node. If new (more exactly: different) local versions of the sources exist, `deploy` copies the new sources and runs the build process.

It keeps a hash of the installed files and warns if anything changed and triggers a rebuild.

tat keeps track of the hashes and installed versions in `NODEPATH/.deploy/installed`

###(Re)start/stop node
    ./tat start|stop|restart|reconfigure

This sends the appropriate command to the init-script of the node. `start` and `restart` check if the node is actually running after sending the command and show an error if not.

`stop` can take some time to have effect as the tor daemon tries to shut down gracefully.

###Node status

    ./tat status

This shows the status of the node, "running" or "stopped" for now.

###Node fingerprint

    ./tat fingerprint

This returns the fingerprint of the node, which is necessary to generate the `myFamily` key.

###Generate 'myFamily'

    ./tat generate-myFamily PATH

This collects the fingerprints of all nodes which are children in PATH and writes them to the `PATH/.conf/myFamily` file.

###Reconfigure node
    ./tat reconfigure

If we changed any settings for a node, the above command rebuilds the torrc and init-script file, upload the files to the host and triggers the tor binary to reload its configuration.

The 'myFamily' setting is usually the same for all nodes, so after changing myFamily, the configuration should be pushed to all nodes.

The init-script and torrc are written to `NODEPATH/.deploy/etc/`.

##Extend

###Conf hierarchy
Organizing the nodes in the conf hierarchy is the most straightforward method to simplify management. Store all settings in the appropriate, most common parent.


###templates
Need a special configuration for a private bridge? Copy `conf/nodes/all/.conf/torrc-template` to the `.conf` directory of the node and change it.

The openssl built process does not work on 16bit MIPS running BeOS because we need to call configure with --magic=yes? Copy `tat_compile_openssl` to your node and change it.

Maybe other parts of the deployment process should be done differently for some group of nodes? Extract the relevant part to a executable key file and put it into `conf/nodes/all/.conf/`, override that keyfile for the group with the desired changes and call it from the `lib/node/deploy_node`. Check out `tat_setup-build-env` and how it is called from `deploy_node` for reference.

##Common problems

`ERROR: get_node_key_path: key 'KEY' not found. (NODEPATH)`

This means that KEY could not be resolved for NODEPATH, so neither NODEPATH not its parent directories up to `conf/nodes/all/` contains a file named KEY in its '.conf' subdirectory. You have to put a KEY file with the appropriate content at a `.conf` dir the appropriate level (at or above the NODEPATH)

Commonly missing keys might be:
 * `tat_setup-build-env`: currently exists only below `conf/nodes/all/linux/ubuntu`, so either put your node there (if it is ubuntu or maybe debian) or copy and adapt the script to the desired distribution
 * `ssh/id_rsa`: did you provide an ssh key (usually at the host level) for your node
 * `hostname`: the hostname must be explicitely set in `.conf/hostname`, it is not derived from the directory name
 * `nodeIP`: every node needs an address to bind to. if you run multiple nodes on the same host you either need multiple IPs or bind to different ports (orPort and dirPort)
